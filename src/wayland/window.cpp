#include <algorithm>
#include <cmath>
#include <cstring>
#include <print>
#include <stdexcept>
#include <unistd.h>

#include <codotaku/wayland/window.hpp>
#include <drm/drm_fourcc.h>

namespace codotaku {

namespace {

constexpr VkDeviceSize FRAME_ARENA_SIZE = 64 * 1024; // 64 KB Dynamic Frame Arena per window
constexpr VkFormat DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;

void xdg_surface_configure_handler(void* data, xdg_surface* surface, uint32_t serial) {
    auto* win = static_cast<Window*>(data);
    xdg_surface_ack_configure(surface, serial);
    win->handle_configure(0, 0);
}

const xdg_surface_listener surface_listener = {
    .configure = xdg_surface_configure_handler,
};

void xdg_toplevel_configure_handler(void* data, xdg_toplevel*, int32_t width, int32_t height, wl_array*) {
    auto* win = static_cast<Window*>(data);
    if (width > 0 && height > 0) {
        win->handle_configure(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    }
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

Window::Window(WaylandContext& wl, VulkanContext& vk, WindowConfig config)
    : m_config(std::move(config)), m_width(m_config.width), m_height(m_config.height) {
    init_wayland_surface(wl);
    init_drm_syncobj_timelines(wl, vk);
    create_dmabuf_buffers(wl, vk);
    create_depth_buffer(vk);
    init_frame_arena(vk);
    create_command_resources(vk);
}

Window::~Window() {
    // Rely on explicit cleanup() in Application or context shutdown
}

void Window::init_wayland_surface(WaylandContext& wl) {
    m_surface = wl_compositor_create_surface(wl.get_compositor());
    if (!m_surface) {
        throw std::runtime_error("Failed to create Wayland surface for window: " + m_config.title);
    }

    m_xdg_surface = xdg_wm_base_get_xdg_surface(wl.get_wm_base(), m_surface);
    xdg_surface_add_listener(m_xdg_surface, &surface_listener, this);

    m_xdg_toplevel = xdg_surface_get_toplevel(m_xdg_surface);
    xdg_toplevel_add_listener(m_xdg_toplevel, &toplevel_listener, this);
    xdg_toplevel_set_title(m_xdg_toplevel, m_config.title.c_str());
    xdg_toplevel_set_app_id(m_xdg_toplevel, "codotaku.vulkan.window");
    xdg_toplevel_set_min_size(m_xdg_toplevel, 100, 100);

    if (wl.get_decoration_mgr()) {
        m_toplevel_decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(
            wl.get_decoration_mgr(), m_xdg_toplevel);
        zxdg_toplevel_decoration_v1_add_listener(m_toplevel_decoration, &decoration_listener, this);
        zxdg_toplevel_decoration_v1_set_mode(
            m_toplevel_decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }

    m_syncobj_surface = wp_linux_drm_syncobj_manager_v1_get_surface(wl.get_syncobj_mgr(), m_surface);
    if (!m_syncobj_surface) {
        throw std::runtime_error("Failed to create wp_linux_drm_syncobj_surface_v1 for window: " + m_config.title);
    }

    wl_surface_commit(m_surface);
    wl_display_roundtrip(wl.get_display());
}

void Window::init_drm_syncobj_timelines(WaylandContext& wl, VulkanContext& vk) {
    init_drm_timeline(vk.get_drm_fd(), wl.get_syncobj_mgr(), m_acquire_timeline);
    init_drm_timeline(vk.get_drm_fd(), wl.get_syncobj_mgr(), m_release_timeline);
}

void Window::create_dmabuf_buffers(WaylandContext& wl, VulkanContext& vk) {
    m_buffers.resize(BUFFER_POOL_SIZE);

    std::vector<uint64_t> modifiers = wl.get_supported_modifiers();
    if (modifiers.empty()) {
        modifiers.push_back(DRM_FORMAT_MOD_LINEAR);
    }

    for (size_t i = 0; i < BUFFER_POOL_SIZE; ++i) {
        auto& buf = m_buffers[i];

        VkExternalMemoryImageCreateInfo external_img_info{
            .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
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
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_B8G8R8A8_UNORM,
            .extent = { m_width, m_height, 1 },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        if (vkCreateImage(vk.get_device(), &image_info, nullptr, &buf.image) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create DRM format modifier image for window: " + m_config.title);
        }

        VkMemoryRequirements mem_reqs;
        vkGetImageMemoryRequirements(vk.get_device(), buf.image, &mem_reqs);

        uint32_t mem_type_index = vk.find_memory_type(
            mem_reqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VkMemoryDedicatedAllocateInfo dedicated_alloc_info{
            .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
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

        if (vkAllocateMemory(vk.get_device(), &alloc_info, nullptr, &buf.memory) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate external DMA-BUF memory");
        }

        if (vkBindImageMemory(vk.get_device(), buf.image, buf.memory, 0) != VK_SUCCESS) {
            throw std::runtime_error("Failed to bind image memory");
        }

        VkMemoryGetFdInfoKHR get_fd_info{
            .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
            .memory = buf.memory,
            .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
        };

        if (vkGetMemoryFdKHR(vk.get_device(), &get_fd_info, &buf.dmabuf_fd) != VK_SUCCESS || buf.dmabuf_fd < 0) {
            throw std::runtime_error("Failed to export DMA-BUF fd from Vulkan memory");
        }

        VkImageDrmFormatModifierPropertiesEXT mod_props{
            .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_PROPERTIES_EXT,
        };
        if (vkGetImageDrmFormatModifierPropertiesEXT(vk.get_device(), buf.image, &mod_props) != VK_SUCCESS) {
            throw std::runtime_error("Failed to query image DRM format modifier properties");
        }

        VkImageSubresource subresource{
            .aspectMask = VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT,
            .mipLevel = 0,
            .arrayLayer = 0,
        };
        VkSubresourceLayout layout{};
        vkGetImageSubresourceLayout(vk.get_device(), buf.image, &subresource, &layout);

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
            m_width,
            m_height,
            DRM_FORMAT_ARGB8888,
            0);
        zwp_linux_buffer_params_v1_destroy(params);

        if (!buf.wbuffer) {
            throw std::runtime_error("Failed to create wl_buffer via zwp_linux_dmabuf_v1_create_immed");
        }

        VkImageViewCreateInfo view_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = buf.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_B8G8R8A8_UNORM,
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

        if (vkCreateImageView(vk.get_device(), &view_info, nullptr, &buf.view) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create image view for DMA-BUF buffer");
        }

        buf.last_release_point = 0;
    }
}

void Window::create_depth_buffer(VulkanContext& vk) {
    VkImageCreateInfo image_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = DEPTH_FORMAT,
        .extent = { m_width, m_height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VmaAllocationCreateInfo alloc_info{
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };

    if (vmaCreateImage(vk.get_allocator(), &image_info, &alloc_info, &m_depth.image, &m_depth.allocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate depth image via VMA for window: " + m_config.title);
    }

    VkImageViewCreateInfo view_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = m_depth.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = DEPTH_FORMAT,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    if (vkCreateImageView(vk.get_device(), &view_info, nullptr, &m_depth.view) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create depth image view for window: " + m_config.title);
    }
}

void Window::cleanup_depth_buffer(VulkanContext& vk) {
    if (m_depth.view != VK_NULL_HANDLE) {
        vkDestroyImageView(vk.get_device(), m_depth.view, nullptr);
        m_depth.view = VK_NULL_HANDLE;
    }
    if (m_depth.image != VK_NULL_HANDLE) {
        vmaDestroyImage(vk.get_allocator(), m_depth.image, m_depth.allocation);
        m_depth.image = VK_NULL_HANDLE;
        m_depth.allocation = VK_NULL_HANDLE;
    }
}

void Window::init_frame_arena(VulkanContext& vk) {
    m_frame_arena.init(
        vk.get_allocator(),
        vk.get_device(),
        FRAME_ARENA_SIZE,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
}

void Window::cleanup_dmabuf_buffers(VulkanContext& vk) {
    for (auto& buf : m_buffers) {
        if (buf.wbuffer) {
            wl_buffer_destroy(buf.wbuffer);
            buf.wbuffer = nullptr;
        }
        if (buf.view != VK_NULL_HANDLE) {
            vkDestroyImageView(vk.get_device(), buf.view, nullptr);
            buf.view = VK_NULL_HANDLE;
        }
        if (buf.dmabuf_fd >= 0) {
            close(buf.dmabuf_fd);
            buf.dmabuf_fd = -1;
        }
        if (buf.image != VK_NULL_HANDLE) {
            vkDestroyImage(vk.get_device(), buf.image, nullptr);
            buf.image = VK_NULL_HANDLE;
        }
        if (buf.memory != VK_NULL_HANDLE) {
            vkFreeMemory(vk.get_device(), buf.memory, nullptr);
            buf.memory = VK_NULL_HANDLE;
        }
    }
    m_buffers.clear();
}

void Window::recreate_buffers(WaylandContext& wl, VulkanContext& vk) {
    vkDeviceWaitIdle(vk.get_device());
    cleanup_depth_buffer(vk);
    cleanup_dmabuf_buffers(vk);
    create_dmabuf_buffers(wl, vk);
    create_depth_buffer(vk);
    m_current_buffer_idx = 0;
}

void Window::create_command_resources(VulkanContext& vk) {
    VkCommandPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = vk.get_queue_family_index(),
    };

    if (vkCreateCommandPool(vk.get_device(), &pool_info, nullptr, &m_command_pool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan command pool for window: " + m_config.title);
    }

    m_command_buffers.resize(BUFFER_POOL_SIZE);
    VkCommandBufferAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = m_command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = static_cast<uint32_t>(BUFFER_POOL_SIZE),
    };

    if (vkAllocateCommandBuffers(vk.get_device(), &alloc_info, m_command_buffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffers for window: " + m_config.title);
    }

    m_in_flight_fences.resize(BUFFER_POOL_SIZE);
    VkFenceCreateInfo fence_info{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    for (size_t i = 0; i < BUFFER_POOL_SIZE; ++i) {
        if (vkCreateFence(vk.get_device(), &fence_info, nullptr, &m_in_flight_fences[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create in-flight fence");
        }
    }

    m_render_complete_semaphores.resize(BUFFER_POOL_SIZE);
    VkExportSemaphoreCreateInfo export_sem_info{
        .sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
    };
    VkSemaphoreCreateInfo sem_info{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &export_sem_info,
    };
    for (size_t i = 0; i < BUFFER_POOL_SIZE; ++i) {
        if (vkCreateSemaphore(vk.get_device(), &sem_info, nullptr, &m_render_complete_semaphores[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create persistent exportable semaphore");
        }
    }
}

void Window::cleanup(VulkanContext& vk) {
    if (vk.get_device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(vk.get_device());

        for (auto fence : m_in_flight_fences) {
            if (fence != VK_NULL_HANDLE) vkDestroyFence(vk.get_device(), fence, nullptr);
        }
        m_in_flight_fences.clear();

        for (auto sem : m_render_complete_semaphores) {
            if (sem != VK_NULL_HANDLE) vkDestroySemaphore(vk.get_device(), sem, nullptr);
        }
        m_render_complete_semaphores.clear();

        if (m_command_pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(vk.get_device(), m_command_pool, nullptr);
            m_command_pool = VK_NULL_HANDLE;
        }

        m_frame_arena.cleanup(vk.get_allocator());
        cleanup_depth_buffer(vk);
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

void Window::handle_configure(uint32_t width, uint32_t height) {
    if (width > 0 && height > 0) {
        if (width != m_width || height != m_height) {
            m_width = width;
            m_height = height;
            m_need_resize = true;
        }
    }
    m_configured = true;
}

void Window::handle_close() {
    m_open = false;
    std::println("[Codotaku] Window '{}' closed.", m_config.title);
}

void Window::render_frame(
    WaylandContext& wl,
    VulkanContext& vk,
    VkPipeline pipeline,
    VkPipelineLayout pipeline_layout,
    VkDeviceAddress vertex_address,
    VkDeviceAddress index_address,
    uint32_t index_count,
    std::chrono::steady_clock::time_point start_time) {
    if (!m_open || !m_configured) return;

    if (m_need_resize) {
        m_need_resize = false;
        recreate_buffers(wl, vk);
    }

    auto& buf = m_buffers[m_current_buffer_idx];

    // Explicit sync: Wait for the compositor to release this specific buffer
    if (buf.last_release_point > 0) {
        timeline_wait_point(vk.get_drm_fd(), m_release_timeline, buf.last_release_point);
    }

    vkWaitForFences(vk.get_device(), 1, &m_in_flight_fences[m_current_buffer_idx], VK_TRUE, UINT64_MAX);
    vkResetFences(vk.get_device(), 1, &m_in_flight_fences[m_current_buffer_idx]);

    auto cmd = m_command_buffers[m_current_buffer_idx];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    vkBeginCommandBuffer(cmd, &begin_info);

    auto now = std::chrono::steady_clock::now();
    float time_sec = std::chrono::duration<float>(now - start_time).count();
    VkClearColorValue clear_color = {
        .float32 = {
            0.06f + 0.03f * std::sin(time_sec),
            0.06f + 0.03f * std::sin(time_sec + 2.0f),
            0.09f + 0.03f * std::sin(time_sec + 4.0f),
            1.0f
        }
    };

    VkImageSubresourceRange color_subresource_range{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };

    VkImageSubresourceRange depth_subresource_range{
        .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };

    // Synchronization 2: Transition Color and Depth attachments
    VkImageMemoryBarrier2 barriers[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = buf.image,
            .subresourceRange = color_subresource_range,
        },
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = m_depth.image,
            .subresourceRange = depth_subresource_range,
        }
    };

    VkDependencyInfo dep_pre_render{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 2,
        .pImageMemoryBarriers = barriers,
    };

    vkCmdPipelineBarrier2(cmd, &dep_pre_render);

    // Dynamic Rendering pass
    VkRenderingAttachmentInfo color_attachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = buf.view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {
            .color = clear_color,
        },
    };

    VkRenderingAttachmentInfo depth_attachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = m_depth.view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue = {
            .depthStencil = { 1.0f, 0 },
        },
    };

    VkRenderingInfo rendering_info{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {
            .offset = {0, 0},
            .extent = {m_width, m_height},
        },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment,
        .pDepthAttachment = &depth_attachment,
    };

    vkCmdBeginRendering(cmd, &rendering_info);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // 3D Matrix Computation
    float aspect = (m_height > 0) ? (static_cast<float>(m_width) / static_cast<float>(m_height)) : 1.0f;
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
    proj[1][1] *= -1.0f;

    glm::mat4 view = glm::lookAt(
        glm::vec3(0.0f, 1.2f, 2.8f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::rotate(model, time_sec * m_config.rotation_speed * 0.9f, glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, time_sec * m_config.rotation_speed * 0.6f, glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, time_sec * m_config.rotation_speed * 0.3f, glm::vec3(0.0f, 0.0f, 1.0f));

    glm::mat4 mvp = proj * view * model;

    // Suballocate SceneData in Dynamic Frame Arena
    VkDeviceSize scene_offset = m_current_buffer_idx * 256;
    SceneData scene_data{
        .mvp = mvp,
        .model = model,
        .tint = m_config.tint,
        ._pad = 0.0f,
        .vertexBufferAddress = vertex_address,
        .indexBufferAddress = index_address,
    };
    std::memcpy(static_cast<uint8_t*>(m_frame_arena.get_mapped_data()) + scene_offset, &scene_data, sizeof(SceneData));

    // Push 8-byte 64-bit Root Buffer GPU address
    struct ShaderPushConstants {
        uint64_t sceneDataAddress;
    } pc = {
        .sceneDataAddress = m_frame_arena.get_base_address() + scene_offset,
    };

    vkCmdPushConstants(
        cmd,
        pipeline_layout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(ShaderPushConstants),
        &pc);

    VkViewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(m_width),
        .height = static_cast<float>(m_height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{
        .offset = {0, 0},
        .extent = {m_width, m_height},
    };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // 100% Bindless Draw (pulls vertices & indices from Static Geometry Arena)
    vkCmdDraw(cmd, index_count, 1, 0, 0);

    vkCmdEndRendering(cmd);

    // Synchronization 2: Transition Color to GENERAL
    VkImageMemoryBarrier2 barrier_to_general{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = buf.image,
        .subresourceRange = color_subresource_range,
    };

    VkDependencyInfo dep_to_general{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier_to_general,
    };

    vkCmdPipelineBarrier2(cmd, &dep_to_general);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &m_render_complete_semaphores[m_current_buffer_idx],
    };

    if (vkQueueSubmit(vk.get_queue(), 1, &submit_info, m_in_flight_fences[m_current_buffer_idx]) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit draw command buffer");
    }

    // Export sync_file from Vulkan semaphore
    VkSemaphoreGetFdInfoKHR get_sem_fd_info{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
        .semaphore = m_render_complete_semaphores[m_current_buffer_idx],
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
    };

    int sync_file_fd = -1;
    if (vkGetSemaphoreFdKHR(vk.get_device(), &get_sem_fd_info, &sync_file_fd) != VK_SUCCESS || sync_file_fd < 0) {
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
    wl_surface_damage_buffer(m_surface, 0, 0, m_width, m_height);
    wl_surface_commit(m_surface);

    m_current_buffer_idx = (m_current_buffer_idx + 1) % BUFFER_POOL_SIZE;
}

} // namespace codotaku
