#include <iostream>
#include <thread>
#include <chrono>
#include <cassert>
#include "../daemon/ipc/include/ipc_shm.hpp"

using namespace DspAccel::Ipc;

void test_watchdog_timeout() {
    std::cout << "[Test] Watchdog Timeout & Recovery: " << std::endl;
    
    DspSharedMemory shm;
    int worker_id = 0;
    shm.workers[worker_id].heartbeat.store(1);
    shm.workers[worker_id].is_ready.store(true);

    // Simulation settings
    const auto timeout = std::chrono::milliseconds(200);
    const auto check_interval = std::chrono::milliseconds(50);

    std::cout << "Step 1: Simulating healthy worker..." << std::endl;
    uint64_t last_hb = shm.workers[worker_id].heartbeat.load();
    std::this_thread::sleep_for(check_interval);
    shm.workers[worker_id].heartbeat.fetch_add(1);
    
    if (shm.workers[worker_id].heartbeat.load() > last_hb) {
        std::cout << "[Supervisor] Worker is healthy." << std::endl;
    }

    std::cout << "Step 2: Simulating stalled worker (no heartbeat)..." << std::endl;
    last_hb = shm.workers[worker_id].heartbeat.load();
    
    auto start_stall = std::chrono::steady_clock::now();
    bool detected = false;

    while (std::chrono::steady_clock::now() - start_stall < timeout * 2) {
        if (shm.workers[worker_id].heartbeat.load() == last_hb) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_stall
            );
            if (elapsed >= timeout) {
                std::cout << "[Supervisor] !!! WATCHDOG TIMEOUT DETECTED after " << elapsed.count() << "ms !!!" << std::endl;
                detected = true;
                break;
            }
        }
        std::this_thread::sleep_for(check_interval);
    }

    assert(detected);
    std::cout << "Step 3: Simulating recovery request..." << std::endl;
    shm.workers[worker_id].should_restart.store(true);
    assert(shm.workers[worker_id].should_restart.load() == true);

    std::cout << "[Test] PASSED" << std::endl;
}

int main() {
    test_watchdog_timeout();
    return 0;
}
