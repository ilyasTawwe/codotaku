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

enum class WindowClosePolicy {
    QuitOnLastWindowClose,    // Default: application exits when the last window is closed
    QuitOnPrimaryWindowClose, // Application exits immediately when the primary window is closed
    QuitOnAnyWindowClose,     // Application exits immediately if any window is closed
    Manual                    // Application runs until manually stopped; doesn't auto-quit
};

class Window;
using WindowCloseCallback = std::function<bool(Window& window)>;

struct ColorFormat {
    VkFormat vk_format{VK_FORMAT_B8G8R8A8_UNORM};
    uint32_t drm_fourcc{DRM_FORMAT_ARGB8888};
    std::vector<uint64_t> available_modifiers;
};

using FormatSelector = std::function<ColorFormat(std::span<const ColorFormat> available_formats)>;

struct AttachmentDesc {
    std::string name{"attachment"};
    VkFormat format{VK_FORMAT_R16G16B16A16_SFLOAT};
    VkImageUsageFlags usage{VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT};
    uint32_t width{0};  // 0 = use window width
    uint32_t height{0}; // 0 = use window height
    VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};
    VkClearValue clear_value{.color = {.float32 = {0.0f, 0.0f, 0.0f, 1.0f}}};
};

struct WindowConfig {
    std::string title{"Codotaku Window"};
    uint32_t width{800};
    uint32_t height{600};
    size_t buffer_count{DEFAULT_BUFFER_COUNT};
    PresentMode present_mode{PresentMode::Fifo};
    FormatSelector format_selector{nullptr};
    std::vector<AttachmentDesc> attachments; // User-defined initial GBuffer attachments
    bool is_primary{false};
    WindowCloseCallback on_close{nullptr};
};

} // namespace codotaku
