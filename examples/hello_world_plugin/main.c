/**
 * Minimal CLAP Plugin Example using the DSP Accel SDK
 * 
 * This example shows how to connect to the hardware supervisor
 * and offload a simple gain/processing block.
 */

#include "../../sdk/include/dsp_accel_sdk.h"
#include <stdio.h>

int main() {
    printf("--- DSP Accel: Hello World Plugin ---\n");

    // 1. Connect to the Supervisor (requesting GPU/Parallel acceleration)
    dsp_accel_sdk_ctx_t *ctx = dsp_accel_sdk_connect(DSP_ACCEL_TYPE_MASSIVELY_PARALLEL, 10.0f);
    
    if (!ctx) {
        printf("Failed to connect to DSP Supervisor. Falling back to CPU.\n");
        return 1;
    }

    // 2. Prepare mock audio data (1 block, 2 channels, 256 frames)
    float left[256], right[256];
    float *buffers[2] = { left, right };
    for (int i = 0; i < 256; i++) { left[i] = right[i] = 0.5f; }

    // 3. Dispatch to hardware
    printf("Dispatching audio block to hardware worker...\n");
    if (dsp_accel_sdk_dispatch(ctx, buffers, 256, 2)) {
        
        // 4. Read back the processed data (wait for design latency)
        if (dsp_accel_sdk_read_output(ctx, buffers, 256, 2)) {
            printf("Processing complete! First sample: %f\n", left[0]);
        }
    }

    // 5. Cleanup
    dsp_accel_sdk_disconnect(ctx);
    printf("Disconnected.\n");

    return 0;
}
