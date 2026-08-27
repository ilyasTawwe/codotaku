#pragma once

#include <volk.h>
#include <codotaku/shader/slang_compiler.hpp>
#include <codotaku/vulkan/context.hpp>

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

    void cleanup();

    VkPipeline get_pipeline() const { return m_pipeline; }
    VkPipelineLayout get_layout() const { return m_layout; }

private:
    VkDevice m_device{VK_NULL_HANDLE};
    VkPipeline m_pipeline{VK_NULL_HANDLE};
    VkPipelineLayout m_layout{VK_NULL_HANDLE};
};

} // namespace codotaku
