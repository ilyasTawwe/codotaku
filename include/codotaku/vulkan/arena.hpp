#pragma once

#include <cstdint>
#include <volk.h>
#include <vk_mem_alloc.h>

namespace codotaku {

class VulkanDevice;

struct GpuVirtualSuballocation {
    VmaVirtualAllocation handle{VK_NULL_HANDLE};
    VkDeviceSize offset{0};
    VkDeviceSize size{0};
    VkDeviceAddress device_address{0};
};

class GpuBufferArena {
public:
    GpuBufferArena() = default;
    ~GpuBufferArena() = default;

    GpuBufferArena(const GpuBufferArena&) = delete;
    GpuBufferArena& operator=(const GpuBufferArena&) = delete;

    GpuBufferArena(GpuBufferArena&& other) noexcept;
    GpuBufferArena& operator=(GpuBufferArena&& other) noexcept;

    void init(
        VulkanDevice& vk,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VmaAllocationCreateFlags vma_flags,
        const char* debug_name = "GPU Buffer Arena");

    void init(
        VmaAllocator allocator,
        VkDevice device,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VmaAllocationCreateFlags vma_flags);

    GpuVirtualSuballocation suballocate(VkDeviceSize req_size, VkDeviceSize alignment = 16);
    void free_suballocation(GpuVirtualSuballocation& sub);
    void reset();
    void cleanup(VmaAllocator allocator);

    VkBuffer get_buffer() const { return m_buffer; }
    VkDeviceAddress get_base_address() const { return m_base_address; }
    void* get_mapped_data() const { return m_mapped_data; }
    VkDeviceSize get_total_size() const { return m_total_size; }

private:
    VkBuffer m_buffer{VK_NULL_HANDLE};
    VmaAllocation m_allocation{VK_NULL_HANDLE};
    VmaVirtualBlock m_virtual_block{VK_NULL_HANDLE};
    VkDeviceAddress m_base_address{0};
    void* m_mapped_data{nullptr};
    VkDeviceSize m_total_size{0};
};

} // namespace codotaku
