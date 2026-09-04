#ifndef CLIB_GRAPHICS
#define CLIB_GRAPHICS
#include "base.h"
// does the rendering library have the base implementation?

typedef struct {
    u16 major, minor, patch;
} VersionInfo;

typedef struct {
    String device_name_hint;
    VersionInfo version_info;
    const char* app_name;
    b8 headless;
} GConfig;

b32 GInit(Arena* arena, GConfig* config);

#ifdef CLIB_GRAPHICS_IMPLEMENTATION
#include <GLFW/glfw3.h>
#include <volk/volk.h>
#include <SPIRV-Reflect/spirv_reflect.h>

#define RETURN_IF_FAILED(R) \
    if ((R) != VK_SUCCESS) { return (R); }

typedef struct 
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
    VkSurfaceCapabilitiesKHR surface_caps;
    VkSurfaceFormatKHR surface_format;
    VkPresentModeKHR present_mode;
} PhysicalDeviceInfo;

typedef struct {
    VkQueue handle;
    VkCommandPool command_pool;
    uint32_t family;
} DeviceQueue;

typedef struct {
    VkDevice handle;
    PhysicalDeviceInfo* phys;
    DeviceQueue queue_graphics;
    DeviceQueue queue_present;
    DeviceQueue queue_compute;
    DeviceQueue queue_transfer;

    //GpuArenaPool gpu_arena_pool;
    //GpuArena gpu_arena_depth_buffer;
    //GpuBufferArena gpu_buffer_arena_device_mesh;
    //GpuBufferArena gpu_buffer_arena_staging;
} LogicalDevice;

typedef struct {
    VkInstance instance;
    GLFWwindow* window;
    VkSurfaceKHR surface;
    LogicalDevice* device;
} Context;

Context ctx = {0};

typedef struct { 
    u32 index;
    VkCommandPoolCreateFlags flags;
    DeviceQueue* out_queue;
} QueueCreateInfo;

VkResult InitQueues(Arena* arena, QueueCreateInfo* items, u16 n_queues) {
    VkResult res = VK_SUCCESS;
    for (u16 i = 0; i < n_queues; ++i) {
        items[i].out_queue->family = items[i].index;
        vkGetDeviceQueue(ctx.device->handle, items[i].index, 0, &items[i].out_queue->handle);
        VkCommandPoolCreateInfo pool_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = items[i].flags,
            .queueFamilyIndex = items[i].index};
        res = vkCreateCommandPool(ctx.device->handle, &pool_info, NULL, &items[i].out_queue->command_pool);
        // TODO: add to free list
        if (res != VK_SUCCESS) break;
    }
    return res;
}

VkResult InitLogicalDevice(Arena *arena, PhysicalDeviceInfo *phys, b8 headless)
{
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
    create_info.enabledExtensionCount = headless ? 0 : ARR_CONST_LENGTH(extensions);
    create_info.ppEnabledExtensionNames = extensions;

    ctx.device = ArenaPush(arena, LogicalDevice);
    VkResult res = vkCreateDevice(phys->handle, &create_info, NULL, &ctx.device->handle);
    RETURN_IF_FAILED(res);
    // should not leak the device here if failed

    // present has no command pool
    ctx.device->queue_present.family = phys->queue_family.present_index;
    ctx.device->queue_present.command_pool = 0;
    vkGetDeviceQueue(ctx.device->handle, phys->queue_family.present_index, 0, &ctx.device->queue_present.handle);
    // all others do
    QueueCreateInfo queues[] = {
        {.index = phys->queue_family.graphics_index,
         .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
         .out_queue = &ctx.device->queue_graphics},
        {.index = phys->queue_family.compute_index,
         .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
         .out_queue = &ctx.device->queue_compute},
        {.index = phys->queue_family.transfer_index,
         .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
         .out_queue = &ctx.device->queue_transfer},
    };
    res = InitQueues(arena, queues, ARR_CONST_LENGTH(queues));
    return res;
}

