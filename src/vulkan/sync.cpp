#include <stdexcept>
#include <unistd.h>
#include <xf86drm.h>

#include <codotaku/vulkan/sync.hpp>

namespace codotaku {

void init_drm_timeline(int drm_fd, wp_linux_drm_syncobj_manager_v1* syncobj_mgr, DrmTimeline& timeline) {
    if (drmSyncobjCreate(drm_fd, 0, &timeline.handle) != 0) {
        throw std::runtime_error("Failed to create DRM syncobj handle");
    }

    int fd = -1;
    if (drmSyncobjHandleToFD(drm_fd, timeline.handle, &fd) != 0 || fd < 0) {
        throw std::runtime_error("Failed to export DRM syncobj to file descriptor");
    }

    timeline.wtimeline = wp_linux_drm_syncobj_manager_v1_import_timeline(syncobj_mgr, fd);
    close(fd);

    if (!timeline.wtimeline) {
        throw std::runtime_error("Failed to import timeline into Wayland syncobj manager");
    }
    timeline.point = 0;
}

void destroy_drm_timeline(int drm_fd, DrmTimeline& timeline) {
    if (timeline.wtimeline) {
        wp_linux_drm_syncobj_timeline_v1_destroy(timeline.wtimeline);
        timeline.wtimeline = nullptr;
    }
    if (timeline.handle != 0 && drm_fd >= 0) {
        drmSyncobjDestroy(drm_fd, timeline.handle);
        timeline.handle = 0;
    }
}

void timeline_attach_sync_fd(int drm_fd, DrmTimeline& timeline, int sync_fd) {
    uint32_t temp_obj = 0;
    if (drmSyncobjCreate(drm_fd, 0, &temp_obj) != 0) {
        close(sync_fd);
        throw std::runtime_error("Failed to create temporary syncobj");
    }

    if (drmSyncobjImportSyncFile(drm_fd, temp_obj, sync_fd) != 0) {
        drmSyncobjDestroy(drm_fd, temp_obj);
        close(sync_fd);
        throw std::runtime_error("Failed to import sync file into DRM syncobj");
    }

    if (drmSyncobjTransfer(drm_fd, timeline.handle, timeline.point + 1, temp_obj, 0, 0) != 0) {
        drmSyncobjDestroy(drm_fd, temp_obj);
        close(sync_fd);
        throw std::runtime_error("Failed to transfer DRM syncobj to timeline point");
    }

    timeline.point++;
    drmSyncobjDestroy(drm_fd, temp_obj);
    close(sync_fd);
}

void timeline_wait_point(int drm_fd, DrmTimeline& timeline, uint64_t point, uint64_t timeout_ns) {
    if (point == 0) return;
    uint32_t handle = timeline.handle;
    drmSyncobjTimelineWait(drm_fd, &handle, &point, 1, timeout_ns, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, nullptr);
}

} // namespace codotaku
