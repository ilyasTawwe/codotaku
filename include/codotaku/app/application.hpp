#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <codotaku/core/camera.hpp>
#include <codotaku/core/scene.hpp>
#include <codotaku/core/types.hpp>
#include <codotaku/shader/slang_compiler.hpp>
#include <codotaku/vulkan/arena.hpp>
#include <codotaku/vulkan/context.hpp>
#include <codotaku/vulkan/indirect.hpp>
#include <codotaku/vulkan/pipeline.hpp>
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

    MeshHandle upload_mesh(const std::vector<Vertex>& vertices, const std::vector<uint16_t>& indices);
    IndirectDrawBatch upload_indirect_command(const IndirectDrawCommand& cmd);
    Pipeline create_pipeline(
        const char* slang_code,
        VkFormat color_format = VK_FORMAT_B8G8R8A8_UNORM,
        VkFormat depth_format = VK_FORMAT_D32_SFLOAT,
        VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT,
        VkFrontFace front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE);

    bool poll_events();

    using RenderCallback = std::function<void(Window& window, FrameContext& frame)>;
    int run(const RenderCallback& render_callback);

    WaylandContext& get_wayland() { return m_wayland; }
    VulkanContext& get_vulkan() { return m_vulkan; }
    GpuBufferArena& get_geometry_arena() { return m_geometry_arena; }
    const std::vector<std::unique_ptr<Window>>& get_windows() const { return m_windows; }

private:
    std::string m_app_name;
    WaylandContext m_wayland;
    VulkanContext m_vulkan;
    SlangCompiler m_slang;

    GpuBufferArena m_geometry_arena;
    std::vector<std::unique_ptr<Window>> m_windows;
};

// Convenience alias
using Engine = Application;

} // namespace codotaku
