#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque context for the Vulkan Compute pipeline
struct dsp_vulkan_context;

// Shader binary version for compatibility checking (fixes gap #5)
typedef struct {
    uint32_t major;  // Breaking: push constant layout or binding changes
    uint32_t minor;  // Non-breaking additions
} DspShaderVersion;

#define DSP_SHADER_VERSION_CURRENT_MAJOR 0
#define DSP_SHADER_VERSION_CURRENT_MINOR 1

// Enumerate available Vulkan compute devices
int dsp_vulkan_enumerate_devices(void);

// Initialize the GPU context via Vulkan API for a specific device index
struct dsp_vulkan_context* dsp_vulkan_init(int device_index);

// Clean up Vulkan context
void dsp_vulkan_destroy(struct dsp_vulkan_context *ctx);

// Dispatch an audio block to the GPU Compute Shader
bool dsp_vulkan_dispatch(struct dsp_vulkan_context *ctx, float* buffer, uint32_t frames, uint32_t channels);

/**
 * @brief Allocate a VRAM buffer and return an opaque handle.
 */
uint32_t dsp_vulkan_allocate_buffer(struct dsp_vulkan_context *ctx, size_t size);

/**
 * @brief Free a VRAM buffer.
 */
void dsp_vulkan_free_buffer(struct dsp_vulkan_context *ctx, uint32_t handle);

/**
 * @brief Upload data into a buffer.
 */
bool dsp_vulkan_upload_buffer(struct dsp_vulkan_context *ctx, uint32_t handle, const void* data, size_t size);

/**
 * @brief Import a memfd (SHM) as a resident Vulkan buffer for 0-copy.
 */
uint32_t dsp_vulkan_import_shm(struct dsp_vulkan_context *ctx, int fd, size_t size);

/**
 * @brief Dispatch using a pre-allocated buffer handle and an offset.
 */
bool dsp_vulkan_dispatch_zero_copy(struct dsp_vulkan_context *ctx, uint32_t buffer_handle, uint32_t offset, uint32_t frames, uint32_t channels);

#ifdef __cplusplus
}
#endif
