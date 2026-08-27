#pragma once

#include <format>
#include <iostream>
#include <print>
#include <string_view>
#include <systemd/sd-journal.h>

namespace codotaku {

enum class LogLevel {
    Debug = LOG_DEBUG,
    Info = LOG_INFO,
    Warning = LOG_WARNING,
    Error = LOG_ERR,
    Critical = LOG_CRIT
};

void log_raw(LogLevel level, std::string_view message);

template <typename... Args>
void log_info(std::format_string<Args...> fmt, Args&&... args) {
    std::string msg = std::format(fmt, std::forward<Args>(args)...);
    log_raw(LogLevel::Info, msg);
}

template <typename... Args>
void log_warn(std::format_string<Args...> fmt, Args&&... args) {
    std::string msg = std::format(fmt, std::forward<Args>(args)...);
    log_raw(LogLevel::Warning, msg);
}

template <typename... Args>
void log_error(std::format_string<Args...> fmt, Args&&... args) {
    std::string msg = std::format(fmt, std::forward<Args>(args)...);
    log_raw(LogLevel::Error, msg);
}

template <typename... Args>
void log_debug(std::format_string<Args...> fmt, Args&&... args) {
    std::string msg = std::format(fmt, std::forward<Args>(args)...);
    log_raw(LogLevel::Debug, msg);
}

} // namespace codotaku
