#include <iostream> // Keep this for basic logging
#include <vector>
#include <string>
#include <memory>
#include <csignal>
#include <cstdlib>
#include <unistd.h>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <map>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <poll.h>
#include <errno.h>
#include <chrono>
#include <sched.h> // For real-time scheduling

#include "vulkan/vulkan_compute.h"
#include "ipc/include/ipc_shm.hpp"

// Helper: receive FD via SCM_RIGHTS (simplified for worker)
static int recv_fd(int socket) {
    char buf[1];
    struct iovec iov = { .iov_base = buf, .iov_len = 1 };
    char cmsg_buf[CMSG_SPACE(sizeof(int))];
    struct msghdr msg = {};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsg_buf;
    msg.msg_controllen = sizeof(cmsg_buf);

    if (recvmsg(socket, &msg, 0) <= 0) return -1;
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if (!cmsg || cmsg->cmsg_type != SCM_RIGHTS) {
        return -1;
    }
    
    int fd;
    memcpy(&fd, CMSG_DATA(cmsg), sizeof(int));
    return fd;
}

// Backend Implementations (normally in separate files, kept here for brevity)
#include "npu_array_backend.hpp"
#include "node_graph.hpp"

namespace DspAccel {
namespace Daemon {

class VulkanBackend : public IDspNode {
    float gain_ = 1.0f;
    float threshold_ = 0.95f;
    float param1_ = 0.5f;
    float param2_ = 0.5f;
    uint32_t algo_id_ = 0;

    // Map of persistent state buffers per instance (e.g., filter delays)
    std::map<uint32_t, uint32_t> instance_states_;

    struct dsp_vulkan_context *vk_ctx_;
public:
    VulkanBackend() : vk_ctx_(nullptr) {}
    ~VulkanBackend() { if(vk_ctx_) dsp_vulkan_destroy(vk_ctx_); }
    bool init(int device_index) override {
        vk_ctx_ = dsp_vulkan_init(device_index);
        return vk_ctx_ != nullptr;
    }
    bool process_stage(Ipc::DspAudioFrame& frame) override {
        if (!vk_ctx_) return false;

        // Ensure this instance has a state buffer on GPU (e.g., 1KB for DSP state)
        if (instance_states_.find(frame.instance_id) == instance_states_.end()) {
            instance_states_[frame.instance_id] = dsp_vulkan_allocate_buffer(vk_ctx_, 1024);
        }
        uint32_t state_handle = instance_states_[frame.instance_id];

        if (shm_buffer_handle_ != 0) {
            // Zero-copy path: Calculate offset from SHM start to this frame
            uint32_t offset = (uint32_t)((uint8_t*)&frame - (uint8_t*)shm_ptr_);
            return dsp_vulkan_dispatch_zero_copy(vk_ctx_, shm_buffer_handle_, offset, frame.frame_count, frame.channel_count, gain_, threshold_, param1_, param2_, algo_id_, state_handle);
        }
        return dsp_vulkan_dispatch(vk_ctx_, frame.samples, frame.frame_count, frame.channel_count, gain_, threshold_, param1_, param2_, algo_id_, state_handle);
    }

    void apply_control_event(uint32_t param_id, float value) override {
        if (param_id == 1) gain_ = value;
        else if (param_id == 2) threshold_ = value;
        else if (param_id == 3) param1_ = value;
        else if (param_id == 4) param2_ = value;
        else if (param_id == 5) algo_id_ = static_cast<uint32_t>(value);
    }

    uint32_t allocate_buffer(size_t size) override {
        return dsp_vulkan_allocate_buffer(vk_ctx_, size);
    }
    bool upload_buffer(uint32_t handle, const void* data, size_t size) override {
        return dsp_vulkan_upload_buffer(vk_ctx_, handle, data, size);
    }
    void free_buffer(uint32_t handle) override {
        dsp_vulkan_free_buffer(vk_ctx_, handle);
    }
    void set_zero_copy_bypass(bool enabled, int shm_fd, size_t shm_size) override {
        if (enabled && shm_fd >= 0) {
            shm_buffer_handle_ = dsp_vulkan_import_shm(vk_ctx_, shm_fd, shm_size);
        } else {
            shm_buffer_handle_ = 0;
        }
    }
    void set_shm_ptr(void* ptr) override { shm_ptr_ = ptr; }

    DspNodeDescriptor get_descriptor() const override {
        return {
            .name = "Vulkan GPU Node",
            .type = DSP_ACCEL_TYPE_MASSIVELY_PARALLEL,
            .max_buffer_size = 16384,
            .supports_staging = true
        };
    }
private:
    uint32_t shm_buffer_handle_ = 0;
    void*    shm_ptr_ = nullptr;
};

}
}

