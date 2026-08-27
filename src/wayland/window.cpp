#include <algorithm>
#include <cmath>
#include <cstring>
#include <print>
#include <stdexcept>
#include <unistd.h>

#include <codotaku/system/log.hpp>
#include <codotaku/wayland/window.hpp>

namespace codotaku {

namespace {

constexpr VkDeviceSize FRAME_ARENA_SIZE = 64 * 1024; // 64 KB Dynamic Frame Arena per window

void xdg_surface_configure_handler(void* data, xdg_surface* surface, uint32_t serial) {
    auto* win = static_cast<Window*>(data);
    xdg_surface_ack_configure(surface, serial);
    win->handle_surface_configure();
}

const xdg_surface_listener surface_listener = {
    .configure = xdg_surface_configure_handler,
};

void xdg_toplevel_configure_handler(void* data, xdg_toplevel*, int32_t width, int32_t height, wl_array*) {
    auto* win = static_cast<Window*>(data);
    win->handle_toplevel_configure(width, height);
}

void xdg_toplevel_close_handler(void* data, xdg_toplevel*) {
    auto* win = static_cast<Window*>(data);
    win->handle_close();
}

const xdg_toplevel_listener toplevel_listener = {
    .configure = xdg_toplevel_configure_handler,
    .close = xdg_toplevel_close_handler,
};

void toplevel_decoration_configure_handler(void*, zxdg_toplevel_decoration_v1*, uint32_t) {}

const zxdg_toplevel_decoration_v1_listener decoration_listener = {
    .configure = toplevel_decoration_configure_handler,
};

} // namespace

void FrameContext::begin_rendering(VkClearColorValue clear_color, VkImageView depth_view, float clear_depth) const {
    VkRenderingAttachmentInfo color_attachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = color_image_view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE,
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {
            .color = clear_color,
        },
    };

    VkRenderingAttachmentInfo depth_attachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = depth_view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE,
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue = {
            .depthStencil = { clear_depth, 0 },
        },
    };

    VkRenderingInfo rendering_info{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .pNext = nullptr,
        .flags = 0,
        .renderArea = {
            .offset = {0, 0},
            .extent = {width, height},
        },
        .layerCount = 1,
        .viewMask = 0,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment,
        .pDepthAttachment = (depth_view != VK_NULL_HANDLE) ? &depth_attachment : nullptr,
        .pStencilAttachment = nullptr,
    };

    vkCmdBeginRendering(cmd, &rendering_info);
}

void FrameContext::begin_rendering_with_attachment(VkClearColorValue clear_color, uint32_t depth_attachment_id, float clear_depth) const {
    VkImageView depth_view = gbuffer.has_attachment(depth_attachment_id)
                                 ? gbuffer.get_view(depth_attachment_id)
                                 : VK_NULL_HANDLE;
    begin_rendering(clear_color, depth_view, clear_depth);
}

void FrameContext::end_rendering() const {
    vkCmdEndRendering(cmd);
}

