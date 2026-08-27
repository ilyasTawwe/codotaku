#pragma once

#include <chrono>
#include <memory>
#include <optional>
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

struct FrameContext {
    VkCommandBuffer cmd{VK_NULL_HANDLE};
    VkImageView color_image_view{VK_NULL_HANDLE};
    VkImageView depth_image_view{VK_NULL_HANDLE};
    uint32_t width{0};
    uint32_t height{0};
    float aspect_ratio{1.0f};
    size_t buffer_index{0};
    GpuBufferArena& frame_arena;

    void begin_rendering(VkClearColorValue clear_color, float clear_depth = 1.0f) const;
    void end_rendering() const;
    void set_viewport_and_scissor() const;
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
    const ColorFormat& get_color_format() const { return m_chosen_format; }

    void handle_configure(uint32_t width, uint32_t height);
    void handle_close();

    std::optional<FrameContext> begin_frame(VulkanContext& vk);
    void submit_and_present(VulkanContext& vk, const FrameContext& frame);

private:
    void init_wayland_surface(WaylandContext& wl);
    void init_drm_syncobj_timelines(WaylandContext& wl, VulkanContext& vk);
    void choose_color_format(WaylandContext& wl);
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
    ColorFormat m_chosen_format{};

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
