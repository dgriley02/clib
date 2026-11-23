#ifndef CLIB_GRAPHICS
#define CLIB_GRAPHICS

#include <volk/volk.h>
#include <GLFW/glfw3.h>
#include <SPIRV-Reflect/spirv_reflect.h>
#include <base.h>
#include <limits.h>

#define TRY(expr) do { \
    VkResult _r = (expr); \
    if (_r != VK_SUCCESS) { \
        LogError("%s failed (%d) at %s:%d", #expr, _r, __FILE__, __LINE__); \
        return _r; \
    } \
} while(0)

#define TRY_GOTO(expr, label) do { \
    VkResult _r = (expr); \
    if (_r != VK_SUCCESS) { \
        LogError("%s failed (%d) at %s:%d", #expr, _r, __FILE__, __LINE__); \
        res = _r; \
        goto label; \
    } \
} while(0)


struct vk_WindowSettings 
{
    u32 width, height;
    const char* title;
};

struct vk_Config {
    struct { 
        u16 major, minor, patch;
    } app_version;
    struct vk_WindowSettings* window_settings; // optional window settigs
    const char* app_name;
};

VkResult vk_Init(struct Arena* arena, struct vk_Config* config, GLFWwindow** out_window);

// gpu allocator

enum GPU_ALLOC_TYPE
{
    BUFFER,
    IMAGE
};

struct GpuAlloc
{
    u64 offset;
};

#define GPU_ARENA_DEFAULT_SIZE (KiloBytes(64))

struct GpuArena 
{
    VkDeviceMemory memory;
    u32 memory_type_index;
    u64 used;
    u64 size;
};

struct PhysicalDeviceInfo 
{
    VkPhysicalDevice handle;
    VkPhysicalDeviceProperties props;
    VkPhysicalDeviceFeatures features;
    VkPhysicalDeviceMemoryProperties mem_props;
    struct
    {
        uint32_t graphics_index;
        uint32_t present_index;
        uint32_t transfer_index;
        uint32_t compute_index;
    } queue_family;
    VkSurfaceFormatKHR surface_format;
    VkPresentModeKHR present_mode;
} ;

struct DeviceQueue 
{
    VkQueue handle;
    VkCommandPool command_pool;
    uint32_t family;
};

struct vk_LogicalDevice 
{
    VkDevice handle;
    struct PhysicalDeviceInfo* phys;
    struct DeviceQueue queue_graphics;
    struct DeviceQueue queue_present;
    struct DeviceQueue queue_compute;
    struct DeviceQueue queue_transfer;

    //GpuArenaPool gpu_arena_pool;
    //GpuArena gpu_arena_depth_buffer;
    //GpuBufferArena gpu_buffer_arena_device_mesh;
    //GpuBufferArena gpu_buffer_arena_staging;
};

struct vk_Context
{
    VkInstance instance;
    GLFWwindow* window;
    VkSurfaceKHR surface;
    struct vk_LogicalDevice device;
    struct {
        VkSwapchainKHR handle;
        u32 n_images;
        VkImage* images;
        VkImageView* image_views;

        struct Arena arena_cpu;
        struct GpuArena arena_gpu;

        VkImage depth_buffer;
        VkDeviceMemory depth_buffer_memory;
        VkImageView depth_buffer_image_view;
    } swapchain;

    VkCommandPool cmd_pool_graphics_transient, cmd_pool_graphics_auto_reset;
    VkCommandBuffer cmd_buffers_graphics[2]; // one per frame
    VkFence fence_submit_finished[2]; // one per frame
    VkSemaphore sem_image_available[2]; // one per frame
    VkSemaphore* sem_render_finished; // one per swapchain image
    VkDescriptorPool descriptor_pool_permanent;
} ctx;

struct QueueCreateInfo 
{ 
    u32 index;
    VkCommandPoolCreateFlags flags;
    struct DeviceQueue* out_queue;
};

#ifdef CLIB_GRAPHICS_IMPL

b32 GpuArenaInit(struct GpuArena* arena, VkMemoryPropertyFlags props, u64 size) {
    VkPhysicalDeviceMemoryProperties p = ctx.device.phys->mem_props;
    int index = -1;
    for (int i = 0; i < p.memoryTypeCount; ++i) {
        if ((p.memoryTypes[i].propertyFlags & props) == props) {
            index = i;
            break;
        }
    }
    if (index == -1) return false;
    VkMemoryAllocateInfo info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = size,
        .memoryTypeIndex = index
    };
    VkDeviceMemory memory;
    if(vkAllocateMemory(ctx.device.handle, &info, NULL, &memory) != VK_SUCCESS) return false;
    arena->memory = memory;
    arena->used = 0;
    arena->size = size;
    return true;
}

