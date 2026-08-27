#include <algorithm>
#include <cstring>
#include <print>
#include <stdexcept>
#include <utility>

#include <codotaku/system/log.hpp>
#include <codotaku/vulkan/descriptor_heap.hpp>
#include <codotaku/vulkan/device.hpp>

namespace codotaku {

static inline VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment) {
    if (alignment == 0) return value;
    return (value + alignment - 1) & ~(alignment - 1);
}

DescriptorHeap::~DescriptorHeap() {
    cleanup();
}

DescriptorHeap::DescriptorHeap(DescriptorHeap&& other) noexcept
    : m_vk(other.m_vk),
      m_device(std::exchange(other.m_device, VK_NULL_HANDLE)),
      m_allocator(std::exchange(other.m_allocator, VK_NULL_HANDLE)),
      m_table(other.m_table),
      m_resource_buffer(std::exchange(other.m_resource_buffer, VK_NULL_HANDLE)),
      m_resource_allocation(std::exchange(other.m_resource_allocation, VK_NULL_HANDLE)),
      m_resource_mapped(std::exchange(other.m_resource_mapped, nullptr)),
      m_resource_heap_address(std::exchange(other.m_resource_heap_address, 0)),
      m_resource_heap_size(std::exchange(other.m_resource_heap_size, 0)),
      m_resource_reserved_size(std::exchange(other.m_resource_reserved_size, 0)),
      m_resource_current_offset(std::exchange(other.m_resource_current_offset, 0)),
      m_resource_heap_alignment(std::exchange(other.m_resource_heap_alignment, 32)),
      m_image_descriptor_size(std::exchange(other.m_image_descriptor_size, 32)),
      m_image_descriptor_alignment(std::exchange(other.m_image_descriptor_alignment, 32)),
      m_buffer_descriptor_size(std::exchange(other.m_buffer_descriptor_size, 16)),
      m_buffer_descriptor_alignment(std::exchange(other.m_buffer_descriptor_alignment, 8)),
      m_sampler_buffer(std::exchange(other.m_sampler_buffer, VK_NULL_HANDLE)),
      m_sampler_allocation(std::exchange(other.m_sampler_allocation, VK_NULL_HANDLE)),
      m_sampler_mapped(std::exchange(other.m_sampler_mapped, nullptr)),
      m_sampler_heap_address(std::exchange(other.m_sampler_heap_address, 0)),
      m_sampler_heap_size(std::exchange(other.m_sampler_heap_size, 0)),
      m_sampler_reserved_size(std::exchange(other.m_sampler_reserved_size, 0)),
      m_sampler_current_offset(std::exchange(other.m_sampler_current_offset, 0)),
      m_sampler_heap_alignment(std::exchange(other.m_sampler_heap_alignment, 32)),
      m_sampler_descriptor_size(std::exchange(other.m_sampler_descriptor_size, 32)),
      m_sampler_descriptor_alignment(std::exchange(other.m_sampler_descriptor_alignment, 32)) {}

DescriptorHeap& DescriptorHeap::operator=(DescriptorHeap&& other) noexcept {
    if (this != &other) {
        cleanup();
        m_vk = other.m_vk;
        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_allocator = std::exchange(other.m_allocator, VK_NULL_HANDLE);
        m_table = other.m_table;
        m_resource_buffer = std::exchange(other.m_resource_buffer, VK_NULL_HANDLE);
        m_resource_allocation = std::exchange(other.m_resource_allocation, VK_NULL_HANDLE);
        m_resource_mapped = std::exchange(other.m_resource_mapped, nullptr);
        m_resource_heap_address = std::exchange(other.m_resource_heap_address, 0);
        m_resource_heap_size = std::exchange(other.m_resource_heap_size, 0);
        m_resource_reserved_size = std::exchange(other.m_resource_reserved_size, 0);
        m_resource_current_offset = std::exchange(other.m_resource_current_offset, 0);
        m_resource_heap_alignment = std::exchange(other.m_resource_heap_alignment, 32);
        m_image_descriptor_size = std::exchange(other.m_image_descriptor_size, 32);
        m_image_descriptor_alignment = std::exchange(other.m_image_descriptor_alignment, 32);
        m_buffer_descriptor_size = std::exchange(other.m_buffer_descriptor_size, 16);
        m_buffer_descriptor_alignment = std::exchange(other.m_buffer_descriptor_alignment, 8);
        m_sampler_buffer = std::exchange(other.m_sampler_buffer, VK_NULL_HANDLE);
        m_sampler_allocation = std::exchange(other.m_sampler_allocation, VK_NULL_HANDLE);
        m_sampler_mapped = std::exchange(other.m_sampler_mapped, nullptr);
        m_sampler_heap_address = std::exchange(other.m_sampler_heap_address, 0);
        m_sampler_heap_size = std::exchange(other.m_sampler_heap_size, 0);
        m_sampler_reserved_size = std::exchange(other.m_sampler_reserved_size, 0);
        m_sampler_current_offset = std::exchange(other.m_sampler_current_offset, 0);
        m_sampler_heap_alignment = std::exchange(other.m_sampler_heap_alignment, 32);
        m_sampler_descriptor_size = std::exchange(other.m_sampler_descriptor_size, 32);
        m_sampler_descriptor_alignment = std::exchange(other.m_sampler_descriptor_alignment, 32);
    }
    return *this;
}

