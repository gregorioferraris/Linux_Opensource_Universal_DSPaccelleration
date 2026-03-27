/**
 * DSP Acceleration SDK — Implementation
 * 
 * Licensed under the MIT License (Permissive).
 * The core system remains under GPLv3.
 */

#include "../include/dsp_accel_sdk.h"
#include "../../daemon/ipc/include/ipc_shm.hpp"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <stdint.h>

#ifdef DSP_PLATFORM_LINUX
#include <sys/eventfd.h>
#endif

#define DSP_DAEMON_SOCKET_PATH "/run/dsp_accel/daemon.sock"
#define DSP_LATENCY_FRAMES     64

// ─── Opaque Context ───────────────────────────────────────────────────────────
struct dsp_accel_sdk_ctx {
    int    socket_fd;          // Unix domain socket to daemon
    int    shm_fd;             // memfd from daemon (DspSharedMemory)
    int    eventfd_send;       // plugin → daemon: "audio ready in in_queue"
    int    eventfd_recv;       // daemon → plugin: "output ready in out_queue"
    
    DspAccel::Ipc::DspSharedMemory *shm; // mmap pointer
    uint32_t worker_id;        // Assigned by daemon (0-7)

    DspWorkloadType workload_type;
    float    cost_mflops;

    // Stats
    uint32_t dispatched_blocks;
    uint32_t xrun_count;
};

// ─── Helper: receive exactly one FD via SCM_RIGHTS ───────────────────────────
static int recv_fd(int socket) {
    char buf[1] = {0};
    struct iovec iov = { .iov_base = buf, .iov_len = 1 };
    char cmsg_buf[CMSG_SPACE(sizeof(int))];
    struct msghdr msg = {
        .msg_iov        = &iov,      .msg_iovlen     = 1,
        .msg_control    = cmsg_buf,  .msg_controllen = sizeof(cmsg_buf)
    };
    if (recvmsg(socket, &msg, 0) <= 0) return -1;
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if (!cmsg || cmsg->cmsg_type != SCM_RIGHTS) return -1;
    int fd = -1;
    memcpy(&fd, CMSG_DATA(cmsg), sizeof(int));
    return fd;
}

// ─── Helper: close an FD only if valid ───────────────────────────────────────
static void safe_close(int *fd) {
    if (*fd >= 0) { close(*fd); *fd = -1; }  // Fix #3: no double-close
}

// ─── Connect ─────────────────────────────────────────────────────────────────
dsp_accel_sdk_ctx_t* dsp_accel_sdk_connect(DspWorkloadType type, float cost_mflops) {
    dsp_accel_sdk_ctx_t *ctx = (dsp_accel_sdk_ctx_t*)calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    ctx->workload_type = type;
    ctx->cost_mflops   = cost_mflops;
    ctx->socket_fd     = -1;
    ctx->shm_fd        = -1;
    ctx->eventfd_send  = -1;
    ctx->eventfd_recv  = -1;
    ctx->shm           = NULL;

#ifdef DSP_PLATFORM_LINUX
    ctx->socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ctx->socket_fd < 0) goto fallback;

    {
        struct sockaddr_un addr = {};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, DSP_DAEMON_SOCKET_PATH, sizeof(addr.sun_path) - 1);
        if (connect(ctx->socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            fprintf(stderr, "[DSP SDK] Daemon not found. CPU fallback.\n");
            goto fallback;
        }
    }

    // Send workload metadata to daemon
    {
        struct { uint32_t wl_type; float cost; } meta = {(uint32_t)type, cost_mflops};
        if (send(ctx->socket_fd, &meta, sizeof(meta), 0) != sizeof(meta)) goto cleanup;
    }

    // Receive Handshake Response: {worker_id}
    {
        struct { uint32_t worker_id; } response;
        if (recv(ctx->socket_fd, &response, sizeof(response), 0) != sizeof(response)) goto cleanup;
        ctx->worker_id = response.worker_id;
    }

    // Receive 3 FDs from daemon: shm_fd, eventfd_send, eventfd_recv
    ctx->shm_fd       = recv_fd(ctx->socket_fd);
    ctx->eventfd_send = recv_fd(ctx->socket_fd);
    ctx->eventfd_recv = recv_fd(ctx->socket_fd);

    // Fix #3: validate ALL fds before using any
    if (ctx->shm_fd < 0 || ctx->eventfd_send < 0 || ctx->eventfd_recv < 0) {
        fprintf(stderr, "[DSP SDK] Failed to receive IPC FDs. CPU fallback.\n");
        goto cleanup;
    }

    // Map the full DspSharedMemory struct (two ring buffers)
    ctx->shm = (DspAccel::Ipc::DspSharedMemory*)mmap(
        NULL, sizeof(DspAccel::Ipc::DspSharedMemory),
        PROT_READ | PROT_WRITE, MAP_SHARED,
        ctx->shm_fd, 0
    );
    if (ctx->shm == MAP_FAILED) { ctx->shm = NULL; goto cleanup; }

    fprintf(stderr, "[DSP SDK] Connected! Hardware path active (type=%d).\n", type);
    return ctx;

