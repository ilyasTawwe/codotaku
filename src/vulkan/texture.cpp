#include <print>
#include <stdexcept>
#include <utility>

#include <codotaku/vulkan/context.hpp>
#include <codotaku/vulkan/texture.hpp>

namespace codotaku {

Texture::~Texture() {
    cleanup();
}

Texture::Texture(Texture&& other) noexcept
    : m_device(std::exchange(other.m_device, VK_NULL_HANDLE)),
      m_allocator(std::exchange(other.m_allocator, VK_NULL_HANDLE)),
      m_desc(std::exchange(other.m_desc, {})),
      m_image(std::exchange(other.m_image, VK_NULL_HANDLE)),
      m_view(std::exchange(other.m_view, VK_NULL_HANDLE)),
      m_allocation(std::exchange(other.m_allocation, VK_NULL_HANDLE)),
      m_sampler(std::exchange(other.m_sampler, VK_NULL_HANDLE)) {}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        cleanup();
        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_allocator = std::exchange(other.m_allocator, VK_NULL_HANDLE);
        m_desc = std::exchange(other.m_desc, {});
        m_image = std::exchange(other.m_image, VK_NULL_HANDLE);
        m_view = std::exchange(other.m_view, VK_NULL_HANDLE);
        m_allocation = std::exchange(other.m_allocation, VK_NULL_HANDLE);
        m_sampler = std::exchange(other.m_sampler, VK_NULL_HANDLE);
    }
    return *this;
}

void Texture::init(
    VkDevice device,
    VmaAllocator allocator,
    VkImage image,
    VkImageView view,
    VmaAllocation allocation,
    VkSampler sampler,
    const TextureDesc& desc) {
    cleanup();
    m_device = device;
    m_allocator = allocator;
    m_image = image;
    m_view = view;
    m_allocation = allocation;
    m_sampler = sampler;
    m_desc = desc;
}

Texture Texture::create_uninitialized(
    VulkanContext& vk,
    uint32_t width,
    uint32_t height,
    const TextureDesc& desc) {
    VkDevice device = vk.get_device();
    VmaAllocator allocator = vk.get_allocator();

    TextureDesc final_desc = desc;
    final_desc.width = width;
    final_desc.height = height;

    VkImageCreateInfo image_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = final_desc.format,
        .extent = { width, height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = final_desc.usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VmaAllocationCreateInfo alloc_info{
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };

    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    if (vmaCreateImage(allocator, &image_info, &alloc_info, &image, &allocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate GPU texture image via VMA: " + final_desc.name);
    }

    VkImageViewCreateInfo view_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = final_desc.format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(device, &view_info, nullptr, &view) != VK_SUCCESS) {
        vmaDestroyImage(allocator, image, allocation);
        throw std::runtime_error("Failed to create texture image view: " + final_desc.name);
    }

    VkSamplerCreateInfo sampler_info{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = final_desc.mag_filter,
        .minFilter = final_desc.min_filter,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = final_desc.address_mode,
        .addressModeV = final_desc.address_mode,
        .addressModeW = final_desc.address_mode,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy = 16.0f,
        .compareEnable = VK_FALSE,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    };

    VkSampler sampler = VK_NULL_HANDLE;
    if (vkCreateSampler(device, &sampler_info, nullptr, &sampler) != VK_SUCCESS) {
        vkDestroyImageView(device, view, nullptr);
        vmaDestroyImage(allocator, image, allocation);
        throw std::runtime_error("Failed to create texture sampler: " + final_desc.name);
    }

    Texture tex;
    tex.init(device, allocator, image, view, allocation, sampler, final_desc);
    return tex;
}

void Texture::cleanup() {
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);

        if (m_sampler != VK_NULL_HANDLE) {
            vkDestroySampler(m_device, m_sampler, nullptr);
            m_sampler = VK_NULL_HANDLE;
        }

        if (m_view != VK_NULL_HANDLE) {
            vkDestroyImageView(m_device, m_view, nullptr);
            m_view = VK_NULL_HANDLE;
        }

        if (m_image != VK_NULL_HANDLE && m_allocator != VK_NULL_HANDLE) {
            vmaDestroyImage(m_allocator, m_image, m_allocation);
            m_image = VK_NULL_HANDLE;
            m_allocation = VK_NULL_HANDLE;
        }

        m_device = VK_NULL_HANDLE;
        m_allocator = VK_NULL_HANDLE;
    }
}

} // namespace codotaku
