#pragma once
/**
 * DSP Acceleration System — Public SDK
 * 
 * This is the single unified header that plugin developers include
 * to integrate hardware DSP acceleration, regardless of whether
 * they use CLAP, VST3, or LV2.
 *
 * Example usage:
 *   #include "dsp_accel_sdk.h"
 *
 *   dsp_accel_sdk_ctx_t* ctx = dsp_accel_sdk_connect(DSP_TYPE_MASSIVELY_PARALLEL, 120.0f);
 *   ... on each audio block:
 *   if (!dsp_accel_sdk_dispatch(ctx, buffers, frame_count, channels)) {
 *       // CPU fallback
 *   }
 *   dsp_accel_sdk_disconnect(ctx);
 */

#include <stdint.h>
#include <stdbool.h>
#include "../platform/platform_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

// ─── SDK Version ──────────────────────────────────────────────────
#define DSP_ACCEL_SDK_VERSION_MAJOR 0
#define DSP_ACCEL_SDK_VERSION_MINOR 1
#define DSP_ACCEL_SDK_VERSION_PATCH 0

// ─── Workload Types ───────────────────────────────────────────────
typedef enum {
    DSP_TYPE_MASSIVELY_PARALLEL  = 0, // FIR, FFT, Convolution Reverb -> GPU (Vulkan/CUDA)
    DSP_TYPE_TENSOR_ML           = 1, // Neural Nets, Voice/Noise AI   -> NPU (OpenVINO/TensorRT)
    DSP_TYPE_SEQUENTIAL_FEEDBACK = 2, // Analog-modeled IIR filters    -> CPU Fallback
    DSP_TYPE_ARRAY_PROCESSOR     = 3, // SIMD/VLIW Data Streams        -> NPU Array / Vector Engine
    DSP_TYPE_FIXED_POINT_DSP     = 4, // 16/24-bit fixed point logic   -> Specialized DSPs
    DSP_TYPE_QUANTIZED_ML        = 5  // INT8/INT4 Inference           -> Hardware Accelerators
} DspWorkloadType;

// ─── Opaque Connection Context ────────────────────────────────────
typedef struct dsp_accel_sdk_ctx dsp_accel_sdk_ctx_t;

// ─── Core API ─────────────────────────────────────────────────────

/**
 * @brief Connect to the DSP daemon. Negotiates a hardware compute slot.
 * @param type         Algorithmic workload type for smart routing.
 * @param cost_mflops  Estimated compute cost per block in MegaFLOPS.
 * @return Opaque context or NULL if no hardware available (use CPU fallback).
 */
dsp_accel_sdk_ctx_t* dsp_accel_sdk_connect(DspWorkloadType type, float cost_mflops);

/**
 * @brief Dispatch an audio block to the assigned hardware accelerator.
 * This function is NON-BLOCKING. It pushes into the IPC ring buffer.
 * @param ctx            Connection context from dsp_accel_sdk_connect().
 * @param channel_buffers Array of float* per channel (interleaving not needed).
 * @param frames          Number of samples per channel in this block.
 * @param channels        Number of audio channels.
 * @return true if the block was dispatched, false if the IPC queue is full.
 */
bool dsp_accel_sdk_dispatch(dsp_accel_sdk_ctx_t* ctx, float** channel_buffers, int frames, int channels);

/**
 * @brief Read processed output from the hardware accelerator back into DAW buffers.
 * Blocks until daemon signals output is ready (fixed 1-block latency by design).
 * Must be called exactly once after each successful dsp_accel_sdk_dispatch().
 * @return true if output was read successfully, false on error.
 */
bool dsp_accel_sdk_read_output(dsp_accel_sdk_ctx_t* ctx, float** channel_buffers, int frames, int channels);

/**
 * @brief Returns the fixed latency (in samples) added by the accelerator.
 * Plugin hosts use this value for Plugin Delay Compensation (PDC).
 */
int dsp_accel_sdk_get_latency_samples(dsp_accel_sdk_ctx_t* ctx);

/**
 * @brief Retrieve a monitoring snapshot of this connection's resource usage.
 */
typedef struct {
    float   hw_load_percent;     // % load on the assigned compute node (0-100)
    uint32_t xrun_count;         // number of buffer overruns since connect
    uint32_t dispatched_blocks;  // total blocks successfully dispatched
    float   avg_roundtrip_us;    // average hardware round-trip in microseconds
} DspAccelStats;

bool dsp_accel_sdk_get_stats(dsp_accel_sdk_ctx_t* ctx, DspAccelStats* out_stats);

bool dsp_accel_sdk_set_param(dsp_accel_sdk_ctx_t* ctx, uint32_t param_id, float value);

/**
 * @brief Allocate a persistent buffer in hardware VRAM.
 * @return A handle to the buffer or 0 on failure.
 */
uint32_t dsp_accel_sdk_allocate_vram(dsp_accel_sdk_ctx_t* ctx, size_t size);

/**
 * @brief Upload data to a previously allocated VRAM buffer.
 */
bool dsp_accel_sdk_upload_buffer(dsp_accel_sdk_ctx_t* ctx, uint32_t handle, const void* data, size_t size);

/**
 * @brief Free a VRAM buffer.
 */
void dsp_accel_sdk_free_vram(dsp_accel_sdk_ctx_t* ctx, uint32_t handle);

/**
 * @brief Enable or disable zero-copy bypass for low-latency tracking.
 * This should be enabled during tracking and disabled for final mixdown.
 */
void dsp_accel_sdk_set_zero_copy_bypass(dsp_accel_sdk_ctx_t* ctx, bool enabled);

/**
 * @brief Disconnect from the daemon and release the hardware compute slot.
 * Safe to call even if ctx is NULL.
 */
void dsp_accel_sdk_disconnect(dsp_accel_sdk_ctx_t* ctx);

#ifdef __cplusplus
}
#endif
