#include <algorithm>
#include <cmath>
#include <print>
#include <stdexcept>
#include <utility>

#include <codotaku/system/log.hpp>
#include <codotaku/vulkan/device.hpp>
#include <codotaku/vulkan/gbuffer.hpp>

namespace codotaku {

namespace {

bool is_depth_format(VkFormat format) {
    switch (format) {
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return true;
        default:
            return false;
    }
}

VkImageAspectFlags get_aspect_mask(VkFormat format) {
    if (is_depth_format(format)) {
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

} // namespace

GBuffer::GBuffer(VulkanDevice& vk, uint32_t default_width, uint32_t default_height) {
    init(vk, default_width, default_height);
}

GBuffer::~GBuffer() {
    cleanup();
}

GBuffer::GBuffer(GBuffer&& other) noexcept
    : m_device(std::exchange(other.m_device, VK_NULL_HANDLE)),
      m_allocator(std::exchange(other.m_allocator, VK_NULL_HANDLE)),
      m_vk(std::exchange(other.m_vk, nullptr)),
      m_default_width(std::exchange(other.m_default_width, 0)),
      m_default_height(std::exchange(other.m_default_height, 0)),
      m_attachments(std::move(other.m_attachments)),
      m_free_indices(std::move(other.m_free_indices)),
      m_active_count(std::exchange(other.m_active_count, 0)) {}

GBuffer& GBuffer::operator=(GBuffer&& other) noexcept {
    if (this != &other) {
        cleanup();
        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_allocator = std::exchange(other.m_allocator, VK_NULL_HANDLE);
        m_vk = std::exchange(other.m_vk, nullptr);
        m_default_width = std::exchange(other.m_default_width, 0);
        m_default_height = std::exchange(other.m_default_height, 0);
        m_attachments = std::move(other.m_attachments);
        m_free_indices = std::move(other.m_free_indices);
        m_active_count = std::exchange(other.m_active_count, 0);
    }
    return *this;
}

void GBuffer::init(VulkanDevice& vk, uint32_t default_width, uint32_t default_height) {
    cleanup();
    m_vk = &vk;
    m_device = vk.get_device();
    m_allocator = vk.get_allocator();
    m_default_width = default_width;
    m_default_height = default_height;
}

void GBuffer::cleanup() {
    if (m_device != VK_NULL_HANDLE && m_allocator != VK_NULL_HANDLE) {
        if (m_vk) m_vk->vkd().vkDeviceWaitIdle(m_device);
        for (auto& att : m_attachments) {
            if (att.active) {
                free_attachment_resources(att);
            }
        }
    }
    m_attachments.clear();
    m_free_indices.clear();
    m_active_count = 0;
    m_device = VK_NULL_HANDLE;
    m_allocator = VK_NULL_HANDLE;
    m_vk = nullptr;
}

uint32_t GBuffer::add_attachment(const AttachmentDesc& desc) {
    if (m_device == VK_NULL_HANDLE || m_allocator == VK_NULL_HANDLE) {
        throw std::runtime_error("GBuffer must be initialized with VulkanDevice before adding attachments");
    }

    uint32_t id = 0;
    if (!m_free_indices.empty()) {
        id = m_free_indices.back();
        m_free_indices.pop_back();
    } else {
        id = static_cast<uint32_t>(m_attachments.size());
        m_attachments.emplace_back();
    }

    auto& att = m_attachments[id];
    att.id = id;
    att.name = desc.name;
    att.desc = desc;
    att.format = desc.format;
    att.aspect_mask = get_aspect_mask(desc.format);
    att.width = (desc.width > 0) ? desc.width : m_default_width;
    att.height = (desc.height > 0) ? desc.height : m_default_height;
    att.active = true;

    allocate_attachment_resources(att);
    m_active_count++;

    log_info("[Codotaku GBuffer] Created attachment [{}] '{}' ({}x{}, format: {})",
        id, att.name, att.width, att.height, static_cast<int>(att.format));

    return id;
}

void GBuffer::remove_attachment(uint32_t id) {
    if (!has_attachment(id)) {
        return;
    }

    auto& att = m_attachments[id];
    log_info("[Codotaku GBuffer] Destroying attachment [{}] '{}'", id, att.name);
    free_attachment_resources(att);
    att.active = false;
    m_free_indices.push_back(id);
    m_active_count--;
}

bool GBuffer::has_attachment(uint32_t id) const {
    return id < m_attachments.size() && m_attachments[id].active;
}

void GBuffer::resize(uint32_t id, uint32_t new_width, uint32_t new_height) {
    if (!has_attachment(id) || new_width == 0 || new_height == 0) return;
    auto& att = m_attachments[id];
    if (att.width == new_width && att.height == new_height) return;

    if (m_device != VK_NULL_HANDLE && m_vk) {
        m_vk->vkd().vkDeviceWaitIdle(m_device);
    }

    free_attachment_resources(att);
    att.width = new_width;
    att.height = new_height;
    allocate_attachment_resources(att);
}

void GBuffer::resize_all(uint32_t new_width, uint32_t new_height) {
    if (new_width == 0 || new_height == 0) return;
    m_default_width = new_width;
    m_default_height = new_height;

    if (m_device != VK_NULL_HANDLE && m_vk) {
        m_vk->vkd().vkDeviceWaitIdle(m_device);
    }

    for (auto& att : m_attachments) {
        if (att.active) {
            free_attachment_resources(att);
            att.width = new_width;
            att.height = new_height;
            allocate_attachment_resources(att);
        }
    }
}

const Attachment& GBuffer::get(uint32_t id) const {
    if (!has_attachment(id)) {
        throw std::runtime_error("Invalid or inactive GBuffer attachment ID: " + std::to_string(id));
    }
    return m_attachments[id];
}

VkImageView GBuffer::get_view(uint32_t id) const {
    return get(id).view;
}

VkImage GBuffer::get_image(uint32_t id) const {
    return get(id).image;
}

VkFormat GBuffer::get_format(uint32_t id) const {
    return get(id).format;
}

uint32_t GBuffer::get_width(uint32_t id) const {
    return get(id).width;
}

uint32_t GBuffer::get_height(uint32_t id) const {
    return get(id).height;
}

VkRenderingAttachmentInfo GBuffer::get_rendering_attachment_info(
    uint32_t id,
    VkAttachmentLoadOp load_op,
    VkAttachmentStoreOp store_op) const {
    const auto& att = get(id);

    VkRenderingAttachmentInfo info{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = att.view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE,
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = load_op,
        .storeOp = store_op,
        .clearValue = att.desc.clear_value,
    };
    return info;
}

void GBuffer::transition(
    VkCommandBuffer cmd,
    uint32_t id,
    VkImageLayout new_layout,
    VkPipelineStageFlags2 dst_stage,
    VkAccessFlags2 dst_access) {
    auto& att = m_attachments[id];
    if (!att.active || att.current_layout == new_layout) {
        return;
    }

    VkImageSubresourceRange range{
        .aspectMask = att.aspect_mask,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };

    VkImageMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext = nullptr,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask = dst_stage,
        .dstAccessMask = dst_access,
        .oldLayout = att.current_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = att.image,
        .subresourceRange = range,
    };

    VkDependencyInfo dep_info{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext = nullptr,
        .dependencyFlags = 0,
        .memoryBarrierCount = 0,
        .pMemoryBarriers = nullptr,
        .bufferMemoryBarrierCount = 0,
        .pBufferMemoryBarriers = nullptr,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };

    if (m_vk) {
        m_vk->vkd().vkCmdPipelineBarrier2(cmd, &dep_info);
    }
    att.current_layout = new_layout;
}

void GBuffer::allocate_attachment_resources(Attachment& att) {
    VkImageUsageFlags usage = att.desc.usage;
    if (is_depth_format(att.format)) {
        usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }

    VkImageCreateInfo image_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = att.format,
        .extent = { att.width, att.height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = att.desc.samples,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VmaAllocationCreateInfo alloc_info{
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };

    if (vmaCreateImage(m_allocator, &image_info, &alloc_info, &att.image, &att.allocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate GBuffer image via VMA for attachment: " + att.name);
    }

    if (m_vk) {
        m_vk->set_name(att.image, att.name.c_str());
    }

    VkImageViewCreateInfo view_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .image = att.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = att.format,
        .components = {},
        .subresourceRange = {
            .aspectMask = att.aspect_mask,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    if (m_vk->vkd().vkCreateImageView(m_device, &view_info, nullptr, &att.view) != VK_SUCCESS) {
        vmaDestroyImage(m_allocator, att.image, att.allocation);
        att.image = VK_NULL_HANDLE;
        att.allocation = VK_NULL_HANDLE;
        throw std::runtime_error("Failed to create GBuffer image view for attachment: " + att.name);
    }

    if (m_vk) {
        m_vk->set_name(att.view, (att.name + "_view").c_str());
        m_vk->execute_single_time_commands([&](VkCommandBuffer cmd) {
            VkImageMemoryBarrier2 init_barrier{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .pNext = nullptr,
                .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
                .srcAccessMask = VK_ACCESS_2_NONE,
                .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = att.image,
                .subresourceRange = {
                    .aspectMask = att.aspect_mask,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            };

            VkDependencyInfo dep{
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .pNext = nullptr,
                .dependencyFlags = 0,
                .memoryBarrierCount = 0,
                .pMemoryBarriers = nullptr,
                .bufferMemoryBarrierCount = 0,
                .pBufferMemoryBarriers = nullptr,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &init_barrier,
            };
            m_vk->vkd().vkCmdPipelineBarrier2(cmd, &dep);
        });
    }

    att.current_layout = VK_IMAGE_LAYOUT_GENERAL;
}

void GBuffer::free_attachment_resources(Attachment& att) {
    if (att.view != VK_NULL_HANDLE) {
        if (m_vk) m_vk->vkd().vkDestroyImageView(m_device, att.view, nullptr);
        att.view = VK_NULL_HANDLE;
    }
    if (att.image != VK_NULL_HANDLE) {
        vmaDestroyImage(m_allocator, att.image, att.allocation);
        att.image = VK_NULL_HANDLE;
        att.allocation = VK_NULL_HANDLE;
    }
    att.current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
}

} // namespace codotaku