void GpuArenaReset(struct GpuArena* arena) {
    arena->used = 0;
}

VkResult GpuAllocUBO(struct GpuArena* 

VkResult GpuAlloc(struct GpuArena* arena, VkMemoryRequirements* requirements, struct GpuAlloc* out_alloc) {
    if ((requirements->memoryTypeBits & (1u << arena->memory_type_index)) == 0) return VK_ERROR_NOT_PERMITTED; // this means our assertion failed
    u64 start = ALIGN_UP(arena->used, requirements->alignment);
    u64 end = start + requirements->size;
    if (end > arena->size) return VK_ERROR_NOT_ENOUGH_SPACE_KHR;
    arena->used = start + requirements->size;
    out_alloc->offset = start;
    return VK_SUCCESS;
}

// shoudl alloc buffer really create it? I need to return the handle too
//b32 GpuAllocBuffer(struct Arena* cpu_arena, struct GpuArena* arena, VkBufferCreateInfo* info, struct GpuAlloc* out_alloc) {
//    VkBuffer buffer;
//    if (vkCreateBuffer(ctx.device.handle, info, NULL, &buffer) != VK_SUCCESS) return false;
//    ListAdd(VkBuffer, cpu_arena, &arena->buffers, buffer);
//    VkMemoryRequirements reqs;
//    vkGetBufferMemoryRequirements(ctx.device.handle, buffer, &reqs);
//    struct GpuAlloc alloc;
//    if (!GpuAlloc(arena, &reqs, &alloc)) return false;
//    if (vkBindBufferMemory(ctx.device.handle, buffer, arena->memory, alloc.offset) != VK_SUCCESS) return false;
//    *out_alloc = alloc;
//    return true;
//}

VkResult GpuAllocAndBindImage(struct GpuArena* arena, VkImage image, struct GpuAlloc* out_alloc) {
    VkMemoryRequirements reqs;
    vkGetImageMemoryRequirements(ctx.device.handle, image, &reqs);
    struct GpuAlloc alloc;
    TRY(GpuAlloc(arena, &reqs, &alloc));
    TRY(vkBindImageMemory(ctx.device.handle, image, arena->memory, alloc.offset)); // not sure when this happens, but ignoring restoring the arena bump for now
    *out_alloc = alloc;
    return VK_SUCCESS;
}

//b32 GpuAllocImage(struct Arena* cpu_arena, struct GpuArena* arena, VkImageCreateInfo* info, struct GpuAlloc* out_alloc) {
//    VkImage image;
//    if (vkCreateImage(ctx.device.handle, info, NULL, &image) != VK_SUCCESS) return false;
//    ListAdd(VkImage, cpu_arena, &arena->images, image);
//    VkMemoryRequirements reqs;
//    vkGetImageMemoryRequirements(ctx.device.handle, image, &reqs);
//    struct GpuAlloc alloc;
//    if (!GpuAlloc(arena, &reqs, &alloc)) return false;
//    if (vkBindImageMemory(ctx.device.handle, image, arena->memory, alloc.offset) != VK_SUCCESS) return false;
//    *out_alloc = alloc;
//    return true;
//}

VkResult vk_InitQueues(struct Arena* arena, struct QueueCreateInfo* items, u16 n_queues) {
    VkResult res = VK_SUCCESS;
    for (int i = 0; i < n_queues; ++i) {
        items[i].out_queue->family = items[i].index;
        vkGetDeviceQueue(ctx.device.handle, items[i].index, 0, &items[i].out_queue->handle);
        VkCommandPoolCreateInfo pool_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = items[i].flags,
            .queueFamilyIndex = items[i].index};
        res = vkCreateCommandPool(ctx.device.handle, &pool_info, NULL, &items[i].out_queue->command_pool);
        // TODO: add to free list
        if (res != VK_SUCCESS) break;
    }
    return res;
}

