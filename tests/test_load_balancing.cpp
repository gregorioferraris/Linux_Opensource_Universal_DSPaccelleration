#include <iostream>
#include <cassert>
#include <thread>
#include "../daemon/compute_backend.hpp"
#include "../daemon/parallel_processor.hpp"
#include "../daemon/ipc/include/ipc_shm.hpp"

using namespace DspAccel::Daemon;
using namespace DspAccel::Ipc;

void test_dynamic_load_balancing() {
    std::cout << "[Test] Dynamic Load Balancing & CPU Fallback: " << std::endl;

    BackendManager manager;
    auto node = std::make_unique<NpuArrayBackend>();
    node->init(0);
    manager.register_node(std::move(node));

    WorkerControlBlock status;
    DspAudioFrame frame;
    for(int i=0; i<frame.frame_count * frame.channel_count; ++i) frame.samples[i] = 1.0f;

    // Case 1: Low Load (HW preferred)
    std::cout << "Step 1: Low load (20%)... " << std::flush;
    status.load_pct.store(20);
    IDspNode* selected = manager.get_optimal_node(DSP_TYPE_ARRAY_PROCESSOR, status);
    assert(selected != nullptr);
    std::cout << "OK (Used Hardware: " << selected->get_descriptor().name << ")" << std::endl;

    // Case 2: High Load (CPU Fallback required)
    std::cout << "Step 2: High load (90%)... " << std::flush;
    status.load_pct.store(90);
    selected = manager.get_optimal_node(DSP_TYPE_ARRAY_PROCESSOR, status);
    assert(selected == nullptr); // Fallback to CPU
    
    if (selected == nullptr) {
        std::cout << "OK (CPU Fallback Triggered)" << std::endl;
        std::cout << "Step 3: Executing Parallel SIMD Gain on CPU... " << std::flush;
        ParallelProcessor::apply_gain_parallel(frame, 0.5f);
        assert(frame.samples[0] == 0.5f);
        std::cout << "OK" << std::endl;
    }

    std::cout << "[Test] PASSED" << std::endl;
}

int main() {
    test_dynamic_load_balancing();
    return 0;
}
