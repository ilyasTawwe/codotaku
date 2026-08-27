#pragma once

#include <vector>
#include <volk.h>
#include <codotaku/shader/slang_compiler.hpp>
#include <codotaku/vulkan/descriptor_heap.hpp>

namespace codotaku {

class VulkanDevice;

struct DescriptorBindingMapping {
    uint32_t set{0};
    uint32_t binding{0};
    uint32_t binding_count{1};
    VkSpirvResourceTypeFlagsEXT resource_mask{VK_SPIRV_RESOURCE_TYPE_ALL_EXT};
    VkDescriptorMappingSourceEXT source{VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT};
    VkDescriptorMappingSourceDataEXT source_data{};
};

struct GraphicsPipelineConfig {
    VkFormat color_format{VK_FORMAT_B8G8R8A8_UNORM};
    VkFormat depth_format{VK_FORMAT_D32_SFLOAT};
    VkFormat stencil_format{VK_FORMAT_UNDEFINED};
    std::vector<DescriptorBindingMapping> mappings{};
    VkCullModeFlags cull_mode{VK_CULL_MODE_BACK_BIT};
    VkFrontFace front_face{VK_FRONT_FACE_COUNTER_CLOCKWISE};
    VkPolygonMode polygon_mode{VK_POLYGON_MODE_FILL};
    float line_width{1.0f};
    bool depth_test_enable{true};
    bool depth_write_enable{true};
    VkCompareOp depth_compare_op{VK_COMPARE_OP_LESS};
    VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState blend_attachment{
        .blendEnable = VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    const char* debug_name{"Graphics Dynamic Rendering Pipeline"};
};

struct ComputePipelineConfig {
    std::vector<DescriptorBindingMapping> mappings{};
    const char* debug_name{"Compute Pipeline"};
};

class Pipeline {
public:
    Pipeline() = default;
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    Pipeline(Pipeline&& other) noexcept;
    Pipeline& operator=(Pipeline&& other) noexcept;

    // Initialize with comprehensive config
    void init_dynamic_rendering_bda(
        VulkanDevice& vk,
        const CompiledShaders& shaders,
        const GraphicsPipelineConfig& config);

    // Convenience initializer
    void init_dynamic_rendering_bda(
        VulkanDevice& vk,
        const CompiledShaders& shaders,
        VkFormat color_format,
        VkFormat depth_format,
        const std::vector<DescriptorBindingMapping>& mappings = {},
        VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT,
        VkFrontFace front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        const char* debug_name = "Graphics Dynamic Rendering Pipeline");

    // Initialize Compute Pipeline with config
    void init_compute(
        VulkanDevice& vk,
        const CompiledShaders& shaders,
        const ComputePipelineConfig& config);

    // Convenience Compute initializer
    void init_compute(
        VulkanDevice& vk,
        const CompiledShaders& shaders,
        const std::vector<DescriptorBindingMapping>& mappings = {},
        const char* debug_name = "Compute Pipeline");

    void cleanup();

    VkPipeline get_pipeline() const { return m_pipeline; }

    // Mapping factory helpers
    static DescriptorBindingMapping map_sampled_texture(
        uint32_t set,
        uint32_t binding,
        uint32_t image_heap_offset,
        uint32_t sampler_heap_offset = 512);

    static DescriptorBindingMapping map_storage_image(
        uint32_t set,
        uint32_t binding,
        uint32_t image_heap_offset);

private:
    VkDevice m_device{VK_NULL_HANDLE};
    VolkDeviceTable m_vkd{};
    VkPipeline m_pipeline{VK_NULL_HANDLE};
};

} // namespace codotaku
