#pragma once

#include <vector>
#include <volk.h>
#include <codotaku/shader/slang_compiler.hpp>
#include <codotaku/vulkan/device.hpp>
#include <codotaku/vulkan/descriptor_heap.hpp>

namespace codotaku {

struct DescriptorBindingMapping {
    uint32_t set{0};
    uint32_t binding{0};
    uint32_t binding_count{1};
    VkSpirvResourceTypeFlagsEXT resource_mask{VK_SPIRV_RESOURCE_TYPE_ALL_EXT};
    VkDescriptorMappingSourceEXT source{VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT};
    VkDescriptorMappingSourceDataEXT source_data{};
};

class Pipeline {
public:
    Pipeline() = default;
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    Pipeline(Pipeline&& other) noexcept;
    Pipeline& operator=(Pipeline&& other) noexcept;

    // Initialize Graphics Dynamic Rendering Pipeline with Descriptor Heap
    void init_dynamic_rendering_bda(
        VulkanDevice& vk,
        const CompiledShaders& shaders,
        VkFormat color_format,
        VkFormat depth_format,
        const std::vector<DescriptorBindingMapping>& mappings = {},
        VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT,
        VkFrontFace front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        const char* debug_name = "Graphics Dynamic Rendering Pipeline");

    // Initialize Compute Pipeline with Descriptor Heap
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
