#include "vulkan_compute.h"
#include <vulkan/vulkan.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct dsp_vulkan_buffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    size_t size;
    bool active;
};

struct dsp_vulkan_context {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue compute_queue;
    uint32_t queue_family_index;
    
    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_set;
    VkCommandPool command_pool;
    
    VkShaderModule shader_module;
    VkPipelineLayout pipeline_layout;
    VkPipeline compute_pipeline;

    // Buffer Pool: Handle-based resident buffers
    struct dsp_vulkan_buffer user_buffers[256];
};

// Helper to open and read the SPIR-V shader binary
static uint32_t* read_spv(const char* filename, size_t* out_size) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        printf("[Vulkan API] ERROR: Cannot open shader file %s\n", filename);
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // Vulkan requires pCode to be 4-byte aligned and size in bytes
    uint32_t* buffer = malloc(size); 
    if(buffer) {
        fread(buffer, 1, size, file);
        *out_size = (size_t)size;
    }
    fclose(file);
    return buffer;
}

int dsp_vulkan_enumerate_devices(void) {
    VkApplicationInfo appInfo = {0};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    VkInstance temp_instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&createInfo, NULL, &temp_instance) != VK_SUCCESS) {
        return 0;
    }
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(temp_instance, &deviceCount, NULL);
    vkDestroyInstance(temp_instance, NULL);
    return (int)deviceCount;
}

struct dsp_vulkan_context* dsp_vulkan_init(int device_index) {
    struct dsp_vulkan_context *ctx = calloc(1, sizeof(struct dsp_vulkan_context));
    if (!ctx) return NULL;

    printf("[Vulkan API] Bootstrapping Vulkan Compute Pipeline...\n");

    // 1. Instance Creation
    VkApplicationInfo appInfo = {0};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "DSP Daemon";
    appInfo.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    if (vkCreateInstance(&createInfo, NULL, &ctx->instance) != VK_SUCCESS) {
        printf("[Vulkan API] ERROR: vkCreateInstance failed.\n");
        free(ctx);
        return NULL;
    }

    // 2. Physical Device Selection
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(ctx->instance, &deviceCount, NULL);
    if (deviceCount == 0) {
        printf("[Vulkan API] ERROR: No Vulkan compatible GPUs found.\n");
        dsp_vulkan_destroy(ctx);
        return NULL;
    }
    VkPhysicalDevice* devices = malloc(sizeof(VkPhysicalDevice) * deviceCount);
    vkEnumeratePhysicalDevices(ctx->instance, &deviceCount, devices);
    if (device_index < 0 || (uint32_t)device_index >= deviceCount) {
        printf("[Vulkan API] ERROR: Device index %d out of bounds (max %d).\n", device_index, deviceCount - 1);
        free(devices);
        dsp_vulkan_destroy(ctx);
        return NULL;
    }
    
    // Choose the specified device 
    ctx->physical_device = devices[device_index];
    free(devices);

    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(ctx->physical_device, &deviceProperties);
    printf("[Vulkan API] Selected GPU: %s\n", deviceProperties.deviceName);

    // 3. Find Compute Queue
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(ctx->physical_device, &queueFamilyCount, NULL);
    VkQueueFamilyProperties* queueFamilies = malloc(sizeof(VkQueueFamilyProperties) * queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(ctx->physical_device, &queueFamilyCount, queueFamilies);

    ctx->queue_family_index = 0xFFFFFFFF;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            ctx->queue_family_index = i;
            break;
        }
    }
    free(queueFamilies);

    // 4. Logical Device Creation with REALTIME Priority (Hardware Scheduling)
    // We check for VK_EXT_global_priority support to ensure audio tasks preempt graphics.
    float queuePriority = 1.0f;
    
    // Request Real-Time Hardware Scheduling and External Memory
    const char* deviceExtensions[] = { 
        VK_EXT_GLOBAL_PRIORITY_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME
    };

    VkDeviceQueueCreateInfo queueCreateInfo = {0};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = ctx->queue_family_index;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo deviceCreateInfo = {0};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceCreateInfo.enabledExtensionCount = 3;
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions;

    if (vkCreateDevice(ctx->physical_device, &deviceCreateInfo, NULL, &ctx->device) != VK_SUCCESS) {
        printf("[Vulkan API] WARNING: Real-Time or External Memory extensions failed. Falling back to standard device.\n");
        
        // Fallback to standard queue if extension is missing (less performant)
        deviceCreateInfo.enabledExtensionCount = 0;
        if (vkCreateDevice(ctx->physical_device, &deviceCreateInfo, NULL, &ctx->device) != VK_SUCCESS) {
            printf("[Vulkan API] ERROR: Logical Device creation failed completely.\n");
            dsp_vulkan_destroy(ctx);
            return NULL;
        }
    }

    vkGetDeviceQueue(ctx->device, ctx->queue_family_index, 0, &ctx->compute_queue);

    // 5. Command Pool For Hardware Interfacing
    VkCommandPoolCreateInfo poolInfo = {0};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = ctx->queue_family_index;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    
    vkCreateCommandPool(ctx->device, &poolInfo, NULL, &ctx->command_pool);

    // 6. Shader Loading
