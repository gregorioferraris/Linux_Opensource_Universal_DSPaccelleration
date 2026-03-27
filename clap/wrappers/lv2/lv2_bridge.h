#pragma once
/**
 * LV2 -> DSP Accel Bridge
 *
 * This header provides glue between the LV2 plugin API and the
 * DSP Acceleration Daemon (PipeWire module).
 *
 * Usage (inside an LV2 plugin's run() callback):
 *   #include "lv2_bridge.h"
 *   // On plugin instantiate:
 *   DspAccelLv2Handle* h = dsp_accel_lv2_connect(DSP_TYPE_MASSIVELY_PARALLEL, 80.0f);
 *   // On run():
 *   if (!dsp_accel_lv2_dispatch(h, input_port, output_port, n_samples)) {
 *       // CPU fallback...
 *   }
 *   // On cleanup:
 *   dsp_accel_lv2_disconnect(h);
 */

#include "../../../sdk/include/dsp_accel_sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef dsp_accel_sdk_ctx_t DspAccelLv2Handle;

/**
 * @brief Connect to the DSP Acceleration daemon from an LV2 plugin.
 * Must be called in the LV2 instantiate() callback.
 */
static inline DspAccelLv2Handle* dsp_accel_lv2_connect(DspWorkloadType type, float cost_mflops) {
    return dsp_accel_sdk_connect(type, cost_mflops);
}

/**
 * @brief Dispatch a mono/stereo audio block from an LV2 port to the daemon.
 * Returns true on success, false on failure (caller should fall back to CPU).
 */
static inline int dsp_accel_lv2_dispatch(DspAccelLv2Handle* h, float* in, float* out, unsigned n_samples) {
    float* bufs[2] = {in, out};
    return dsp_accel_sdk_dispatch(h, bufs, (int)n_samples, 2);
}

/**
 * @brief Returns latency samples for LV2 latency port reporting.
 */
static inline int dsp_accel_lv2_latency(DspAccelLv2Handle* h) {
    return dsp_accel_sdk_get_latency_samples(h);
}

/**
 * @brief Disconnect and clean up. Call from LV2 cleanup().
 */
static inline void dsp_accel_lv2_disconnect(DspAccelLv2Handle* h) {
    dsp_accel_sdk_disconnect(h);
}

#ifdef __cplusplus
}
#endif
