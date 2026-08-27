#pragma once

#include <cstdint>
#include <span>
#include <vector>
#include <volk.h>
#include <vk_mem_alloc.h>
#include <codotaku/core/scene.hpp>
#include <codotaku/vulkan/arena.hpp>
#include <codotaku/vulkan/context.hpp>
#include <codotaku/vulkan/texture.hpp>

namespace codotaku {

struct BufferAllocation {
    VkBuffer buffer{VK_NULL_HANDLE};
    VmaAllocation allocation{VK_NULL_HANDLE};
    VkDeviceAddress device_address{0};
    VkDeviceSize size{0};
};

class Uploader {
public:
    explicit Uploader(VulkanContext& vk);
    ~Uploader();

    Uploader(const Uploader&) = delete;
    Uploader& operator=(const Uploader&) = delete;

    Uploader(Uploader&& other) noexcept;
    Uploader& operator=(Uploader&& other) noexcept;

    // 1. Upload Buffer Data: Allocates dedicated device-local buffer & returns handle immediately
    BufferAllocation upload_buffer(
        const void* data,
        VkDeviceSize size,
        VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VkDeviceSize alignment = 16);

    template <typename T>
    BufferAllocation upload_buffer(
        std::span<const T> data_span,
        VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VkDeviceSize alignment = 16) {
        return upload_buffer(
            data_span.data(),
            sizeof(T) * data_span.size(),
            usage,
            alignment);
    }

    // Suballocate from an existing GpuBufferArena and enqueue upload
    GpuVirtualSuballocation upload_to_arena(
        GpuBufferArena& arena,
        const void* data,
        VkDeviceSize size,
        VkDeviceSize alignment = 16);

    template <typename T>
    GpuVirtualSuballocation upload_to_arena(
        GpuBufferArena& arena,
        std::span<const T> data_span,
        VkDeviceSize alignment = 16) {
        return upload_to_arena(
            arena,
            data_span.data(),
            sizeof(T) * data_span.size(),
            alignment);
    }

    template <typename T>
    GpuVirtualSuballocation upload_to_arena(
        GpuBufferArena& arena,
        const T& object,
        VkDeviceSize alignment = 16) {
        return upload_to_arena(
            arena,
            &object,
            sizeof(T),
            alignment);
    }

    // 2. Upload Texture: Allocates GPU image, view, sampler & returns Texture handle immediately
    Texture upload_texture(
        uint32_t width,
        uint32_t height,
        VkFormat format,
        std::span<const uint8_t> pixel_data,
        const TextureDesc& desc = {});

    // 3. Batch submit all staging copies to GPU and signal fence
    // Reuses existing staging buffer if capacity suffices, or lazily grows it
    void upload();

    // 4. Synchronization
    void wait(uint64_t timeout_ns = UINT64_MAX);
    bool is_ready() const;

private:
    struct BufferUploadTask {
        VkBuffer dst_buffer{VK_NULL_HANDLE};
        VkDeviceSize dst_offset{0};
        const void* data{nullptr};
        VkDeviceSize size{0};
        VkDeviceSize staging_offset{0};
    };

    struct ImageUploadTask {
        VkImage dst_image{VK_NULL_HANDLE};
        VkExtent3D extent{0, 0, 0};
        VkFormat format{VK_FORMAT_UNDEFINED};
        const void* data{nullptr};
        VkDeviceSize size{0};
        VkImageLayout target_layout{VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDeviceSize staging_offset{0};
    };

    void free_staging_resources();

    VkDevice m_device{VK_NULL_HANDLE};
    VkQueue m_queue{VK_NULL_HANDLE};
    VmaAllocator m_allocator{VK_NULL_HANDLE};

    VkCommandPool m_command_pool{VK_NULL_HANDLE};
    VkCommandBuffer m_cmd{VK_NULL_HANDLE};
    VkFence m_fence{VK_NULL_HANDLE};

    VkBuffer m_staging_buffer{VK_NULL_HANDLE};
    VmaAllocation m_staging_allocation{VK_NULL_HANDLE};
    void* m_staging_mapped_ptr{nullptr};
    VkDeviceSize m_staging_capacity{0};

    std::vector<BufferUploadTask> m_buffer_tasks;
    std::vector<ImageUploadTask> m_image_tasks;
    bool m_in_flight{false};
};

} // namespace codotaku
