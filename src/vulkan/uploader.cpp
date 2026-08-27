#include <algorithm>
#include <cstring>
#include <print>
#include <stdexcept>
#include <utility>

#include <codotaku/system/log.hpp>
#include <codotaku/vulkan/device.hpp>
#include <codotaku/vulkan/uploader.hpp>

namespace codotaku {

namespace {

VkDeviceSize align_up(VkDeviceSize offset, VkDeviceSize alignment) {
    if (alignment == 0) return offset;
    return (offset + alignment - 1) & ~(alignment - 1);
}

} // namespace

Uploader::Uploader(VulkanDevice& vk)
    : m_vk(&vk),
      m_device(vk.get_device()),
      m_queue(vk.get_transfer_queue().handle),
      m_allocator(vk.get_allocator()),
      m_limits(vk.get_alignment_limits()),
      m_table(vk.get_table()) {
    VkCommandPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = vk.get_transfer_queue().family_index,
    };

    if (m_table.vkCreateCommandPool(m_device, &pool_info, nullptr, &m_command_pool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Uploader command pool");
    }

    VkCommandBufferAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = m_command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    if (m_table.vkAllocateCommandBuffers(m_device, &alloc_info, &m_cmd) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate Uploader command buffer");
    }

    VkFenceCreateInfo fence_info{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    if (m_table.vkCreateFence(m_device, &fence_info, nullptr, &m_fence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Uploader fence");
    }

    m_vk->set_name(m_fence, "Uploader Batch Transfer Fence");
    m_vk->set_name(m_cmd, "Uploader Batch Command Buffer");
}

Uploader::~Uploader() {
    wait();
    free_staging_resources();

    if (m_device != VK_NULL_HANDLE) {
        if (m_fence != VK_NULL_HANDLE) {
            m_table.vkDestroyFence(m_device, m_fence, nullptr);
            m_fence = VK_NULL_HANDLE;
        }
        if (m_command_pool != VK_NULL_HANDLE) {
            m_table.vkDestroyCommandPool(m_device, m_command_pool, nullptr);
            m_command_pool = VK_NULL_HANDLE;
        }
    }
}

Uploader::Uploader(Uploader&& other) noexcept
    : m_vk(other.m_vk),
      m_device(std::exchange(other.m_device, VK_NULL_HANDLE)),
      m_queue(std::exchange(other.m_queue, VK_NULL_HANDLE)),
      m_allocator(std::exchange(other.m_allocator, VK_NULL_HANDLE)),
      m_limits(other.m_limits),
      m_table(other.m_table),
      m_command_pool(std::exchange(other.m_command_pool, VK_NULL_HANDLE)),
      m_cmd(std::exchange(other.m_cmd, VK_NULL_HANDLE)),
      m_fence(std::exchange(other.m_fence, VK_NULL_HANDLE)),
      m_staging_buffer(std::exchange(other.m_staging_buffer, VK_NULL_HANDLE)),
      m_staging_allocation(std::exchange(other.m_staging_allocation, VK_NULL_HANDLE)),
      m_staging_mapped_ptr(std::exchange(other.m_staging_mapped_ptr, nullptr)),
      m_staging_capacity(std::exchange(other.m_staging_capacity, 0)),
      m_buffer_tasks(std::move(other.m_buffer_tasks)),
      m_image_tasks(std::move(other.m_image_tasks)),
      m_in_flight(std::exchange(other.m_in_flight, false)) {}

Uploader& Uploader::operator=(Uploader&& other) noexcept {
    if (this != &other) {
        wait();
        free_staging_resources();

        m_vk = other.m_vk;
        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_queue = std::exchange(other.m_queue, VK_NULL_HANDLE);
        m_allocator = std::exchange(other.m_allocator, VK_NULL_HANDLE);
        m_limits = other.m_limits;
        m_table = other.m_table;
        m_command_pool = std::exchange(other.m_command_pool, VK_NULL_HANDLE);
        m_cmd = std::exchange(other.m_cmd, VK_NULL_HANDLE);
        m_fence = std::exchange(other.m_fence, VK_NULL_HANDLE);
        m_staging_buffer = std::exchange(other.m_staging_buffer, VK_NULL_HANDLE);
        m_staging_allocation = std::exchange(other.m_staging_allocation, VK_NULL_HANDLE);
        m_staging_mapped_ptr = std::exchange(other.m_staging_mapped_ptr, nullptr);
        m_staging_capacity = std::exchange(other.m_staging_capacity, 0);
        m_buffer_tasks = std::move(other.m_buffer_tasks);
        m_image_tasks = std::move(other.m_image_tasks);
        m_in_flight = std::exchange(other.m_in_flight, false);
    }
    return *this;
}

void Uploader::free_staging_resources() {
    if (m_staging_buffer != VK_NULL_HANDLE && m_allocator != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, m_staging_buffer, m_staging_allocation);
        m_staging_buffer = VK_NULL_HANDLE;
        m_staging_allocation = VK_NULL_HANDLE;
        m_staging_mapped_ptr = nullptr;
        m_staging_capacity = 0;
    }
}

BufferAllocation Uploader::upload_buffer(
    const void* data,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkDeviceSize alignment,
    const char* debug_name) {
    if (size == 0) {
        return {};
    }

    VkBufferCreateInfo buffer_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = size,
        .usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo alloc_info{
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };

    BufferAllocation result{};
    result.size = size;

    if (vmaCreateBuffer(m_allocator, &buffer_info, &alloc_info, &result.buffer, &result.allocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate destination GPU buffer in Uploader");
    }

    VkBufferDeviceAddressInfo bda_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .pNext = nullptr,
        .buffer = result.buffer,
    };
    result.device_address = m_table.vkGetBufferDeviceAddress(m_device, &bda_info);

    if (debug_name && m_vk) {
        m_vk->set_name(result.buffer, debug_name);
    }

    m_buffer_tasks.push_back(BufferUploadTask{
        .dst_buffer = result.buffer,
        .dst_offset = 0,
        .data = data,
        .size = size,
        .staging_offset = 0,
    });

    return result;
}

GpuVirtualSuballocation Uploader::upload_to_arena(
    GpuBufferArena& arena,
    const void* data,
    VkDeviceSize size,
    VkDeviceSize alignment) {
    auto suballoc = arena.suballocate(size, alignment);

    m_buffer_tasks.push_back(BufferUploadTask{
        .dst_buffer = arena.get_buffer(),
        .dst_offset = suballoc.offset,
        .data = data,
        .size = size,
        .staging_offset = 0,
    });

    return suballoc;
}

Texture Uploader::upload_texture(
    uint32_t width,
    uint32_t height,
    VkFormat format,
    std::span<const uint8_t> pixel_data,
    const TextureDesc& desc) {
    if (!m_vk) throw std::runtime_error("Uploader has invalid device pointer");

    Texture tex = Texture::create_uninitialized(*m_vk, width, height, desc);

    m_image_tasks.push_back(ImageUploadTask{
        .dst_image = tex.get_image(),
        .extent = { width, height, 1 },
        .format = format,
        .data = pixel_data.data(),
        .size = pixel_data.size_bytes(),
        .target_layout = VK_IMAGE_LAYOUT_GENERAL,
        .staging_offset = 0,
    });

    return tex;
}

void Uploader::upload() {
    if (m_buffer_tasks.empty() && m_image_tasks.empty()) {
        return;
    }

    wait();

    VkDeviceSize current_staging_offset = 0;
    VkDeviceSize copy_alignment = m_limits.optimal_buffer_copy_offset_alignment;

    for (auto& task : m_buffer_tasks) {
        current_staging_offset = align_up(current_staging_offset, copy_alignment);
        task.staging_offset = current_staging_offset;
        current_staging_offset += task.size;
    }

    for (auto& task : m_image_tasks) {
        current_staging_offset = align_up(current_staging_offset, copy_alignment);
        task.staging_offset = current_staging_offset;
        current_staging_offset += task.size;
    }

    VkDeviceSize required_staging_size = current_staging_offset;

    if (m_staging_capacity < required_staging_size) {
        free_staging_resources();

        m_staging_capacity = std::max(required_staging_size, m_staging_capacity * 2);

        VkBufferCreateInfo staging_info{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .size = m_staging_capacity,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };

        VmaAllocationCreateInfo staging_alloc_info{
            .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO,
        };

        VmaAllocationInfo alloc_res{};
        if (vmaCreateBuffer(m_allocator, &staging_info, &staging_alloc_info, &m_staging_buffer, &m_staging_allocation, &alloc_res) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate dynamic staging buffer in Uploader");
        }

        m_staging_mapped_ptr = alloc_res.pMappedData;
        if (m_vk) m_vk->set_name(m_staging_buffer, "Uploader Staging Buffer");
        log_debug("[Codotaku Uploader] Lazily allocated staging buffer (Capacity: {} KB)", m_staging_capacity / 1024);
    }

    // Copy CPU memory into host-mapped staging buffer
    for (const auto& task : m_buffer_tasks) {
        if (task.data && task.size > 0) {
            std::memcpy(static_cast<uint8_t*>(m_staging_mapped_ptr) + task.staging_offset, task.data, task.size);
        }
    }

    for (const auto& task : m_image_tasks) {
        if (task.data && task.size > 0) {
            std::memcpy(static_cast<uint8_t*>(m_staging_mapped_ptr) + task.staging_offset, task.data, task.size);
        }
    }

    vmaFlushAllocation(m_allocator, m_staging_allocation, 0, required_staging_size);

    m_table.vkResetFences(m_device, 1, &m_fence);

    VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    };
    m_table.vkBeginCommandBuffer(m_cmd, &begin_info);

    if (m_vk) m_vk->begin_debug_label(m_cmd, "Batch GPU Upload", {0.2f, 0.8f, 0.3f, 1.0f});

    // 1. Record Buffer Transfers
    for (const auto& task : m_buffer_tasks) {
        VkBufferCopy copy_region{
            .srcOffset = task.staging_offset,
            .dstOffset = task.dst_offset,
            .size = task.size,
        };
        m_table.vkCmdCopyBuffer(m_cmd, m_staging_buffer, task.dst_buffer, 1, &copy_region);
    }

    // 2. Record Image Transfers
    for (const auto& task : m_image_tasks) {
        VkImageSubresourceRange subresource_range{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };

        VkImageMemoryBarrier2 pre_copy_barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = task.dst_image,
            .subresourceRange = subresource_range,
        };

        VkDependencyInfo pre_dep{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = nullptr,
            .dependencyFlags = 0,
            .memoryBarrierCount = 0,
            .pMemoryBarriers = nullptr,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers = nullptr,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &pre_copy_barrier,
        };
        m_table.vkCmdPipelineBarrier2(m_cmd, &pre_dep);

        VkBufferImageCopy copy_region{
            .bufferOffset = task.staging_offset,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .imageOffset = { 0, 0, 0 },
            .imageExtent = task.extent,
        };

        m_table.vkCmdCopyBufferToImage(m_cmd, m_staging_buffer, task.dst_image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy_region);

        VkImageMemoryBarrier2 post_copy_barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = task.target_layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = task.dst_image,
            .subresourceRange = subresource_range,
        };

