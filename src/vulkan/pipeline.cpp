#include <stdexcept>
#include <utility>

#include <codotaku/vulkan/pipeline.hpp>

namespace codotaku {

Pipeline::~Pipeline() {
    cleanup();
}

Pipeline::Pipeline(Pipeline&& other) noexcept
    : m_device(std::exchange(other.m_device, VK_NULL_HANDLE)),
      m_pipeline(std::exchange(other.m_pipeline, VK_NULL_HANDLE)),
      m_layout(std::exchange(other.m_layout, VK_NULL_HANDLE)),
      m_set_layouts(std::move(other.m_set_layouts)),
      m_descriptor_pool(std::exchange(other.m_descriptor_pool, VK_NULL_HANDLE)) {}

Pipeline& Pipeline::operator=(Pipeline&& other) noexcept {
    if (this != &other) {
        cleanup();
        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_pipeline = std::exchange(other.m_pipeline, VK_NULL_HANDLE);
        m_layout = std::exchange(other.m_layout, VK_NULL_HANDLE);
        m_set_layouts = std::move(other.m_set_layouts);
        m_descriptor_pool = std::exchange(other.m_descriptor_pool, VK_NULL_HANDLE);
    }
    return *this;
}

void Pipeline::create_descriptor_infrastructure(const CompiledShaders& shaders) {
    // 1. Create Reflected Descriptor Set Layouts
    for (const auto& [set_idx, bindings] : shaders.reflection.descriptor_sets) {
        std::vector<VkDescriptorSetLayoutBinding> vk_bindings;
        for (const auto& b : bindings) {
            vk_bindings.push_back({
                .binding = b.binding,
                .descriptorType = b.descriptor_type,
                .descriptorCount = 1,
                .stageFlags = b.stage_flags,
            });
        }

        VkDescriptorSetLayoutCreateInfo layout_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = static_cast<uint32_t>(vk_bindings.size()),
            .pBindings = vk_bindings.data(),
        };

        VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
        if (vkCreateDescriptorSetLayout(m_device, &layout_info, nullptr, &set_layout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create descriptor set layout");
        }
        m_set_layouts.push_back(set_layout);
    }

    // 2. Create Descriptor Pool if descriptor sets are used
    if (!m_set_layouts.empty()) {
        VkDescriptorPoolSize pool_sizes[] = {
            { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 64 },
            { .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 64 },
            { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 64 },
            { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 64 },
        };

        VkDescriptorPoolCreateInfo pool_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            .maxSets = 64,
            .poolSizeCount = 4,
            .pPoolSizes = pool_sizes,
        };

        if (vkCreateDescriptorPool(m_device, &pool_info, nullptr, &m_descriptor_pool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create pipeline descriptor pool");
        }
    }

    // 3. Create Pipeline Layout with Reflected Push Constants & Set Layouts
    const auto& push_constants = shaders.reflection.push_constants;
    VkPipelineLayoutCreateInfo pipeline_layout_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = static_cast<uint32_t>(m_set_layouts.size()),
        .pSetLayouts = m_set_layouts.data(),
        .pushConstantRangeCount = static_cast<uint32_t>(push_constants.size()),
        .pPushConstantRanges = push_constants.data(),
    };

