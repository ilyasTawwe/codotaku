#include <chrono>
#include <csignal>
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

} // namespace

Application::Application(std::string app_name)
    : m_app_name(std::move(app_name)) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::println("[Codotaku] Initializing Engine '{}' (C++26)...", m_app_name);
}

Application::~Application() {
    std::println("[Codotaku] Shutting down application and cleaning resources...");
    for (auto& win : m_windows) {
        win->cleanup(m_vulkan);
    }
    m_windows.clear();
}

Window* Application::create_window(WindowConfig config) {
    auto win = std::make_unique<Window>(m_wayland, m_vulkan, std::move(config));
    Window* ptr = win.get();
    m_windows.push_back(std::move(win));
    return ptr;
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

Pipeline Application::create_compute_pipeline(const char* slang_code) {
    auto compiled_shaders = m_slang.compile_source(slang_code, "compute_pipeline");
    Pipeline pipeline;
    pipeline.init_compute(m_vulkan, compiled_shaders);
    return pipeline;
}

bool Application::poll_events() {
    if ((m_close_policy != WindowClosePolicy::Manual && m_windows.empty()) || g_interrupted.load()) {
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

    // Process closed windows according to the configured WindowClosePolicy
    bool should_terminate = false;
    for (auto it = m_windows.begin(); it != m_windows.end();) {
        if (!(*it)->is_open()) {
            if (m_close_policy == WindowClosePolicy::QuitOnAnyWindowClose) {
                std::println("[Codotaku] Terminating application (Policy: QuitOnAnyWindowClose).");
                should_terminate = true;
            } else if (m_close_policy == WindowClosePolicy::QuitOnPrimaryWindowClose && (*it)->is_primary()) {
                std::println("[Codotaku] Primary window closed. Terminating application (Policy: QuitOnPrimaryWindowClose).");
                should_terminate = true;
            }
            (*it)->cleanup(m_vulkan);
            it = m_windows.erase(it);
        } else {
            ++it;
        }
    }

    if (should_terminate) {
        for (auto& win : m_windows) {
            win->cleanup(m_vulkan);
        }
        m_windows.clear();
        return false;
    }

    if (m_close_policy != WindowClosePolicy::Manual && m_windows.empty()) {
        return false;
    }

    return !g_interrupted.load();
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
