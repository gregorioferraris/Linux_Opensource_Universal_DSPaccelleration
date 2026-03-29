#pragma once
#include "compute_backend.hpp"
#include <iostream>

namespace DspAccel {
namespace Daemon {

/**
 * @brief Backend implementation for NPU / Processor Arrays.
 * This is where specialized drivers (OpenVINO, Qualcomm SNPE, etc.) interface.
 */
class NpuArrayBackend : public IDspNode {
    int device_id_;
public:
    bool init(int device_index) override {
        device_id_ = device_index;
        std::cout << "[NPU Node] Initializing Processor Array " << device_index << "..." << std::endl;
        return true;
    }
    bool process_stage(Ipc::DspAudioFrame& frame) override {
        // NPU Logic: DMA -> Inference -> DMA Back
        return true; 
    }
    void apply_control_event(uint32_t param_id, float value) override {
        std::cout << "[NPU Node] Control event: " << param_id << " = " << value << std::endl;
    }
    uint32_t allocate_buffer(size_t size) override { return 0; }
    bool upload_buffer(uint32_t handle, const void* data, size_t size) override { return false; }
    void free_buffer(uint32_t handle) override {}
    void set_shm_ptr(void* ptr) override {
        // NPU backend might need the SHM pointer for direct access or logging
        // shm_ptr_ = (Ipc::DspSharedMemory*)ptr;
    }
    void set_zero_copy_bypass(bool enabled, int shm_fd, size_t shm_size) override {}

    DspNodeDescriptor get_descriptor() const override {
        return {
            .name = "NPU Array Engine #" + std::to_string(device_id_),
            .type = DSP_ACCEL_TYPE_ARRAY_PROCESSOR,
            .max_buffer_size = 4096,
            .supports_zero_copy = false, // NPU might not support Vulkan-style zero-copy
            .supports_staging = true,
            .max_buffer_size = 4096 // Example max buffer size
        };
    }
};

} // namespace Daemon
} // namespace DspAccel