    if (vkCreatePipelineLayout(m_device, &pipeline_layout_info, nullptr, &m_layout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create pipeline layout");
    }
}

void Pipeline::init_dynamic_rendering_bda(
    VulkanContext& vk,
    const CompiledShaders& shaders,
    VkFormat color_format,
    VkFormat depth_format,
    VkCullModeFlags cull_mode,
    VkFrontFace front_face) {
    cleanup();
    m_device = vk.get_device();

    VkShaderModule vs_module = create_shader_module(m_device, shaders.vs_spirv);
    VkShaderModule fs_module = create_shader_module(m_device, shaders.fs_spirv);

    VkPipelineShaderStageCreateInfo shader_stages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vs_module,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fs_module,
            .pName = "main",
        },
    };

    // Programmable Vertex Pulling: zero fixed-function vertex input bindings
    VkPipelineVertexInputStateCreateInfo vertex_input_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 0,
        .pVertexBindingDescriptions = nullptr,
        .vertexAttributeDescriptionCount = 0,
        .pVertexAttributeDescriptions = nullptr,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    VkPipelineViewportStateCreateInfo viewport_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    VkPipelineRasterizationStateCreateInfo rasterizer{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = cull_mode,
        .frontFace = front_face,
        .lineWidth = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisampling{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
    };

    VkPipelineDepthStencilStateCreateInfo depth_stencil{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
    };

    VkPipelineColorBlendAttachmentState color_blend_attachment{
        .blendEnable = VK_FALSE,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };

    VkPipelineColorBlendStateCreateInfo color_blending{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment,
    };

    VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    VkPipelineDynamicStateCreateInfo dynamic_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamic_states,
    };

    create_descriptor_infrastructure(shaders);

    VkFormat color_fmt = color_format;
    VkFormat depth_fmt = depth_format;
    VkPipelineRenderingCreateInfo pipeline_rendering_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &color_fmt,
        .depthAttachmentFormat = depth_fmt,
    };

    VkGraphicsPipelineCreateInfo pipeline_info{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &pipeline_rendering_info,
        .stageCount = 2,
        .pStages = shader_stages,
        .pVertexInputState = &vertex_input_info,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &color_blending,
        .pDynamicState = &dynamic_state,
        .layout = m_layout,
        .renderPass = VK_NULL_HANDLE,
    };

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &m_pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create graphics pipeline for 3D dynamic rendering with BDA");
    }

    vkDestroyShaderModule(m_device, fs_module, nullptr);
    vkDestroyShaderModule(m_device, vs_module, nullptr);
}

void Pipeline::init_compute(
    VulkanContext& vk,
    const CompiledShaders& shaders) {
    cleanup();
    m_device = vk.get_device();

    VkShaderModule cs_module = create_shader_module(m_device, shaders.cs_spirv);

    create_descriptor_infrastructure(shaders);

    VkComputePipelineCreateInfo pipeline_info{
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = cs_module,
            .pName = "main",
        },
        .layout = m_layout,
    };

    if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &m_pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create compute pipeline");
    }

    vkDestroyShaderModule(m_device, cs_module, nullptr);
}

VkDescriptorSetLayout Pipeline::get_descriptor_set_layout(uint32_t set_index) const {
    if (set_index >= m_set_layouts.size()) {
        throw std::runtime_error("Descriptor set index out of bounds");
    }
    return m_set_layouts[set_index];
}

VkDescriptorSet Pipeline::create_texture_descriptor_set(const Texture& texture, uint32_t set_index, uint32_t binding_index) const {
    if (m_descriptor_pool == VK_NULL_HANDLE || set_index >= m_set_layouts.size()) {
        throw std::runtime_error("Cannot allocate descriptor set: no layout or pool available");
    }

    VkDescriptorSetLayout set_layout = m_set_layouts[set_index];
    VkDescriptorSetAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = m_descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &set_layout,
    };

    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(m_device, &alloc_info, &set) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor set for texture");
    }

    VkDescriptorImageInfo image_info = texture.get_descriptor_image_info();

    VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set,
        .dstBinding = binding_index,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &image_info,
    };

    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    return set;
}

VkDescriptorSet Pipeline::create_storage_image_descriptor_set(VkImageView image_view, uint32_t set_index, uint32_t binding_index) const {
    if (m_descriptor_pool == VK_NULL_HANDLE || set_index >= m_set_layouts.size()) {
        throw std::runtime_error("Cannot allocate descriptor set: no layout or pool available");
    }

    VkDescriptorSetLayout set_layout = m_set_layouts[set_index];
    VkDescriptorSetAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = m_descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &set_layout,
    };

    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(m_device, &alloc_info, &set) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor set for storage image");
    }

    VkDescriptorImageInfo image_info{
        .sampler = VK_NULL_HANDLE,
        .imageView = image_view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };

    VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set,
        .dstBinding = binding_index,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo = &image_info,
    };

    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    return set;
}

void Pipeline::cleanup() {
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);

        if (m_descriptor_pool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_device, m_descriptor_pool, nullptr);
            m_descriptor_pool = VK_NULL_HANDLE;
        }

        for (auto layout : m_set_layouts) {
            if (layout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(m_device, layout, nullptr);
            }
        }
        m_set_layouts.clear();

        if (m_pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, m_pipeline, nullptr);
            m_pipeline = VK_NULL_HANDLE;
        }
        if (m_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_device, m_layout, nullptr);
            m_layout = VK_NULL_HANDLE;
        }
        m_device = VK_NULL_HANDLE;
    }
}

} // namespace codotaku
