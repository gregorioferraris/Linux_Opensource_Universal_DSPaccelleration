#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Tipi di accelerazione hardware supportati.
 */
typedef enum {
    DSP_ACCEL_TYPE_MASSIVELY_PARALLEL = 0,
    DSP_ACCEL_TYPE_ARRAY_PROCESSOR     = 1,
    DSP_ACCEL_TYPE_REMOTE              = 2
} DspWorkloadType;

typedef struct dsp_accel_sdk_ctx dsp_accel_sdk_ctx_t;

typedef struct {
    uint32_t xrun_count;
    uint32_t dispatched_blocks;
    float    hw_load_percent;
    float    avg_roundtrip_us;
} DspAccelStats;

/** API di connessione al daemon */
dsp_accel_sdk_ctx_t* dsp_accel_sdk_connect(DspWorkloadType type, float timeout_ms);
void dsp_accel_sdk_disconnect(dsp_accel_sdk_ctx_t* ctx);

/** API di elaborazione audio */
bool dsp_accel_sdk_dispatch(dsp_accel_sdk_ctx_t* ctx, float** buffers, int frames, int channels);
bool dsp_accel_sdk_read_output(dsp_accel_sdk_ctx_t* ctx, float** buffers, int frames, int channels);

/** API di monitoraggio */
bool dsp_accel_sdk_get_stats(dsp_accel_sdk_ctx_t *ctx, DspAccelStats *out);

#ifdef __cplusplus
}
#endif