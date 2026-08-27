#include <chrono>
#include <csignal>
#include <stdexcept>
#include <utility>

#include <codotaku/system/event_loop.hpp>

namespace codotaku {

struct EventLoop::CallbackStorage {
    IoCallback io_fn;
    SignalCallback signal_fn;
    TimerCallback timer_fn;
    DeferCallback defer_fn;
    uint64_t interval_usec{0};
    bool repeating{false};
    EventLoop* parent{nullptr};
};

namespace {

int io_handler(sd_event_source*, int fd, uint32_t revents, void* userdata) {
    auto* storage = static_cast<EventLoop::CallbackStorage*>(userdata);
    if (storage && storage->io_fn) {
        storage->io_fn(fd, revents);
    }
    return 0;
}

int signal_handler(sd_event_source*, const struct signalfd_siginfo* si, void* userdata) {
    auto* storage = static_cast<EventLoop::CallbackStorage*>(userdata);
    if (storage && storage->signal_fn && si) {
        storage->signal_fn(static_cast<int>(si->ssi_signo));
    }
    return 0;
}

int timer_handler(sd_event_source* s, uint64_t usec, void* userdata) {
    auto* storage = static_cast<EventLoop::CallbackStorage*>(userdata);
    if (storage && storage->timer_fn) {
        storage->timer_fn(usec);
        if (storage->repeating) {
            uint64_t next_usec = usec + storage->interval_usec;
            sd_event_source_set_time(s, next_usec);
            sd_event_source_set_enabled(s, SD_EVENT_ONESHOT);
        }
    }
    return 0;
}

int defer_handler(sd_event_source*, void* userdata) {
    auto* storage = static_cast<EventLoop::CallbackStorage*>(userdata);
    if (storage && storage->defer_fn) {
        storage->defer_fn();
    }
    return 0;
}

} // namespace

EventLoop::EventLoop() {
    if (sd_event_default(&m_loop) < 0) {
        throw std::runtime_error("Failed to create systemd sd-event loop");
    }
}

EventLoop::~EventLoop() {
    if (m_loop) {
        sd_event_unref(m_loop);
        m_loop = nullptr;
    }
}

EventLoop::EventLoop(EventLoop&& other) noexcept
    : m_loop(std::exchange(other.m_loop, nullptr)),
      m_running(std::exchange(other.m_running, false)),
      m_callbacks(std::move(other.m_callbacks)) {}

EventLoop& EventLoop::operator=(EventLoop&& other) noexcept {
    if (this != &other) {
        if (m_loop) {
            sd_event_unref(m_loop);
        }
        m_loop = std::exchange(other.m_loop, nullptr);
        m_running = std::exchange(other.m_running, false);
        m_callbacks = std::move(other.m_callbacks);
    }
    return *this;
}

sd_event_source* EventLoop::add_io(int fd, uint32_t epoll_events, IoCallback callback) {
    auto storage = std::make_unique<CallbackStorage>();
    storage->io_fn = std::move(callback);
    storage->parent = this;

    sd_event_source* source = nullptr;
    if (sd_event_add_io(m_loop, &source, fd, epoll_events, io_handler, storage.get()) < 0) {
        throw std::runtime_error("Failed to add IO source to sd-event loop");
    }

    m_callbacks.push_back(std::move(storage));
    return source;
}

sd_event_source* EventLoop::add_signal(int signal, SignalCallback callback) {
    sigset_t ss;
    sigemptyset(&ss);
    sigaddset(&ss, signal);
    pthread_sigmask(SIG_BLOCK, &ss, nullptr);

    auto storage = std::make_unique<CallbackStorage>();
    storage->signal_fn = std::move(callback);
    storage->parent = this;

    sd_event_source* source = nullptr;
    if (sd_event_add_signal(m_loop, &source, signal, signal_handler, storage.get()) < 0) {
        throw std::runtime_error("Failed to add signal source to sd-event loop");
    }

    m_callbacks.push_back(std::move(storage));
    return source;
}

sd_event_source* EventLoop::add_timer(uint64_t interval_usec, TimerCallback callback, bool repeating) {
    auto storage = std::make_unique<CallbackStorage>();
    storage->timer_fn = std::move(callback);
    storage->interval_usec = interval_usec;
    storage->repeating = repeating;
    storage->parent = this;

    uint64_t now_usec = 0;
    sd_event_now(m_loop, CLOCK_MONOTONIC, &now_usec);

    sd_event_source* source = nullptr;
    if (sd_event_add_time(m_loop, &source, CLOCK_MONOTONIC, now_usec + interval_usec, 0, timer_handler, storage.get()) < 0) {
        throw std::runtime_error("Failed to add timer source to sd-event loop");
    }

    m_callbacks.push_back(std::move(storage));
    return source;
}

sd_event_source* EventLoop::add_defer(DeferCallback callback) {
    auto storage = std::make_unique<CallbackStorage>();
    storage->defer_fn = std::move(callback);
    storage->parent = this;

    sd_event_source* source = nullptr;
    if (sd_event_add_defer(m_loop, &source, defer_handler, storage.get()) < 0) {
        throw std::runtime_error("Failed to add defer source to sd-event loop");
    }

    m_callbacks.push_back(std::move(storage));
    return source;
}

sd_event_source* EventLoop::add_post(DeferCallback callback) {
    auto storage = std::make_unique<CallbackStorage>();
    storage->defer_fn = std::move(callback);
    storage->parent = this;

    sd_event_source* source = nullptr;
    if (sd_event_add_post(m_loop, &source, defer_handler, storage.get()) < 0) {
        throw std::runtime_error("Failed to add post source to sd-event loop");
    }

    m_callbacks.push_back(std::move(storage));
    return source;
}

void EventLoop::remove_source(sd_event_source* source) {
    if (source) {
        sd_event_source_unref(source);
    }
}

int EventLoop::run() {
    m_running = true;
    int ret = sd_event_loop(m_loop);
    m_running = false;
    return ret;
}

int EventLoop::run_iteration(uint64_t timeout_usec) {
    m_running = true;
    int ret = sd_event_run(m_loop, timeout_usec);
    if (ret < 0) {
        m_running = false;
    }
    return ret;
}

void EventLoop::exit(int return_code) {
    m_running = false;
    sd_event_exit(m_loop, return_code);
}

} // namespace codotaku
