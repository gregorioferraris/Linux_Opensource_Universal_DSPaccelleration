#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <cassert>
#include "../daemon/ipc/include/ipc_shm.hpp"

using namespace DspAccel::Ipc;

void test_ring_buffer_mt() {
    std::cout << "[Test] Ring Buffer Multi-Threaded (SPSC): " << std::flush;
    
    constexpr size_t CAPACITY = 1000;
    constexpr int NUM_ITEMS = 100000;
    
    ShmSPSCRingBuffer<int, CAPACITY> rb;
    std::atomic<bool> start{false};
    std::atomic<int> received_count{0};
    std::atomic<bool> failed{false};

    // Consumer thread
    std::thread consumer([&]() {
        while (!start) std::this_thread::yield();
        
        int last_val = -1;
        while (received_count < NUM_ITEMS) {
            int val;
            if (rb.pop(val)) {
                if (val != last_val + 1) {
                    std::cerr << "\n[Error] Out of order! Expected " << last_val + 1 << ", got " << val << std::endl;
                    failed = true;
                    break;
                }
                last_val = val;
                received_count.fetch_add(1);
            }
        }
    });

    // Producer thread
    std::thread producer([&]() {
        while (!start) std::this_thread::yield();
        
        for (int i = 0; i < NUM_ITEMS; i++) {
            while (!rb.push(i)) {
                if (failed) return;
                std::this_thread::yield(); // Wait for space
            }
        }
    });

    start = true;
    producer.join();
    consumer.join();

    if (!failed) {
        assert(received_count == NUM_ITEMS);
        std::cout << "PASSED (" << NUM_ITEMS << " items)" << std::endl;
    } else {
        std::cout << "FAILED" << std::endl;
        exit(1);
    }
}

int main() {
    try {
        test_ring_buffer_mt();
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