#ifdef SHADER_PATH
    const char* spv_path = SHADER_PATH;
#else
    const char* spv_path = "dsp_shader.spv";
#endif

    size_t spv_size = 0;
    uint32_t* code_ptr = read_spv(spv_path, &spv_size);
    if(code_ptr) {
        VkShaderModuleCreateInfo createShaderInfo = {0};
        createShaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createShaderInfo.codeSize = spv_size;
        createShaderInfo.pCode = code_ptr;
        
        vkCreateShaderModule(ctx->device, &createShaderInfo, NULL, &ctx->shader_module);
        free(code_ptr);
        
        // 7. Pipeline Layout (Push Constants for DSP params injection)
        VkPushConstantRange push_constant = {0};
        push_constant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        push_constant.offset = 0;
        push_constant.size = 24; // Expanded for algo_id and params
        
        VkPipelineLayoutCreateInfo pipelineLayoutInfo = {0};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &push_constant;
        
        vkCreatePipelineLayout(ctx->device, &pipelineLayoutInfo, NULL, &ctx->pipeline_layout);

        // 8. Create Compute Pipeline (Missing in previous version)
        VkComputePipelineCreateInfo pipelineInfo = {0};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = ctx->shader_module;
        pipelineInfo.stage.pName = "main";
        pipelineInfo.layout = ctx->pipeline_layout;

        vkCreateComputePipelines(ctx->device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &ctx->compute_pipeline);

        // 9. Descriptor Set Layout (For Audio and State buffers)
        VkDescriptorSetLayoutBinding bindings[2] = {0};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo descriptorLayout = {0};
        descriptorLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorLayout.bindingCount = 2;
        descriptorLayout.pBindings = bindings;
        vkCreateDescriptorSetLayout(ctx->device, &descriptorLayout, NULL, &ctx->descriptor_set_layout);

        // 10. Descriptor Pool & Set
        VkDescriptorPoolSize poolSize = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
        VkDescriptorPoolCreateInfo poolCreateInfo = {0};
        poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolCreateInfo.maxSets = 1;
        poolCreateInfo.poolSizeCount = 1;
        poolCreateInfo.pPoolSizes = &poolSize;
        vkCreateDescriptorPool(ctx->device, &poolCreateInfo, NULL, &ctx->descriptor_pool);

        VkDescriptorSetAllocateInfo setAllocInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        setAllocInfo.descriptorPool = ctx->descriptor_pool;
        setAllocInfo.descriptorSetCount = 1;
        setAllocInfo.pSetLayouts = &ctx->descriptor_set_layout;
        vkAllocateDescriptorSets(ctx->device, &setAllocInfo, &ctx->descriptor_set);
    }

    return ctx;
}

void dsp_vulkan_destroy(struct dsp_vulkan_context *ctx) {
    if (!ctx) return;
    if (ctx->device) {
        if(ctx->pipeline_layout) vkDestroyPipelineLayout(ctx->device, ctx->pipeline_layout, NULL);
        if(ctx->shader_module) vkDestroyShaderModule(ctx->device, ctx->shader_module, NULL);
        if(ctx->compute_pipeline) vkDestroyPipeline(ctx->device, ctx->compute_pipeline, NULL);
        if(ctx->descriptor_pool) vkDestroyDescriptorPool(ctx->device, ctx->descriptor_pool, NULL);
        if(ctx->descriptor_set_layout) vkDestroyDescriptorSetLayout(ctx->device, ctx->descriptor_set_layout, NULL);
        if(ctx->command_pool) vkDestroyCommandPool(ctx->device, ctx->command_pool, NULL);
        vkDestroyDevice(ctx->device, NULL);
    }
    if (ctx->instance) vkDestroyInstance(ctx->instance, NULL);
    free(ctx);
    printf("[Vulkan API] Contesto Vulkan distrutto in modo sicuro.\n");
}

