#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>
#include <volk.h>
#include <vk_mem_alloc.h>
#include <wayland-client.h>

#include <codotaku/core/types.hpp>
#include <codotaku/vulkan/arena.hpp>
#include <codotaku/vulkan/context.hpp>
#include <codotaku/vulkan/sync.hpp>
#include <codotaku/wayland/context.hpp>

#include "linux-dmabuf-v1-client-protocol.h"
#include "linux-drm-syncobj-v1-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

namespace codotaku {

struct DmaBufBuffer {
    VkImage image{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
    VkDeviceMemory memory{VK_NULL_HANDLE};
    int dmabuf_fd{-1};
    wl_buffer* wbuffer{nullptr};
    uint64_t last_release_point{0};
};

struct DepthBuffer {
    VkImage image{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
    VmaAllocation allocation{VK_NULL_HANDLE};
};

class Window {
public:
    Window(WaylandContext& wl, VulkanContext& vk, WindowConfig config);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    void cleanup(VulkanContext& vk);

    bool is_open() const { return m_open; }
    bool is_configured() const { return m_configured; }
    uint32_t get_width() const { return m_width; }
    uint32_t get_height() const { return m_height; }
    float get_aspect_ratio() const {
        return (m_height > 0) ? (static_cast<float>(m_width) / static_cast<float>(m_height)) : 1.0f;
    }
    const std::string& get_title() const { return m_config.title; }
    float get_rotation_speed() const { return m_config.rotation_speed; }
    glm::vec3 get_tint() const { return m_config.tint; }

    void handle_configure(uint32_t width, uint32_t height);
    void handle_close();

    void render_frame(
        WaylandContext& wl,
        VulkanContext& vk,
        VkPipeline pipeline,
        VkPipelineLayout pipeline_layout,
        VkDeviceAddress vertex_address,
        VkDeviceAddress index_address,
        uint32_t index_count,
        std::chrono::steady_clock::time_point start_time);

private:
    void init_wayland_surface(WaylandContext& wl);
    void init_drm_syncobj_timelines(WaylandContext& wl, VulkanContext& vk);
    void create_dmabuf_buffers(WaylandContext& wl, VulkanContext& vk);
    void cleanup_dmabuf_buffers(VulkanContext& vk);
    void create_depth_buffer(VulkanContext& vk);
    void cleanup_depth_buffer(VulkanContext& vk);
    void init_frame_arena(VulkanContext& vk);
    void recreate_buffers(WaylandContext& wl, VulkanContext& vk);
    void create_command_resources(VulkanContext& vk);

    WindowConfig m_config;
    uint32_t m_width{800};
    uint32_t m_height{600};

    bool m_configured{false};
    bool m_open{true};
    bool m_need_resize{false};

    wl_surface* m_surface{nullptr};
    xdg_surface* m_xdg_surface{nullptr};
    xdg_toplevel* m_xdg_toplevel{nullptr};
    zxdg_toplevel_decoration_v1* m_toplevel_decoration{nullptr};
    wp_linux_drm_syncobj_surface_v1* m_syncobj_surface{nullptr};

    DrmTimeline m_acquire_timeline{};
    DrmTimeline m_release_timeline{};

    std::vector<DmaBufBuffer> m_buffers;
    DepthBuffer m_depth{};
    GpuBufferArena m_frame_arena{};
    size_t m_current_buffer_idx{0};

    VkCommandPool m_command_pool{VK_NULL_HANDLE};
    std::vector<VkCommandBuffer> m_command_buffers;
    std::vector<VkFence> m_in_flight_fences;
    std::vector<VkSemaphore> m_render_complete_semaphores;
};

} // namespace codotaku