static PhysicalDeviceInfo *GDeviceList(Arena *arena, u16 *count)
{
    u32 n_devices;
    VkResult res = vkEnumeratePhysicalDevices(ctx.instance, &n_devices, NULL);
    if (res != VK_SUCCESS)
        return NULL;
    Arena* scratch = ArenaScratch();
    VkPhysicalDevice *device_handles = ArenaPushArray(scratch, VkPhysicalDevice, n_devices);
    PhysicalDeviceInfo *valid_device_infos = ArenaPushArray(scratch, PhysicalDeviceInfo, n_devices);
    res = vkEnumeratePhysicalDevices(ctx.instance, &n_devices, device_handles);
    if (res != VK_SUCCESS)
        return NULL;

    u32 n_valid = 0;
    PhysicalDeviceInfo valid_device;
    for (u32 i = 0; i < n_devices; i++)
    {
        uint32_t n_qfprops;
        uint32_t graphics_index = UINT32_MAX;
        uint32_t present_index = UINT32_MAX;
        uint32_t compute_index = UINT32_MAX;
        uint32_t transfer_index = UINT32_MAX;
        memset(&valid_device, 0, sizeof(PhysicalDeviceInfo));
        valid_device.handle = device_handles[i];

        // Queue families
        vkGetPhysicalDeviceQueueFamilyProperties(device_handles[i], &n_qfprops, NULL);
        VkQueueFamilyProperties qf_props[n_qfprops];
        vkGetPhysicalDeviceQueueFamilyProperties(device_handles[i], &n_qfprops, qf_props);
        for (u32 j = 0; j < n_qfprops; j++)
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
                VkBool32 present_supported = VK_FALSE;
                // TODO: this makes it so that we need to create the window first!
                res = vkGetPhysicalDeviceSurfaceSupportKHR(device_handles[i], j, ctx.surface, &present_supported);
                if (res == VK_SUCCESS && present_supported)
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
        VkBool32 extensions_supported = VK_TRUE;
        // TODO pass device extensions here
        const char* extension_names[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        for (u32 j = 0; j < ARR_CONST_LENGTH(extension_names); j++)
        {
            VkBool32 found = VK_FALSE;
            for (u32 k = 0; k < n_exprops; k++)
            {
                if (strcmp(extension_names[j], exprops[k].extensionName) == 0)
                {
                    found = VK_TRUE;
                    break;
                }
            }
            if (!found)
            {
                extensions_supported = VK_FALSE;
                break;
            }
        }
        if (!extensions_supported)
            continue; // next device
        // surface capabilities
        // res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(devices[i], surface, &valid_device.surface_caps);
        // if (res != VK_SUCCESS) continue;

        vkGetPhysicalDeviceProperties(device_handles[i], &valid_device.props);
        vkGetPhysicalDeviceFeatures(device_handles[i], &valid_device.features);
        vkGetPhysicalDeviceMemoryProperties(device_handles[i], &valid_device.mem_props);

        // device is valid!
        valid_device_infos[n_valid++] = valid_device;
    }
    *count = n_valid;
    PhysicalDeviceInfo* out_devices = ArenaPushArray(arena, PhysicalDeviceInfo, n_valid);
    for (u32 i = 0; i < n_valid; ++i) out_devices[i] = valid_device_infos[i];
    return out_devices;
}


b32 GInit(Arena *arena, GConfig* config)
{
    VkResult res = volkInitialize();
    if (res != VK_SUCCESS)
        return false; // vulkan not present

    VkApplicationInfo app_info = {0};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Cube Test";
    app_info.applicationVersion = VK_MAKE_VERSION(config->version_info.major, config->version_info.minor, config->version_info.patch);
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
    res = vkCreateInstance(&instance_create_info, NULL, &ctx.instance);
    if (res != VK_SUCCESS)
        return false;

    volkLoadInstance(ctx.instance);

    Arena *scratch = ArenaScratch();
    u16 count;
    PhysicalDeviceInfo *devices = GDeviceList(scratch, &count);
    u16 chosen = 0;
    for (u16 i = 0; i < count; ++i)
    {
        if (StrEquals(S(devices[i].props.deviceName), config->device_name_hint))
        {
            chosen = i;
            break;
        }
    }

    res = InitLogicalDevice(arena, devices + chosen, config->headless);
    if (res != VK_SUCCESS) return false;

    volkLoadDevice(ctx.device->handle);

    ArenaReset(scratch, false);
    return true;
};

// intializes window, swapchain creation is zii since we need to detect resizes anyway
b32 InitWindow(i32 width, i32 height, const char *title)
{
    if (!glfwInit())
        return false;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    ctx.window = glfwCreateWindow(width, height, title, NULL, NULL);
    VkSurfaceKHR surface;
    GLFWAPI VkResult glfwCreateWindowSurface(VkInstance instance, GLFWwindow * window, const VkAllocationCallbacks *allocator, VkSurfaceKHR *surface);
    VkResult res = glfwCreateWindowSurface(ctx.instance, ctx.window, NULL, &ctx.surface);
    return res == VK_SUCCESS;
}

#endif

#endif // CLIB_RENDERING