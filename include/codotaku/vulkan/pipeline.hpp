#pragma once

#include <vector>
#include <volk.h>
#include <codotaku/shader/slang_compiler.hpp>
#include <codotaku/vulkan/context.hpp>
#include <codotaku/vulkan/texture.hpp>

namespace codotaku {

class Pipeline {
public:
    Pipeline() = default;
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    Pipeline(Pipeline&& other) noexcept;
    Pipeline& operator=(Pipeline&& other) noexcept;

    void init_dynamic_rendering_bda(
        VulkanContext& vk,
        const CompiledShaders& shaders,
        VkFormat color_format,
        VkFormat depth_format,
        VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT,
        VkFrontFace front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE);

    VkDescriptorSet create_texture_descriptor_set(const Texture& texture, uint32_t set_index = 0, uint32_t binding_index = 0) const;

    void cleanup();

    VkPipeline get_pipeline() const { return m_pipeline; }
    VkPipelineLayout get_layout() const { return m_layout; }
    VkDescriptorSetLayout get_descriptor_set_layout(uint32_t set_index = 0) const;

private:
    VkDevice m_device{VK_NULL_HANDLE};
    VkPipeline m_pipeline{VK_NULL_HANDLE};
    VkPipelineLayout m_layout{VK_NULL_HANDLE};
    std::vector<VkDescriptorSetLayout> m_set_layouts;
    VkDescriptorPool m_descriptor_pool{VK_NULL_HANDLE};
};

} // namespace codotaku
