#include <stdexcept>
#include <utility>
#include <codotaku/vulkan/arena.hpp>

namespace codotaku {

GpuBufferArena::GpuBufferArena(GpuBufferArena&& other) noexcept
    : m_buffer(std::exchange(other.m_buffer, VK_NULL_HANDLE)),
      m_allocation(std::exchange(other.m_allocation, VK_NULL_HANDLE)),
      m_virtual_block(std::exchange(other.m_virtual_block, VK_NULL_HANDLE)),
      m_base_address(std::exchange(other.m_base_address, 0)),
      m_mapped_data(std::exchange(other.m_mapped_data, nullptr)),
      m_total_size(std::exchange(other.m_total_size, 0)) {}

GpuBufferArena& GpuBufferArena::operator=(GpuBufferArena&& other) noexcept {
    if (this != &other) {
        m_buffer = std::exchange(other.m_buffer, VK_NULL_HANDLE);
        m_allocation = std::exchange(other.m_allocation, VK_NULL_HANDLE);
        m_virtual_block = std::exchange(other.m_virtual_block, VK_NULL_HANDLE);
        m_base_address = std::exchange(other.m_base_address, 0);
        m_mapped_data = std::exchange(other.m_mapped_data, nullptr);
        m_total_size = std::exchange(other.m_total_size, 0);
    }
    return *this;
}

void GpuBufferArena::init(VmaAllocator allocator, VkDevice device, VkDeviceSize size, VkBufferUsageFlags usage, VmaAllocationCreateFlags vma_flags) {
    m_total_size = size;

    VkBufferCreateInfo buffer_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo alloc_info{
        .flags = vma_flags,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };

    VmaAllocationInfo allocation_info{};
    if (vmaCreateBuffer(allocator, &buffer_info, &alloc_info, &m_buffer, &m_allocation, &allocation_info) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate GPU Buffer Arena via VMA");
    }

    m_mapped_data = allocation_info.pMappedData;

    VkBufferDeviceAddressInfo address_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = m_buffer,
    };
    m_base_address = vkGetBufferDeviceAddress(device, &address_info);

    VmaVirtualBlockCreateInfo block_info{
        .size = size,
    };
    if (vmaCreateVirtualBlock(&block_info, &m_virtual_block) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VMA Virtual Block for GPU Buffer Arena");
    }
}

GpuVirtualSuballocation GpuBufferArena::suballocate(VkDeviceSize req_size, VkDeviceSize alignment) {
    VmaVirtualAllocationCreateInfo alloc_info{
        .size = req_size,
        .alignment = alignment,
    };
    GpuVirtualSuballocation sub{};
    sub.size = req_size;
    if (vmaVirtualAllocate(m_virtual_block, &alloc_info, &sub.handle, &sub.offset) != VK_SUCCESS) {
        throw std::runtime_error("Failed to suballocate memory chunk from GPU Buffer Arena via VmaVirtualBlock");
    }
    sub.device_address = m_base_address + sub.offset;
    return sub;
}

void GpuBufferArena::free_suballocation(GpuVirtualSuballocation& sub) {
    if (sub.handle != VK_NULL_HANDLE) {
        vmaVirtualFree(m_virtual_block, sub.handle);
        sub.handle = VK_NULL_HANDLE;
    }
}

void GpuBufferArena::reset() {
    if (m_virtual_block != VK_NULL_HANDLE) {
        vmaClearVirtualBlock(m_virtual_block);
    }
}

void GpuBufferArena::cleanup(VmaAllocator allocator) {
    if (m_virtual_block != VK_NULL_HANDLE) {
        vmaClearVirtualBlock(m_virtual_block);
        vmaDestroyVirtualBlock(m_virtual_block);
        m_virtual_block = VK_NULL_HANDLE;
    }
    if (m_buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, m_buffer, m_allocation);
        m_buffer = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
    }
    m_mapped_data = nullptr;
    m_base_address = 0;
}

} // namespace codotaku
