#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include <systemd/sd-event.h>

namespace codotaku {

class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    EventLoop(EventLoop&& other) noexcept;
    EventLoop& operator=(EventLoop&& other) noexcept;

    using IoCallback = std::function<void(int fd, uint32_t revents)>;
    using SignalCallback = std::function<void(int signal)>;
    using TimerCallback = std::function<void(uint64_t timestamp_usec)>;
    using DeferCallback = std::function<void()>;

    // Add Wayland socket or arbitrary file descriptor to epoll loop
    sd_event_source* add_io(int fd, uint32_t epoll_events, IoCallback callback);

    // Add UNIX signal handler integrated into epoll loop
    sd_event_source* add_signal(int signal, SignalCallback callback);

    // Add monotonic timer callback
    sd_event_source* add_timer(uint64_t interval_usec, TimerCallback callback, bool repeating = false);

    // Add deferred / post callback executed on loop iteration
    sd_event_source* add_defer(DeferCallback callback);
    sd_event_source* add_post(DeferCallback callback);

    // Remove source
    void remove_source(sd_event_source* source);

    // Run the event loop
    int run();

    // Run a single iteration of the event loop (timeout in microseconds, UINT64_MAX for blocking)
    int run_iteration(uint64_t timeout_usec = 10000);

    // Request the loop to terminate
    void exit(int return_code = 0);

    bool is_running() const { return m_running; }
    sd_event* get_raw_loop() const { return m_loop; }

    struct CallbackStorage;

private:
    sd_event* m_loop{nullptr};
    bool m_running{false};

    std::vector<std::unique_ptr<CallbackStorage>> m_callbacks;
};

} // namespace codotaku
