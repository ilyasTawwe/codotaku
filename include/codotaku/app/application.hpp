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
#include <codotaku/vulkan/arena.hpp>
#include <codotaku/vulkan/context.hpp>
#include <codotaku/vulkan/gbuffer.hpp>
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

    template <typename VertexType, typename IndexType = uint16_t>
    MeshHandle upload_mesh(std::span<const VertexType> vertices, std::span<const IndexType> indices) {
        return upload_mesh_raw(
            vertices.data(), sizeof(VertexType) * vertices.size(), static_cast<uint32_t>(vertices.size()),
            indices.data(), sizeof(IndexType) * indices.size(), static_cast<uint32_t>(indices.size())
        );
    }

    template <typename VertexType, typename IndexType = uint16_t>
    MeshHandle upload_mesh(const std::vector<VertexType>& vertices, const std::vector<IndexType>& indices) {
        return upload_mesh(std::span<const VertexType>(vertices), std::span<const IndexType>(indices));
    }

    MeshHandle upload_mesh_raw(
        const void* vertex_data, size_t vertex_data_size, uint32_t vertex_count,
        const void* index_data, size_t index_data_size, uint32_t index_count);

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

    void set_close_policy(WindowClosePolicy policy) { m_close_policy = policy; }
    WindowClosePolicy get_close_policy() const { return m_close_policy; }

    WaylandContext& get_wayland() { return m_wayland; }
    VulkanContext& get_vulkan() { return m_vulkan; }
    GpuBufferArena& get_geometry_arena() { return m_geometry_arena; }
    const std::vector<std::unique_ptr<Window>>& get_windows() const { return m_windows; }

private:
    std::string m_app_name;
    WindowClosePolicy m_close_policy{WindowClosePolicy::QuitOnLastWindowClose};
    WaylandContext m_wayland;
    VulkanContext m_vulkan;
    SlangCompiler m_slang;

    GpuBufferArena m_geometry_arena;
    std::vector<std::unique_ptr<Window>> m_windows;
};

using Engine = Application;

} // namespace codotaku
