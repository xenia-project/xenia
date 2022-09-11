/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_LOGGING_H_
#define XENIA_BASE_LOGGING_H_

#include <cstdarg>
#include <cstdint>
#include <string>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/string.h"

namespace xe {

#define XE_OPTION_ENABLE_LOGGING 1

// Log level is a general indication of the importance of a given log line.
//
// While log levels are named, they are a rough correlation of what the log line
// may be related to. These names should not be taken as fact as what a given
// log line from any log level actually is.
enum class LogLevel {
  Error = 0,
  Warning,
  Info,
  Debug,
  Trace,
};

class LogSink {
 public:
  virtual ~LogSink() = default;

  virtual void Write(const char* buf, size_t size) = 0;
  virtual void Flush() = 0;
};

class FileLogSink final : public LogSink {
 public:
  explicit FileLogSink(FILE* file, bool own_file)
      : file_(file), owns_file_(own_file) {}
  ~FileLogSink();

  void Write(const char* buf, size_t size) override;
  void Flush() override;

 private:
  FILE* file_;
  bool owns_file_;
};

class DebugPrintLogSink final : public LogSink {
 public:
  DebugPrintLogSink() = default;

  void Write(const char* buf, size_t size) override;
  void Flush() override {}
};

// Initializes the logging system and any outputs requested.
// Must be called on startup.
void InitializeLogging(const std::string_view app_name);
void ShutdownLogging();

namespace logging {
namespace internal {

bool ShouldLog(LogLevel log_level);
std::pair<char*, size_t> GetThreadBuffer();
XE_NOALIAS
void AppendLogLine(LogLevel log_level, const char prefix_char, size_t written);

}  // namespace internal
//technically, noalias is incorrect here, these functions do in fact alias global memory,
//but msvc will not optimize the calls away, and the global memory modified by the calls is limited to internal logging variables,
//so it might as well be noalias
template <typename... Args>
XE_NOALIAS
XE_NOINLINE XE_COLD static void AppendLogLineFormat_Impl(LogLevel log_level,
                                                         const char prefix_char,
                                                         const char* format,
                                                         const Args&... args) {
  auto target = internal::GetThreadBuffer();
  auto result = fmt::format_to_n(target.first, target.second, format, args...);
  internal::AppendLogLine(log_level, prefix_char, result.size);
}

  // Appends a line to the log with {fmt}-style formatting.
//chrispy: inline the initial check, outline the append. the append should happen rarely for end users
template <typename... Args>
XE_FORCEINLINE static void AppendLogLineFormat(LogLevel log_level, const char prefix_char,
                         const char* format, const Args&... args) {
  if (!internal::ShouldLog(log_level)) {
    return;
  }
  AppendLogLineFormat_Impl(log_level, prefix_char, format, args...);
}

// Appends a line to the log.
void AppendLogLine(LogLevel log_level, const char prefix_char,
                   const std::string_view str);

}  // namespace logging

// Logs a fatal error and aborts the program.
[[noreturn]] void FatalError(const std::string_view str);

}  // namespace xe

#if XE_OPTION_ENABLE_LOGGING

template <typename... Args>
XE_COLD void XELOGE(const char* format, const Args&... args) {
  xe::logging::AppendLogLineFormat(xe::LogLevel::Error, '!', format, args...);
}

template <typename... Args>
XE_COLD
void XELOGW(const char* format, const Args&... args) {
  xe::logging::AppendLogLineFormat(xe::LogLevel::Warning, 'w', format, args...);
}

template <typename... Args>
void XELOGI(const char* format, const Args&... args) {
  xe::logging::AppendLogLineFormat(xe::LogLevel::Info, 'i', format, args...);
}

template <typename... Args>
void XELOGD(const char* format, const Args&... args) {
  xe::logging::AppendLogLineFormat(xe::LogLevel::Debug, 'd', format, args...);
}

template <typename... Args>
void XELOGCPU(const char* format, const Args&... args) {
  xe::logging::AppendLogLineFormat(xe::LogLevel::Info, 'C', format, args...);
}

template <typename... Args>
void XELOGAPU(const char* format, const Args&... args) {
  xe::logging::AppendLogLineFormat(xe::LogLevel::Debug, 'A', format, args...);
}

template <typename... Args>
void XELOGGPU(const char* format, const Args&... args) {
  xe::logging::AppendLogLineFormat(xe::LogLevel::Debug, 'G', format, args...);
}

template <typename... Args>
void XELOGKERNEL(const char* format, const Args&... args) {
  xe::logging::AppendLogLineFormat(xe::LogLevel::Info, 'K', format, args...);
}

template <typename... Args>
void XELOGFS(const char* format, const Args&... args) {
  xe::logging::AppendLogLineFormat(xe::LogLevel::Info, 'F', format, args...);
}

#else

#define __XELOGDUMMY \
  do {               \
  } while (false)

#define XELOGE(...) __XELOGDUMMY
#define XELOGW(...) __XELOGDUMMY
#define XELOGI(...) __XELOGDUMMY
#define XELOGD(...) __XELOGDUMMY
#define XELOGCPU(...) __XELOGDUMMY
#define XELOGAPU(...) __XELOGDUMMY
#define XELOGGPU(...) __XELOGDUMMY
#define XELOGKERNEL(...) __XELOGDUMMY
#define XELOGFS(...) __XELOGDUMMY

#endif  // ENABLE_LOGGING

#endif  // XENIA_BASE_LOGGING_H_
