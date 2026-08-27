#pragma once

#include <cstdint>
#include <volk.h>

namespace codotaku {

struct MeshHandle {
    VkDeviceAddress vertex_address{0};
    VkDeviceAddress index_address{0};
    uint32_t vertex_count{0};
    uint32_t index_count{0};
    VkDeviceSize vertex_offset{0};
    VkDeviceSize index_offset{0};
};

} // namespace codotaku
