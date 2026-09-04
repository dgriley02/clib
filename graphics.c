#define GOTO_IF_FAILED(R, L) \
    if ((R) != VK_SUCCESS) { goto L; }
#define RETURN_IF_FAILED(R) \
    if ((R) != VK_SUCCESS) { return (R); }

const char* g_validation_layer_names[] = {
    "VK_LAYER_KHRONOS_validation"
};

const char* g_device_extension_names[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

VkResult __vk_init_queue(VkDevice device, uint32_t index, VkCommandPoolCreateFlags flags, DeviceQueue* out_queue) {
    out_queue->family = index;
    vkGetDeviceQueue(device, index, 0, &out_queue->handle);
    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = flags,
        .queueFamilyIndex = index
    };
    VkResult res = vkCreateCommandPool(device, &pool_info, NULL, &out_queue->command_pool);
    return res;
}

typedef struct {
    VkDevice handle;
    PhysicalDeviceInfo* phys;
    DeviceQueue queue_graphics;
    DeviceQueue queue_present;
    DeviceQueue queue_compute;
    DeviceQueue queue_transfer;

    GpuArenaPool gpu_arena_pool;
    GpuArena gpu_arena_depth_buffer;
    GpuBufferArena gpu_buffer_arena_device_mesh;
    GpuBufferArena gpu_buffer_arena_staging;
} LogicalDevice;

VkResult vk_create_logical_device(PhysicalDeviceInfo* phys, LogicalDevice* out_device) {
    // TODO: consider priority maybe?
    float priority = 1.0f;

    uint32_t candidates[4] = {
        phys->queue_family.graphics_index,
        phys->queue_family.present_index,
        phys->queue_family.compute_index,
        phys->queue_family.transfer_index
    };

    VkDeviceQueueCreateInfo unique_infos[4] = {};
    uint32_t n_unique = 0;
    for (int i = 0; i < 4; i++) {
        VkBool32 unique = VK_TRUE;
        for (int j = 0; j < n_unique; j++) {
            // already added
            if (candidates[i] == unique_infos[j].queueFamilyIndex) { unique = VK_FALSE; break; }
        }
        if (!unique) continue;
        unique_infos[n_unique].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        unique_infos[n_unique].queueFamilyIndex = candidates[i];
        unique_infos[n_unique].queueCount = 1;
        unique_infos[n_unique].pQueuePriorities = &priority;
        n_unique++;
    }

    VkDeviceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = n_unique,
        .pQueueCreateInfos = unique_infos, 
        .enabledLayerCount = VK_LENGTH(g_validation_layer_names),
        .ppEnabledLayerNames = g_validation_layer_names,
        .enabledExtensionCount = VK_LENGTH(g_device_extension_names),
        .ppEnabledExtensionNames = g_device_extension_names,
    };

    LogicalDevice device = {
        .phys = phys
    };
    VkResult res = vkCreateDevice(phys->handle, &create_info, NULL, &device.handle);
    RETURN_IF_FAILED(res);
    // should not leak the device here if failed

    res = __vk_init_queue(
            device.handle, 
            phys->queue_family.graphics_index, 
            VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, 
            &device.queue_graphics);
    GOTO_IF_FAILED(res, err_cleanup_device);
    res = __vk_init_queue(
            device.handle, 
            phys->queue_family.compute_index, 
            VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, 
            &device.queue_compute);
    GOTO_IF_FAILED(res, err_cleanup_graphics_pool);
    res = __vk_init_queue(
            device.handle, 
            phys->queue_family.transfer_index, 
            VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            &device.queue_transfer);
    GOTO_IF_FAILED(res, err_cleanup_compute_pool);
    device.queue_present.family = phys->queue_family.present_index;
    device.queue_present.command_pool = 0;
    vkGetDeviceQueue(device.handle, phys->queue_family.present_index, 0, &device.queue_present.handle);

    *out_device = device;
    return res;
err_cleanup_compute_pool:
    vkDestroyCommandPool(device.handle, device.queue_compute.command_pool, NULL);
err_cleanup_graphics_pool:
    vkDestroyCommandPool(device.handle, device.queue_graphics.command_pool, NULL);
err_cleanup_device:
    vkDestroyDevice(device.handle, NULL);
    return res;
}

#define VK_FRAMES_IN_FLIGHT 3
#define VK_SWAPCHAIN_IMAGE_COUNT (VK_FRAMES_IN_FLIGHT + 1) // figure out why (driver internals doesn't seem legit)

typedef struct {
    VkSwapchainKHR handle;
    VkFormat format;
    VkExtent2D extent;
    uint32_t n_images;
    VkFramebuffer frame_buffers[VK_SWAPCHAIN_IMAGE_COUNT];
    VkImageView image_views[VK_SWAPCHAIN_IMAGE_COUNT];
    VkImage images[VK_SWAPCHAIN_IMAGE_COUNT];
} Swapchain;

