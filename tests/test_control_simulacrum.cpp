#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include "../daemon/ipc/include/ipc_shm.hpp"
#include "../daemon/compute_backend.hpp"

using namespace DspAccel::Ipc;
using namespace DspAccel::Daemon;

/**
 * @brief A Mock Node that acts as our "Simulacrum"
 * It simply logs parameter changes to stdout.
 */
class MockSimulacrumNode : public IDspNode {
    float gain_ = 1.0f;
public:
    bool init(int index) override { return true; }
    bool process_stage(DspAudioFrame& frame) override { return true; }
    
    void apply_control_event(uint32_t param_id, float value) override {
        std::cout << "[Simulacrum] Hardware Parameter " << param_id << " updated to: " << value << std::endl;
        if (param_id == 1) gain_ = value;
    }

    uint32_t allocate_buffer(size_t size) override { return 0; }
    bool upload_buffer(uint32_t handle, const void* data, size_t size) override { return true; }
    void free_buffer(uint32_t handle) override {}
    void set_zero_copy_bypass(bool enabled, int shm_fd, size_t shm_size) override {}
    void set_shm_ptr(void* ptr) override {}

    DspNodeDescriptor get_descriptor() const override {
        return { "Simulacrum Node", DSP_ACCEL_TYPE_MASSIVELY_PARALLEL, 1024, false };
    }
};

void test_control_flow() {
    std::cout << "[Test] Starting Control Bus Simulacrum Test..." << std::endl;

    // 1. Setup Shared Memory (Mock)
    DspSharedMemory shm = {};
    
    // 2. Setup SDK-like producer behavior
    const int NUM_EVENTS = 5;
    std::cout << "Plugin: Pushing " << NUM_EVENTS << " events to Control Bus..." << std::endl;
    
    for (int i = 0; i < NUM_EVENTS; ++i) {
        uint32_t param_id = i + 1;
        float value = 0.1f * (i + 1);
        DspControlEvent event = { param_id, value };
        bool pushed = shm.control_bus.push(event);
        assert(pushed);
    }

    // Verify buffer isn't empty
    assert(!shm.control_bus.empty());

    // 3. Setup Worker-like consumer behavior
    MockSimulacrumNode node;
    std::cout << "Worker: Processing all events..." << std::endl;
    
    DspControlEvent received_event;
    int events_processed = 0;
    while (shm.control_bus.pop(received_event)) {
        node.apply_control_event(received_event.parameter_id, received_event.value);
        assert(received_event.parameter_id == (uint32_t)(events_processed + 1));
        events_processed++;
    }

    assert(events_processed == NUM_EVENTS);
    assert(shm.control_bus.empty());
    std::cout << "[Test] Control Bus Simulacrum: PASSED" << std::endl;
}

int main() {
    test_control_flow();
    return 0;
}