void FrameContext::set_viewport_and_scissor() const {
    VkViewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(width),
        .height = static_cast<float>(height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor{
        .offset = {0, 0},
        .extent = {width, height},
    };

    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

Window::Window(WaylandContext& wl, VulkanDevice& vk, WindowConfig config)
    : m_wayland_ctx(&wl),
      m_config(std::move(config)),
      m_width(m_config.width),
      m_height(m_config.height) {
    init_wayland_surface(wl);
    init_drm_syncobj_timelines(wl, vk);
    choose_color_format(wl);
    create_dmabuf_buffers(wl, vk);
    init_frame_arena(vk);
    create_command_resources(vk);

    m_gbuffer.init(vk, m_width, m_height);
    for (const auto& att_desc : m_config.attachments) {
        m_gbuffer.add_attachment(att_desc);
    }
}

Window::~Window() {
    // Rely on explicit cleanup() or destructor
}

void Window::init_wayland_surface(WaylandContext& wl) {
    m_surface = wl_compositor_create_surface(wl.get_compositor());
    if (!m_surface) {
        throw std::runtime_error("Failed to create Wayland surface");
    }

    m_xdg_surface = xdg_wm_base_get_xdg_surface(wl.get_wm_base(), m_surface);
    if (!m_xdg_surface) {
        throw std::runtime_error("Failed to create xdg_surface");
    }
    xdg_surface_add_listener(m_xdg_surface, &surface_listener, this);

    m_xdg_toplevel = xdg_surface_get_toplevel(m_xdg_surface);
    if (!m_xdg_toplevel) {
        throw std::runtime_error("Failed to create xdg_toplevel");
    }
    xdg_toplevel_add_listener(m_xdg_toplevel, &toplevel_listener, this);

    xdg_toplevel_set_title(m_xdg_toplevel, m_config.title.c_str());
    xdg_toplevel_set_app_id(m_xdg_toplevel, m_config.title.c_str());

    if (wl.get_decoration_mgr()) {
        m_toplevel_decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(
            wl.get_decoration_mgr(), m_xdg_toplevel);
        if (m_toplevel_decoration) {
            zxdg_toplevel_decoration_v1_add_listener(m_toplevel_decoration, &decoration_listener, this);
            zxdg_toplevel_decoration_v1_set_mode(
                m_toplevel_decoration,
                ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
        }
    }

    wl_surface_commit(m_surface);
    wl_display_roundtrip(wl.get_display());
}

void Window::init_drm_syncobj_timelines(WaylandContext& wl, VulkanDevice& vk) {
    if (vk.get_drm_fd() < 0 || !wl.get_syncobj_mgr()) {
        log_warn("[Window] DRM syncobj manager not available, continuing without explicit sync.");
        return;
    }

    m_syncobj_surface = wp_linux_drm_syncobj_manager_v1_get_surface(
        wl.get_syncobj_mgr(), m_surface);
    if (!m_syncobj_surface) {
        throw std::runtime_error("Failed to get DRM syncobj surface");
    }

    init_drm_timeline(vk.get_drm_fd(), wl.get_syncobj_mgr(), m_acquire_timeline);
    init_drm_timeline(vk.get_drm_fd(), wl.get_syncobj_mgr(), m_release_timeline);
}

void Window::choose_color_format(WaylandContext& wl) {
    const auto& supported = wl.get_available_color_formats();
    if (supported.empty()) {
        m_chosen_format = ColorFormat{
            .vk_format = VK_FORMAT_B8G8R8A8_UNORM,
            .drm_fourcc = DRM_FORMAT_XRGB8888,
            .available_modifiers = {DRM_FORMAT_MOD_LINEAR},
        };
        return;
    }

    for (const auto& fmt : supported) {
        if (fmt.drm_fourcc == DRM_FORMAT_ARGB8888 || fmt.drm_fourcc == DRM_FORMAT_XRGB8888) {
            m_chosen_format = fmt;
            return;
        }
    }
    m_chosen_format = supported.front();
}

void Window::create_dmabuf_buffers(WaylandContext& wl, VulkanDevice& vk) {
    m_buffers.resize(m_config.buffer_count);

    std::vector<uint64_t> modifiers = m_chosen_format.available_modifiers;
    if (modifiers.empty()) {
        modifiers.push_back(DRM_FORMAT_MOD_LINEAR);
    }

    for (size_t i = 0; i < m_config.buffer_count; ++i) {
        auto& buf = m_buffers[i];

        VkExternalMemoryImageCreateInfo external_img_info{
            .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
            .pNext = nullptr,
            .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
        };

        VkImageDrmFormatModifierListCreateInfoEXT mod_list{
            .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT,
            .pNext = &external_img_info,
            .drmFormatModifierCount = static_cast<uint32_t>(modifiers.size()),
            .pDrmFormatModifiers = modifiers.data(),
        };

        VkImageCreateInfo image_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = &mod_list,
            .flags = 0,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = m_chosen_format.vk_format,
            .extent = { m_width, m_height, 1 },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        if (vk.get_table().vkCreateImage(vk.get_device(), &image_info, nullptr, &buf.image) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create DRM format modifier image for window: " + m_config.title);
        }

        std::string img_name = m_config.title + "_dmabuf_image_" + std::to_string(i);
        vk.set_name(buf.image, img_name.c_str());

        VkMemoryRequirements mem_reqs;
        vk.get_table().vkGetImageMemoryRequirements(vk.get_device(), buf.image, &mem_reqs);

        uint32_t mem_type_index = vk.find_memory_type(
            mem_reqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VkMemoryDedicatedAllocateInfo dedicated_alloc_info{
            .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
            .pNext = nullptr,
            .image = buf.image,
            .buffer = VK_NULL_HANDLE,
        };

        VkExportMemoryAllocateInfo export_alloc_info{
            .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
            .pNext = &dedicated_alloc_info,
            .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
        };

        VkMemoryAllocateInfo alloc_info{
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = &export_alloc_info,
            .allocationSize = mem_reqs.size,
            .memoryTypeIndex = mem_type_index,
        };

        if (vk.get_table().vkAllocateMemory(vk.get_device(), &alloc_info, nullptr, &buf.memory) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate external DMA-BUF memory");
        }

        if (vk.get_table().vkBindImageMemory(vk.get_device(), buf.image, buf.memory, 0) != VK_SUCCESS) {
            throw std::runtime_error("Failed to bind image memory");
        }

        VkMemoryGetFdInfoKHR get_fd_info{
            .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
            .pNext = nullptr,
            .memory = buf.memory,
            .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
        };

        if (vk.get_table().vkGetMemoryFdKHR(vk.get_device(), &get_fd_info, &buf.dmabuf_fd) != VK_SUCCESS || buf.dmabuf_fd < 0) {
            throw std::runtime_error("Failed to export DMA-BUF fd from Vulkan memory");
        }

        VkImageDrmFormatModifierPropertiesEXT mod_props{
            .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_PROPERTIES_EXT,
            .pNext = nullptr,
            .drmFormatModifier = 0,
        };
        if (vk.get_table().vkGetImageDrmFormatModifierPropertiesEXT(vk.get_device(), buf.image, &mod_props) != VK_SUCCESS) {
            throw std::runtime_error("Failed to query image DRM format modifier properties");
        }

        VkImageSubresource subresource{
            .aspectMask = VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT,
            .mipLevel = 0,
            .arrayLayer = 0,
        };
        VkSubresourceLayout layout{};
        vk.get_table().vkGetImageSubresourceLayout(vk.get_device(), buf.image, &subresource, &layout);

        zwp_linux_buffer_params_v1* params = zwp_linux_dmabuf_v1_create_params(wl.get_dmabuf());
        zwp_linux_buffer_params_v1_add(
            params,
            buf.dmabuf_fd,
            0,
            layout.offset,
            layout.rowPitch,
            mod_props.drmFormatModifier >> 32,
            mod_props.drmFormatModifier & 0xffffffff);

        buf.wbuffer = zwp_linux_buffer_params_v1_create_immed(
            params,
            static_cast<int32_t>(m_width),
            static_cast<int32_t>(m_height),
            m_chosen_format.drm_fourcc,
            0);
        zwp_linux_buffer_params_v1_destroy(params);

        if (!buf.wbuffer) {
            throw std::runtime_error("Failed to create wl_buffer via zwp_linux_dmabuf_v1_create_immed");
        }

        VkImageViewCreateInfo view_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = buf.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = m_chosen_format.vk_format,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        if (vk.get_table().vkCreateImageView(vk.get_device(), &view_info, nullptr, &buf.view) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create image view for DMA-BUF buffer");
        }

        std::string view_name = m_config.title + "_dmabuf_view_" + std::to_string(i);
        vk.set_name(buf.view, view_name.c_str());

        buf.last_release_point = 0;
    }

    // Initialize all DMA-BUF images to VK_IMAGE_LAYOUT_GENERAL once upon creation
    vk.execute_single_time_commands([&](VkCommandBuffer cmd) {
        std::vector<VkImageMemoryBarrier2> init_barriers;
        for (const auto& buf : m_buffers) {
            init_barriers.push_back({
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .pNext = nullptr,
                .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
                .srcAccessMask = VK_ACCESS_2_NONE,
                .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = buf.image,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            });
        }
        VkDependencyInfo dep_info{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = nullptr,
            .dependencyFlags = 0,
            .memoryBarrierCount = 0,
            .pMemoryBarriers = nullptr,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers = nullptr,
            .imageMemoryBarrierCount = static_cast<uint32_t>(init_barriers.size()),
            .pImageMemoryBarriers = init_barriers.data(),
        };
        vk.get_table().vkCmdPipelineBarrier2(cmd, &dep_info);
    });
}

void Window::init_frame_arena(VulkanDevice& vk) {
    m_frame_arena.init(
        vk,
        FRAME_ARENA_SIZE,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        (m_config.title + " Frame Arena").c_str());
}

void Window::cleanup_dmabuf_buffers(VulkanDevice& vk) {
    for (auto& buf : m_buffers) {
        if (buf.wbuffer) {
            wl_buffer_destroy(buf.wbuffer);
            buf.wbuffer = nullptr;
        }
        if (buf.view != VK_NULL_HANDLE) {
            vk.get_table().vkDestroyImageView(vk.get_device(), buf.view, nullptr);
            buf.view = VK_NULL_HANDLE;
        }
        if (buf.dmabuf_fd >= 0) {
            ::close(buf.dmabuf_fd);
            buf.dmabuf_fd = -1;
        }
        if (buf.image != VK_NULL_HANDLE) {
            vk.get_table().vkDestroyImage(vk.get_device(), buf.image, nullptr);
            buf.image = VK_NULL_HANDLE;
        }
        if (buf.memory != VK_NULL_HANDLE) {
            vk.get_table().vkFreeMemory(vk.get_device(), buf.memory, nullptr);
            buf.memory = VK_NULL_HANDLE;
        }
    }
    m_buffers.clear();
}

void Window::create_command_resources(VulkanDevice& vk) {
    VkCommandPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = vk.get_graphics_queue().family_index,
    };

    if (vk.get_table().vkCreateCommandPool(vk.get_device(), &pool_info, nullptr, &m_command_pool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan command pool for window: " + m_config.title);
    }
    vk.set_name(m_command_pool, (m_config.title + "_command_pool").c_str());

    m_command_buffers.resize(m_config.buffer_count);
    VkCommandBufferAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = m_command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = static_cast<uint32_t>(m_config.buffer_count),
    };

    if (vk.get_table().vkAllocateCommandBuffers(vk.get_device(), &alloc_info, m_command_buffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffers for window: " + m_config.title);
    }

    m_in_flight_fences.resize(m_config.buffer_count);
    VkFenceCreateInfo fence_info{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    for (size_t i = 0; i < m_config.buffer_count; ++i) {
        if (vk.get_table().vkCreateFence(vk.get_device(), &fence_info, nullptr, &m_in_flight_fences[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create in-flight fence");
        }
        vk.set_name(m_in_flight_fences[i], (m_config.title + "_fence_" + std::to_string(i)).c_str());
        vk.set_name(m_command_buffers[i], (m_config.title + "_cmd_" + std::to_string(i)).c_str());
    }

    m_render_complete_semaphores.resize(m_config.buffer_count);
    VkExportSemaphoreCreateInfo export_sem_info{
        .sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
        .pNext = nullptr,
        .handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
    };
    VkSemaphoreCreateInfo sem_info{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &export_sem_info,
        .flags = 0,
    };
    for (size_t i = 0; i < m_config.buffer_count; ++i) {
        if (vk.get_table().vkCreateSemaphore(vk.get_device(), &sem_info, nullptr, &m_render_complete_semaphores[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create persistent exportable semaphore");
        }
        vk.set_name(m_render_complete_semaphores[i], (m_config.title + "_sem_" + std::to_string(i)).c_str());
    }
}

void Window::cleanup(VulkanDevice& vk) {
    if (vk.get_device() != VK_NULL_HANDLE) {
        vk.get_table().vkDeviceWaitIdle(vk.get_device());

        for (auto fence : m_in_flight_fences) {
            if (fence != VK_NULL_HANDLE) vk.get_table().vkDestroyFence(vk.get_device(), fence, nullptr);
        }
        m_in_flight_fences.clear();

        for (auto sem : m_render_complete_semaphores) {
            if (sem != VK_NULL_HANDLE) vk.get_table().vkDestroySemaphore(vk.get_device(), sem, nullptr);
        }
        m_render_complete_semaphores.clear();

        if (m_command_pool != VK_NULL_HANDLE) {
            vk.get_table().vkDestroyCommandPool(vk.get_device(), m_command_pool, nullptr);
            m_command_pool = VK_NULL_HANDLE;
        }

        m_frame_arena.cleanup(vk.get_allocator());
        m_gbuffer.cleanup();
        cleanup_dmabuf_buffers(vk);

        destroy_drm_timeline(vk.get_drm_fd(), m_acquire_timeline);
        destroy_drm_timeline(vk.get_drm_fd(), m_release_timeline);
    }

    if (m_syncobj_surface) {
        wp_linux_drm_syncobj_surface_v1_destroy(m_syncobj_surface);
        m_syncobj_surface = nullptr;
    }
    if (m_toplevel_decoration) {
        zxdg_toplevel_decoration_v1_destroy(m_toplevel_decoration);
        m_toplevel_decoration = nullptr;
    }
    if (m_xdg_toplevel) {
        xdg_toplevel_destroy(m_xdg_toplevel);
        m_xdg_toplevel = nullptr;
    }
    if (m_xdg_surface) {
        xdg_surface_destroy(m_xdg_surface);
        m_xdg_surface = nullptr;
    }
    if (m_surface) {
        wl_surface_destroy(m_surface);
        m_surface = nullptr;
    }
}

void Window::handle_toplevel_configure(int32_t width, int32_t height) {
    if (width > 0 && height > 0) {
        m_pending_width = static_cast<uint32_t>(width);
        m_pending_height = static_cast<uint32_t>(height);
    }
}

void Window::handle_surface_configure() {
    if (m_pending_width > 0 && m_pending_height > 0) {
        if (m_pending_width != m_width || m_pending_height != m_height) {
            m_width = m_pending_width;
            m_height = m_pending_height;
            m_need_resize = true;
        }
    }
    m_configured = true;
}

void Window::handle_close() {
    if (m_config.on_close) {
        bool allow_close = m_config.on_close(*this);
        if (!allow_close) {
            log_info("[Codotaku] Window '{}' close request cancelled by callback.", m_config.title);
            return;
        }
    }
    m_open = false;
    log_info("[Codotaku] Window '{}' closed.", m_config.title);
}

void Window::close() {
    handle_close();
}

void Window::recreate_buffers(VulkanDevice& vk) {
    if (!m_wayland_ctx) return;
    vk.get_table().vkDeviceWaitIdle(vk.get_device());
    cleanup_dmabuf_buffers(vk);
    create_dmabuf_buffers(*m_wayland_ctx, vk);

    m_gbuffer.resize_all(m_width, m_height);
    m_current_buffer_idx = 0;
}

std::optional<FrameContext> Window::begin_frame(VulkanDevice& vk) {
    if (!m_open || !m_configured) return std::nullopt;

    if (m_need_resize) {
        m_need_resize = false;
        recreate_buffers(vk);
    }

    if (m_current_buffer_idx == 0) {
        m_frame_arena.reset();
    }

    auto& buf = m_buffers[m_current_buffer_idx];

    // Explicit sync: Wait for the compositor to release this specific buffer
    if (buf.last_release_point > 0) {
        timeline_wait_point(vk.get_drm_fd(), m_release_timeline, buf.last_release_point);
    }

    vk.get_table().vkWaitForFences(vk.get_device(), 1, &m_in_flight_fences[m_current_buffer_idx], VK_TRUE, UINT64_MAX);
    vk.get_table().vkResetFences(vk.get_device(), 1, &m_in_flight_fences[m_current_buffer_idx]);

    auto cmd = m_command_buffers[m_current_buffer_idx];
    vk.get_table().vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = 0,
        .pInheritanceInfo = nullptr,
    };
    vk.get_table().vkBeginCommandBuffer(cmd, &begin_info);

    float aspect = (m_height > 0) ? (static_cast<float>(m_width) / static_cast<float>(m_height)) : 1.0f;

    return FrameContext{
        .cmd = cmd,
        .color_image_view = buf.view,
        .width = m_width,
        .height = m_height,
        .aspect_ratio = aspect,
        .buffer_index = m_current_buffer_idx,
        .frame_arena = m_frame_arena,
        .gbuffer = m_gbuffer,
    };
}

void Window::submit_and_present(VulkanDevice& vk, const FrameContext& frame) {
    auto& buf = m_buffers[m_current_buffer_idx];

    vk.get_table().vkEndCommandBuffer(frame.cmd);

    VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &frame.cmd,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &m_render_complete_semaphores[m_current_buffer_idx],
    };

    if (vk.get_table().vkQueueSubmit(vk.get_graphics_queue().handle, 1, &submit_info, m_in_flight_fences[m_current_buffer_idx]) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit draw command buffer");
    }

    // Export sync_file from Vulkan semaphore
    VkSemaphoreGetFdInfoKHR get_sem_fd_info{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
        .pNext = nullptr,
        .semaphore = m_render_complete_semaphores[m_current_buffer_idx],
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
    };

    int sync_file_fd = -1;
    if (vk.get_table().vkGetSemaphoreFdKHR(vk.get_device(), &get_sem_fd_info, &sync_file_fd) != VK_SUCCESS || sync_file_fd < 0) {
        throw std::runtime_error("Failed to export sync file fd from semaphore");
    }

    // Attach sync_file to DRM acquire timeline
    timeline_attach_sync_fd(vk.get_drm_fd(), m_acquire_timeline, sync_file_fd);

    // Advance release timeline point for this buffer
    m_release_timeline.point++;
    buf.last_release_point = m_release_timeline.point;

    // Set explicit sync points on Wayland surface
    wp_linux_drm_syncobj_surface_v1_set_acquire_point(
        m_syncobj_surface,
        m_acquire_timeline.wtimeline,
        m_acquire_timeline.point >> 32,
        m_acquire_timeline.point & 0xffffffff);

    wp_linux_drm_syncobj_surface_v1_set_release_point(
        m_syncobj_surface,
        m_release_timeline.wtimeline,
        m_release_timeline.point >> 32,
        m_release_timeline.point & 0xffffffff);

    wl_surface_attach(m_surface, buf.wbuffer, 0, 0);
    wl_surface_damage_buffer(m_surface, 0, 0, static_cast<int32_t>(m_width), static_cast<int32_t>(m_height));
    wl_surface_commit(m_surface);

    m_current_buffer_idx = (m_current_buffer_idx + 1) % m_config.buffer_count;
}

} // namespace codotaku
