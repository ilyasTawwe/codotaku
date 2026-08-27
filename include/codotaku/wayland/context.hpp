#pragma once

#include <cstdint>
#include <vector>
#include <wayland-client.h>

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
    const std::vector<uint64_t>& get_supported_modifiers() const { return m_supported_modifiers; }

    void add_modifier(uint64_t modifier);
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

    std::vector<uint64_t> m_supported_modifiers;
};

} // namespace codotaku
