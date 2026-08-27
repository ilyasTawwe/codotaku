#pragma once

#include <cstdint>
#include <volk.h>
#include <codotaku/vulkan/arena.hpp>

namespace codotaku {

struct IndirectDrawBatch {
    GpuVirtualSuballocation suballocation{};
    uint32_t draw_count{1};
    uint32_t stride{sizeof(VkDrawIndirectCommand)};
    VkDeviceSize offset{0};
    VkDeviceAddress device_address{0};
};

struct IndirectDrawCommand {
    uint32_t vertexCount{0};
    uint32_t instanceCount{1};
    uint32_t firstVertex{0};
    uint32_t firstInstance{0};
};

} // namespace codotaku
