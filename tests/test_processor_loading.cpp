#include "../daemon/ipc/include/ipc_shm.hpp"
#include <iostream>
#include <chrono>
#include <cstring>

using namespace DspAccel::Ipc;

/**
 * @brief Benchmark del caricamento dati nell'area di staging per i processori (GPU/Array/NPU).
 */
void test_data_loading_throughput() {
    std::cout << "=== Data Loading Benchmark (Staging Area) ===" << std::endl;

    DspSharedMemory* shm = new DspSharedMemory();
    const size_t test_size = 1024 * 1024 * 4; // 4MB
    uint8_t* dummy_data = new uint8_t[test_size];
    std::memset(dummy_data, 0xAA, test_size);

    // Simula caricamento di un'Impulse Response o Array di coefficienti
    auto start = std::chrono::high_resolution_clock::now();
    
    // 1. Copia in staging area
    std::memcpy(shm->data_staging, dummy_data, test_size);

    // 2. Invio richiesta di allocazione/upload
    DspMemoryRequest req;
    req.request_id = 1;
    req.type = DspMemoryRequestType::UPLOAD;
    req.size = test_size;
    req.shm_offset = 0;

    bool pushed = shm->memory_request_bus.push(req);
    
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;

    if (pushed) {
        double throughput = (test_size / (1024.0 * 1024.0)) / (elapsed.count() / 1000.0);
        std::cout << "Data Size: 4 MB" << std::endl;
        std::cout << "Transfer + Request Time: " << elapsed.count() << " ms" << std::endl;
        std::cout << "Throughput: " << throughput << " MB/s" << std::endl;
    } else {
        std::cerr << "Request Bus Full!" << std::endl;
    }

    delete shm;
    delete[] dummy_data;
}

int main() {
    test_data_loading_throughput();
    return 0;
}