bool dsp_vulkan_dispatch(struct dsp_vulkan_context *ctx, float* buffer, uint32_t frames, uint32_t channels, float gain, float threshold, float p1, float p2, uint32_t algo, uint32_t state_handle) {
    if (!ctx) return false;

    // Implementation for Dispatching Audio
    VkCommandBufferAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = ctx->command_pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(ctx->device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Bind Pipeline & Descriptors
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->compute_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->pipeline_layout, 0, 1, &ctx->descriptor_set, 0, NULL);

    // Pass params (frames, gain, threshold) via Push Constants
    float params[6] = { (float)frames, gain, threshold, p1, p2, (float)algo };
    vkCmdPushConstants(cmd, ctx->pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params), params);

    // Dispatch GPU Threads: Ensure we cover all samples (frames * channels)
    uint32_t total_samples = frames * channels;
    vkCmdDispatch(cmd, (total_samples + 63) / 64, 1, 1);

    vkEndCommandBuffer(cmd);

    // Submission
    VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VkFence fence;
    VkFenceCreateInfo fenceInfo = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    vkCreateFence(ctx->device, &fenceInfo, NULL, &fence);

    if (vkQueueSubmit(ctx->compute_queue, 1, &submitInfo, fence) != VK_SUCCESS) {
        vkDestroyFence(ctx->device, fence, NULL);
        vkFreeCommandBuffers(ctx->device, ctx->command_pool, 1, &cmd);
        return false;
    }

    // Wait with a 2ms timeout (Real-time safety)
    vkWaitForFences(ctx->device, 1, &fence, VK_TRUE, 2000000);
    vkDestroyFence(ctx->device, fence, NULL);
    vkFreeCommandBuffers(ctx->device, ctx->command_pool, 1, &cmd);

    return true;
}

// Memory Type Helper
static int32_t find_memory_type(VkPhysicalDevice physical_device, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return (int32_t)i;
        }
    }
    return -1; // Ritorna signed per gestire l'errore
}

