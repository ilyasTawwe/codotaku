#pragma once

#include <cstdint>
#include <string>
#include <volk.h>
#include <vk_mem_alloc.h>

namespace codotaku {

class VulkanDevice;
class DescriptorHeap;

struct TextureDesc {
    std::string name{"texture"};
    uint32_t width{1};
    uint32_t height{1};
    VkFormat format{VK_FORMAT_R8G8B8A8_UNORM};
    VkImageUsageFlags usage{VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT};
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
        const VolkDeviceTable& table,
        VkImage image,
        VkImageView view,
        VmaAllocation allocation,
        VkSampler sampler,
        const TextureDesc& desc);

    static Texture create_uninitialized(
        VulkanDevice& vk,
        uint32_t width,
        uint32_t height,
        const TextureDesc& desc = {});

    void write_to_descriptor_heap(DescriptorHeap& heap);

    void cleanup();

    VkImage get_image() const { return m_image; }
    VkImageView get_view() const { return m_view; }
    VkSampler get_sampler() const { return m_sampler; }
    VkFormat get_format() const { return m_desc.format; }
    uint32_t get_width() const { return m_desc.width; }
    uint32_t get_height() const { return m_desc.height; }
    const TextureDesc& get_desc() const { return m_desc; }

    uint32_t get_sampled_heap_offset() const { return m_sampled_heap_offset; }
    uint32_t get_storage_heap_offset() const { return m_storage_heap_offset; }
    uint32_t get_sampler_heap_offset() const { return m_sampler_heap_offset; }

private:
    VkDevice m_device{VK_NULL_HANDLE};
    VmaAllocator m_allocator{VK_NULL_HANDLE};
    VolkDeviceTable m_table{};
    TextureDesc m_desc{};

    VkImage m_image{VK_NULL_HANDLE};
    VkImageView m_view{VK_NULL_HANDLE};
    VmaAllocation m_allocation{VK_NULL_HANDLE};
    VkSampler m_sampler{VK_NULL_HANDLE};

    uint32_t m_sampled_heap_offset{0};
    uint32_t m_storage_heap_offset{0};
    uint32_t m_sampler_heap_offset{0};
};

} // namespace codotaku
