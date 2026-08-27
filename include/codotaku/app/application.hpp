#pragma once

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <codotaku/core/camera.hpp>
#include <codotaku/core/scene.hpp>
#include <codotaku/core/types.hpp>
#include <codotaku/shader/slang_compiler.hpp>
#include <codotaku/system/event_loop.hpp>
#include <codotaku/system/log.hpp>
#include <codotaku/vulkan/arena.hpp>
#include <codotaku/vulkan/context.hpp>
#include <codotaku/vulkan/gbuffer.hpp>
#include <codotaku/vulkan/indirect.hpp>
#include <codotaku/vulkan/pipeline.hpp>
#include <codotaku/vulkan/uploader.hpp>
#include <codotaku/wayland/context.hpp>
#include <codotaku/wayland/window.hpp>

namespace codotaku {

class Application {
public:
    explicit Application(std::string app_name = "Codotaku App");
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    Window* create_window(WindowConfig config);

    Pipeline create_pipeline(
        const char* slang_code,
        VkFormat color_format = VK_FORMAT_B8G8R8A8_UNORM,
        VkFormat depth_format = VK_FORMAT_D32_SFLOAT,
        VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT,
        VkFrontFace front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE);

    Pipeline create_compute_pipeline(const char* slang_code);

    bool poll_events();

    using RenderCallback = std::function<void(Window& window, FrameContext& frame)>;
    int run(const RenderCallback& render_callback);

    void set_close_policy(WindowClosePolicy policy) { m_close_policy = policy; }
    WindowClosePolicy get_close_policy() const { return m_close_policy; }

    WaylandContext& get_wayland() { return m_wayland; }
    VulkanContext& get_vulkan() { return m_vulkan; }
    SlangCompiler& get_slang() { return m_slang; }
    EventLoop& get_event_loop() { return m_event_loop; }
    const std::vector<std::unique_ptr<Window>>& get_windows() const { return m_windows; }

private:
    void setup_event_loop_handlers();

    std::string m_app_name;
    WindowClosePolicy m_close_policy{WindowClosePolicy::QuitOnLastWindowClose};
    EventLoop m_event_loop;
    WaylandContext m_wayland;
    VulkanContext m_vulkan;
    SlangCompiler m_slang;

    std::vector<std::unique_ptr<Window>> m_windows;
};

using Engine = Application;

} // namespace codotaku
