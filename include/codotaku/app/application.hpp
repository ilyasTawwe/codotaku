#pragma once

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <codotaku/core/types.hpp>
#include <codotaku/shader/slang_compiler.hpp>
#include <codotaku/system/event_loop.hpp>
#include <codotaku/system/log.hpp>
#include <codotaku/vulkan/instance.hpp>
#include <codotaku/vulkan/device.hpp>
#include <codotaku/vulkan/pipeline.hpp>
#include <codotaku/wayland/context.hpp>
#include <codotaku/wayland/window.hpp>

namespace codotaku {

class Application {
public:
    explicit Application(std::string app_name = "Codotaku App", bool enable_validation = true);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    std::unique_ptr<VulkanDevice> create_device(VkPhysicalDevice preferred_gpu = VK_NULL_HANDLE) {
        return std::make_unique<VulkanDevice>(m_vulkan_instance, preferred_gpu);
    }

    Window* create_window(WindowConfig config, VulkanDevice& device);

    bool poll_events();

    using RenderCallback = std::function<void(Window& window, FrameContext& frame)>;
    int run(const RenderCallback& render_callback);

    void set_close_policy(WindowClosePolicy policy) { m_close_policy = policy; }
    WindowClosePolicy get_close_policy() const { return m_close_policy; }

    WaylandContext& get_wayland() { return m_wayland; }
    VulkanInstance& get_vulkan_instance() { return m_vulkan_instance; }
    VulkanInstance& get_instance() { return m_vulkan_instance; }
    SlangCompiler& get_slang() { return m_slang; }
    EventLoop& get_event_loop() { return m_event_loop; }
    const std::vector<std::unique_ptr<Window>>& get_windows() const { return m_windows; }

private:
    void setup_event_loop_handlers();

    std::string m_app_name;
    WindowClosePolicy m_close_policy{WindowClosePolicy::QuitOnLastWindowClose};
    EventLoop m_event_loop;
    WaylandContext m_wayland;
    VulkanInstance m_vulkan_instance;
    SlangCompiler m_slang;

    std::vector<std::unique_ptr<Window>> m_windows;
};

using Engine = Application;

} // namespace codotaku
