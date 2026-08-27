#include <print>
#include <stdexcept>
#include <utility>
#include <vector>

#include <codotaku/vulkan/pipeline.hpp>

namespace codotaku {

Pipeline::~Pipeline() {
    cleanup();
}

Pipeline::Pipeline(Pipeline&& other) noexcept
    : m_device(std::exchange(other.m_device, VK_NULL_HANDLE)),
      m_pipeline(std::exchange(other.m_pipeline, VK_NULL_HANDLE)) {}

Pipeline& Pipeline::operator=(Pipeline&& other) noexcept {
    if (this != &other) {
        cleanup();
        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_pipeline = std::exchange(other.m_pipeline, VK_NULL_HANDLE);
    }
    return *this;
}

DescriptorBindingMapping Pipeline::map_sampled_texture(
    uint32_t set,
    uint32_t binding,
    uint32_t image_heap_offset,
    uint32_t sampler_heap_offset) {
    DescriptorBindingMapping m{
        .set = set,
        .binding = binding,
        .binding_count = 1,
        .resource_mask = VK_SPIRV_RESOURCE_TYPE_COMBINED_SAMPLED_IMAGE_BIT_EXT,
        .source = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT,
    };
    m.source_data.constantOffset.heapOffset = image_heap_offset;
    m.source_data.constantOffset.heapArrayStride = 32;
    m.source_data.constantOffset.samplerHeapOffset = sampler_heap_offset;
    m.source_data.constantOffset.samplerHeapArrayStride = 32;
    return m;
}

DescriptorBindingMapping Pipeline::map_storage_image(
    uint32_t set,
    uint32_t binding,
    uint32_t image_heap_offset) {
    DescriptorBindingMapping m{
        .set = set,
        .binding = binding,
        .binding_count = 1,
        .resource_mask = VK_SPIRV_RESOURCE_TYPE_READ_WRITE_IMAGE_BIT_EXT,
        .source = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT,
    };
    m.source_data.constantOffset.heapOffset = image_heap_offset;
    m.source_data.constantOffset.heapArrayStride = 32;
    return m;
}

void Pipeline::init_dynamic_rendering_bda(
    VulkanContext& vk,
    const CompiledShaders& shaders,
    VkFormat color_format,
    VkFormat depth_format,
    const std::vector<DescriptorBindingMapping>& mappings,
    VkCullModeFlags cull_mode,
    VkFrontFace front_face) {
    cleanup();
    m_device = vk.get_device();

    VkShaderModule vs_module = create_shader_module(m_device, shaders.vs_spirv);
    VkShaderModule fs_module = create_shader_module(m_device, shaders.fs_spirv);

    // Convert mappings to Vulkan Descriptor Set and Binding Mappings
    std::vector<VkDescriptorSetAndBindingMappingEXT> vk_mappings;
    for (const auto& m : mappings) {
        vk_mappings.push_back({
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_AND_BINDING_MAPPING_EXT,
            .descriptorSet = m.set,
            .firstBinding = m.binding,
            .bindingCount = m.binding_count,
            .resourceMask = m.resource_mask,
            .source = m.source,
            .sourceData = m.source_data,
        });
    }

    VkShaderDescriptorSetAndBindingMappingInfoEXT mapping_info{
        .sType = VK_STRUCTURE_TYPE_SHADER_DESCRIPTOR_SET_AND_BINDING_MAPPING_INFO_EXT,
        .mappingCount = static_cast<uint32_t>(vk_mappings.size()),
        .pMappings = vk_mappings.empty() ? nullptr : vk_mappings.data(),
    };

    void* stage_pnext = vk_mappings.empty() ? nullptr : &mapping_info;

    VkPipelineShaderStageCreateInfo shader_stages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = stage_pnext,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vs_module,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = stage_pnext,
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
        .depthTestEnable = (depth_format != VK_FORMAT_UNDEFINED) ? VK_TRUE : VK_FALSE,
        .depthWriteEnable = (depth_format != VK_FORMAT_UNDEFINED) ? VK_TRUE : VK_FALSE,
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
        .attachmentCount = (color_format != VK_FORMAT_UNDEFINED) ? 1u : 0u,
        .pAttachments = (color_format != VK_FORMAT_UNDEFINED) ? &color_blend_attachment : nullptr,
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

    VkFormat color_fmt = color_format;
    VkFormat depth_fmt = depth_format;
    VkPipelineRenderingCreateInfo pipeline_rendering_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = (color_format != VK_FORMAT_UNDEFINED) ? 1u : 0u,
        .pColorAttachmentFormats = (color_format != VK_FORMAT_UNDEFINED) ? &color_fmt : nullptr,
        .depthAttachmentFormat = depth_fmt,
    };

    VkPipelineCreateFlags2CreateInfo flags2_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO,
        .pNext = &pipeline_rendering_info,
        .flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT,
    };

    VkGraphicsPipelineCreateInfo pipeline_info{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &flags2_info,
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
        .layout = VK_NULL_HANDLE, // Completely layout-less!
        .renderPass = VK_NULL_HANDLE,
    };

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &m_pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create graphics pipeline with VK_EXT_descriptor_heap");
    }

    vkDestroyShaderModule(m_device, fs_module, nullptr);
    vkDestroyShaderModule(m_device, vs_module, nullptr);
}

void Pipeline::init_compute(
    VulkanContext& vk,
    const CompiledShaders& shaders,
    const std::vector<DescriptorBindingMapping>& mappings) {
    cleanup();
    m_device = vk.get_device();

    VkShaderModule cs_module = create_shader_module(m_device, shaders.cs_spirv);

    std::vector<VkDescriptorSetAndBindingMappingEXT> vk_mappings;
    for (const auto& m : mappings) {
        vk_mappings.push_back({
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_AND_BINDING_MAPPING_EXT,
            .descriptorSet = m.set,
            .firstBinding = m.binding,
            .bindingCount = m.binding_count,
            .resourceMask = m.resource_mask,
            .source = m.source,
            .sourceData = m.source_data,
        });
    }

    VkShaderDescriptorSetAndBindingMappingInfoEXT mapping_info{
        .sType = VK_STRUCTURE_TYPE_SHADER_DESCRIPTOR_SET_AND_BINDING_MAPPING_INFO_EXT,
        .mappingCount = static_cast<uint32_t>(vk_mappings.size()),
        .pMappings = vk_mappings.empty() ? nullptr : vk_mappings.data(),
    };

    void* stage_pnext = vk_mappings.empty() ? nullptr : &mapping_info;

    VkPipelineCreateFlags2CreateInfo flags2_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO,
        .flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT,
    };

    VkComputePipelineCreateInfo pipeline_info{
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .pNext = &flags2_info,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = stage_pnext,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = cs_module,
            .pName = "main",
        },
        .layout = VK_NULL_HANDLE, // Completely layout-less!
    };

    if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &m_pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create compute pipeline with VK_EXT_descriptor_heap");
    }

    vkDestroyShaderModule(m_device, cs_module, nullptr);
}

void Pipeline::cleanup() {
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);

        if (m_pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, m_pipeline, nullptr);
            m_pipeline = VK_NULL_HANDLE;
        }
        m_device = VK_NULL_HANDLE;
    }
}

} // namespace codotaku
