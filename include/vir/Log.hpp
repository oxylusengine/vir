#pragma once

#include <cstdlib>
#include <fmt/format.h>
#include <string>
#include <string_view>

#include "vir/Base.hpp"

namespace vir {
enum class LogLevel : u8 { Debug, Warn, Error, Fatal };

using LogSink = void (*)(LogLevel level, std::string_view message, const c8* file, u32 line);

auto set_log_sink(LogSink sink) -> void;
auto log_dispatch(LogLevel level, std::string_view message, const c8* file, u32 line) -> void;
[[noreturn]]
auto log_fatal(std::string_view condition, std::string_view message, const c8* file, u32 line) -> void;
} // namespace vir

#define VIR_LOG_IMPL(level, ...) ::vir::log_dispatch(level, ::fmt::format(__VA_ARGS__), __FILE__, __LINE__)

#ifdef VIR_DEBUG
  #define VIR_LOG_DEBUG(...) VIR_LOG_IMPL(::vir::LogLevel::Debug, __VA_ARGS__)
#else
  #define VIR_LOG_DEBUG(...) ((void)0)
#endif

#define VIR_LOG_WARN(...) VIR_LOG_IMPL(::vir::LogLevel::Warn, __VA_ARGS__)
#define VIR_LOG_ERROR(...) VIR_LOG_IMPL(::vir::LogLevel::Error, __VA_ARGS__)

#define VIR_ASSERT(cond, ...)                                                                                          \
  do {                                                                                                                 \
    if (!(cond)) [[unlikely]] {                                                                                        \
      ::vir::log_fatal(                                                                                                \
        "assertion failed: " #cond,                                                                                    \
        ::std::string {} __VA_OPT__(+::fmt::format(__VA_ARGS__)),                                                      \
        __FILE__,                                                                                                      \
        __LINE__                                                                                                       \
      );                                                                                                               \
    }                                                                                                                  \
  } while (false)

#define VIR_CHECK_LE(a, b, ...)                                                                                        \
  do {                                                                                                                 \
    if (!((a) <= (b))) [[unlikely]] {                                                                                  \
      ::vir::log_fatal(                                                                                                \
        "check failed: " #a " <= " #b,                                                                                 \
        ::std::string {} __VA_OPT__(+::fmt::format(__VA_ARGS__)),                                                      \
        __FILE__,                                                                                                      \
        __LINE__                                                                                                       \
      );                                                                                                               \
    }                                                                                                                  \
  } while (false)
