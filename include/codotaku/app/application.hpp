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
#include <codotaku/vulkan/instance.hpp>
#include <codotaku/vulkan/device.hpp>
#include <codotaku/vulkan/descriptor_heap.hpp>
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
        const std::vector<DescriptorBindingMapping>& mappings = {},
        VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT,
        VkFrontFace front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        const char* debug_name = "Graphics Pipeline");

    Pipeline create_compute_pipeline(
        const char* slang_code,
        const std::vector<DescriptorBindingMapping>& mappings = {},
        const char* debug_name = "Compute Pipeline");

    bool poll_events();

    using RenderCallback = std::function<void(Window& window, FrameContext& frame)>;
    int run(const RenderCallback& render_callback);

    void set_close_policy(WindowClosePolicy policy) { m_close_policy = policy; }
    WindowClosePolicy get_close_policy() const { return m_close_policy; }

    WaylandContext& get_wayland() { return m_wayland; }
    VulkanInstance& get_vulkan_instance() { return m_vulkan_instance; }
    VulkanDevice& get_vulkan_device() { return m_vulkan_device; }
    VulkanDevice& get_vulkan() { return m_vulkan_device; }
    DescriptorHeap& get_descriptor_heap() { return m_descriptor_heap; }
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
    VulkanDevice m_vulkan_device;
    DescriptorHeap m_descriptor_heap;
    SlangCompiler m_slang;

    std::vector<std::unique_ptr<Window>> m_windows;
};

using Engine = Application;

} // namespace codotaku
