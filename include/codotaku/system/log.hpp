#pragma once

#include <chrono>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace codotaku {

enum class LogLevel : uint8_t {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    Fatal = 5,
    Off = 6
};

struct LogMessage {
    LogLevel level{LogLevel::Info};
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
    uint64_t thread_id{0};
    std::string_view message;
    std::string_view tag;
};

class LogSink {
public:
    virtual ~LogSink() = default;
    virtual void log(const LogMessage& msg) = 0;
    virtual void flush() = 0;
};

class ConsoleSink : public LogSink {
public:
    explicit ConsoleSink(bool use_colors = true);
    void log(const LogMessage& msg) override;
    void flush() override;

private:
    bool m_use_colors{true};
    std::mutex m_mutex;
};

class FileSink : public LogSink {
public:
    explicit FileSink(std::string file_path, bool append = true);
    ~FileSink() override;
    void log(const LogMessage& msg) override;
    void flush() override;

private:
    std::string m_file_path;
    FILE* m_file{nullptr};
    std::mutex m_mutex;
};

class SystemdJournalSink : public LogSink {
public:
    SystemdJournalSink() = default;
    void log(const LogMessage& msg) override;
    void flush() override {}
};

class Logger {
public:
    Logger();
    ~Logger() = default;

    void set_level(LogLevel level) { m_level = level; }
    LogLevel get_level() const { return m_level; }

    void add_sink(std::shared_ptr<LogSink> sink);
    void clear_sinks();

    void add_file_sink(const std::string& path);

    void log(LogLevel level, std::string_view tag, std::string_view message);

    static Logger& get_global();

private:
    LogLevel m_level{LogLevel::Debug};
    std::vector<std::shared_ptr<LogSink>> m_sinks;
    std::mutex m_mutex;
};

// Global Logging Functions with formatting
template <typename... Args>
void log_trace(std::format_string<Args...> fmt, Args&&... args) {
    std::string msg = std::format(fmt, std::forward<Args>(args)...);
    Logger::get_global().log(LogLevel::Trace, "Codotaku", msg);
}

template <typename... Args>
void log_debug(std::format_string<Args...> fmt, Args&&... args) {
    std::string msg = std::format(fmt, std::forward<Args>(args)...);
    Logger::get_global().log(LogLevel::Debug, "Codotaku", msg);
}

template <typename... Args>
void log_info(std::format_string<Args...> fmt, Args&&... args) {
    std::string msg = std::format(fmt, std::forward<Args>(args)...);
    Logger::get_global().log(LogLevel::Info, "Codotaku", msg);
}

template <typename... Args>
void log_warn(std::format_string<Args...> fmt, Args&&... args) {
    std::string msg = std::format(fmt, std::forward<Args>(args)...);
    Logger::get_global().log(LogLevel::Warn, "Codotaku", msg);
}

template <typename... Args>
void log_error(std::format_string<Args...> fmt, Args&&... args) {
    std::string msg = std::format(fmt, std::forward<Args>(args)...);
    Logger::get_global().log(LogLevel::Error, "Codotaku", msg);
}

template <typename... Args>
void log_fatal(std::format_string<Args...> fmt, Args&&... args) {
    std::string msg = std::format(fmt, std::forward<Args>(args)...);
    Logger::get_global().log(LogLevel::Fatal, "Codotaku", msg);
}

} // namespace codotaku