VkResult vk_InitLogicalDevice(struct Arena *arena, struct PhysicalDeviceInfo *phys)
{
    VkResult res;
    // TODO: consider priority maybe?
    float priority = 1.0f;

    uint32_t candidates[4] = {
        phys->queue_family.graphics_index,
        phys->queue_family.present_index,
        phys->queue_family.compute_index,
        phys->queue_family.transfer_index};

    VkDeviceQueueCreateInfo unique_infos[4] = {};
    uint32_t n_unique = 0;
    for (int i = 0; i < 4; i++)
    {
        VkBool32 unique = VK_TRUE;
        for (int j = 0; j < n_unique; j++)
        {
            // already added
            if (candidates[i] == unique_infos[j].queueFamilyIndex)
            {
                unique = VK_FALSE;
                break;
            }
        }
        if (!unique)
            continue;
        unique_infos[n_unique].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        unique_infos[n_unique].queueFamilyIndex = candidates[i];
        unique_infos[n_unique].queueCount = 1;
        unique_infos[n_unique].pQueuePriorities = &priority;
        n_unique++;
    }

    VkDeviceCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.queueCreateInfoCount = n_unique;
    create_info.pQueueCreateInfos = unique_infos;

    // TODO: use dynamic array 
    const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    create_info.enabledExtensionCount = ARR_CONST_LENGTH(extensions);
    create_info.ppEnabledExtensionNames = extensions;

    ctx.device.phys = phys;
    TRY(vkCreateDevice(phys->handle, &create_info, NULL, &ctx.device.handle));
    // should not leak the device here if failed

    // present has no command pool
    ctx.device.queue_present.family = phys->queue_family.present_index;
    ctx.device.queue_present.command_pool = 0;
    vkGetDeviceQueue(ctx.device.handle, phys->queue_family.present_index, 0, &ctx.device.queue_present.handle);
    // all others do
    struct QueueCreateInfo queues[] = {
        {.index = phys->queue_family.graphics_index,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .out_queue = &ctx.device.queue_graphics},
        {.index = phys->queue_family.compute_index,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .out_queue = &ctx.device.queue_compute},
        {.index = phys->queue_family.transfer_index,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .out_queue = &ctx.device.queue_transfer},
    };
    res = vk_InitQueues(arena, queues, ARR_CONST_LENGTH(queues));
    return res;
}

