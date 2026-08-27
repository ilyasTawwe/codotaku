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

    std::println("[Codotaku] Initializing Application '{}' (C++26)...", m_app_name);

    m_geometry_arena.init(
        m_vulkan.get_allocator(),
        m_vulkan.get_device(),
        GEOMETRY_ARENA_SIZE,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
}

Application::~Application() {
    std::println("[Codotaku] Shutting down application and cleaning resources...");
    for (auto& win : m_windows) {
        win->cleanup(m_vulkan);
    }
    m_windows.clear();

    m_pipeline.cleanup(m_vulkan.get_device());
    m_geometry_arena.cleanup(m_vulkan.get_allocator());
}

Window* Application::create_window(WindowConfig config) {
    auto win = std::make_unique<Window>(m_wayland, m_vulkan, std::move(config));
    Window* ptr = win.get();
    m_windows.push_back(std::move(win));
    return ptr;
}

void Application::set_mesh_data(const std::vector<Vertex>& vertices, const std::vector<uint16_t>& indices) {
    VkDeviceSize vb_size = sizeof(Vertex) * vertices.size();
    m_vertex_suballoc = m_geometry_arena.suballocate(vb_size, 64);
    std::memcpy(
        static_cast<uint8_t*>(m_geometry_arena.get_mapped_data()) + m_vertex_suballoc.offset,
        vertices.data(),
        vb_size);

    VkDeviceSize ib_size = sizeof(uint16_t) * indices.size();
    m_index_suballoc = m_geometry_arena.suballocate(ib_size, 64);
    std::memcpy(
        static_cast<uint8_t*>(m_geometry_arena.get_mapped_data()) + m_index_suballoc.offset,
        indices.data(),
        ib_size);

    m_index_count = static_cast<uint32_t>(indices.size());

    std::println("[Codotaku] Uploaded geometry to Static Arena (Vertices: {}, Indices: {})",
        vertices.size(), indices.size());
}

void Application::set_shader_source(const char* slang_code) {
    auto compiled_shaders = m_slang.compile_source(slang_code, "app_shader");
    m_pipeline.init_dynamic_rendering_bda(
        m_vulkan,
        compiled_shaders,
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_D32_SFLOAT,
        VK_CULL_MODE_BACK_BIT,
        VK_FRONT_FACE_COUNTER_CLOCKWISE);

    std::println("[Codotaku] Shader compiled and graphics pipeline initialized via Slang reflection.");
}

int Application::run() {
    std::println("[Codotaku] Entering main event loop with {} active window(s)...", m_windows.size());
    std::fflush(stdout);
    auto start_time = std::chrono::steady_clock::now();

    while (!m_windows.empty() && !g_interrupted.load()) {
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

        if (g_interrupted.load()) {
            break;
        }

        // Render each active window independently
        for (auto& win : m_windows) {
            if (win->is_open() && win->is_configured()) {
                win->render_frame(
                    m_wayland,
                    m_vulkan,
                    m_pipeline.get_pipeline(),
                    m_pipeline.get_layout(),
                    m_vertex_suballoc.device_address,
                    m_index_suballoc.device_address,
                    m_index_count,
                    start_time);
            }
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
    }

    std::println("[Codotaku] Main event loop exited.");
    return 0;
}

} // namespace codotaku
