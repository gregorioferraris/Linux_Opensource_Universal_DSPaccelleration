#pragma once
#include <cstdint>
#include <algorithm>
#include "../ipc/include/ipc_shm.hpp"

namespace DspAccel {
namespace Daemon {

/**
 * @brief Utility for high-performance parallel DSP on CPU (SIMD fallback).
 */
class ParallelProcessor {
public:
    /**
     * @brief Parallelized gain application.
     * In a real implementation, this would use AVX/SSE intrinsics.
     */
    static void apply_gain_parallel(Ipc::DspAudioFrame& frame, float gain) {
        // Simple loop that compilers can auto-vectorize
        uint32_t total_samples = frame.frame_count * frame.channel_count;
        for (uint32_t i = 0; i < total_samples; i++) {
            frame.samples[i] *= gain;
        }
    }

    /**
     * @brief Parallelized stereo-link compression stage (Mock).
     */
    static void process_compression_parallel(Ipc::DspAudioFrame& frame) {
        // Split processing into chunks for OMP or simple vectorization
        // ... implementation of parallelizable sidechain / reduction ...
    }
};

} // namespace Daemon
} // namespace DspAccel