static VkResult vk_DeviceList(struct Arena *arena, struct PhysicalDeviceInfo** out_devices, u16 *out_count)
{
    VkResult res;
    u32 n_devices;
    TRY(vkEnumeratePhysicalDevices(ctx.instance, &n_devices, NULL));
    struct Arena* scratch = ArenaScratch();
    VkPhysicalDevice *device_handles = ArenaPushArray(scratch, VkPhysicalDevice, n_devices);
    struct PhysicalDeviceInfo *valid_device_infos = ArenaPushArray(scratch, struct PhysicalDeviceInfo, n_devices);
    TRY(vkEnumeratePhysicalDevices(ctx.instance, &n_devices, device_handles));

    u32 n_valid = 0;
    struct PhysicalDeviceInfo valid_device;
    b32 found = false;
    for (int i = 0; i < n_devices; i++)
    {
        uint32_t n_qfprops;
        uint32_t graphics_index = UINT32_MAX;
        uint32_t present_index = UINT32_MAX;
        uint32_t compute_index = UINT32_MAX;
        uint32_t transfer_index = UINT32_MAX;
        memset(&valid_device, 0, sizeof(struct PhysicalDeviceInfo));
        valid_device.handle = device_handles[i];

        // Queue families
        vkGetPhysicalDeviceQueueFamilyProperties(device_handles[i], &n_qfprops, NULL);
        VkQueueFamilyProperties qf_props[n_qfprops];
        vkGetPhysicalDeviceQueueFamilyProperties(device_handles[i], &n_qfprops, qf_props);
        for (int j = 0; j < n_qfprops; j++)
        {
            uint32_t flags = qf_props[j].queueFlags;

            // graphics, first
            if (graphics_index == UINT32_MAX && (flags & VK_QUEUE_GRAPHICS_BIT))
            {
                graphics_index = j;
            }
            // present, prefer same as graphics
            if (present_index == UINT32_MAX || j == graphics_index)
            {
                VkBool32 presentSupport = false;
                if (vkGetPhysicalDeviceSurfaceSupportKHR(device_handles[i], j, ctx.surface, &presentSupport) == VK_SUCCESS && presentSupport)
                {
                    present_index = j;
                }
            }
            // compute, prefer dedicated (non-graphics)
            if (flags & VK_QUEUE_COMPUTE_BIT)
            {
                if (compute_index == UINT32_MAX || (flags & VK_QUEUE_GRAPHICS_BIT) == 0)
                {
                    compute_index = j;
                }
            }
            // transfer, prefer dedicated (non-graphics + non-compute)
            if (flags & VK_QUEUE_TRANSFER_BIT)
            {
                if (transfer_index == UINT32_MAX || (flags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == 0)
                {
                    transfer_index = j;
                }
            }
        }
        if (graphics_index == UINT32_MAX || present_index == UINT32_MAX || compute_index == UINT32_MAX || transfer_index == UINT32_MAX)
            continue; // device not suitable
        valid_device.queue_family.graphics_index = graphics_index;
        valid_device.queue_family.present_index = present_index;
        valid_device.queue_family.compute_index = compute_index;
        valid_device.queue_family.transfer_index = transfer_index;

        // Extensions properties
        uint32_t n_exprops;
        vkEnumerateDeviceExtensionProperties(device_handles[i], NULL, &n_exprops, NULL);
        VkExtensionProperties exprops[n_exprops];
        vkEnumerateDeviceExtensionProperties(device_handles[i], NULL, &n_exprops, exprops);
        // TODO pass additional device extensions here
        const char* extension_names[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        for (int j = 0; j < ARR_CONST_LENGTH(extension_names); j++)
        {
            found = false;
            for (int k = 0; k < n_exprops; k++)
            {
                if (strcmp(extension_names[j], exprops[k].extensionName) == 0)
                {
                    found = true;
                    break;
                }
            }
            if (!found) break;
        }
        if (!found) continue; 

        // formats and present modes
        u32 n_formats;
        if (vkGetPhysicalDeviceSurfaceFormatsKHR(device_handles[i], ctx.surface, &n_formats, NULL) != VK_SUCCESS) continue;
        VkSurfaceFormatKHR* formats = ArenaPushArray(scratch, VkSurfaceFormatKHR, n_formats);
        if (vkGetPhysicalDeviceSurfaceFormatsKHR(device_handles[i], ctx.surface, &n_formats, formats) != VK_SUCCESS) continue;

        b32 found = false;
        for (int i = 0; i < n_formats; ++i) {
            if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                found = true;
                valid_device.surface_format = formats[i];
                break;
            }
        }
        if (!found) continue;

        // remove because we don't want to use mailbox - it makes us render more than necessary if the display is out bound
        //u32 n_present_modes;
        //vkGetPhysicalDeviceSurfacePresentModesKHR(device_handles[i], ctx.surface, &n_present_modes, NULL);
        //VkPresentModeKHR* present_modes = ArenaPushArray(scratch, VkPresentModeKHR, n_present_modes);
        //vkGetPhysicalDeviceSurfacePresentModesKHR(device_handles[i], ctx.surface, &n_present_modes, present_modes);

        valid_device.present_mode = VK_PRESENT_MODE_FIFO_KHR;
        //for (int i = 0; i < n_formats; ++i) {
        //    if (present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
        //        valid_device.present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
        //        break;
        //    }
        //}

        vkGetPhysicalDeviceProperties(device_handles[i], &valid_device.props);
        vkGetPhysicalDeviceFeatures(device_handles[i], &valid_device.features);
        vkGetPhysicalDeviceMemoryProperties(device_handles[i], &valid_device.mem_props);

        // depth buffer support
        VkFormatProperties format_props;
        vkGetPhysicalDeviceFormatProperties(device_handles[i], VK_FORMAT_D32_SFLOAT, &format_props);
        b32 depth_buffer_supported = format_props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if (!depth_buffer_supported) continue;

        // device is valid!
        valid_device_infos[n_valid++] = valid_device;
    }
    *out_count = n_valid;
    struct PhysicalDeviceInfo* devices = ArenaPushArray(arena, struct PhysicalDeviceInfo, n_valid);
    for (int i = 0; i < n_valid; ++i) devices[i] = valid_device_infos[i];
    *out_devices = devices;
    return n_valid > 0 ? VK_SUCCESS : VK_ERROR_UNKNOWN;
}

static VkResult FindMemoryType(u32 bits, VkMemoryPropertyFlags usage_flags, u32* out_type) {
    VkPhysicalDeviceMemoryProperties* props = &ctx.device.phys->mem_props;
    for (u32 i = 0; i < props->memoryTypeCount; ++i) {
        if (bits & (1u << i) && (usage_flags & props->memoryTypes[i].propertyFlags) == usage_flags) {
            *out_type = i;
            return VK_SUCCESS;
        }
    }
    return VK_ERROR_UNKNOWN;
}

static VkResult vk_RecreateSwapchain() {
    VkResult res = VK_SUCCESS;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkImageView* image_views = NULL;
    VkImage depth_buffer = VK_NULL_HANDLE;
    VkDeviceMemory depth_buffer_memory = VK_NULL_HANDLE;
    VkImageView depth_buffer_image_view = VK_NULL_HANDLE;

    VkSurfaceCapabilitiesKHR caps;
    TRY(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.device.phys->handle, ctx.surface, &caps));

    // images + extent
    u32 n_images = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && n_images > caps.maxImageCount) {
        n_images = caps.maxImageCount;
    }

    // TODO: handle minimized window, ie glfwGetFrameBufferSize returning 0
    VkExtent2D extent;
    {
        if (caps.currentExtent.width != UINT_MAX) extent = caps.currentExtent;
        else {
            int width, height;
            glfwGetFramebufferSize(ctx.window, &width, &height);
            extent.width = Clamp((u32)width, caps.minImageExtent.width, caps.maxImageExtent.width);
            extent.height = Clamp((u32)height, caps.minImageExtent.height, caps.maxImageExtent.height);
        }
    }

    VkSwapchainCreateInfoKHR info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = ctx.surface,
        .minImageCount = n_images,
        .imageFormat = ctx.device.phys->surface_format.format,
        .imageColorSpace = ctx.device.phys->surface_format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = NULL,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = ctx.device.phys->present_mode,
        .clipped = VK_TRUE,
        .oldSwapchain = ctx.swapchain.handle
    };

    u32 indices[2] = {ctx.device.queue_graphics.family, ctx.device.queue_present.family};
    if (indices[0] != indices[1]) {
        info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        info.queueFamilyIndexCount = 2;
        info.pQueueFamilyIndices = indices;
    }
    TRY(vkCreateSwapchainKHR(ctx.device.handle, &info, NULL, &swapchain));

    // TODO: push transient to scratch, update, destroy old (reset old arena) and then copy over from scratch
    struct Arena* scratch = ArenaScratch();
    TRY_GOTO(vkGetSwapchainImagesKHR(ctx.device.handle, swapchain, &n_images, NULL), fail);
    VkImage* images = ArenaPushArray(scratch, VkImage, n_images);
    TRY_GOTO(vkGetSwapchainImagesKHR(ctx.device.handle, swapchain, &n_images, images), fail);

    image_views = ArenaPushArrayZeroed(scratch, VkImageView, n_images);
    for (int i = 0; i < n_images; ++i) {
        VkImageViewCreateInfo image_view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = ctx.device.phys->surface_format.format,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        TRY_GOTO(vkCreateImageView(ctx.device.handle, &image_view_info, NULL, image_views + i), fail);
    }

    // depth buffer

    VkImageCreateInfo depth_buffer_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_D32_SFLOAT,
        .extent = { extent.width, extent.height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    TRY_GOTO(vkCreateImage(ctx.device.handle, &depth_buffer_info, NULL, &depth_buffer), fail);
    VkMemoryRequirements reqs;
    vkGetImageMemoryRequirements(ctx.device.handle, depth_buffer, &reqs);
    u32 mem_index = 0;
    TRY_GOTO(FindMemoryType(reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &mem_index), fail);
    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = reqs.size,
        .memoryTypeIndex = mem_index
    };
    TRY_GOTO(vkAllocateMemory(ctx.device.handle, &alloc_info, NULL, &depth_buffer_memory), fail);
    TRY_GOTO(vkBindImageMemory(ctx.device.handle, depth_buffer, depth_buffer_memory, 0), fail);

    VkImageViewCreateInfo depth_view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = depth_buffer,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_D32_SFLOAT,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .levelCount = 1,
            .layerCount = 1
        }
    };
    TRY_GOTO(vkCreateImageView(ctx.device.handle, &depth_view_info, NULL, &depth_buffer_image_view), fail);

    // clean up old, and replace in arena
    for (int i = 0; i < ctx.swapchain.n_images; ++i) {
        vkDestroyImageView(ctx.device.handle, ctx.swapchain.image_views[i], NULL);
    }
    vkDestroyImageView(ctx.device.handle, ctx.swapchain.depth_buffer_image_view, NULL);
    vkFreeMemory(ctx.device.handle, ctx.swapchain.depth_buffer_memory, NULL);
    vkDestroyImage(ctx.device.handle, ctx.swapchain.depth_buffer, NULL);
    vkDestroySwapchainKHR(ctx.device.handle, ctx.swapchain.handle, NULL);
    ArenaReset(&ctx.swapchain.arena_cpu);
    VkImage* new_images = ArenaPushArray(&ctx.swapchain.arena_cpu, VkImage, n_images);
    VkImageView* new_image_views = ArenaPushArray(&ctx.swapchain.arena_cpu, VkImageView, n_images);
    MemCopy(new_images, images, sizeof(VkImage) * n_images);
    MemCopy(new_image_views, image_views, sizeof(VkImageView) * n_images);
    images = new_images;
    image_views = new_image_views;

    // done
    ctx.swapchain.handle = swapchain;
    ctx.swapchain.images = images;
    ctx.swapchain.image_views = image_views;
    ctx.swapchain.n_images = n_images;
    ctx.swapchain.depth_buffer = depth_buffer;
    ctx.swapchain.depth_buffer_memory = depth_buffer_memory;
    ctx.swapchain.depth_buffer_image_view = depth_buffer_image_view;
    return VK_SUCCESS;
