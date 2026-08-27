#pragma once

#include <cstdint>
#include <codotaku/core/types.hpp>

namespace codotaku {

struct SceneData {
    glm::mat4 view_proj{1.0f};
    glm::mat4 view{1.0f};
    glm::mat4 proj{1.0f};
    glm::mat4 model{1.0f};
    glm::vec3 camera_pos{0.0f, 0.0f, 0.0f};
    float time{0.0f};

    glm::vec3 light_dir{0.5f, 0.8f, 0.7f};
    float ambient_intensity{0.25f};
    glm::vec3 light_color{1.0f, 1.0f, 1.0f};
    float _pad0{0.0f};

    glm::vec3 tint{1.0f, 1.0f, 1.0f};
    float _pad1{0.0f};

    uint64_t vertexBufferAddress{0};
    uint64_t indexBufferAddress{0};
    uint64_t indirectCommandsAddress{0};
    uint64_t customDataAddress{0};
};

struct MeshHandle {
    VkDeviceAddress vertex_address{0};
    VkDeviceAddress index_address{0};
    uint32_t vertex_count{0};
    uint32_t index_count{0};
    VkDeviceSize vertex_offset{0};
    VkDeviceSize index_offset{0};
};

} // namespace codotaku
