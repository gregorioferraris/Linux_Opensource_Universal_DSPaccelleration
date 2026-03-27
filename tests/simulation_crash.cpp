#include <iostream>
#include <thread>
#include <chrono>
#include "../daemon/ipc/include/ipc_shm.hpp"

using namespace DspAccel::Ipc;

/**
 * Simulation of a Worker that "crashes" or "fails" after some time.
 */
void simulated_worker(DspSharedMemory* shm, int id) {
    std::cout << "[Worker Sim] Started Node " << id << std::endl;
    shm->workers[id].is_ready.store(true);
    
    for (int i = 0; i < 20; i++) {
        shm->workers[id].heartbeat.fetch_add(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        if (i == 10) {
            std::cout << "[Worker Sim] !!! CRITICAL HARDWARE FAILURE !!!" << std::endl;
            shm->workers[id].last_error.store(WorkerError::DEVICE_LOST);
            return; // Simulate process exit
        }
    }
}

/**
 * Simulation of the Supervisor monitoring the worker.
 */
void simulated_supervisor(DspSharedMemory* shm, int id) {
    uint64_t last_hb = 0;
    std::cout << "[Supervisor Sim] Monitoring Worker " << id << "..." << std::endl;

    for (int i = 0; i < 40; i++) {
        auto error = shm->workers[id].last_error.load();
        if (error != WorkerError::NONE) {
            std::cout << "[Supervisor Sim] DETECTED FAILURE: " << (int)error << std::endl;
            std::cout << "[Supervisor Sim] >> TRIGGERING ISOLATION & CPU FALLBACK <<" << std::endl;
            return;
        }

        uint64_t hb = shm->workers[id].heartbeat.load();
        if (hb > 0 && hb == last_hb) {
            std::cout << "[Supervisor Sim] WARNING: Heartbeat Stalled!" << std::endl;
        }
        last_hb = hb;

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

int main() {
    std::cout << "--- Crash Resilience Simulation ---" << std::endl;
    
    // Allocate a mock DspSharedMemory block
    auto* shm = new DspSharedMemory();
    
    std::thread worker_thread(simulated_worker, shm, 0);
    std::thread supervisor_thread(simulated_supervisor, shm, 0);

    worker_thread.join();
    supervisor_thread.join();

    delete shm;
    std::cout << "Simulation complete." << std::endl;
    return 0;
}
