#include <algorithm>
#include <cstring>
#include <print>
#include <stdexcept>
#include <utility>

#include <codotaku/vulkan/context.hpp>
#include <codotaku/vulkan/uploader.hpp>

namespace codotaku {

namespace {

VkDeviceSize align_up(VkDeviceSize offset, VkDeviceSize alignment) {
    return (offset + alignment - 1) & ~(alignment - 1);
}

} // namespace

Uploader::Uploader(VulkanContext& vk)
    : m_device(vk.get_device()),
      m_queue(vk.get_queue()),
      m_allocator(vk.get_allocator()) {
    VkCommandPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = vk.get_queue_family_index(),
    };

    if (vkCreateCommandPool(m_device, &pool_info, nullptr, &m_command_pool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Uploader command pool");
    }

    VkCommandBufferAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = m_command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    if (vkAllocateCommandBuffers(m_device, &alloc_info, &m_cmd) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate Uploader command buffer");
    }

    VkFenceCreateInfo fence_info{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    if (vkCreateFence(m_device, &fence_info, nullptr, &m_fence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Uploader fence");
    }
}

Uploader::~Uploader() {
    wait();
    free_staging_resources();

    if (m_device != VK_NULL_HANDLE) {
        if (m_fence != VK_NULL_HANDLE) {
            vkDestroyFence(m_device, m_fence, nullptr);
            m_fence = VK_NULL_HANDLE;
        }
        if (m_command_pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_device, m_command_pool, nullptr);
            m_command_pool = VK_NULL_HANDLE;
        }
    }
}

Uploader::Uploader(Uploader&& other) noexcept
    : m_device(std::exchange(other.m_device, VK_NULL_HANDLE)),
      m_queue(std::exchange(other.m_queue, VK_NULL_HANDLE)),
      m_allocator(std::exchange(other.m_allocator, VK_NULL_HANDLE)),
      m_command_pool(std::exchange(other.m_command_pool, VK_NULL_HANDLE)),
      m_cmd(std::exchange(other.m_cmd, VK_NULL_HANDLE)),
      m_fence(std::exchange(other.m_fence, VK_NULL_HANDLE)),
      m_staging_buffer(std::exchange(other.m_staging_buffer, VK_NULL_HANDLE)),
      m_staging_allocation(std::exchange(other.m_staging_allocation, VK_NULL_HANDLE)),
      m_buffer_tasks(std::move(other.m_buffer_tasks)),
      m_image_tasks(std::move(other.m_image_tasks)),
      m_in_flight(std::exchange(other.m_in_flight, false)) {}

Uploader& Uploader::operator=(Uploader&& other) noexcept {
    if (this != &other) {
        wait();
        free_staging_resources();

        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_queue = std::exchange(other.m_queue, VK_NULL_HANDLE);
        m_allocator = std::exchange(other.m_allocator, VK_NULL_HANDLE);
        m_command_pool = std::exchange(other.m_command_pool, VK_NULL_HANDLE);
        m_cmd = std::exchange(other.m_cmd, VK_NULL_HANDLE);
        m_fence = std::exchange(other.m_fence, VK_NULL_HANDLE);
        m_staging_buffer = std::exchange(other.m_staging_buffer, VK_NULL_HANDLE);
        m_staging_allocation = std::exchange(other.m_staging_allocation, VK_NULL_HANDLE);
        m_buffer_tasks = std::move(other.m_buffer_tasks);
        m_image_tasks = std::move(other.m_image_tasks);
        m_in_flight = std::exchange(other.m_in_flight, false);
    }
    return *this;
}

BufferAllocation Uploader::upload_buffer(
    const void* data,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkDeviceSize alignment) {
    if (size == 0) return {};

    VkBufferCreateInfo buffer_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo alloc_info{
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };

    BufferAllocation alloc{};
    alloc.size = size;

    if (vmaCreateBuffer(m_allocator, &buffer_info, &alloc_info, &alloc.buffer, &alloc.allocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate device-local buffer via VMA");
    }

    VkBufferDeviceAddressInfo address_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = alloc.buffer,
    };
    alloc.device_address = vkGetBufferDeviceAddress(m_device, &address_info);

    m_buffer_tasks.push_back({
        .dst_buffer = alloc.buffer,
        .dst_offset = 0,
        .data = data,
        .size = size,
        .staging_offset = 0,
    });

    return alloc;
}

GpuVirtualSuballocation Uploader::upload_to_arena(
    GpuBufferArena& arena,
    const void* data,
    VkDeviceSize size,
    VkDeviceSize alignment) {
    auto sub = arena.suballocate(size, alignment);
    m_buffer_tasks.push_back({
        .dst_buffer = arena.get_buffer(),
        .dst_offset = sub.offset,
        .data = data,
        .size = size,
        .staging_offset = 0,
    });
    return sub;
}

Texture Uploader::upload_texture(
    uint32_t width,
    uint32_t height,
    VkFormat format,
    std::span<const uint8_t> pixel_data,
    const TextureDesc& desc) {
    TextureDesc final_desc = desc;
    final_desc.format = format;
    final_desc.width = width;
    final_desc.height = height;

    VkImageCreateInfo image_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = { width, height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VmaAllocationCreateInfo alloc_info{
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };

    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    if (vmaCreateImage(m_allocator, &image_info, &alloc_info, &image, &allocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate GPU texture image via VMA: " + final_desc.name);
    }

    VkImageViewCreateInfo view_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(m_device, &view_info, nullptr, &view) != VK_SUCCESS) {
        vmaDestroyImage(m_allocator, image, allocation);
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
    if (vkCreateSampler(m_device, &sampler_info, nullptr, &sampler) != VK_SUCCESS) {
        vkDestroyImageView(m_device, view, nullptr);
        vmaDestroyImage(m_allocator, image, allocation);
        throw std::runtime_error("Failed to create texture sampler: " + final_desc.name);
    }

    Texture tex;
    tex.init(m_device, m_allocator, image, view, allocation, sampler, final_desc);

    m_image_tasks.push_back({
        .dst_image = image,
        .extent = { width, height, 1 },
        .format = format,
        .data = pixel_data.data(),
        .size = pixel_data.size_bytes(),
        .target_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .staging_offset = 0,
    });

    return tex;
}

void Uploader::upload() {
    if (m_buffer_tasks.empty() && m_image_tasks.empty()) {
        return;
    }

    if (m_in_flight) {
        wait();
    }
    free_staging_resources();

    // 1. Calculate required staging buffer size taking alignments into account
    VkDeviceSize current_offset = 0;
    for (auto& task : m_buffer_tasks) {
        current_offset = align_up(current_offset, 16);
        task.staging_offset = current_offset;
        current_offset += task.size;
    }

    for (auto& task : m_image_tasks) {
        current_offset = align_up(current_offset, 16);
        task.staging_offset = current_offset;
        current_offset += task.size;
    }

    if (current_offset == 0) return;

    // 2. Allocate 1 single host-mapped staging buffer for the entire batch
    VkBufferCreateInfo staging_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = current_offset,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo staging_alloc_info{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };

    VmaAllocationInfo mapped_info{};
    if (vmaCreateBuffer(m_allocator, &staging_info, &staging_alloc_info, &m_staging_buffer, &m_staging_allocation, &mapped_info) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate staging buffer in Uploader");
    }

    uint8_t* mapped_ptr = static_cast<uint8_t*>(mapped_info.pMappedData);

    // 3. Copy all data into staging memory
    for (const auto& task : m_buffer_tasks) {
        std::memcpy(mapped_ptr + task.staging_offset, task.data, task.size);
    }
    for (const auto& task : m_image_tasks) {
        std::memcpy(mapped_ptr + task.staging_offset, task.data, task.size);
    }

    // 4. Record batched GPU DMA transfer commands
    vkResetFences(m_device, 1, &m_fence);
    vkResetCommandBuffer(m_cmd, 0);

    VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(m_cmd, &begin_info);

    // Copy buffers
    for (const auto& task : m_buffer_tasks) {
        VkBufferCopy copy_region{
            .srcOffset = task.staging_offset,
            .dstOffset = task.dst_offset,
            .size = task.size,
        };
        vkCmdCopyBuffer(m_cmd, m_staging_buffer, task.dst_buffer, 1, &copy_region);
    }

    // Copy images with Synchronization 2 layout transitions
    for (const auto& task : m_image_tasks) {
        VkImageSubresourceRange subresource_range{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };

        // Transition: UNDEFINED -> TRANSFER_DST_OPTIMAL
        VkImageMemoryBarrier2 barrier_to_transfer{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = task.dst_image,
            .subresourceRange = subresource_range,
        };

        VkDependencyInfo dep_to_transfer{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier_to_transfer,
        };
        vkCmdPipelineBarrier2(m_cmd, &dep_to_transfer);

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
            .imageOffset = {0, 0, 0},
            .imageExtent = task.extent,
        };
        vkCmdCopyBufferToImage(m_cmd, m_staging_buffer, task.dst_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

        // Transition: TRANSFER_DST_OPTIMAL -> target_layout
        VkImageMemoryBarrier2 barrier_to_target{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = task.target_layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = task.dst_image,
            .subresourceRange = subresource_range,
        };

        VkDependencyInfo dep_to_target{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier_to_target,
        };
        vkCmdPipelineBarrier2(m_cmd, &dep_to_target);
    }

    vkEndCommandBuffer(m_cmd);

    // 5. Submit to queue with fence
    VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &m_cmd,
    };

    if (vkQueueSubmit(m_queue, 1, &submit_info, m_fence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit Uploader batch command buffer");
    }

    m_in_flight = true;

    // Reset task queues so new items can be added immediately for subsequent batches
    m_buffer_tasks.clear();
    m_image_tasks.clear();

    std::println("[Codotaku Uploader] Submitted batch upload (staging size: {} KB) - executing in background.",
        current_offset / 1024);
}

void Uploader::wait(uint64_t timeout_ns) {
    if (!m_in_flight) return;
    vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, timeout_ns);
    free_staging_resources();
    m_in_flight = false;
}

bool Uploader::is_ready() const {
    if (!m_in_flight) return true;
    return vkGetFenceStatus(m_device, m_fence) == VK_SUCCESS;
}

void Uploader::free_staging_resources() {
    if (m_staging_buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, m_staging_buffer, m_staging_allocation);
        m_staging_buffer = VK_NULL_HANDLE;
        m_staging_allocation = VK_NULL_HANDLE;
    }
}

} // namespace codotaku
