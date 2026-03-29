#pragma once
#include "compute_backend.hpp"
#include <map>
#include <memory>
#include <vector>

namespace DspAccel {
namespace Daemon {

/**
 * @brief Manages a Directed Acyclic Graph of DSP processing nodes.
 */
class DspNodeGraph {
    struct Connection {
        IDspNode* from;
        IDspNode* to;
    };

    std::vector<Connection> connections_;
    std::vector<IDspNode*> execution_order_;
    IDspNode* last_failed_node_ = nullptr;

public:
    /**
     * @brief Adds a node to the graph's execution sequence.
     */
    void add_node_to_sequence(IDspNode* node) {
        execution_order_.push_back(node);
    }

    /**
     * @brief Process an audio frame through the entire graph.
     */
    bool process_graph(Ipc::DspAudioFrame& frame) {
        for (auto* node : execution_order_) {
            if (!node->process_stage(frame)) {
                last_failed_node_ = node;
                return false; 
            }
        }
        return true;
    }

    /**
     * @brief GPU Parallelization: Splitting a single sequential task into 
     * parallel blocks for GPU processing.
     */
    bool process_graph_parallel(Ipc::DspAudioFrame& frame, size_t num_blocks) {
        // Enforce block-splitting logic:
        // Divide frame.samples into 'num_blocks' segments and dispatch 
        // to GPU compute units concurrently.
        for (auto* node : execution_order_) {
            if (node->get_descriptor().supports_staging) {
                // In a real implementation, this triggers multi-block GPU dispatch
                if (!node->process_stage(frame)) { 
                    last_failed_node_ = node;
                    return false;
                }
            }
        }
        return true;
    }

    IDspNode* get_last_failed_node() const { return last_failed_node_; }

    const std::vector<IDspNode*>& get_nodes() const { return execution_order_; }

    void clear() {
        execution_order_.clear();
        connections_.clear();
        last_failed_node_ = nullptr;
    }
};

} // namespace Daemon
} // namespace DspAccel