cleanup:
    // Fix #3: close only what was successfully opened before the failure
    safe_close(&ctx->shm_fd);
    safe_close(&ctx->eventfd_send);
    safe_close(&ctx->eventfd_recv);
    safe_close(&ctx->socket_fd);
fallback:
#endif
    fprintf(stderr, "[DSP SDK] Running CPU fallback.\n");
    return ctx; // shm == NULL signals fallback to dispatch()
}

// ─── Dispatch (Non-blocking, Fix #2 + partial #1) ────────────────────────────
bool dsp_accel_sdk_dispatch(dsp_accel_sdk_ctx_t *ctx,
                             float **channel_buffers,
                             int frames, int channels)
{
    if (!ctx || !ctx->shm) return false;

#ifdef DSP_PLATFORM_LINUX
    // Fix #5: clamp to DspAudioFrame maximums (256 frames x 8 channels)
    uint32_t fc = (uint32_t)(frames  > 256 ? 256 : frames);
    uint32_t cc = (uint32_t)(channels >  8  ?  8 : channels);

    DspAccel::Ipc::DspAudioFrame frame_in = {};
    frame_in.frame_count   = fc;
    frame_in.channel_count = cc;
    for (uint32_t ch = 0; ch < cc; ch++)
        memcpy(&frame_in.samples[ch * 256], channel_buffers[ch], fc * sizeof(float));

    // Fix #2: use SPSC push() — RT-safe, no direct indexing
    if (!ctx->shm->in_queue.push(frame_in)) {
        ctx->xrun_count++;
        fprintf(stderr, "[DSP SDK] XRUN: in_queue full!\n");
        return false;
    }

    // Signal daemon: audio is ready
    uint64_t one = 1;
    if (write(ctx->eventfd_send, &one, sizeof(one)) < 0) {
        ctx->xrun_count++;
        return false;
    }

    ctx->dispatched_blocks++;
    return true;
#else
    return false;
#endif
}

// ─── Readback: called after dispatch to copy output into DAW buffers ──────────
// Fix #1: plugin reads output from out_queue (daemon writes here after GPU calc)
#include <poll.h>

// ... (in dsp_accel_sdk_read_output)
bool dsp_accel_sdk_read_output(dsp_accel_sdk_ctx_t *ctx,
                                float **channel_buffers,
                                int frames, int channels)
{
    if (!ctx || !ctx->shm) return false;

#ifdef DSP_PLATFORM_LINUX
    // Fix #13: Use poll with 10ms timeout to avoid hanging the DAW
    struct pollfd pfd = { .fd = ctx->eventfd_recv, .events = POLLIN };
    int ret = poll(&pfd, 1, 10); // 10ms timeout
    if (ret <= 0) {
        // Timeout or error: signal CPU fallback (returns false)
        return false;
    }

    uint64_t val = 0;
    if (read(ctx->eventfd_recv, &val, sizeof(val)) < 0) return false;

    DspAccel::Ipc::DspAudioFrame frame_out = {};
    if (!ctx->shm->out_queue.pop(frame_out)) return false;

    uint32_t fc = frame_out.frame_count;
    uint32_t cc = frame_out.channel_count;
    for (uint32_t ch = 0; ch < cc && ch < (uint32_t)channels; ch++)
        memcpy(channel_buffers[ch], &frame_out.samples[ch * 256], fc * sizeof(float));

    return true;
#else
    return false;
#endif
}

// ─── Latency (Fix #4) ────────────────────────────────────────────────────────
int dsp_accel_sdk_get_latency_samples(dsp_accel_sdk_ctx_t *ctx) {
    if (!ctx || !ctx->shm) return 0;
    return DSP_LATENCY_FRAMES; // exactly 1 block of latency (compensated by DAW PDC)
}

