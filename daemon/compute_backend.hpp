#pragma once
#include "ipc/include/ipc_shm.hpp"
#include <string>
#include <vector>

namespace DspAccel {
namespace Daemon {

struct DspNodeDescriptor {
    std::string name;
    DspWorkloadType type;
    size_t vram_capacity_mb;
    bool supports_zero_copy;
};

/**
 * @brief Interface for any hardware accelerator (GPU, NPU, FPGA, DSP Array).
 */
class IDspNode {
public:
    virtual ~IDspNode() = default;

    // Life-cycle
    virtual bool init(int index) = 0;
    virtual DspNodeDescriptor get_descriptor() const = 0;

    // Audio Processing
    virtual bool process_stage(Ipc::DspAudioFrame& frame) = 0;
    
    // Control & Modulation
    virtual void apply_control_event(uint32_t param_id, float value) = 0;

    // Memory Management (v1.0 & v2.0)
    virtual uint32_t allocate_buffer(size_t size) = 0;
    virtual bool upload_buffer(uint32_t handle, const void* data, size_t size) = 0;
    virtual void free_buffer(uint32_t handle) = 0;
    
    // Tracking Mode (v2.0)
    virtual void set_zero_copy_bypass(bool enabled, int shm_fd, size_t shm_size) = 0;
};

}} // namespace DspAccel::Daemon