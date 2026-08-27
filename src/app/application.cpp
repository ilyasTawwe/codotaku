#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <poll.h>
#include <print>

#include <codotaku/app/application.hpp>

namespace codotaku {

namespace {

std::atomic<bool> g_interrupted{false};

void signal_handler(int) {
    g_interrupted.store(true);
}

constexpr VkDeviceSize GEOMETRY_ARENA_SIZE = 4 * 1024 * 1024; // 4 MB Static Geometry Arena

} // namespace

Application::Application(std::string app_name)
    : m_app_name(std::move(app_name)) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::println("[Codotaku] Initializing Engine '{}' (C++26)...", m_app_name);

    m_geometry_arena.init(
        m_vulkan.get_allocator(),
        m_vulkan.get_device(),
        GEOMETRY_ARENA_SIZE,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
}

Application::~Application() {
    std::println("[Codotaku] Shutting down application and cleaning resources...");
    for (auto& win : m_windows) {
        win->cleanup(m_vulkan);
    }
    m_windows.clear();

    m_geometry_arena.cleanup(m_vulkan.get_allocator());
}

Window* Application::create_window(WindowConfig config) {
    auto win = std::make_unique<Window>(m_wayland, m_vulkan, std::move(config));
    Window* ptr = win.get();
    m_windows.push_back(std::move(win));
    return ptr;
}

MeshHandle Application::upload_mesh(const std::vector<Vertex>& vertices, const std::vector<uint16_t>& indices) {
    VkDeviceSize vb_size = sizeof(Vertex) * vertices.size();
    auto vb_sub = m_geometry_arena.suballocate(vb_size, 64);
    std::memcpy(
        static_cast<uint8_t*>(m_geometry_arena.get_mapped_data()) + vb_sub.offset,
        vertices.data(),
        vb_size);

    VkDeviceSize ib_size = sizeof(uint16_t) * indices.size();
    auto ib_sub = m_geometry_arena.suballocate(ib_size, 64);
    std::memcpy(
        static_cast<uint8_t*>(m_geometry_arena.get_mapped_data()) + ib_sub.offset,
        indices.data(),
        ib_size);

    return MeshHandle{
        .vertex_address = vb_sub.device_address,
        .index_address = ib_sub.device_address,
        .vertex_count = static_cast<uint32_t>(vertices.size()),
        .index_count = static_cast<uint32_t>(indices.size()),
        .vertex_offset = vb_sub.offset,
        .index_offset = ib_sub.offset,
    };
}

IndirectDrawBatch Application::upload_indirect_command(const IndirectDrawCommand& cmd) {
    VkDrawIndirectCommand vk_cmd{
        .vertexCount = cmd.vertexCount,
        .instanceCount = cmd.instanceCount,
        .firstVertex = cmd.firstVertex,
        .firstInstance = cmd.firstInstance,
    };

    VkDeviceSize cmd_size = sizeof(VkDrawIndirectCommand);
    auto sub = m_geometry_arena.suballocate(cmd_size, 16);
    std::memcpy(
        static_cast<uint8_t*>(m_geometry_arena.get_mapped_data()) + sub.offset,
        &vk_cmd,
        cmd_size);

    return IndirectDrawBatch{
        .suballocation = sub,
        .draw_count = 1,
        .stride = static_cast<uint32_t>(cmd_size),
        .offset = sub.offset,
        .device_address = sub.device_address,
    };
}

Pipeline Application::create_pipeline(
    const char* slang_code,
    VkFormat color_format,
    VkFormat depth_format,
    VkCullModeFlags cull_mode,
    VkFrontFace front_face) {
    auto compiled_shaders = m_slang.compile_source(slang_code, "app_pipeline");
    Pipeline pipeline;
    pipeline.init_dynamic_rendering_bda(
        m_vulkan,
        compiled_shaders,
        color_format,
        depth_format,
        cull_mode,
        front_face);
    return pipeline;
}

bool Application::poll_events() {
    if (m_windows.empty() || g_interrupted.load()) {
        return false;
    }

    while (wl_display_prepare_read(m_wayland.get_display()) != 0) {
        m_wayland.dispatch_pending();
    }
    m_wayland.flush();

    struct pollfd pfd = {
        .fd = wl_display_get_fd(m_wayland.get_display()),
        .events = POLLIN,
        .revents = 0,
    };

    int ret = poll(&pfd, 1, 10);
    if (ret > 0) {
        wl_display_read_events(m_wayland.get_display());
        m_wayland.dispatch_pending();
    } else {
        wl_display_cancel_read(m_wayland.get_display());
    }

    // Clean up closed windows dynamically
    for (auto it = m_windows.begin(); it != m_windows.end();) {
        if (!(*it)->is_open()) {
            (*it)->cleanup(m_vulkan);
            it = m_windows.erase(it);
        } else {
            ++it;
        }
    }

    return !m_windows.empty() && !g_interrupted.load();
}

int Application::run(const RenderCallback& render_callback) {
    std::println("[Codotaku] Starting render loop with {} window(s)...", m_windows.size());
    std::fflush(stdout);

    while (poll_events()) {
        for (auto& win : m_windows) {
            if (auto frame = win->begin_frame(m_vulkan)) {
                render_callback(*win, *frame);
                win->submit_and_present(m_vulkan, *frame);
            }
        }
    }

    std::println("[Codotaku] Render loop exited.");
    return 0;
}

} // namespace codotaku