void DescriptorHeap::init(
    VulkanDevice& vk,
    VkDeviceSize resource_heap_size,
    VkDeviceSize sampler_heap_size) {
    cleanup();
    m_vk = &vk;
    m_device = vk.get_device();
    m_allocator = vk.get_allocator();
    m_table = vk.get_table();

    const auto& props = vk.get_descriptor_heap_properties();
    m_resource_heap_alignment = props.resourceHeapAlignment ? props.resourceHeapAlignment : 32;
    m_sampler_heap_alignment = props.samplerHeapAlignment ? props.samplerHeapAlignment : 32;
    m_image_descriptor_size = props.imageDescriptorSize ? props.imageDescriptorSize : 32;
    m_image_descriptor_alignment = props.imageDescriptorAlignment ? props.imageDescriptorAlignment : 32;
    m_buffer_descriptor_size = props.bufferDescriptorSize ? props.bufferDescriptorSize : 16;
    m_buffer_descriptor_alignment = props.bufferDescriptorAlignment ? props.bufferDescriptorAlignment : 8;
    m_sampler_descriptor_size = props.samplerDescriptorSize ? props.samplerDescriptorSize : 32;
    m_sampler_descriptor_alignment = props.samplerDescriptorAlignment ? props.samplerDescriptorAlignment : 32;

    m_resource_reserved_size = props.minResourceHeapReservedRange;
    m_sampler_reserved_size = props.minSamplerHeapReservedRange;

    m_resource_heap_size = align_up(std::max(resource_heap_size, m_resource_reserved_size + 64 * 1024), m_resource_heap_alignment);
    m_sampler_heap_size = align_up(std::max(sampler_heap_size, m_sampler_reserved_size + 16 * 1024), m_sampler_heap_alignment);

    // 1. Allocate Resource Heap Buffer
    VkBufferCreateInfo res_buf_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = m_resource_heap_size,
        .usage = VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo res_alloc_info{
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };

    VmaAllocationInfo res_alloc_result{};
    if (vmaCreateBuffer(m_allocator, &res_buf_info, &res_alloc_info, &m_resource_buffer, &m_resource_allocation, &res_alloc_result) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Resource Descriptor Heap buffer");
    }
    m_resource_mapped = res_alloc_result.pMappedData;
    m_resource_heap_address = vk.get_buffer_device_address(m_resource_buffer);
    m_resource_current_offset = static_cast<uint32_t>(align_up(m_resource_reserved_size, m_image_descriptor_alignment));
    m_vk->set_name(m_resource_buffer, "Global Resource Descriptor Heap");

    // 2. Allocate Sampler Heap Buffer
    VkBufferCreateInfo samp_buf_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = m_sampler_heap_size,
        .usage = VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo samp_alloc_info{
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };

    VmaAllocationInfo samp_alloc_result{};
    if (vmaCreateBuffer(m_allocator, &samp_buf_info, &samp_alloc_info, &m_sampler_buffer, &m_sampler_allocation, &samp_alloc_result) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Sampler Descriptor Heap buffer");
    }
    m_sampler_mapped = samp_alloc_result.pMappedData;
    m_sampler_heap_address = vk.get_buffer_device_address(m_sampler_buffer);
    m_sampler_current_offset = static_cast<uint32_t>(align_up(m_sampler_reserved_size, m_sampler_descriptor_alignment));
    m_vk->set_name(m_sampler_buffer, "Global Sampler Descriptor Heap");

    log_info("[DescriptorHeap] Initialized: ResourceHeap {} B (Addr: 0x{:x}, Resvd: {} B), SamplerHeap {} B (Addr: 0x{:x}, Resvd: {} B)",
        m_resource_heap_size, m_resource_heap_address, m_resource_reserved_size,
        m_sampler_heap_size, m_sampler_heap_address, m_sampler_reserved_size);
}

