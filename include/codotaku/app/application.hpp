#pragma once

#include <memory>
#include <string>
#include <vector>

#include <codotaku/core/types.hpp>
#include <codotaku/shader/slang_compiler.hpp>
#include <codotaku/vulkan/arena.hpp>
#include <codotaku/vulkan/context.hpp>
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

    void set_mesh_data(const std::vector<Vertex>& vertices, const std::vector<uint16_t>& indices);
    void set_shader_source(const char* slang_code);

    int run();

    WaylandContext& get_wayland() { return m_wayland; }
    VulkanContext& get_vulkan() { return m_vulkan; }

private:
    std::string m_app_name;
    WaylandContext m_wayland;
    VulkanContext m_vulkan;
    SlangCompiler m_slang;

    GpuBufferArena m_geometry_arena;
    GpuVirtualSuballocation m_vertex_suballoc;
    GpuVirtualSuballocation m_index_suballoc;
    uint32_t m_index_count{0};

    Pipeline m_pipeline;

    std::vector<std::unique_ptr<Window>> m_windows;
};

} // namespace codotaku
