/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
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

// Initializes the logging system and any outputs requested.
// Must be called on startup.
void InitializeLogging(const std::string_view app_name);
void ShutdownLogging();

namespace logging {
namespace internal {

bool ShouldLog(LogLevel log_level);
std::pair<char*, size_t> GetThreadBuffer();

void AppendLogLine(LogLevel log_level, const char prefix_char, size_t written);

}  // namespace internal

// Appends a line to the log with {fmt}-style formatting.
template <typename... Args>
void AppendLogLineFormat(LogLevel log_level, const char prefix_char,
                         const char* format, const Args&... args) {
  if (!internal::ShouldLog(log_level)) {
    return;
  }
  auto target = internal::GetThreadBuffer();
  auto result = fmt::format_to_n(target.first, target.second, format, args...);
  internal::AppendLogLine(log_level, prefix_char, result.size);
}

// Appends a line to the log.
void AppendLogLine(LogLevel log_level, const char prefix_char,
                   const std::string_view str);

}  // namespace logging

// Logs a fatal error and aborts the program.
void FatalError(const std::string_view str);

}  // namespace xe

#if XE_OPTION_ENABLE_LOGGING

template <typename... Args>
void XELOGE(const char* format, const Args&... args) {
  xe::logging::AppendLogLineFormat(xe::LogLevel::Error, '!', format, args...);
}

template <typename... Args>
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
  xe::logging::AppendLogLineFormat(xe::LogLevel::Info, 'A', format, args...);
}

template <typename... Args>
void XELOGGPU(const char* format, const Args&... args) {
  xe::logging::AppendLogLineFormat(xe::LogLevel::Info, 'G', format, args...);
}

template <typename... Args>
void XELOGKERNEL(const char* format, const Args&... args) {
  xe::logging::AppendLogLineFormat(xe::LogLevel::Info, 'K', format, args...);
}

template <typename... Args>
void XELOGFS(const char* format, const Args&... args) {
  xe::logging::AppendLogLineFormat(xe::LogLevel::Info, 'F', format, args...);
}

// Redirect SDL_Log* output (internal library stuff) to our log system.
// Can't execute this code here without linking SDL2 into all projects.
// Use this macro everywhere SDL_InitSubSystem() is called.
#define XELOG_SDL_INIT()                                              \
  {                                                                   \
    SDL_LogSetOutputFunction(                                         \
        [](void* userdata, int category, SDL_LogPriority priority,    \
           const char* message) {                                     \
          const char* msg_fmt = "SDL: {}";                            \
          switch (priority) {                                         \
            case SDL_LOG_PRIORITY_VERBOSE:                            \
            case SDL_LOG_PRIORITY_DEBUG:                              \
              XELOGD(msg_fmt, message);                               \
              break;                                                  \
            case SDL_LOG_PRIORITY_INFO:                               \
              XELOGI(msg_fmt, message);                               \
              break;                                                  \
            case SDL_LOG_PRIORITY_WARN:                               \
              XELOGW(msg_fmt, message);                               \
              break;                                                  \
            case SDL_LOG_PRIORITY_ERROR:                              \
            case SDL_LOG_PRIORITY_CRITICAL:                           \
              XELOGE(msg_fmt, message);                               \
              break;                                                  \
            default:                                                  \
              XELOGI(msg_fmt, message);                               \
              assert_always("SDL: Unknown log priority");             \
              break;                                                  \
          }                                                           \
        },                                                            \
        nullptr);                                                     \
    SDL_LogSetAllPriority(SDL_LogPriority::SDL_LOG_PRIORITY_VERBOSE); \
  }

#else

#define XELOGDUMMY \
  do {             \
  } while (false)

#define XELOGE(...) XELOGDUMMY
#define XELOGW(...) XELOGDUMMY
#define XELOGI(...) XELOGDUMMY
#define XELOGD(...) XELOGDUMMY
#define XELOGCPU(...) XELOGDUMMY
#define XELOGAPU(...) XELOGDUMMY
#define XELOGGPU(...) XELOGDUMMY
#define XELOGKERNEL(...) XELOGDUMMY
#define XELOGFS(...) XELOGDUMMY

#define XELOG_SDL_INIT() XELOGDUMMY

#undef XELOGDUMMY

#endif  // ENABLE_LOGGING

#endif  // XENIA_BASE_LOGGING_H_