uint32_t dsp_vulkan_allocate_buffer(struct dsp_vulkan_context *ctx, size_t size) {
    if (!ctx) return 0;

    // Find first free slot
    uint32_t handle = 0;
    for (int i = 1; i < 256; i++) {
        if (!ctx->user_buffers[i].active) {
            handle = i;
            break;
        }
    }
    if (handle == 0) return 0; // Pool full

    VkBufferCreateInfo bufferInfo = {0};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(ctx->device, &bufferInfo, NULL, &ctx->user_buffers[handle].buffer) != VK_SUCCESS)
        return 0;

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(ctx->device, ctx->user_buffers[handle].buffer, &memReqs);

    VkMemoryAllocateInfo allocInfo = {0};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    
    // Cerchiamo memoria DEVICE_LOCAL + HOST_VISIBLE per semplicità in Alpha.
    // Se non disponibile, facciamo fallback su HOST_VISIBLE.
    int32_t memType = find_memory_type(ctx->physical_device, memReqs.memoryTypeBits, 
                                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
                                       
    if (memType < 0) {
        memType = find_memory_type(ctx->physical_device, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    }

    if (memType < 0) {
        printf("[Vulkan API] ERROR: No compatible memory type found\n");
        return 0;
    }
    allocInfo.memoryTypeIndex = (uint32_t)memType;

    if (vkAllocateMemory(ctx->device, &allocInfo, NULL, &ctx->user_buffers[handle].memory) != VK_SUCCESS) {
        vkDestroyBuffer(ctx->device, ctx->user_buffers[handle].buffer, NULL);
        return 0;
    }

    vkBindBufferMemory(ctx->device, ctx->user_buffers[handle].buffer, ctx->user_buffers[handle].memory, 0);
    
    ctx->user_buffers[handle].size = size;
    ctx->user_buffers[handle].active = true;
    return handle;
}

void dsp_vulkan_free_buffer(struct dsp_vulkan_context *ctx, uint32_t handle) {
    if (!ctx || handle == 0 || handle >= 256 || !ctx->user_buffers[handle].active) return;
    
    vkDestroyBuffer(ctx->device, ctx->user_buffers[handle].buffer, NULL);
    vkFreeMemory(ctx->device, ctx->user_buffers[handle].memory, NULL);
    ctx->user_buffers[handle].active = false;
}

bool dsp_vulkan_upload_buffer(struct dsp_vulkan_context *ctx, uint32_t handle, const void* data, size_t size) {
    if (!ctx || handle == 0 || handle >= 256 || !ctx->user_buffers[handle].active) return false;
    
    void* mapped_data;
    if (vkMapMemory(ctx->device, ctx->user_buffers[handle].memory, 0, size, 0, &mapped_data) != VK_SUCCESS)
        return false;
        
    memcpy(mapped_data, data, size);
    vkUnmapMemory(ctx->device, ctx->user_buffers[handle].memory);
    return true;
}

uint32_t dsp_vulkan_import_shm(struct dsp_vulkan_context *ctx, int fd, size_t size) {
    if (!ctx || fd < 0) return 0;

    // Find free slot
    uint32_t handle = 0;
    for (int i = 1; i < 256; i++) {
        if (!ctx->user_buffers[i].active) {
            handle = i;
            break;
        }
    }
    if (handle == 0) return 0;

    // Import external FD as a buffer
    VkExternalMemoryBufferCreateInfo externalInfo = {0};
    externalInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
    externalInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

    VkBufferCreateInfo bufferInfo = {0};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.pNext = &externalInfo;
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    if (vkCreateBuffer(ctx->device, &bufferInfo, NULL, &ctx->user_buffers[handle].buffer) != VK_SUCCESS)
        return 0;

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(ctx->device, ctx->user_buffers[handle].buffer, &memReqs);

    VkImportMemoryFdInfoKHR importInfo = {0};
    importInfo.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
    importInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    importInfo.fd = dup(fd); // We must own the FD

    VkMemoryAllocateInfo allocInfo = {0};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = &importInfo;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = find_memory_type(ctx->physical_device, memReqs.memoryTypeBits, 0);

    if (vkAllocateMemory(ctx->device, &allocInfo, NULL, &ctx->user_buffers[handle].memory) != VK_SUCCESS) {
        vkDestroyBuffer(ctx->device, ctx->user_buffers[handle].buffer, NULL);
        return 0;
    }

    vkBindBufferMemory(ctx->device, ctx->user_buffers[handle].buffer, ctx->user_buffers[handle].memory, 0);
    ctx->user_buffers[handle].size = size;
    ctx->user_buffers[handle].active = true;
    
    printf("[Vulkan API] Imported external SHM memfd (handle=%u, size=%zu) as persistent buffer.\n", handle, size);
    return handle;
}

bool dsp_vulkan_dispatch_zero_copy(struct dsp_vulkan_context *ctx, uint32_t handle, uint32_t offset, uint32_t frames, uint32_t channels, float gain, float threshold, float p1, float p2, uint32_t algo, uint32_t state_handle) {
    if (!ctx || !ctx->user_buffers[handle].active) return false;
    
    // Aggiorniamo i Descriptor per Audio (Binding 0) e Stato (Binding 1)
    VkDescriptorBufferInfo bInfo[2] = {0};
    bInfo[0].buffer = ctx->user_buffers[handle].buffer;
    bInfo[0].offset = offset;
    bInfo[0].range = frames * channels * sizeof(float);

    bInfo[1].buffer = ctx->user_buffers[state_handle].buffer;
    bInfo[1].offset = 0;
    bInfo[1].range = ctx->user_buffers[state_handle].size;

    VkWriteDescriptorSet dWrite[2] = {0};
    for(int i=0; i<2; i++) {
        dWrite[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        dWrite[i].dstSet = ctx->descriptor_set;
        dWrite[i].dstBinding = i;
        dWrite[i].descriptorCount = 1;
        dWrite[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        dWrite[i].pBufferInfo = &bInfo[i];
    }

    vkUpdateDescriptorSets(ctx->device, 2, dWrite, 0, NULL);

    // Ora possiamo lanciare il dispatch standard usando il buffer aggiornato
    return dsp_vulkan_dispatch(ctx, (float*)1, frames, channels, gain, threshold, p1, p2, algo, state_handle);
}
