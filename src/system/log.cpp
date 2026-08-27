#include <chrono>
#include <cstdio>
#include <format>
#include <iostream>
#include <print>
#include <systemd/sd-journal.h>
#include <thread>
#include <unistd.h>

#include <codotaku/system/log.hpp>

namespace codotaku {

namespace {

const char* level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
        default:              return "INFO ";
    }
}

const char* level_to_color(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "\033[90m";     // Gray
        case LogLevel::Debug: return "\033[36m";     // Cyan
        case LogLevel::Info:  return "\033[32m";     // Green
        case LogLevel::Warn:  return "\033[33m";     // Yellow
        case LogLevel::Error: return "\033[31m";     // Red
        case LogLevel::Fatal: return "\033[1;35m";   // Bold Magenta
        default:              return "\033[0m";
    }
}

int level_to_journal_priority(LogLevel level) {
    switch (level) {
        case LogLevel::Trace:
        case LogLevel::Debug: return LOG_DEBUG;
        case LogLevel::Info:  return LOG_INFO;
        case LogLevel::Warn:  return LOG_WARNING;
        case LogLevel::Error: return LOG_ERR;
        case LogLevel::Fatal: return LOG_CRIT;
        default:              return LOG_INFO;
    }
}

} // namespace

ConsoleSink::ConsoleSink(bool use_colors)
    : m_use_colors(use_colors && isatty(fileno(stdout))) {}

void ConsoleSink::log(const LogMessage& msg) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto time_c = std::chrono::system_clock::to_time_t(msg.timestamp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(msg.timestamp.time_since_epoch()) % 1000;
    std::tm tm_buf{};
    localtime_r(&time_c, &tm_buf);

    char time_str[32];
    std::strftime(time_str, sizeof(time_str), "%H:%M:%S", &tm_buf);

    if (m_use_colors) {
        std::println(stdout, "\033[90m[{}.{:03d}]\033[0m {}{}\033[0m \033[90m[{}]\033[0m {}",
            time_str, ms.count(),
            level_to_color(msg.level), level_to_string(msg.level),
            msg.tag.empty() ? "Engine" : msg.tag,
            msg.message);
    } else {
        std::println(stdout, "[{}.{:03d}] [{}] [{}] {}",
            time_str, ms.count(),
            level_to_string(msg.level),
            msg.tag.empty() ? "Engine" : msg.tag,
            msg.message);
    }
    std::fflush(stdout);
}

void ConsoleSink::flush() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::fflush(stdout);
}

FileSink::FileSink(std::string file_path, bool append)
    : m_file_path(std::move(file_path)) {
    m_file = std::fopen(m_file_path.c_str(), append ? "a" : "w");
}

FileSink::~FileSink() {
    if (m_file) {
        std::fclose(m_file);
        m_file = nullptr;
    }
}

void FileSink::log(const LogMessage& msg) {
    if (!m_file) return;
    std::lock_guard<std::mutex> lock(m_mutex);

    auto time_c = std::chrono::system_clock::to_time_t(msg.timestamp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(msg.timestamp.time_since_epoch()) % 1000;
    std::tm tm_buf{};
    localtime_r(&time_c, &tm_buf);

    char time_str[32];
    std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_buf);

    std::println(m_file, "[{}.{:03d}] [{}] [{}] {}",
        time_str, ms.count(),
        level_to_string(msg.level),
        msg.tag.empty() ? "Engine" : msg.tag,
        msg.message);
    std::fflush(m_file);
}

void FileSink::flush() {
    if (m_file) {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::fflush(m_file);
    }
}

void SystemdJournalSink::log(const LogMessage& msg) {
    int prio = level_to_journal_priority(msg.level);
    sd_journal_send(
        "MESSAGE=%.*s", static_cast<int>(msg.message.size()), msg.message.data(),
        "PRIORITY=%i", prio,
        "SYSLOG_IDENTIFIER=%.*s", static_cast<int>(msg.tag.size()), msg.tag.data(),
        nullptr);
}

Logger::Logger() {
    m_sinks.push_back(std::make_shared<ConsoleSink>(true));
    m_sinks.push_back(std::make_shared<SystemdJournalSink>());
}

void Logger::add_sink(std::shared_ptr<LogSink> sink) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sinks.push_back(std::move(sink));
}

void Logger::clear_sinks() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sinks.clear();
}

void Logger::add_file_sink(const std::string& path) {
    add_sink(std::make_shared<FileSink>(path));
}

void Logger::log(LogLevel level, std::string_view tag, std::string_view message) {
    if (static_cast<uint8_t>(level) < static_cast<uint8_t>(m_level)) {
        return;
    }

    LogMessage msg{
        .level = level,
        .timestamp = std::chrono::system_clock::now(),
        .thread_id = std::hash<std::thread::id>{}(std::this_thread::get_id()),
        .message = message,
        .tag = tag,
    };

    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& sink : m_sinks) {
        sink->log(msg);
    }
}

Logger& Logger::get_global() {
    static Logger s_logger;
    return s_logger;
}

} // namespace codotaku
