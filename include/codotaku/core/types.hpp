#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <volk.h>
#include <drm/drm_fourcc.h>

namespace codotaku {

constexpr size_t DEFAULT_BUFFER_COUNT = 3;

enum class PresentMode {
    Fifo,       // VSync ON (throttled to monitor refresh cycle)
    Immediate   // VSync OFF / Uncapped (low latency)
};

struct ColorFormat {
    VkFormat vk_format{VK_FORMAT_B8G8R8A8_UNORM};
    uint32_t drm_fourcc{DRM_FORMAT_ARGB8888};
    std::vector<uint64_t> available_modifiers;
};

using FormatSelector = std::function<ColorFormat(std::span<const ColorFormat> available_formats)>;

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec3 normal;
};

struct WindowConfig {
    std::string title{"Codotaku Window"};
    uint32_t width{800};
    uint32_t height{600};
    size_t buffer_count{DEFAULT_BUFFER_COUNT};
    PresentMode present_mode{PresentMode::Fifo};
    FormatSelector format_selector{nullptr};
};

} // namespace codotaku
