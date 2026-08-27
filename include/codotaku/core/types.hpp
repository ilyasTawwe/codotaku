#pragma once

#include <cstdint>
#include <string>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace codotaku {

constexpr size_t BUFFER_POOL_SIZE = 3;

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec3 normal;
};

struct SceneData {
    glm::mat4 mvp;
    glm::mat4 model;
    glm::vec3 tint;
    float _pad{0.0f};
    uint64_t vertexBufferAddress{0};
    uint64_t indexBufferAddress{0};
};

struct WindowConfig {
    std::string title{"Codotaku Window"};
    uint32_t width{800};
    uint32_t height{600};
    float rotation_speed{1.0f};
    glm::vec3 tint{1.0f, 1.0f, 1.0f};
};

} // namespace codotaku
