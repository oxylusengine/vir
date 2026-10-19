#include "vir/Log.hpp"

#include <atomic>
#include <cstdio>

namespace vir {
static auto default_sink(LogLevel level, std::string_view message, const c8* file, u32 line) -> void {
  const auto* name = "debug";
  switch (level) {
    case LogLevel::Debug: name = "debug"; break;
    case LogLevel::Warn : name = "warn"; break;
    case LogLevel::Error: name = "error"; break;
    case LogLevel::Fatal: name = "fatal"; break;
  }

  fmt::print(stderr, "[vir {}] {}:{}: {}\n", name, file, line, message);
}

static auto active_sink() -> std::atomic<LogSink>& {
  static std::atomic<LogSink> sink{default_sink};
  return sink;
}

auto set_log_sink(LogSink sink) -> void { active_sink().store(sink ? sink : default_sink); }

auto log_dispatch(LogLevel level, std::string_view message, const c8* file, u32 line) -> void {
  active_sink().load()(level, message, file, line);
}

auto log_fatal(std::string_view condition, std::string_view message, const c8* file, u32 line) -> void {
  if (message.empty()) {
    log_dispatch(LogLevel::Fatal, condition, file, line);
  } else {
    log_dispatch(LogLevel::Fatal, fmt::format("{}: {}", condition, message), file, line);
  }

  std::abort();
}
} // namespace vir