void DescriptorHeap::cleanup() {
    if (m_allocator != VK_NULL_HANDLE) {
        if (m_resource_buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_allocator, m_resource_buffer, m_resource_allocation);
            m_resource_buffer = VK_NULL_HANDLE;
            m_resource_allocation = VK_NULL_HANDLE;
            m_resource_mapped = nullptr;
        }
        if (m_sampler_buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_allocator, m_sampler_buffer, m_sampler_allocation);
            m_sampler_buffer = VK_NULL_HANDLE;
            m_sampler_allocation = VK_NULL_HANDLE;
            m_sampler_mapped = nullptr;
        }
    }
    m_device = VK_NULL_HANDLE;
    m_allocator = VK_NULL_HANDLE;
    m_vk = nullptr;
}

uint32_t DescriptorHeap::write_sampled_image(const VkImageViewCreateInfo& view_info, VkImageLayout layout) {
    if (!m_resource_mapped) {
        throw std::runtime_error("DescriptorHeap is not initialized");
    }

    uint32_t aligned_offset = static_cast<uint32_t>(align_up(m_resource_current_offset, m_image_descriptor_alignment));
    if (aligned_offset + m_image_descriptor_size > m_resource_heap_size) {
        throw std::runtime_error("Resource Descriptor Heap out of memory");
    }

    VkImageDescriptorInfoEXT img_desc{
        .sType = VK_STRUCTURE_TYPE_IMAGE_DESCRIPTOR_INFO_EXT,
        .pNext = nullptr,
        .pView = &view_info,
        .layout = layout,
    };

    VkResourceDescriptorInfoEXT res_info{
        .sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT,
        .pNext = nullptr,
        .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .data = { .pImage = &img_desc },
    };

    VkHostAddressRangeEXT host_range{
        .address = static_cast<uint8_t*>(m_resource_mapped) + aligned_offset,
        .size = m_image_descriptor_size,
    };

    VkResult res = m_table.vkWriteResourceDescriptorsEXT(m_device, 1, &res_info, &host_range);
    if (res != VK_SUCCESS) {
        throw std::runtime_error("Failed to write Sampled Image descriptor");
    }

    m_resource_current_offset = aligned_offset + m_image_descriptor_size;
    return aligned_offset;
}

uint32_t DescriptorHeap::write_storage_image(const VkImageViewCreateInfo& view_info, VkImageLayout layout) {
    if (!m_resource_mapped) {
        throw std::runtime_error("DescriptorHeap is not initialized");
    }

    uint32_t aligned_offset = static_cast<uint32_t>(align_up(m_resource_current_offset, m_image_descriptor_alignment));
    if (aligned_offset + m_image_descriptor_size > m_resource_heap_size) {
        throw std::runtime_error("Resource Descriptor Heap out of memory");
    }

    VkImageDescriptorInfoEXT img_desc{
        .sType = VK_STRUCTURE_TYPE_IMAGE_DESCRIPTOR_INFO_EXT,
        .pNext = nullptr,
        .pView = &view_info,
        .layout = layout,
    };

    VkResourceDescriptorInfoEXT res_info{
        .sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT,
        .pNext = nullptr,
        .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .data = { .pImage = &img_desc },
    };

    VkHostAddressRangeEXT host_range{
        .address = static_cast<uint8_t*>(m_resource_mapped) + aligned_offset,
        .size = m_image_descriptor_size,
    };

    VkResult res = m_table.vkWriteResourceDescriptorsEXT(m_device, 1, &res_info, &host_range);
    if (res != VK_SUCCESS) {
        throw std::runtime_error("Failed to write Storage Image descriptor");
    }

    m_resource_current_offset = aligned_offset + m_image_descriptor_size;
    return aligned_offset;
}

uint32_t DescriptorHeap::write_sampler(const VkSamplerCreateInfo& sampler_info) {
    if (!m_sampler_mapped) {
        throw std::runtime_error("Sampler DescriptorHeap is not initialized");
    }

    uint32_t aligned_offset = static_cast<uint32_t>(align_up(m_sampler_current_offset, m_sampler_descriptor_alignment));
    if (aligned_offset + m_sampler_descriptor_size > m_sampler_heap_size) {
        throw std::runtime_error("Sampler Descriptor Heap out of memory");
    }

    VkHostAddressRangeEXT host_range{
        .address = static_cast<uint8_t*>(m_sampler_mapped) + aligned_offset,
        .size = m_sampler_descriptor_size,
    };

    VkResult res = m_table.vkWriteSamplerDescriptorsEXT(m_device, 1, &sampler_info, &host_range);
    if (res != VK_SUCCESS) {
        throw std::runtime_error("Failed to write Sampler descriptor");
    }

    m_sampler_current_offset = aligned_offset + m_sampler_descriptor_size;
    return aligned_offset;
}

