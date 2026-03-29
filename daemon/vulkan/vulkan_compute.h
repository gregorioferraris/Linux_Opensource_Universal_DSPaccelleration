#pragma once

#include <cstdint>
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

struct dsp_vulkan_context;

struct dsp_vulkan_context* dsp_vulkan_init(int device_index);
void dsp_vulkan_destroy(struct dsp_vulkan_context* ctx);

bool dsp_vulkan_dispatch(struct dsp_vulkan_context* ctx, float* samples, uint32_t frame_count, uint32_t channel_count, float gain, float threshold, float param1, float param2, uint32_t algo_id, uint32_t state_handle);
bool dsp_vulkan_dispatch_zero_copy(struct dsp_vulkan_context* ctx, uint32_t shm_buffer_handle, uint32_t shm_offset, uint32_t frame_count, uint32_t channel_count, float gain, float threshold, float param1, float param2, uint32_t algo_id, uint32_t state_handle);

uint32_t dsp_vulkan_allocate_buffer(struct dsp_vulkan_context* ctx, size_t size);
bool dsp_vulkan_upload_buffer(struct dsp_vulkan_context* ctx, uint32_t handle, const void* data, size_t size);
void dsp_vulkan_free_buffer(struct dsp_vulkan_context* ctx, uint32_t handle);
uint32_t dsp_vulkan_import_shm(struct dsp_vulkan_context* ctx, int shm_fd, size_t shm_size);

#ifdef __cplusplus
}
#endif