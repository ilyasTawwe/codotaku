#pragma once

#include <cstdint>
#include <string>
#include <volk.h>
#include <vk_mem_alloc.h>

namespace codotaku {

class VulkanContext;

struct TextureDesc {
    std::string name{"texture"};
    uint32_t width{1};
    uint32_t height{1};
    VkFormat format{VK_FORMAT_R8G8B8A8_UNORM};
    VkFilter min_filter{VK_FILTER_LINEAR};
    VkFilter mag_filter{VK_FILTER_LINEAR};
    VkSamplerAddressMode address_mode{VK_SAMPLER_ADDRESS_MODE_REPEAT};
};

class Texture {
public:
    Texture() = default;
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;

    void init(
        VkDevice device,
        VmaAllocator allocator,
        VkImage image,
        VkImageView view,
        VmaAllocation allocation,
        VkSampler sampler,
        const TextureDesc& desc);

    static Texture create_uninitialized(
        VulkanContext& vk,
        uint32_t width,
        uint32_t height,
        const TextureDesc& desc = {});

    void cleanup();

    VkImage get_image() const { return m_image; }
    VkImageView get_view() const { return m_view; }
    VkSampler get_sampler() const { return m_sampler; }
    VkFormat get_format() const { return m_desc.format; }
    uint32_t get_width() const { return m_desc.width; }
    uint32_t get_height() const { return m_desc.height; }
    const TextureDesc& get_desc() const { return m_desc; }

    VkDescriptorImageInfo get_descriptor_image_info() const {
        return VkDescriptorImageInfo{
            .sampler = m_sampler,
            .imageView = m_view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
    }

private:
    VkDevice m_device{VK_NULL_HANDLE};
    VmaAllocator m_allocator{VK_NULL_HANDLE};
    TextureDesc m_desc{};

    VkImage m_image{VK_NULL_HANDLE};
    VkImageView m_view{VK_NULL_HANDLE};
    VmaAllocation m_allocation{VK_NULL_HANDLE};
    VkSampler m_sampler{VK_NULL_HANDLE};
};

} // namespace codotaku