// ─── Stats ───────────────────────────────────────────────────────────────────
bool dsp_accel_sdk_get_stats(dsp_accel_sdk_ctx_t *ctx, DspAccelStats *out) {
    if (!ctx || !out) return false;
    out->xrun_count        = ctx->xrun_count;
    out->dispatched_blocks = ctx->dispatched_blocks;
    out->hw_load_percent   = 0.0f;
    out->avg_roundtrip_us  = 0.0f;
    return true;
}

bool dsp_accel_sdk_set_param(dsp_accel_sdk_ctx_t* ctx, uint32_t param_id, float value) {
    if (!ctx || !ctx->shm) return false;
    
    DspAccel::Ipc::DspControlEvent event = { param_id, value };
    return ctx->shm->control_bus.push(event);
}

// ─── VRAM Management (Asynchronous handshake) ──────────────────────────────
uint32_t dsp_accel_sdk_allocate_vram(dsp_accel_sdk_ctx_t* ctx, size_t size) {
    if (!ctx || !ctx->shm) return 0;
    
    uint32_t req_id = (uint32_t)rand();
    DspAccel::Ipc::DspMemoryRequest req = { req_id, DspAccel::Ipc::DspMemoryRequestType::ALLOCATE, size, 0, 0 };
    
    if (!ctx->shm->memory_request_bus.push(req)) return 0;
    
    // Poll for response (Max 500ms timeout)
    DspAccel::Ipc::DspMemoryResponse resp;
    for (int i = 0; i < 500; i++) {
        if (ctx->shm->memory_response_bus.pop(resp)) {
            if (resp.request_id == req_id && resp.success) return resp.handle;
        }
        usleep(1000);
    }
    return 0;
}

bool dsp_accel_sdk_upload_buffer(dsp_accel_sdk_ctx_t* ctx, uint32_t handle, const void* data, size_t size) {
    if (!ctx || !ctx->shm || size > (1024 * 1024 * 4)) return false;
    
    // Copy to staging area
    memcpy(ctx->shm->data_staging, data, size);
    
    uint32_t req_id = (uint32_t)rand();
    DspAccel::Ipc::DspMemoryRequest req = { 
        req_id, 
        DspAccel::Ipc::DspMemoryRequestType::UPLOAD, 
        size, 
        handle, 
        (uint64_t)((uint8_t*)ctx->shm->data_staging - (uint8_t*)ctx->shm) 
    };
    
    if (!ctx->shm->memory_request_bus.push(req)) return false;

    // Poll for confirmation
    DspAccel::Ipc::DspMemoryResponse resp;
    for (int i = 0; i < 500; i++) {
        if (ctx->shm->memory_response_bus.pop(resp)) {
            if (resp.request_id == req_id) return resp.success;
        }
        usleep(1000);
    }
    return false;
}

void dsp_accel_sdk_free_vram(dsp_accel_sdk_ctx_t* ctx, uint32_t handle) {
    if (!ctx || !ctx->shm || handle == 0) return;
    
    DspAccel::Ipc::DspMemoryRequest req = { 0, DspAccel::Ipc::DspMemoryRequestType::FREE, 0, handle, 0 };
    ctx->shm->memory_request_bus.push(req);
}

void dsp_accel_sdk_set_zero_copy_bypass(dsp_accel_sdk_ctx_t* ctx, bool enabled) {
    if (!ctx || !ctx->shm) return;
    ctx->shm->workers[ctx->worker_id].bypass_zero_copy.store(enabled, std::memory_order_release);
}

// ─── Disconnect (Fix #7 prevention: safe_close prevents double-close) ─────────
void dsp_accel_sdk_disconnect(dsp_accel_sdk_ctx_t *ctx) {
    if (!ctx) return;
#ifdef DSP_PLATFORM_LINUX
    if (ctx->shm)
        munmap(ctx->shm, sizeof(DspAccel::Ipc::DspSharedMemory));
    safe_close(&ctx->shm_fd);
    safe_close(&ctx->eventfd_send);
    safe_close(&ctx->eventfd_recv);
    safe_close(&ctx->socket_fd);
#endif
    free(ctx);
    fprintf(stderr, "[DSP SDK] Disconnected.\n");
}
