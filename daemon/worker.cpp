#include <iostream>
#include <vector>
#include <memory>
#include <csignal>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <getopt.h>
#include "vulkan/vulkan_compute.h"
#include "ipc/include/ipc_shm.hpp"

// Helper: receive FD via SCM_RIGHTS (simplified for worker)
static int recv_fd(int socket) {
    char buf[1];
    struct iovec iov = { .iov_base = buf, .iov_len = 1 };
    char cmsg_buf[CMSG_SPACE(sizeof(int))];
    struct msghdr msg = {
        .msg_iov = &iov, .msg_iovlen = 1,
        .msg_control = cmsg_buf, .msg_controllen = sizeof(cmsg_buf)
    };
    if (recvmsg(socket, &msg, 0) <= 0) return -1;
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    int fd;
    memcpy(&fd, CMSG_DATA(cmsg), sizeof(int));
    return fd;
}

// Backend Implementations (normally in separate files, kept here for brevity)
#include "node_graph.hpp"

namespace DspAccel {
namespace Daemon {

class VulkanBackend : public IDspNode {
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
        if (shm_buffer_handle_ != 0) {
            // Zero-copy path: Calculate offset from SHM start to this frame
            uint32_t offset = (uint32_t)((uint8_t*)&frame - (uint8_t*)shm_ptr_);
            return dsp_vulkan_dispatch_zero_copy(vk_ctx_, shm_buffer_handle_, offset, frame.frame_count, frame.channel_count);
        }
        return dsp_vulkan_dispatch(vk_ctx_, frame.samples, frame.frame_count, frame.channel_count);
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
            .type = DSP_TYPE_MASSIVELY_PARALLEL,
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
        return 1;
    }

    graph.add_node_to_sequence(primary_node.get());

    // 2. Attach to Supervisor SHM and EventFDs
    int shm_fd = recv_fd(STDIN_FILENO); 
    int e_send = recv_fd(STDIN_FILENO); // daemon receives on this
    int e_recv = recv_fd(STDIN_FILENO); // daemon signals on this

    if (shm_fd < 0 || e_send < 0 || e_recv < 0) {
        std::cerr << "[DSP Worker #" << worker_id << "] IPC Error: Failed to receive IPC FDs." << std::endl;
        return 1;
    }

    auto* shm = (DspAccel::Ipc::DspSharedMemory*)mmap(NULL, sizeof(DspAccel::Ipc::DspSharedMemory),
                                                      PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    
    if (shm == MAP_FAILED) return 1;
    shm->workers[worker_id].is_ready.store(true);
    primary_node->set_shm_ptr(shm);

    std::cout << "[DSP Worker #" << worker_id << "] Graph READY (" 
              << primary_node->get_descriptor().name << "). Monitoring queue." << std::endl;

    while (running) {
        shm->workers[worker_id].heartbeat.fetch_add(1, std::memory_order_release);
        
        // Mock load reporting: in a real worker, this would come from Vulkan/NPU usage metrics
        uint32_t mock_load = 25; // 25% base load
        shm->workers[worker_id].load_pct.store(mock_load, std::memory_order_relaxed);

        // Real-time bypass monitoring (toggleable on-fly)
        bool bypass_active = shm->workers[worker_id].bypass_zero_copy.load(std::memory_order_relaxed);
        primary_node->set_zero_copy_bypass(bypass_active, shm_fd, sizeof(DspAccel::Ipc::DspSharedMemory));

        // Wait for plugin to signal "audio ready"
        uint64_t val;
        if (read(e_send, &val, sizeof(val)) > 0) {
            DspAccel::Ipc::DspAudioFrame frame;
            // Pop from input queue, process through graph, push to output queue
            if (shm->in_queue.pop(frame)) {
                // Check if plugin wants parallelization OR if we should force it
                bool plugin_prefers = shm->prefer_block_parallel.load(std::memory_order_relaxed);
                uint32_t current_load = shm->workers[worker_id].load_pct.load(std::memory_order_relaxed);
                
                // Only split if plugin prefers AND GPU has sufficient headroom (< 60% load)
                bool should_split = plugin_prefers && (current_load < 60);

                if (should_split && frame.frame_count >= 128) {
                    graph.process_graph_parallel(frame, 4); // Parallel GPU dispatch
                } else {
                    graph.process_graph(frame); // Linear dispatch
                }
                
                shm->out_queue.push(frame);

                // Signal plugin: "output ready"
                uint64_t one = 1;
                write(e_recv, &one, sizeof(one));
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