        VkDependencyInfo post_dep{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = nullptr,
            .dependencyFlags = 0,
            .memoryBarrierCount = 0,
            .pMemoryBarriers = nullptr,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers = nullptr,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &post_copy_barrier,
        };
        m_table.vkCmdPipelineBarrier2(m_cmd, &post_dep);
    }

    if (m_vk) m_vk->end_debug_label(m_cmd);

    m_table.vkEndCommandBuffer(m_cmd);

    VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &m_cmd,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    };

    if (m_table.vkQueueSubmit(m_queue, 1, &submit_info, m_fence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit batch upload commands to queue");
    }

    m_in_flight = true;
    m_buffer_tasks.clear();
    m_image_tasks.clear();

    log_debug("[Codotaku Uploader] Submitted batch upload (batch size: {} KB) - executing asynchronously.",
        required_staging_size / 1024);
}

void Uploader::wait(uint64_t timeout_ns) {
    if (m_in_flight && m_fence != VK_NULL_HANDLE) {
        m_table.vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, timeout_ns);
        m_in_flight = false;
    }
}

bool Uploader::is_ready() const {
    if (!m_in_flight) {
        return true;
    }
    return (m_table.vkGetFenceStatus(m_device, m_fence) == VK_SUCCESS);
}

} // namespace codotaku
