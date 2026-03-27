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

    DspNodeDescriptor get_descriptor() const override {
        return { "Simulacrum Node", DSP_TYPE_MASSIVELY_PARALLEL, 1024, false };
    }
};

void test_control_flow() {
    std::cout << "[Test] Starting Control Bus Simulacrum Test..." << std::endl;

    // 1. Setup Shared Memory (Mock)
    DspSharedMemory shm = {};
    
    // 2. Setup SDK-like producer behavior
    uint32_t PARAM_GAIN = 1;
    DspControlEvent event = { PARAM_GAIN, 0.75f };
    std::cout << "Plugin: Pushing Gain = 0.75 to Control Bus..." << std::endl;
    bool pushed = shm.control_bus.push(event);
    assert(pushed);

    // 3. Setup Worker-like consumer behavior
    MockSimulacrumNode node;
    std::cout << "Worker: Draining Control Bus..." << std::endl;
    
    DspControlEvent received_event;
    int events_processed = 0;
    while (shm.control_bus.pop(received_event)) {
        node.apply_control_event(received_event.parameter_id, received_event.value);
        events_processed++;
    }

    assert(events_processed == 1);
    std::cout << "[Test] Control Bus Simulacrum: PASSED" << std::endl;
}

int main() {
    test_control_flow();
    return 0;
}
