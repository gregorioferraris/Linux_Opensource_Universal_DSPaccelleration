#include <iostream>
#include <cassert>
#include <thread>
#include <vector>
#include "../daemon/ipc/include/ipc_shm.hpp"

using namespace DspAccel::Ipc;

void test_ring_buffer_basic() {
    std::cout << "[Test] Ring Buffer Basic: ";
    ShmSPSCRingBuffer<int, 10> rb;
    
    assert(rb.empty());
    
    for (int i = 0; i < 9; i++) {
        assert(rb.push(i));
    }
    
    // Should be full (capacity 10, but SPSC logic uses one slot for sentinel or similar depending on implementation)
    // Actually our implementation uses (current + 1) % Capacity == head.
    // So for Capacity 10, it can hold 9 items.
    assert(!rb.push(99)); 

    for (int i = 0; i < 9; i++) {
        int val;
        assert(rb.pop(val));
        assert(val == i);
    }
    
    assert(rb.empty());
    std::cout << "PASSED" << std::endl;
}

void test_worker_control_block() {
    std::cout << "[Test] Worker Control Block: ";
    WorkerControlBlock wcb;
    wcb.heartbeat.store(0);
    wcb.last_error.store(WorkerError::NONE);
    
    wcb.heartbeat.fetch_add(1);
    assert(wcb.heartbeat.load() == 1);
    
    wcb.last_error.store(WorkerError::DEVICE_LOST);
    assert(wcb.last_error.load() == WorkerError::DEVICE_LOST);
    std::cout << "PASSED" << std::endl;
}

int main() {
    std::cout << "=========================================" << std::endl;
    std::cout << "   Linux DSP Accel - Core Unit Tests     " << std::endl;
    std::cout << "=========================================" << std::endl;

    try {
        test_ring_buffer_basic();
        test_worker_control_block();
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "=========================================" << std::endl;
    std::cout << "   All core tests PASSED successfully!   " << std::endl;
    return 0;
}