uint32_t DescriptorHeap::write_storage_buffer(VkDeviceAddress address, VkDeviceSize size) {
    if (!m_resource_mapped) {
        throw std::runtime_error("DescriptorHeap is not initialized");
    }

    uint32_t aligned_offset = static_cast<uint32_t>(align_up(m_resource_current_offset, m_buffer_descriptor_alignment));
    if (aligned_offset + m_buffer_descriptor_size > m_resource_heap_size) {
        throw std::runtime_error("Resource Descriptor Heap out of memory");
    }

    VkDeviceAddressRangeEXT addr_range{
        .address = address,
        .size = size,
    };

    VkResourceDescriptorInfoEXT res_info{
        .sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT,
        .pNext = nullptr,
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .data = { .pAddressRange = &addr_range },
    };

    VkHostAddressRangeEXT host_range{
        .address = static_cast<uint8_t*>(m_resource_mapped) + aligned_offset,
        .size = m_buffer_descriptor_size,
    };

    VkResult res = m_table.vkWriteResourceDescriptorsEXT(m_device, 1, &res_info, &host_range);
    if (res != VK_SUCCESS) {
        throw std::runtime_error("Failed to write Storage Buffer descriptor");
    }

    m_resource_current_offset = aligned_offset + m_buffer_descriptor_size;
    return aligned_offset;
}

uint32_t DescriptorHeap::write_uniform_buffer(VkDeviceAddress address, VkDeviceSize size) {
    if (!m_resource_mapped) {
        throw std::runtime_error("DescriptorHeap is not initialized");
    }

    uint32_t aligned_offset = static_cast<uint32_t>(align_up(m_resource_current_offset, m_buffer_descriptor_alignment));
    if (aligned_offset + m_buffer_descriptor_size > m_resource_heap_size) {
        throw std::runtime_error("Resource Descriptor Heap out of memory");
    }

    VkDeviceAddressRangeEXT addr_range{
        .address = address,
        .size = size,
    };

    VkResourceDescriptorInfoEXT res_info{
        .sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT,
        .pNext = nullptr,
        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .data = { .pAddressRange = &addr_range },
    };

    VkHostAddressRangeEXT host_range{
        .address = static_cast<uint8_t*>(m_resource_mapped) + aligned_offset,
        .size = m_buffer_descriptor_size,
    };

    VkResult res = m_table.vkWriteResourceDescriptorsEXT(m_device, 1, &res_info, &host_range);
    if (res != VK_SUCCESS) {
        throw std::runtime_error("Failed to write Uniform Buffer descriptor");
    }

    m_resource_current_offset = aligned_offset + m_buffer_descriptor_size;
    return aligned_offset;
}

void DescriptorHeap::bind_resource_heap(VkCommandBuffer cmd) const {
    VkBindHeapInfoEXT bind_info{
        .sType = VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT,
        .pNext = nullptr,
        .heapRange = {
            .address = m_resource_heap_address,
            .size = m_resource_heap_size,
        },
        .reservedRangeOffset = 0,
        .reservedRangeSize = m_resource_reserved_size,
    };
    m_table.vkCmdBindResourceHeapEXT(cmd, &bind_info);
}

void DescriptorHeap::bind_sampler_heap(VkCommandBuffer cmd) const {
    VkBindHeapInfoEXT bind_info{
        .sType = VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT,
        .pNext = nullptr,
        .heapRange = {
            .address = m_sampler_heap_address,
            .size = m_sampler_heap_size,
        },
        .reservedRangeOffset = 0,
        .reservedRangeSize = m_sampler_reserved_size,
    };
    m_table.vkCmdBindSamplerHeapEXT(cmd, &bind_info);
}

void DescriptorHeap::bind(VkCommandBuffer cmd) const {
    bind_resource_heap(cmd);
    bind_sampler_heap(cmd);
}

void DescriptorHeap::push_data(VkCommandBuffer cmd, uint32_t offset, uint32_t size, const void* data) {
    VkPushDataInfoEXT push_info{
        .sType = VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT,
        .pNext = nullptr,
        .offset = offset,
        .data = {
            .address = data,
            .size = size,
        },
    };
    vkCmdPushDataEXT(cmd, &push_info);
}

} // namespace codotaku
