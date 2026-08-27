#include <iostream>
#include <print>
#include <systemd/sd-journal.h>

#include <codotaku/system/log.hpp>

namespace codotaku {

void log_raw(LogLevel level, std::string_view message) {
    int prio = static_cast<int>(level);

    // Send structured log to systemd journal
    sd_journal_print(prio, "%.*s", static_cast<int>(message.size()), message.data());

    // Print to stdout / stderr
    if (level == LogLevel::Error || level == LogLevel::Critical) {
        std::println(std::cerr, "{}", message);
    } else {
        std::println("{}", message);
    }
}

} // namespace codotaku