// enable passing the same argument for old and new
VkResult vk_create_swapchain(VkSurfaceKHR surface, LogicalDevice* device, Swapchain* old_swapchain, Swapchain* out_new) {
    VkSwapchainCreateInfoKHR info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = device->phys->surface_caps.minImageCount,
        .imageFormat = device->phys->surface_format.format,
        .imageColorSpace = device->phys->surface_format.colorSpace,
        .imageExtent = device->phys->surface_caps.currentExtent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, // later
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = NULL,
        .preTransform = device->phys->surface_caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = device->phys->present_mode,
        .clipped = VK_TRUE,
        .oldSwapchain = old_swapchain->handle
    };

    // if graphics and present are different queues, they need concurrent access to the swapchain
    uint32_t shared_indices[2] = { device->queue_graphics.family, device->queue_present.family };
    if (device->queue_graphics.family != device->queue_present.family) {
        info.queueFamilyIndexCount = 2;
        info.pQueueFamilyIndices = shared_indices;
        info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    }

    VkResult res = vkCreateSwapchainKHR(device->handle, &info, NULL, &out_new->handle);
    RETURN_IF_FAILED(res);
    // no turning back, destroy old
    for (int i = 0; i < VK_SWAPCHAIN_IMAGE_COUNT; i++) {
        vkDestroyFramebuffer(device->handle, old_swapchain->frame_buffers[i], NULL);
        vkDestroyImageView(device->handle, old_swapchain->image_views[i], NULL);
        // images not destroyed (managed by swapchain)
    }
    vkDestroySwapchainKHR(device->handle, old_swapchain->handle, NULL);

    // create new
    uint32_t n_images = 0;
    res = vkGetSwapchainImagesKHR(device->handle, out_new->handle, &n_images, NULL);
    RETURN_IF_FAILED(res);
    VkImage images[n_images];
    res = vkGetSwapchainImagesKHR(device->handle, out_new->handle, &n_images, images);
    RETURN_IF_FAILED(res);
    assert(n_images == VK_SWAPCHAIN_IMAGE_COUNT);

    // create and bind depth buffer
    VkImageCreateInfo depth_buffer_image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_D32_SFLOAT,
        .extent = { device->phys->surface_caps.currentExtent.width,
            device->phys->surface_caps.currentExtent.height, 
            1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    VkImage depth_buffer;
    res = vkCreateImage(device->handle, &depth_buffer_image_info, NULL, &depth_buffer);
    RETURN_IF_FAILED(res);

    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(device->handle, depth_buffer, &mem_reqs);

    // alloc space for image and bind it
    //
    gpu_arena_alloc(&device->gpu_arena_pool, &device->phys->mem_props, &mem_reqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkImageViewCreateInfo depth_image_view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        //.image = ,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_D32_SFLOAT,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    // color
    for (int i = 0; i < n_images; i++) {
        VkImageViewCreateInfo col_image_view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = device->phys->surface_format.format,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        res = vkCreateImageView(device->handle, &col_image_view_info, NULL, out_new->image_views + i);
        RETURN_IF_FAILED(res);

        VkImageView attachments[] = {out_new->image_views[i], /*TODO: add depth*/};
        VkFramebufferCreateInfo frame_buffer_info = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = 0/* do we put 0 here due to dynamic rendering? */,
            .attachmentCount = VK_LENGTH(attachments),
            .pAttachments = out_new->image_views + i,
            .width = device->phys->surface_caps.currentExtent.width,
            .height = device->phys->surface_caps.currentExtent.height,
            .layers = 1
        };
        res = vkCreateFramebuffer(device->handle, &frame_buffer_info, NULL, out_new->frame_buffers + i);
        RETURN_IF_FAILED(res);
    }

    return res;
}

typedef struct {
    VkInstance instance;
    VkSurfaceKHR surface;
    LogicalDevice device;
} VkCtx;

