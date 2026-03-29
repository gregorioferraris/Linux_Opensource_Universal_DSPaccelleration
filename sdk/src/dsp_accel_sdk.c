#include "dsp_accel_sdk.h"
#include "../../daemon/ipc/include/ipc_shm.hpp"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/eventfd.h>
#include <poll.h>

#define DAEMON_SOCKET_PATH "/tmp/dsp_accel_daemon.sock"

using namespace DspAccel::Ipc;

struct dsp_accel_sdk_ctx {
    DspWorkloadType type;
    int socket_fd;
    int shm_fd;
    int e_send; // Per segnalare al worker: "audio pronto"
    int e_recv; // Per ricevere dal worker: "audio processato"
    DspSharedMemory* shm;
};

// Helper interno per ricevere FD via Unix Socket
static int recv_fd(int socket) {
    char buf[1];
    struct iovec iov = { .iov_base = buf, .iov_len = 1 };
    char cmsg_buf[CMSG_SPACE(sizeof(int))];
    struct msghdr msg = {0};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsg_buf;
    msg.msg_controllen = sizeof(cmsg_buf);

    if (recvmsg(socket, &msg, 0) <= 0) return -1;
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if (!cmsg || cmsg->cmsg_type != SCM_RIGHTS) return -1;
    
    int fd;
    memcpy(&fd, CMSG_DATA(cmsg), sizeof(int));
    return fd;
}

dsp_accel_sdk_ctx_t* dsp_accel_sdk_connect(DspWorkloadType type, float timeout_ms) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return NULL;

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, DAEMON_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return NULL;
    }

    // 1. Invia Metadati (Tipo carico e costo stimato)
    struct { uint32_t wl_type; float cost; } meta = { (uint32_t)type, timeout_ms };
    send(sock, &meta, sizeof(meta), 0);

    // 2. Ricevi conferma Worker ID
    struct { uint32_t worker_id; } resp;
    if (recv(sock, &resp, sizeof(resp), 0) != sizeof(resp)) {
        close(sock);
        return NULL;
    }

    // 3. Ricevi i 3 File Descriptor vitali
    int shm_fd = recv_fd(sock);
    int e_send = recv_fd(sock);
    int e_recv = recv_fd(sock);

    if (shm_fd < 0 || e_send < 0 || e_recv < 0) {
        close(sock);
        return NULL;
    }

    // v1.0 Security: Ensure the shared memory is sealed to prevent unauthorized resizing
    // This is part of the "Hardened Isolation" objective.
#ifdef F_ADD_SEALS
    if (fcntl(shm_fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL) < 0) {
        // Note: some older kernels might not support all seals, handle gracefully
    }
#endif

    dsp_accel_sdk_ctx_t* ctx = (dsp_accel_sdk_ctx_t*)calloc(1, sizeof(dsp_accel_sdk_ctx_t));
    ctx->type = type;
    ctx->socket_fd = sock;
    ctx->shm_fd = shm_fd;
    ctx->e_send = e_send;
    ctx->e_recv = e_recv;

    // 4. Mappa la SHM con controllo di errore (Critico per evitare segfault)
    ctx->shm = (DspSharedMemory*)mmap(NULL, sizeof(DspSharedMemory), 
                                      PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    
    if (ctx->shm == MAP_FAILED) {
        dsp_accel_sdk_disconnect(ctx);
        return NULL;
    }

    return ctx;
}

void dsp_accel_sdk_disconnect(dsp_accel_sdk_ctx_t* ctx) {
    if (!ctx) return;
    if (ctx->shm && ctx->shm != (void*)-1) {
        munmap(ctx->shm, sizeof(DspSharedMemory));
    }
    if (ctx->shm_fd >= 0) close(ctx->shm_fd);
    if (ctx->e_send >= 0) close(ctx->e_send);
    if (ctx->e_recv >= 0) close(ctx->e_recv);
    if (ctx->socket_fd >= 0) close(ctx->socket_fd);
    free(ctx);
}

bool dsp_accel_sdk_dispatch(dsp_accel_sdk_ctx_t* ctx, float** buffers, int frames, int channels) {
    if (!ctx || !ctx->shm || frames > 256) return false;

    DspAudioFrame frame;
    frame.frame_count = frames;
    frame.channel_count = channels;

    // Interleaving o copia diretta (dipende dal layout del backend)
    for (int c = 0; c < channels; c++) {
        memcpy(&frame.samples[c * 256], buffers[c], frames * sizeof(float));
    }

    if (!ctx->shm->in_queue.push(frame)) return false;

    // Segnala al worker che c'è lavoro
    uint64_t signal = 1;
    if (write(ctx->e_send, &signal, sizeof(signal)) != sizeof(signal)) {
        return false;
    }

    return true;
}

bool dsp_accel_sdk_read_output(dsp_accel_sdk_ctx_t* ctx, float** buffers, int frames, int channels) {
    if (!ctx || !ctx->shm) return false;

    // Aspetta il segnale dal worker con un timeout di 5ms (fondamentale per audio RT)
    struct pollfd pfd = { .fd = ctx->e_recv, .events = POLLIN };
    int res = poll(&pfd, 1, 5);

    if (res <= 0) {
        // Timeout o errore: il worker è troppo lento o crashato
        return false;
    }

    uint64_t val;
    read(ctx->e_recv, &val, sizeof(val));

    DspAudioFrame out_frame;
    if (!ctx->shm->out_queue.pop(out_frame)) return false;

    for (int c = 0; c < channels; c++) {
        memcpy(buffers[c], &out_frame.samples[c * 256], frames * sizeof(float));
    }

    return true;
}