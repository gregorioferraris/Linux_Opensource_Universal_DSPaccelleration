#pragma once
/**
 * VST3 -> DSP Accel Bridge
 * 
 * This header provides the glue layer allowing VST3 plugins on Linux
 * to communicate with the DSP Acceleration Daemon (PipeWire module).
 * 
 * Usage (inside a VST3 plugin's processAudio override):
 *   #include "vst3_bridge.h"
 *   DspAccel::Vst3Bridge bridge;
 *   if (bridge.connect()) {
 *       bridge.dispatch(data.inputs[0].channelBuffers32, numSamples, numChannels);
 *   }
 */

#include "../../../sdk/include/dsp_accel_sdk.h"

#ifdef __cplusplus
namespace DspAccel {

class Vst3Bridge {
public:
    Vst3Bridge() : sdk_ctx_(nullptr) {}
    ~Vst3Bridge() {
        if (sdk_ctx_) dsp_accel_sdk_disconnect(sdk_ctx_);
    }

    /**
     * @brief Connects to the DSP daemon via IPC.
     * Must be called from the VST3 initialize() or setActive(true) callback.
     */
    bool connect(DspWorkloadType type = DSP_TYPE_MASSIVELY_PARALLEL, float cost_mflops = 100.0f) {
        sdk_ctx_ = dsp_accel_sdk_connect(type, cost_mflops);
        return sdk_ctx_ != nullptr;
    }

    /**
     * @brief Dispatches audio to the hardware accelerator.
     * If dispatch fails, signals the caller to do CPU fallback.
     * @param channel_buffers  Array of float pointers (one per channel)
     * @param num_samples      Number of audio frames in this block
     * @param num_channels     Number of audio channels
     */
    bool dispatch(float** channel_buffers, int num_samples, int num_channels) {
        if (!sdk_ctx_) return false;
        return dsp_accel_sdk_dispatch(sdk_ctx_, channel_buffers, num_samples, num_channels);
    }

    /**
     * @brief Returns the latency in samples introduced by the hardware accelerator.
     * VST3 hosts use this to apply Plugin Delay Compensation (PDC) automatically.
     */
    int get_latency_samples() const {
        if (!sdk_ctx_) return 0;
        return dsp_accel_sdk_get_latency_samples(sdk_ctx_);
    }

private:
    dsp_accel_sdk_ctx_t* sdk_ctx_;
};

} // namespace DspAccel
#endif // __cplusplus
