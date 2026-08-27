#include <algorithm>
#include <cmath>
#include <cstring>
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

void Texture::init_from_rgba8(
    VulkanContext& vk,
    const uint8_t* pixel_data,
    uint32_t width,
    uint32_t height,
    const TextureDesc& desc) {
    cleanup();
    m_device = vk.get_device();
    m_allocator = vk.get_allocator();
    m_desc = desc;
    m_desc.width = width;
    m_desc.height = height;

    VkDeviceSize data_size = width * height * 4;
    create_image_and_upload(vk, pixel_data, data_size);
    create_sampler(m_device);

    std::println("[Codotaku Texture] Loaded texture '{}' ({}x{})", m_desc.name, m_desc.width, m_desc.height);
}

void Texture::create_image_and_upload(VulkanContext& vk, const uint8_t* pixel_data, VkDeviceSize data_size) {
    // 1. Create Staging Buffer
    VkBufferCreateInfo staging_buffer_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = data_size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo staging_alloc_info{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };

    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VmaAllocation staging_allocation = VK_NULL_HANDLE;
    VmaAllocationInfo mapped_info{};

    if (vmaCreateBuffer(m_allocator, &staging_buffer_info, &staging_alloc_info, &staging_buffer, &staging_allocation, &mapped_info) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate staging buffer for texture: " + m_desc.name);
    }

    std::memcpy(mapped_info.pMappedData, pixel_data, data_size);

    // 2. Create GPU Texture Image
    VkImageCreateInfo image_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = m_desc.format,
        .extent = { m_desc.width, m_desc.height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VmaAllocationCreateInfo image_alloc_info{
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };

    if (vmaCreateImage(m_allocator, &image_info, &image_alloc_info, &m_image, &m_allocation, nullptr) != VK_SUCCESS) {
        vmaDestroyBuffer(m_allocator, staging_buffer, staging_allocation);
        throw std::runtime_error("Failed to allocate GPU image for texture: " + m_desc.name);
    }

    // 3. Record and submit copy commands with Synchronization 2
    vk.execute_single_time_commands([&](VkCommandBuffer cmd) {
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
            .image = m_image,
            .subresourceRange = subresource_range,
        };

        VkDependencyInfo dep_to_transfer{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier_to_transfer,
        };
        vkCmdPipelineBarrier2(cmd, &dep_to_transfer);

        // Copy Buffer to Image
        VkBufferImageCopy copy_region{
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .imageOffset = {0, 0, 0},
            .imageExtent = { m_desc.width, m_desc.height, 1 },
        };
        vkCmdCopyBufferToImage(cmd, staging_buffer, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

        // Transition: TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
        VkImageMemoryBarrier2 barrier_to_shader{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = m_image,
            .subresourceRange = subresource_range,
        };

        VkDependencyInfo dep_to_shader{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier_to_shader,
        };
        vkCmdPipelineBarrier2(cmd, &dep_to_shader);
    });

    // Cleanup staging buffer
    vmaDestroyBuffer(m_allocator, staging_buffer, staging_allocation);

    // 4. Create Image View
    VkImageViewCreateInfo view_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = m_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = m_desc.format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    if (vkCreateImageView(m_device, &view_info, nullptr, &m_view) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image view for texture: " + m_desc.name);
    }
}

void Texture::create_sampler(VkDevice device) {
    VkSamplerCreateInfo sampler_info{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = m_desc.mag_filter,
        .minFilter = m_desc.min_filter,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = m_desc.address_mode,
        .addressModeV = m_desc.address_mode,
        .addressModeW = m_desc.address_mode,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy = 16.0f,
        .compareEnable = VK_FALSE,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    };

    if (vkCreateSampler(device, &sampler_info, nullptr, &m_sampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create texture sampler");
    }
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

Texture Texture::create_checkerboard(VulkanContext& vk, uint32_t width, uint32_t height, uint32_t tile_size) {
    std::vector<uint8_t> pixels(width * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            bool check = ((x / tile_size) + (y / tile_size)) % 2 == 0;
            size_t idx = (y * width + x) * 4;
            uint8_t val = check ? 245 : 35;
            pixels[idx + 0] = val;
            pixels[idx + 1] = check ? val : 120; // Subtle color difference on dark tile
            pixels[idx + 2] = check ? val : 200;
            pixels[idx + 3] = 255;
        }
    }

    Texture tex;
    tex.init_from_rgba8(vk, pixels.data(), width, height, { .name = "checkerboard" });
    return tex;
}

Texture Texture::create_solid_color(VulkanContext& vk, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    uint8_t pixels[4] = { r, g, b, a };
    Texture tex;
    tex.init_from_rgba8(vk, pixels, 1, 1, { .name = "solid_color" });
    return tex;
}

Texture Texture::create_grid_pattern(VulkanContext& vk, uint32_t width, uint32_t height) {
    std::vector<uint8_t> pixels(width * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            bool edge = (x % 32 == 0) || (y % 32 == 0) || (x == width - 1) || (y == height - 1);
            size_t idx = (y * width + x) * 4;
            if (edge) {
                pixels[idx + 0] = 255;
                pixels[idx + 1] = 255;
                pixels[idx + 2] = 255;
                pixels[idx + 3] = 255;
            } else {
                pixels[idx + 0] = static_cast<uint8_t>((x * 255) / width);
                pixels[idx + 1] = static_cast<uint8_t>((y * 255) / height);
                pixels[idx + 2] = 160;
                pixels[idx + 3] = 255;
            }
        }
    }

    Texture tex;
    tex.init_from_rgba8(vk, pixels.data(), width, height, { .name = "grid_pattern" });
    return tex;
}

} // namespace codotaku
