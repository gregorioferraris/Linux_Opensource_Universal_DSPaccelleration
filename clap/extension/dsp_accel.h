#pragma once
#include <clap/clap.h>

#ifdef __cplusplus
extern "C" {
#endif

// Main extension ID for requesting hardware acceleration
#define CLAP_EXT_DSP_ACCEL "com.example.dsp_accel"

// Workload types for smart routing
typedef enum {
    DSP_TYPE_MASSIVELY_PARALLEL = 0, // GPUs (FIR, FFT, massive additive)
    DSP_TYPE_TENSOR_ML = 1,          // NPUs (Neural Nets, Voice Isolation)
    DSP_TYPE_SEQUENTIAL_FEEDBACK = 2 // CPUs (Analog modeled compressors, IIR)
} clap_dsp_workload_type_t;

// Metadata extension ID exposed by the plugin
#define CLAP_EXT_DSP_ACCEL_METADATA "com.example.dsp_accel_metadata"

// Metadata exposed by the plugin to the host Load Balancer
typedef struct clap_plugin_dsp_accel_metadata {
    // Returns the ideal workload type for this plugin algorithm
    clap_dsp_workload_type_t (*get_workload_type)(const clap_plugin_t *plugin);
    
    // Returns the estimated cost in MFLOPS to help the Load Balancer
    float (*get_estimated_cost)(const clap_plugin_t *plugin);
} clap_plugin_dsp_accel_metadata_t;

// API exposed by the host (PipeWire) to the plugin
typedef struct clap_host_dsp_accel {
    // Requests the host to allocate an IPC shared memory buffer 
    // tailored for the plugin's metadata workload type.
    // Returns a file descriptor for the IPC shm ring buffer, or -1 for CPU fallback.
    int (*request_hardware_routing)(const clap_host_t *host, const clap_plugin_t *plugin);

    // Notifies the host that the plugin no longer needs the hardware slot.
    // This allows the Load Balancer to re-allocate the compute resource to another plugin.
    void (*release_hardware_routing)(const clap_host_t *host, const clap_plugin_t *plugin);
} clap_host_dsp_accel_t;

#ifdef __cplusplus
}
#endif