static bool running = true;
void signal_handler(int sig) { running = false; }

static void send_worker_log(DspAccel::Ipc::DspSharedMemory* shm, const char* tag, const char* msg, uint32_t level) {
    if (!shm) return;
    DspAccel::Ipc::DspLogEntry entry = { level };
    strncpy(entry.tag, tag, 15);
    strncpy(entry.msg, msg, 127);
    shm->log_bus.push(entry);
}

int main(int argc, char *argv[]) {
    int worker_id = 0;
    std::string type = "vulkan";

    // CLI Parsing for specialized worker launch
    static struct option long_options[] = {
        {"type", required_argument, 0, 't'},
        {"id",   required_argument, 0, 'i'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "t:i:", long_options, NULL)) != -1) {
        switch (opt) {
            case 't': type = optarg; break;
            case 'i': worker_id = atoi(optarg); break;
        }
    }

    std::cout << "[DSP Worker #" << worker_id << "] Starting (" << type << ")..." << std::endl;
    std::signal(SIGINT, signal_handler);

    // 0. Set Real-Time Priority (SCHED_FIFO)
    // This ensures the worker isn't preempted by non-audio tasks.
    struct sched_param sp = { .sched_priority = 80 }; // High priority
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) == 0) {
        std::cout << "[DSP Worker] Thread priority set to SCHED_FIFO (80)" << std::endl;
    } else {
        std::cerr << "[DSP Worker] WARNING: Could not set SCHED_FIFO. Check limits.conf or run with CAP_SYS_NICE." << std::endl;
    }

    #ifdef __linux__
    // Set thread name for easier debugging in 'htop' or 'top'
    pthread_setname_np(pthread_self(), ("dsp-wrk-" + std::to_string(worker_id)).c_str());
    #endif

    // 1. Attach to Supervisor SHM and EventFDs (IPC Setup FIRST)
    // The supervisor passes these FDs via SCM_RIGHTS on a connected socket.
    // For simplicity in this example, we assume they are passed via a pre-established channel
    // or directly from the supervisor's fork/exec. In a real system, the worker would connect
    // to a dedicated socket from the supervisor to receive these FDs.
    DspAccel::Ipc::DspSharedMemory* shm = nullptr;

    if (shm_fd >= 0) {
        shm = (DspAccel::Ipc::DspSharedMemory*)mmap(NULL, sizeof(DspAccel::Ipc::DspSharedMemory),
                                                          PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    }

    if (shm_fd < 0 || e_send < 0 || e_recv < 0 || shm == MAP_FAILED || !shm) {
        std::cerr << "[DSP Worker #" << worker_id << "] IPC Error: Handshake failed." << std::endl;
        if (shm_fd < 0) std::cerr << " -> Missing SHM FD" << std::endl;
        if (e_send < 0) std::cerr << " -> Missing Send EventFD" << std::endl;
        if (shm == MAP_FAILED) std::cerr << " -> Memory mapping failed" << std::endl;
        return 1;
    }
    
    shm->workers[worker_id].is_ready.store(false); // Not ready until HW init

    // 1. Node Graph & Backend Initialization
    DspAccel::Daemon::DspNodeGraph graph;
    std::unique_ptr<DspAccel::Daemon::IDspNode> primary_node;

    if (type == "vulkan") {
        primary_node = std::make_unique<DspAccel::Daemon::VulkanBackend>();
    } else if (type == "npu") {
        primary_node = std::make_unique<DspAccel::Daemon::NpuArrayBackend>();
    }

    if (!primary_node || !primary_node->init(0)) {
        std::cerr << "[DSP Worker #" << worker_id << "] FATAL: Hardware init failed." << std::endl;
        shm->workers[worker_id].last_error.store(DspAccel::Ipc::WorkerError::INTERNAL_ERROR, std::memory_order_release);
        send_worker_log(shm, "CORE", "Hardware initialization failed", 2);
        return 1;
    }

    shm->workers[worker_id].is_ready.store(true);
    primary_node->set_shm_ptr(shm);
    graph.add_node_to_sequence(primary_node.get());
    send_worker_log(shm, "CORE", "Worker graph initialized successfully", 0);

    std::cout << "[DSP Worker #" << worker_id << "] Graph READY (" 
              << primary_node->get_descriptor().name << "). Monitoring queue." << std::endl;

    while (running) {
        shm->workers[worker_id].heartbeat.fetch_add(1, std::memory_order_release);

        // Real-time bypass monitoring (toggleable on-fly)
        bool bypass_active = shm->workers[worker_id].bypass_zero_copy.load(std::memory_order_relaxed);
        primary_node->set_zero_copy_bypass(bypass_active, shm_fd, sizeof(DspAccel::Ipc::DspSharedMemory));

        // NEW: Process Control Bus events
        DspAccel::Ipc::DspControlEvent ctrl_evt;
        while (shm->control_bus.pop(ctrl_evt)) {
            primary_node->apply_control_event(ctrl_evt.parameter_id, ctrl_evt.value);
        }

        // Wait for plugin to signal "audio ready"
        struct pollfd pfd = { .fd = e_send, .events = POLLIN };
        uint64_t val;
        // Poll with 1ms timeout to keep memory bus and heartbeat alive
        if (poll(&pfd, 1, 1) > 0 && (pfd.revents & POLLIN)) {
            if (read(e_send, &val, sizeof(val)) < 0) continue;

            // Zero-Copy: Accediamo al frame direttamente nella memoria condivisa
            DspAccel::Ipc::DspAudioFrame* frame_ptr = shm->in_queue.peek_read();
            if (frame_ptr) {
                // Check if plugin wants parallelization OR if we should force it
                bool plugin_prefers = shm->prefer_block_parallel.load(std::memory_order_relaxed);
                uint32_t current_load = shm->workers[worker_id].load_pct.load(std::memory_order_relaxed);
                
                // Only split if plugin prefers AND GPU has sufficient headroom (< 60% load)
                bool should_split = plugin_prefers && (current_load < 60);

                auto start_gpu = std::chrono::steady_clock::now();
                bool success = false;
                if (should_split && frame_ptr->frame_count >= 128) {
                    success = graph.process_graph_parallel(*frame_ptr, 4); 
                } else {
                    success = graph.process_graph(*frame_ptr);
                }
                auto end_gpu = std::chrono::steady_clock::now();

                // Calculate actual GPU load based on buffer duration
                // e.g. 256 samples at 48kHz = 5.33ms. If GPU took 0.5ms, load is ~10%
                auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end_gpu - start_gpu).count();
                float block_duration_us = (frame_ptr->frame_count * 1000000.0f) / 48000.0f;
                uint32_t actual_load = (uint32_t)((elapsed_us * 100.0f) / block_duration_us);
                shm->workers[worker_id].load_pct.store(actual_load > 100 ? 100 : actual_load, std::memory_order_relaxed);

                if (!success) {
                    shm->workers[worker_id].last_error.store(DspAccel::Ipc::WorkerError::DEVICE_LOST, std::memory_order_release);
                }
                
                // Zero-Copy output: Reserve a slot in the output queue and move processed data directly
                DspAccel::Ipc::DspAudioFrame* out_slot = shm->out_queue.reserve_write();
                if (out_slot) {
                    // Optimization: copy only metadata and valid samples per channel
                    out_slot->frame_count = frame_ptr->frame_count;
                    out_slot->channel_count = frame_ptr->channel_count;
                    out_slot->timestamp_ns = frame_ptr->timestamp_ns; // Preserve for SDK RTT
                    for (uint32_t ch = 0; ch < frame_ptr->channel_count; ch++) {
                        memcpy(&out_slot->samples[ch * 256], &frame_ptr->samples[ch * 256], frame_ptr->frame_count * sizeof(float));
                    }
                    shm->out_queue.commit_write();
                }
                
                shm->in_queue.commit_read();

                // Signal plugin: "output ready"
                uint64_t one = 1;
                ssize_t wres;
                do {
                    wres = write(e_recv, &one, sizeof(one));
                } while (wres < 0 && errno == EINTR);
            }
        }

        // Memory Management Bus: plugin → daemon (Alloc/Free/Upload)
        DspAccel::Ipc::DspMemoryRequest mem_request;
        while (shm->memory_request_bus.pop(mem_request)) {
            DspAccel::Ipc::DspMemoryResponse response = { .request_id = mem_request.request_id, .success = false };
            
            switch (mem_request.type) {
                case DspAccel::Ipc::DspMemoryRequestType::ALLOCATE:
                    response.handle = primary_node->allocate_buffer(mem_request.size);
                    response.success = (response.handle != 0);
                    break;
                case DspAccel::Ipc::DspMemoryRequestType::UPLOAD:
                    // Data is in shm->data_staging (offset from preamble)
                    response.success = primary_node->upload_buffer(mem_request.handle, 
                                                                 (uint8_t*)shm + mem_request.shm_offset, 
                                                                 mem_request.size);
                    break;
                case DspAccel::Ipc::DspMemoryRequestType::FREE:
                    primary_node->free_buffer(mem_request.handle);
                    response.success = true; 
                    break;
            }
            shm->memory_response_bus.push(response);
        }
    }

    std::cout << "[DSP Worker #" << worker_id << "] Shutting down clean." << std::endl;
    return 0;
}
