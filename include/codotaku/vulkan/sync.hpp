#pragma once

#include <cstdint>
#include <wayland-client.h>
#include "linux-drm-syncobj-v1-client-protocol.h"

namespace codotaku {

struct DrmTimeline {
    uint32_t handle{0};
    uint64_t point{0};
    wp_linux_drm_syncobj_timeline_v1* wtimeline{nullptr};
};

void init_drm_timeline(int drm_fd, wp_linux_drm_syncobj_manager_v1* syncobj_mgr, DrmTimeline& timeline);
void destroy_drm_timeline(int drm_fd, DrmTimeline& timeline);
void timeline_attach_sync_fd(int drm_fd, DrmTimeline& timeline, int sync_fd);
void timeline_wait_point(int drm_fd, DrmTimeline& timeline, uint64_t point, uint64_t timeout_ns = 1000000000ULL);

} // namespace codotaku
