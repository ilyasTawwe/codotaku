#pragma once

#include <cstdint>
#include <volk.h>
#include <vk_mem_alloc.h>
#include <codotaku/vulkan/device.hpp>

namespace codotaku {

class DescriptorHeap {
public:
    DescriptorHeap() = default;
    ~DescriptorHeap();

    DescriptorHeap(const DescriptorHeap&) = delete;
    DescriptorHeap& operator=(const DescriptorHeap&) = delete;

    DescriptorHeap(DescriptorHeap&& other) noexcept;
    DescriptorHeap& operator=(DescriptorHeap&& other) noexcept;

    void init(
        VulkanDevice& vk,
        VkDeviceSize resource_heap_size = 4 * 1024 * 1024,
        VkDeviceSize sampler_heap_size = 64 * 1024);

    void cleanup();

    // Descriptor Writers (return byte offset in respective heap)
    uint32_t write_sampled_image(const VkImageViewCreateInfo& view_info, VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL);
    uint32_t write_storage_image(const VkImageViewCreateInfo& view_info, VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL);
    uint32_t write_sampler(const VkSamplerCreateInfo& sampler_info);
    uint32_t write_storage_buffer(VkDeviceAddress address, VkDeviceSize size);
    uint32_t write_uniform_buffer(VkDeviceAddress address, VkDeviceSize size);

    // Command Buffer Binding
    void bind(VkCommandBuffer cmd) const;
    void bind_resource_heap(VkCommandBuffer cmd) const;
    void bind_sampler_heap(VkCommandBuffer cmd) const;

    // Push Data Helper
    static void push_data(VkCommandBuffer cmd, uint32_t offset, uint32_t size, const void* data);

    template <typename T>
    static void push_data(VkCommandBuffer cmd, const T& data, uint32_t offset = 0) {
        push_data(cmd, offset, sizeof(T), &data);
    }

    VkDeviceAddress get_resource_heap_address() const { return m_resource_heap_address; }
    VkDeviceAddress get_sampler_heap_address() const { return m_sampler_heap_address; }
    VkDeviceSize get_resource_heap_size() const { return m_resource_heap_size; }
    VkDeviceSize get_sampler_heap_size() const { return m_sampler_heap_size; }

private:
    VulkanDevice* m_vk{nullptr};
    VkDevice m_device{VK_NULL_HANDLE};
    VmaAllocator m_allocator{VK_NULL_HANDLE};
    VolkDeviceTable m_table{};

    // Resource Heap
    VkBuffer m_resource_buffer{VK_NULL_HANDLE};
    VmaAllocation m_resource_allocation{VK_NULL_HANDLE};
    void* m_resource_mapped{nullptr};
    VkDeviceAddress m_resource_heap_address{0};
    VkDeviceSize m_resource_heap_size{0};
    VkDeviceSize m_resource_reserved_size{0};
    uint32_t m_resource_current_offset{0};
    VkDeviceSize m_resource_heap_alignment{32};
    uint32_t m_image_descriptor_size{32};
    uint32_t m_image_descriptor_alignment{32};
    uint32_t m_buffer_descriptor_size{16};
    uint32_t m_buffer_descriptor_alignment{8};

    // Sampler Heap
    VkBuffer m_sampler_buffer{VK_NULL_HANDLE};
    VmaAllocation m_sampler_allocation{VK_NULL_HANDLE};
    void* m_sampler_mapped{nullptr};
    VkDeviceAddress m_sampler_heap_address{0};
    VkDeviceSize m_sampler_heap_size{0};
    VkDeviceSize m_sampler_reserved_size{0};
    uint32_t m_sampler_current_offset{0};
    VkDeviceSize m_sampler_heap_alignment{32};
    uint32_t m_sampler_descriptor_size{32};
    uint32_t m_sampler_descriptor_alignment{32};
};

} // namespace codotaku