VkResult vk_init(VkInstance instance, VkSurfaceKHR surface, PhysicalDeviceInfo* selected_phys_device, VkCtx* out_ctx) {
    VkResult res;
    PhysicalDeviceList valid_devices;
    if (!selected_phys_device) {
        res = vk_enumerate_valid_devices(instance, surface, &valid_devices);
        RETURN_IF_FAILED(res);
        selected_phys_device = valid_devices.items + 0; // if none passed by user, we choose the first one
    }

    LogicalDevice device;
    res = vk_create_logical_device(selected_phys_device, &device);
    RETURN_IF_FAILED(res);

    // gpu mem
    device.gpu_buffer_arena_device_mesh = gpu_buffer_arena_create(
            device.handle,
            device.phys->handle,
            &device.phys->mem_props,
            16*1024*1024,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    device.gpu_buffer_arena_staging = gpu_buffer_arena_create(
            device.handle,
            device.phys->handle,
            &device.phys->mem_props,
            16*1024*1024,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    //device.gpu_arena_depth_buffer = gpu_arena_init(
    //        device.handle,
    //        &device.phys->mem_props,
    //        16*1024*1024,

    // create command buffers, need to know how many frames in flight
    // frames in flight comes from surface caps and depends on present mode

    out_ctx->instance = instance;
    out_ctx->surface = surface;
    out_ctx->device = device;
    return res;
}

typedef struct {
    void* vertex_data;
    size_t vertex_data_stride;
    uint32_t n_vertices;
    uint16_t* indices;
    uint32_t n_indices;
} MeshInfo;

MeshInfo vk_gen_cube(/*some way of giving memory, arena maybe*/) {
    vec3 vertices[] = {
        {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
        {0.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}
    };
    uint16_t indices[] = {
        0, 1, 2, 2, 3, 0, // front
        1, 6, 5, 5, 2, 1, // right
        6, 7, 4, 4, 5, 6, // back
        7, 0, 1, 1, 4, 7, // left
        7, 6, 1, 1, 0, 7, // bottom
        3, 2, 5, 5, 4, 3  // top
    };

    return (MeshInfo) {
        .vertex_data = vertices,
            .vertex_data_stride = sizeof(vec3),
            .n_vertices = VK_LENGTH(vertices),
            .indices = indices,
            .n_indices = VK_LENGTH(indices)
    };
}

typedef struct {
    VkShaderModule vertex;
    VkShaderModule fragment;
} Shader;

typedef struct {
    VkPipeline pipeline;
    VkPipelineLayout pipeline_layout;
    VkDescriptorSetLayout descriptor_set_layout;
} GraphicsPipeline;

typedef struct {
    Shader* shader;
    // data to aid setting uniforms
    VkDescriptorSet* descriptor_sets; // which textures for example
    uint32_t n_descriptor_sets;
    VkPipeline pipeline;
} Material;

typedef struct {
    VkFormat color_format;
    VkFormat depth_format;
    VkBool32 alpha_blend;
    // potential vertex input, or we derive from shader
} ShaderCreateInfo;

// load array of shader code then?

void pipelines_init() {

}

void vk_load_shader(Arena* arena, void** shader_code, size_t n_shaders) 
{
    // allocate stuffs
    for (int i = 0; i < n_shaders; i++) {
        SpvReflectShaderModule module;
        SpvReflect
    }
}

void vk_create_shader(
        Arena* arena,
        VkDevice device,
        void* code, size_t size,
        Shader* out_shader,
        Hashmap* cache
        )
{
    SpvReflectShaderModule module;
    SpvReflectResult res = spvReflectCreateShaderModule(size, code, &module);
    assert(res == SPV_REFLECT_RESULT_SUCCESS);

    uint32_t n_sets;
    res = spvReflectEnumerateDescriptorSets(&module, &n_sets, NULL);
    assert(res == SPV_REFLECT_RESULT_SUCCESS);
    SpvReflectDescriptorSet** sets = ARENA_ALLOC_N(arena, SpvReflectDescriptorSet*, n_sets);
    res = spvReflectEnumerateDescriptorSets(&module, &n_sets, sets);
    assert(res == SPV_REFLECT_RESULT_SUCCESS);

    for (int i = 0; i < n_sets; i++) {
        VkDescriptorSetLayoutBinding bindings[sets[i]->binding_count];
        for (int j = 0; j < sets[i]->binding_count; j++) {
            bindings[j].binding = sets[i]->bindings[j]->binding;
            bindings[j].descriptorType = (VkDescriptorType)sets[i]->bindings[j]->descriptor_type;
            bindings[j].descriptorCount = sets[i]->bindings[j]->count;
            bindings[j].stageFlags = (VkShaderStageFlagBits)module.shader_stage;
            bindings[j].pImmutableSamplers = 0;
        }
        VkDescriptorSetLayoutCreateInfo layout_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = sets[i]->binding_count,
            .pBindings = bindings
        };
        vkCreateDescriptorSetLayout(device, &layout_info, NULL, NULL/*todo, where to put it*/);
    }
    spvReflectDestroyShaderModule(&module);
}

VkResult vk_create_graphics_shader(
        VkDevice device,
        VkDescriptorSetLayout dsl_global,
        void* vert_code, uint32_t vert_code_size, 
        void* frag_code, uint32_t frag_code_size, 
        ShaderCreateInfo* shader_info,
        Shader* out_shader) 
{

    // create shaders
    VkResult res;

    // input bindings, hardcoded for now
    VkVertexInputBindingDescription vertex_input_bindings[] = {
        {.binding = 0, .stride = sizeof(vec3), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX}
    };
    VkVertexInputAttributeDescription vertex_input_attributes[] = {
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0}
    };

    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = VK_LENGTH(vertex_input_bindings),
        .pVertexBindingDescriptions = vertex_input_bindings,
        .vertexAttributeDescriptionCount = VK_LENGTH(vertex_input_attributes),
        .pVertexAttributeDescriptions = vertex_input_attributes
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        // everything else that prevents me from constructing render passes, eg depth stuff
    };
    VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = VK_LENGTH(dynamic_states),
        .pDynamicStates = dynamic_states,
    };

    VkPipelineViewportStateCreateInfo viewportInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    // Rasterization
    VkPipelineRasterizationStateCreateInfo rasterizerInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .lineWidth = 1.0f
    };

    // create new pipelines for this shader (based on some global setting?)
    //  create shader modules, then sensible defaults only.
    //  create descriptor sets from SPIRV reflection? And exposed mapped void ptr members in shader then?
    //  Then we can define a helper function for setting these uniforms via string or whatever

    // Multisample (antialiasing?, thought that was done in PP not per material, but cool...)
    VkPipelineMultisampleStateCreateInfo multisampleInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
    };

    // DepthStencil
    VkPipelineDepthStencilStateCreateInfo depthStencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE
    };

    // Color Blending
    VkPipelineColorBlendAttachmentState blendAttachment = {
        .blendEnable = VK_FALSE, // TODO: learn how to add blend
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };

    VkPipelineColorBlendStateCreateInfo blendStateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .attachmentCount = 1,
        .pAttachments = &blendAttachment
    };

    // descriptor set layout, should be derived form shader inputs. For now, no descriptor sets
    // Pipeline layout (descriptor sets)
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &dsl_global
    };
    VkPipelineLayout layout;
    res = vkCreatePipelineLayout(device, &pipelineLayoutInfo, NULL, &layout);
    RETURN_IF_FAILED(res); // need to destroy shader modules before.. maybe I should do the do while with break logic and an if at the end?
                           // alternatively, we create the shader modules last, probably better

    VkShaderModule vert_module, frag_module;
    VkResult res_vert, res_frag;
    {
        VkShaderModuleCreateInfo vert = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = vert_code_size,
            .pCode = (uint32_t*)vert_code // alignment??
        };
        res_vert = vkCreateShaderModule(device, &vert, NULL, &vert_module);
        VkShaderModuleCreateInfo frag = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = frag_code_size,
            .pCode = (uint32_t*)frag_code
        };
        res_frag = vkCreateShaderModule(device, &frag, NULL, &frag_module);
    }
    if (res_vert == VK_SUCCESS && res_frag == VK_SUCCESS) {
        VkPipelineShaderStageCreateInfo shader_stage_infos[2] = {
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = vert_module,
                .pName = "main"
            },
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = frag_module,
                .pName = "main"
            }
        };

        VkGraphicsPipelineCreateInfo pipelineInfo = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount = VK_LENGTH(shader_stage_infos),
            .pStages = shader_stage_infos,
            .pVertexInputState = &vertex_input,
            .pInputAssemblyState = &input_assembly,
            // skipping tesselation
            .pViewportState = &viewportInfo,
            .pRasterizationState = &rasterizerInfo,
            .pMultisampleState = &multisampleInfo,
            .pDepthStencilState = &depthStencil,
            .pColorBlendState = &blendStateInfo,
            .pDynamicState = &dynamic_state,
            // skipping dynamic state
            .layout = layout,
            .renderPass = 0, // should not be required if we do dynamic rendering
            .subpass = 0,
        };
        // TODO: create several pipelines at once instead
        VkPipeline pipeline; // where to put this then?
        res = vkCreateGraphicsPipelines(device, NULL/*TODO:cache required*/, 1, &pipelineInfo, NULL, &pipeline);
    }

    // destroy shader modules once ctx->pipeline is created
    vkDestroyShaderModule(device, vert_module, NULL);
    vkDestroyShaderModule(device, frag_module, NULL);
    RETURN_IF_FAILED(res);

    //out_shader->pipeline = pipeline;
    //out_shader->pipeline_layout = layout;
    //out_shader->descriptor_set_layout = 0;
    assert(res == VK_SUCCESS);
    return res;
}

typedef struct {
    //GpuAlloc vertices;
    //GpuAlloc indices;
    //uint32_t n_indices;
} Mesh;

VkResult vk_upload_mesh(MeshInfo* mesh, Mesh* out_mesh) {
    return VK_SUCCESS;
}

void vk_draw(VkCommandBuffer cmd, Mesh* mesh, Material* mat, mat4 transform) {

}