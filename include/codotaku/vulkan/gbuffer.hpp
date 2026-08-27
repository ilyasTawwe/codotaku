#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>
#include <codotaku/core/types.hpp>

namespace codotaku {

class VulkanDevice;

struct Attachment {
    uint32_t id{0};
    std::string name;
    VkImage image{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
    VmaAllocation allocation{VK_NULL_HANDLE};
    VkFormat format{VK_FORMAT_UNDEFINED};
    uint32_t width{0};
    uint32_t height{0};
    VkImageAspectFlags aspect_mask{VK_IMAGE_ASPECT_COLOR_BIT};
    VkImageLayout current_layout{VK_IMAGE_LAYOUT_UNDEFINED};
    AttachmentDesc desc{};
    bool active{false};
};

class GBuffer {
public:
    GBuffer() = default;
    GBuffer(VulkanDevice& vk, uint32_t default_width = 800, uint32_t default_height = 600);
    ~GBuffer();

    GBuffer(const GBuffer&) = delete;
    GBuffer& operator=(const GBuffer&) = delete;

    GBuffer(GBuffer&& other) noexcept;
    GBuffer& operator=(GBuffer&& other) noexcept;

    void init(VulkanDevice& vk, uint32_t default_width = 800, uint32_t default_height = 600);
    void cleanup();

    // Dynamic attachment management (returns stable integer index ID)
    uint32_t add_attachment(const AttachmentDesc& desc);
    void remove_attachment(uint32_t id);
    bool has_attachment(uint32_t id) const;

    // Resizing
    void resize(uint32_t id, uint32_t new_width, uint32_t new_height);
    void resize_all(uint32_t new_width, uint32_t new_height);

    // Accessors by index ID
    const Attachment& get(uint32_t id) const;
    VkImageView get_view(uint32_t id) const;
    VkImage get_image(uint32_t id) const;
    VkFormat get_format(uint32_t id) const;
    uint32_t get_width(uint32_t id) const;
    uint32_t get_height(uint32_t id) const;
    size_t get_active_count() const { return m_active_count; }

    // Dynamic Rendering & Synchronization 2 helpers
    VkRenderingAttachmentInfo get_rendering_attachment_info(
        uint32_t id,
        VkAttachmentLoadOp load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
        VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_STORE) const;

    void transition(
        VkCommandBuffer cmd,
        uint32_t id,
        VkImageLayout new_layout,
        VkPipelineStageFlags2 dst_stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        VkAccessFlags2 dst_access = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);

private:
    void allocate_attachment_resources(Attachment& att);
    void free_attachment_resources(Attachment& att);

    VkDevice m_device{VK_NULL_HANDLE};
    VmaAllocator m_allocator{VK_NULL_HANDLE};
    VulkanDevice* m_vk{nullptr};
    uint32_t m_default_width{800};
    uint32_t m_default_height{600};

    std::vector<Attachment> m_attachments;
    std::vector<uint32_t> m_free_indices;
    size_t m_active_count{0};
};

} // namespace codotaku
