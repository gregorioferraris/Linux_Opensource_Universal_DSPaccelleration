#include "../sdk/include/dsp_accel_sdk.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <sys/resource.h>
#include <iomanip>
#include <cmath>

/**
 * @brief Simula il comportamento di una DAW (es. Bitwig, REAPER) che carica un plugin DSPA.
 * Monitora la latenza RTT e il tempo CPU speso dall'host per il dispatch.
 */
void run_daw_simulation(const char* mode_name, int frames, int channels, DspWorkloadType type) {
    std::cout << "\n=== DAW Simulation: " << mode_name << " ===" << std::endl;
    std::cout << "Config: " << frames << " samples, " << channels << " channels" << std::endl;

    // 1. Simulazione Caricamento Plugin
    auto start_load = std::chrono::high_resolution_clock::now();
    dsp_acc_sdk_ctx_t* ctx = dsp_accel_sdk_connect(type, 100.0f);
    auto end_load = std::chrono::high_resolution_clock::now();

    if (!ctx) {
        std::cerr << "FAILED: Could not connect to DSPA Daemon. Is it running?" << std::endl;
        return;
    }
    
    auto load_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_load - start_load).count();
    std::cout << "Plugin Load Time: " << load_time << "ms" << std::endl;

    // 2. Setup Audio Buffers
    std::vector<float*> buffers(channels);
    for (int i = 0; i < channels; ++i) {
        buffers[i] = new float[frames];
        for (int s = 0; s < frames; ++s) buffers[i][s] = std::sin(s * 0.1f);
    }

    // 3. Loop di processamento (1000 blocchi)
    const int num_blocks = 1000;
    std::vector<double> latencies;
    latencies.reserve(num_blocks);

    struct rusage usage_start, usage_end;
    getrusage(RUSAGE_SELF, &usage_start);

    auto start_proc = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_blocks; ++i) {
        auto t1 = std::chrono::high_resolution_clock::now();
        
        if (!dsp_accel_sdk_dispatch(ctx, buffers.data(), frames, channels)) break;
        if (!dsp_accel_sdk_read_output(ctx, buffers.data(), frames, channels)) break;

        auto t2 = std::chrono::high_resolution_clock::now();
        latencies.push_back(std::chrono::duration<double, std::micro>(t2 - t1).count());
    }

    auto end_proc = std::chrono::high_resolution_clock::now();
    getrusage(RUSAGE_SELF, &usage_end);

    // 4. Analisi Risultati
    double avg_latency = 0;
    for (double l : latencies) avg_latency += l;
    avg_latency /= latencies.size();

    double total_cpu_time = (usage_end.ru_utime.tv_sec - usage_start.ru_utime.tv_sec) +
                            (usage_end.ru_utime.tv_usec - usage_start.ru_utime.tv_usec) / 1e6;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Avg Round-Trip Latency: " << avg_latency << " us" << std::endl;
    std::cout << "Total Host CPU Time (User): " << total_cpu_time << " s" << std::endl;
    std::cout << "Efficiency: " << (total_cpu_time / num_blocks) * 1e6 << " us/block" << std::endl;

    // Cleanup
    dsp_accel_sdk_disconnect(ctx);
    for (auto b : buffers) delete[] b;
}

int main() {
    // Test v1.0: Mixing (High Buffer)
    run_daw_simulation("V1.0 MIXING (GPU)", 256, 2, DSP_ACCEL_TYPE_MASSIVELY_PARALLEL);

    // Test v2.0: Tracking (Low Buffer)
    run_daw_simulation("V2.0 TRACKING (ARRAY)", 16, 2, DSP_ACCEL_TYPE_ARRAY_PROCESSOR);

    return 0;
}