fail:
    vkDestroyImageView(ctx.device.handle, depth_buffer_image_view, NULL);
    vkFreeMemory(ctx.device.handle, depth_buffer_memory, NULL);
    vkDestroyImage(ctx.device.handle, depth_buffer, NULL);
    if (image_views) {
        for (int i = 0; i < n_images; ++i) {
            vkDestroyImageView(ctx.device.handle, image_views[i], NULL);
        }
    }
    vkDestroySwapchainKHR(ctx.device.handle, swapchain, NULL);
    return res;
}

static void vk_Teardown(void) {
    vkDestroyDescriptorPool(ctx.device.handle, ctx.descriptor_pool_permanent, NULL);
    for (int i = 0; i < 2; ++i) vkDestroySemaphore(ctx.device.handle, ctx.sem_image_available[i], NULL);
    for (int i = 0; i < 2; ++i) vkDestroyFence(ctx.device.handle, ctx.fence_submit_finished[i], NULL);
    if (ctx.sem_render_finished) for (int i = 0; i < ctx.swapchain.n_images; ++i) vkDestroySemaphore(ctx.device.handle, ctx.sem_render_finished[i], NULL);
    vkDestroyCommandPool(ctx.device.handle, ctx.cmd_pool_graphics_auto_reset, NULL);
    vkDestroyCommandPool(ctx.device.handle, ctx.cmd_pool_graphics_transient, NULL);
    vkDestroySwapchainKHR(ctx.device.handle, ctx.swapchain.handle, NULL);
    if (ctx.swapchain.image_views) {
        for (int i = 0; i < ctx.swapchain.n_images; ++i) {
            vkDestroyImageView(ctx.device.handle, ctx.swapchain.image_views[i], NULL);
        }
    }
    vkDestroyDevice(ctx.device.handle, NULL);
    vkDestroySurfaceKHR(ctx.instance, ctx.surface, NULL);
    vkDestroyInstance(ctx.instance, NULL);
    ctx = (struct vk_Context){0};
}

