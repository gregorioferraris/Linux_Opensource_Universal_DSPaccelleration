#pragma once
#include "../../sdk/include/dsp_accel_sdk.h"
#include "ipc_shm.hpp"
#include <vector>
#include <memory>
#include <string>

namespace DspAccel {
namespace Daemon {

/**
 * @brief Capabilities and metadata for a DSP Node.
 */
struct DspNodeDescriptor {
    std::string name;
    DspWorkloadType type;
    size_t max_buffer_size;
    bool supports_staging; // Can it receive data from another HW node?
};

/**
 * @brief Abstract interface for a DSP Node in the processing graph.
 */
class IDspNode {
public:
    virtual ~IDspNode() = default;

    /**
     * @brief Initialize hardware with a specific device.
     */
    virtual bool init(int device_index) = 0;

    /**
     * @brief Perform processing for a specific stage in the graph.
     */
    virtual bool process_stage(Ipc::DspAudioFrame& frame) = 0;

    /**
     * @brief Apply a real-time control event (parameter change).
     */
    virtual void apply_control_event(uint32_t param_id, float value) {
        // Default: ignore
    }

    /**
     * @brief Allocate a persistent VRAM buffer and return a handle.
     */
    virtual uint32_t allocate_buffer(size_t size) = 0;

    /**
     * @brief Upload data to a previously allocated VRAM buffer.
     */
    virtual bool upload_buffer(uint32_t handle, const void* data, size_t size) = 0;

    /**
     * @brief Free a previously allocated VRAM buffer.
     */
    virtual void free_buffer(uint32_t handle) = 0;

    /**
     * @brief Enable or disable zero-copy bypass for low-latency tracking.
     */
    virtual void set_zero_copy_bypass(bool enabled, int shm_fd, size_t shm_size) = 0;

    /**
     * @brief Provide a pointer to the start of SHM for offset calculation.
     */
    virtual void set_shm_ptr(void* ptr) = 0;

    /**
     * @brief Returns metadata about the node.
     */
    virtual DspNodeDescriptor get_descriptor() const = 0;
};

/**
 * @brief Manages the heterogeneous hardware pool and performs load balancing.
 */
class BackendManager {
    std::vector<std::unique_ptr<IDspNode>> nodes_;
    const uint32_t LOAD_THRESHOLD = 80; // 80% load threshold for fallback
public:
    void register_node(std::unique_ptr<IDspNode> node) {
        nodes_.push_back(std::move(node));
    }

    /**
     * @brief Identifies the best processor (HW or CPU) based on current load.
     * @return Pointer to node if HW is optimal, nullptr if CPU fallback is required.
     */
    IDspNode* get_optimal_node(DspWorkloadType type, const Ipc::WorkerControlBlock& status) {
        for (auto& node : nodes_) {
            if (node->get_descriptor().type == type) {
                // Check if HW is overloaded
                if (status.load_pct.load(std::memory_order_relaxed) < LOAD_THRESHOLD) {
                    return node.get();
                }
            }
        }
        return nullptr; // Fallback to CPU
    }

    const std::vector<std::unique_ptr<IDspNode>>& get_all_nodes() const {
        return nodes_;
    }
};

} // namespace Daemon
} // namespace DspAccel
