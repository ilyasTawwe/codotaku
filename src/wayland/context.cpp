#include <algorithm>
#include <cstring>
#include <stdexcept>

#include <codotaku/wayland/context.hpp>

namespace codotaku {

namespace {

VkFormat drm_fourcc_to_vk_format(uint32_t fourcc) {
    switch (fourcc) {
        case DRM_FORMAT_ARGB8888:
        case DRM_FORMAT_XRGB8888:
            return VK_FORMAT_B8G8R8A8_UNORM;
        case DRM_FORMAT_ABGR8888:
        case DRM_FORMAT_XBGR8888:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case DRM_FORMAT_ARGB2101010:
            return VK_FORMAT_A2R10G10B10_UNORM_PACK32;
        case DRM_FORMAT_ABGR2101010:
            return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        case DRM_FORMAT_ABGR16161616F:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        default:
            return VK_FORMAT_UNDEFINED;
    }
}

void dmabuf_format_handler(void*, zwp_linux_dmabuf_v1*, uint32_t) {}

void dmabuf_modifier_handler(void* data, zwp_linux_dmabuf_v1*, uint32_t format, uint32_t modifier_hi, uint32_t modifier_lo) {
    auto* ctx = static_cast<WaylandContext*>(data);
    uint64_t mod = (static_cast<uint64_t>(modifier_hi) << 32) | modifier_lo;
    if (mod != DRM_FORMAT_MOD_INVALID) {
        ctx->add_format_modifier(format, mod);
    }
}

const zwp_linux_dmabuf_v1_listener dmabuf_listener = {
    .format = dmabuf_format_handler,
    .modifier = dmabuf_modifier_handler,
};

void xdg_wm_base_ping_handler(void*, xdg_wm_base* wm_base, uint32_t serial) {
    xdg_wm_base_pong(wm_base, serial);
}

const xdg_wm_base_listener wm_base_listener = {
    .ping = xdg_wm_base_ping_handler,
};

void registry_global_handler(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    auto* ctx = static_cast<WaylandContext*>(data);
    ctx->bind_global(registry, name, interface, version);
}

void registry_global_remove_handler(void*, wl_registry*, uint32_t) {}

const wl_registry_listener registry_listener = {
    .global = registry_global_handler,
    .global_remove = registry_global_remove_handler,
};

} // namespace

WaylandContext::WaylandContext() {
    init();
}

WaylandContext::~WaylandContext() {
    cleanup();
}

void WaylandContext::add_format_modifier(uint32_t drm_fourcc, uint64_t modifier) {
    VkFormat vk_fmt = drm_fourcc_to_vk_format(drm_fourcc);
    if (vk_fmt == VK_FORMAT_UNDEFINED) return;

    m_modifier_map[drm_fourcc].push_back(modifier);

    // Update color formats list
    auto it = std::find_if(m_color_formats.begin(), m_color_formats.end(),
                           [drm_fourcc](const ColorFormat& cf) { return cf.drm_fourcc == drm_fourcc; });
    if (it != m_color_formats.end()) {
        it->available_modifiers.push_back(modifier);
    } else {
        m_color_formats.push_back({
            .vk_format = vk_fmt,
            .drm_fourcc = drm_fourcc,
            .available_modifiers = { modifier },
        });
    }
}

void WaylandContext::bind_global(wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
        m_compositor = static_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 4u)));
    } else if (std::strcmp(interface, xdg_wm_base_interface.name) == 0) {
        m_wm_base = static_cast<xdg_wm_base*>(
            wl_registry_bind(registry, name, &xdg_wm_base_interface, std::min(version, 1u)));
        xdg_wm_base_add_listener(m_wm_base, &wm_base_listener, this);
    } else if (std::strcmp(interface, zwp_linux_dmabuf_v1_interface.name) == 0) {
        m_dmabuf = static_cast<zwp_linux_dmabuf_v1*>(
            wl_registry_bind(registry, name, &zwp_linux_dmabuf_v1_interface, std::min(version, 3u)));
        zwp_linux_dmabuf_v1_add_listener(m_dmabuf, &dmabuf_listener, this);
    } else if (std::strcmp(interface, wp_linux_drm_syncobj_manager_v1_interface.name) == 0) {
        m_syncobj_mgr = static_cast<wp_linux_drm_syncobj_manager_v1*>(
            wl_registry_bind(registry, name, &wp_linux_drm_syncobj_manager_v1_interface, 1));
    } else if (std::strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
        m_decoration_mgr = static_cast<zxdg_decoration_manager_v1*>(
            wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1));
    }
}

void WaylandContext::dispatch_pending() {
    wl_display_dispatch_pending(m_display);
}

void WaylandContext::flush() {
    wl_display_flush(m_display);
}

void WaylandContext::init() {
    m_display = wl_display_connect(nullptr);
    if (!m_display) {
        throw std::runtime_error("Failed to connect to Wayland display server");
    }

    m_registry = wl_display_get_registry(m_display);
    wl_registry_add_listener(m_registry, &registry_listener, this);
    wl_display_roundtrip(m_display);

    if (!m_compositor || !m_wm_base || !m_dmabuf || !m_syncobj_mgr) {
        throw std::runtime_error("Compositor missing required interfaces: wl_compositor, xdg_wm_base, zwp_linux_dmabuf_v1, or wp_linux_drm_syncobj_manager_v1");
    }

    wl_display_roundtrip(m_display);
}

void WaylandContext::cleanup() {
    if (m_decoration_mgr) {
        zxdg_decoration_manager_v1_destroy(m_decoration_mgr);
        m_decoration_mgr = nullptr;
    }
    if (m_syncobj_mgr) {
        wp_linux_drm_syncobj_manager_v1_destroy(m_syncobj_mgr);
        m_syncobj_mgr = nullptr;
    }
    if (m_dmabuf) {
        zwp_linux_dmabuf_v1_destroy(m_dmabuf);
        m_dmabuf = nullptr;
    }
    if (m_wm_base) {
        xdg_wm_base_destroy(m_wm_base);
        m_wm_base = nullptr;
    }
    if (m_compositor) {
        wl_compositor_destroy(m_compositor);
        m_compositor = nullptr;
    }
    if (m_registry) {
        wl_registry_destroy(m_registry);
        m_registry = nullptr;
    }
    if (m_display) {
        wl_display_disconnect(m_display);
        m_display = nullptr;
    }
}

} // namespace codotaku
