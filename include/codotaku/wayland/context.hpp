#pragma once

#include <cstdint>
#include <map>
#include <vector>
#include <wayland-client.h>

#include <codotaku/core/types.hpp>

#include "linux-dmabuf-v1-client-protocol.h"
#include "linux-drm-syncobj-v1-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

namespace codotaku {

class WaylandContext {
public:
    WaylandContext();
    ~WaylandContext();

    WaylandContext(const WaylandContext&) = delete;
    WaylandContext& operator=(const WaylandContext&) = delete;

    wl_display* get_display() const { return m_display; }
    wl_compositor* get_compositor() const { return m_compositor; }
    xdg_wm_base* get_wm_base() const { return m_wm_base; }
    zwp_linux_dmabuf_v1* get_dmabuf() const { return m_dmabuf; }
    wp_linux_drm_syncobj_manager_v1* get_syncobj_mgr() const { return m_syncobj_mgr; }
    zxdg_decoration_manager_v1* get_decoration_mgr() const { return m_decoration_mgr; }

    const std::vector<ColorFormat>& get_available_color_formats() const { return m_color_formats; }

    void add_format_modifier(uint32_t drm_fourcc, uint64_t modifier);
    void bind_global(wl_registry* registry, uint32_t name, const char* interface, uint32_t version);
    void dispatch_pending();
    void flush();

private:
    void init();
    void cleanup();

    wl_display* m_display{nullptr};
    wl_registry* m_registry{nullptr};
    wl_compositor* m_compositor{nullptr};
    xdg_wm_base* m_wm_base{nullptr};
    zwp_linux_dmabuf_v1* m_dmabuf{nullptr};
    wp_linux_drm_syncobj_manager_v1* m_syncobj_mgr{nullptr};
    zxdg_decoration_manager_v1* m_decoration_mgr{nullptr};

    std::map<uint32_t, std::vector<uint64_t>> m_modifier_map;
    std::vector<ColorFormat> m_color_formats;
};

} // namespace codotaku