VkResult vk_Init(struct Arena *arena, struct vk_Config* config, GLFWwindow** out_window)
{
    VkResult res;
    TRY(volkInitialize());

    // create instance
    VkApplicationInfo app_info = {0};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = config->app_name;
    app_info.applicationVersion = VK_MAKE_VERSION(config->app_version.major, config->app_version.minor, config->app_version.patch);
    app_info.pEngineName = "No Engine";
    app_info.engineVersion = VK_MAKE_VERSION(0, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo instance_create_info = {0};
    instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_create_info.pApplicationInfo = &app_info;
#ifdef DEBUG
    const char* layers[] = {"VK_LAYER_KHRONOS_validation"};
    instance_create_info.enabledLayerCount = ARR_CONST_LENGTH(layers);
    instance_create_info.ppEnabledLayerNames = layers;
#endif
    instance_create_info.ppEnabledExtensionNames = glfwGetRequiredInstanceExtensions(&instance_create_info.enabledExtensionCount);
    TRY(vkCreateInstance(&instance_create_info, NULL, &ctx.instance));

    volkLoadInstance(ctx.instance);

    // create window and surface
    struct vk_WindowSettings settings = {.width = 640, .height = 480, .title = "Window Title"};
    if (config->window_settings) settings = *config->window_settings;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(settings.width, settings.height, settings.title, NULL, NULL);
    TRY_GOTO(glfwCreateWindowSurface(ctx.instance, window, NULL, &ctx.surface), fail);
    *out_window = window;

    u16 count;
    struct PhysicalDeviceInfo *devices;
    TRY_GOTO(vk_DeviceList(arena, &devices, &count), fail);
    u16 chosen = 0;
    // TODO: scoring
    //for (u16 i = 0; i < count; ++i)
    //{
    //    if (StrEquals(S(devices[i].props.deviceName), config->device_name_hint))
    //    {
    //        chosen = i;
    //        break;
    //    }
    //}

    TRY_GOTO(vk_InitLogicalDevice(arena, devices + chosen), fail);

    volkLoadDevice(ctx.device.handle);

    u32 frames_in_flight = 2; // TODO: change dynamically based on if we have budget to make it all serial

    // TODO: query pools
    //
    res = vk_RecreateSwapchain();
    // do what with res??

    // command bools and buffers
    {
        VkCommandPoolCreateInfo info1 = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = ctx.device.queue_graphics.family
        };
        TRY_GOTO(vkCreateCommandPool(ctx.device.handle, &info1, NULL, &ctx.cmd_pool_graphics_auto_reset), fail);

        VkCommandPoolCreateInfo info2 = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = ctx.device.queue_graphics.family
        };
        TRY_GOTO(vkCreateCommandPool(ctx.device.handle, &info2, NULL, &ctx.cmd_pool_graphics_transient), fail);

        VkCommandBufferAllocateInfo info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = ctx.cmd_pool_graphics_auto_reset,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = frames_in_flight
        };
        TRY_GOTO(vkAllocateCommandBuffers(ctx.device.handle, &info, ctx.cmd_buffers_graphics), fail);
    }

    // sync objects
    ctx.sem_render_finished = ArenaPushArrayZeroed(arena, VkSemaphore, ctx.swapchain.n_images);
    {
        VkSemaphoreCreateInfo info_sem = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
        };
        VkFenceCreateInfo info_fence = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT
        };

        for (int i = 0; i < 2; ++i) {
            TRY_GOTO(vkCreateSemaphore(ctx.device.handle, &info_sem, NULL, ctx.sem_image_available + i), fail);
            TRY_GOTO(vkCreateFence(ctx.device.handle, &info_fence, NULL, ctx.fence_submit_finished + i), fail);
        }
        for (int i = 0; i < ctx.swapchain.n_images; ++i) TRY_GOTO(vkCreateSemaphore(ctx.device.handle, &info_sem, NULL, ctx.sem_render_finished + i), fail);
    }
    // descriptor pools
    const u32 desc_limit = 16; // plenty for now (TODO: compute exact at comp time, when asset types are known)
    VkDescriptorPoolSize pool_sizes[] = {
        {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 2 * desc_limit // one per frame
        },
        {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 2 * desc_limit // one per frame
        }
    };
    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = ARR_CONST_LENGTH(pool_sizes) * 2 * desc_limit,
        .poolSizeCount = ARR_CONST_LENGTH(pool_sizes),
        .pPoolSizes = pool_sizes
    };

    TRY_GOTO(vkCreateDescriptorPool(ctx.device.handle, &pool_info, NULL, &ctx.descriptor_pool_permanent), fail);

    // global uniforms (later)
    // TODO: spirv reflect for pipeline creation from shader files

    // fully dynamic rendering, so no passes
    // pipelines, spirv reflect all of them from file. Render using material id mapped to file name. How to set uniforms??
    return VK_SUCCESS;
fail: 
    vk_Teardown();
    return res;
};
#endif // CLIB_GRAPHICS_IMPL
#endif // CLIB_RENDERING
