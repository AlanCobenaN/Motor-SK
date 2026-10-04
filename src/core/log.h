#pragma once

#include <cstdarg>
#include <cstdio>

namespace sk {

enum class LogLevel { Info, Warn, Error };

inline void log(LogLevel level, const char* fmt, ...) {
    const char* tag = "INFO ";
    if (level == LogLevel::Warn) tag = "WARN ";
    if (level == LogLevel::Error) tag = "ERROR";

    va_list args;
    va_start(args, fmt);
    std::printf("[%s] ", tag);
    std::vprintf(fmt, args);
    std::printf("\n");
    va_end(args);
    std::fflush(stdout);
}

} // namespace sk

#define SK_INFO(...)  ::sk::log(::sk::LogLevel::Info,  __VA_ARGS__)
#define SK_WARN(...)  ::sk::log(::sk::LogLevel::Warn,  __VA_ARGS__)
#define SK_ERROR(...) ::sk::log(::sk::LogLevel::Error, __VA_ARGS__)
