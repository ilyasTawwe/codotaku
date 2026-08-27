#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <poll.h>
#include <print>

#include <codotaku/app/application.hpp>
#include <codotaku/system/log.hpp>

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
    log_info("[Codotaku] Initializing Engine '{}' (C++26)...", m_app_name);
    setup_event_loop_handlers();
}

Application::~Application() {
    log_info("[Codotaku] Shutting down application and cleaning resources...");
    for (auto& win : m_windows) {
        win->cleanup(m_vulkan);
    }
    m_windows.clear();
}

void Application::setup_event_loop_handlers() {
    // Register Unix Signals with sd-event
    m_event_loop.add_signal(SIGINT, [this](int) {
        log_info("[Codotaku] SIGINT received via sd-event. Terminating...");
        for (auto& win : m_windows) {
            win->close();
        }
        m_event_loop.exit(0);
    });
    m_event_loop.add_signal(SIGTERM, [this](int) {
        log_info("[Codotaku] SIGTERM received via sd-event. Terminating...");
        for (auto& win : m_windows) {
            win->close();
        }
        m_event_loop.exit(0);
    });
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
    if (g_interrupted.load() || (m_close_policy != WindowClosePolicy::Manual && m_windows.empty())) {
        return false;
    }

    // 1. Prepare Wayland read intent
    while (m_wayland.get_display() && wl_display_prepare_read(m_wayland.get_display()) != 0) {
        m_wayland.dispatch_pending();
    }
    m_wayland.flush();

    // 2. Non-blocking poll on Wayland socket
    struct pollfd pfd = {
        .fd = wl_display_get_fd(m_wayland.get_display()),
        .events = POLLIN,
        .revents = 0,
    };

    int ret = poll(&pfd, 1, 0);
    if (ret > 0 && (pfd.revents & POLLIN)) {
        wl_display_read_events(m_wayland.get_display());
        m_wayland.dispatch_pending();
    } else {
        wl_display_cancel_read(m_wayland.get_display());
    }

    // 3. Process systemd sd-event sources (signals, timers, deferred tasks) without blocking
    m_event_loop.run_iteration(0);

    if (g_interrupted.load()) {
        return false;
    }

    // 4. Process closed windows according to the configured WindowClosePolicy
    bool should_terminate = false;
    for (auto it = m_windows.begin(); it != m_windows.end();) {
        if (!(*it)->is_open()) {
            if (m_close_policy == WindowClosePolicy::QuitOnAnyWindowClose) {
                log_info("[Codotaku] Terminating application (Policy: QuitOnAnyWindowClose).");
                should_terminate = true;
            } else if (m_close_policy == WindowClosePolicy::QuitOnPrimaryWindowClose && (*it)->is_primary()) {
                log_info("[Codotaku] Primary window closed. Terminating application (Policy: QuitOnPrimaryWindowClose).");
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
        m_event_loop.exit(0);
        return false;
    }

    if (m_close_policy != WindowClosePolicy::Manual && m_windows.empty()) {
        m_event_loop.exit(0);
        return false;
    }

    return true;
}

int Application::run(const RenderCallback& render_callback) {
    log_info("[Codotaku] Starting render loop with {} window(s)...", m_windows.size());
    std::fflush(stdout);

    while (poll_events()) {
        for (auto& win : m_windows) {
            if (auto frame = win->begin_frame(m_vulkan)) {
                render_callback(*win, *frame);
                win->submit_and_present(m_vulkan, *frame);
            }
        }
    }

    log_info("[Codotaku] Render loop exited.");
    return 0;
}

} // namespace codotaku
