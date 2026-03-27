#include <iostream>
#include <vector>
#include <memory>
#include "../daemon/node_graph.hpp"

using namespace DspAccel::Daemon;
using namespace DspAccel::Ipc;

class MockNode : public IDspNode {
    std::string name_;
    bool should_fail_ = false;
public:
    MockNode(const std::string& name) : name_(name) {}
    
    void set_should_fail(bool fail) { should_fail_ = fail; }

    bool init(int index) override { return true; }
    
    bool process_stage(DspAudioFrame& frame) override {
        if (should_fail_) {
            std::cout << "[MockNode " << name_ << "] !!! SIMULATED CRASH !!!" << std::endl;
            return false;
        }
        std::cout << "[MockNode " << name_ << "] Processing buffer..." << std::endl;
        return true;
    }

    DspNodeDescriptor get_descriptor() const override {
        return { .name = name_, .type = DSP_TYPE_MASSIVELY_PARALLEL };
    }
};

void test_graph_crash_resilience() {
    std::cout << "[Test] Node-based Graph Crash Resilience: " << std::endl;
    
    DspNodeGraph graph;
    MockNode node1("GPU_FFT");
    MockNode node2("NPU_ML_DENOISER");
    MockNode node3("GPU_IFFT");

    graph.add_node_to_sequence(&node1);
    graph.add_node_to_sequence(&node2);
    graph.add_node_to_sequence(&node3);

    DspAudioFrame frame;

    // Normal operation
    std::cout << "Step 1: Normal processing..." << std::endl;
    assert(graph.process_graph(frame));

    // Simulated failure in the middle of the graph
    std::cout << "Step 2: Inducing failure in middle node..." << std::endl;
    node2.set_should_fail(true);
    
    if (!graph.process_graph(frame)) {
        auto* failed = graph.get_last_failed_node();
        std::cout << "[Resilience] Detected failure in node: " << failed->get_descriptor().name << std::endl;
        std::cout << "[Resilience] Action: Falling back to CPU processing for this plugin instance." << std::endl;
        assert(failed == &node2);
    } else {
        std::cerr << "FAILED: Graph did not report node failure!" << std::endl;
        exit(1);
    }

    std::cout << "[Test] PASSED" << std::endl;
}

int main() {
    test_graph_crash_resilience();
    return 0;
}
