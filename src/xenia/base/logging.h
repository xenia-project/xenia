/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_LOGGING_H_
#define XENIA_BASE_LOGGING_H_

#include <cstdint>
#include <string>

#include "xenia/base/string.h"

namespace xe {

#define XE_OPTION_ENABLE_LOGGING 1

enum class LogLevel {
  LOG_LEVEL_ERROR,
  LOG_LEVEL_WARNING,
  LOG_LEVEL_INFO,
  LOG_LEVEL_DEBUG,
};

// Initializes the logging system and any outputs requested.
// Must be called on startup.
void InitializeLogging(const std::wstring& app_name);
void ShutdownLogging();

// Appends a line to the log with printf-style formatting.
void LogLineFormat(LogLevel log_level, const char prefix_char, const char* fmt,
                   ...);
void LogLineVarargs(LogLevel log_level, const char prefix_char, const char* fmt,
                    va_list args);
// Appends a line to the log.
void LogLine(LogLevel log_level, const char prefix_char, const char* str,
             size_t str_length = std::string::npos);
void LogLine(LogLevel log_level, const char prefix_char,
             const std::string& str);

// Logs a fatal error with printf-style formatting and aborts the program.
void FatalError(const char* fmt, ...);
// Logs a fatal error and aborts the program.
void FatalError(const std::string& str);

#if XE_OPTION_ENABLE_LOGGING
#define XELOGCORE(level, prefix, fmt, ...) \
  xe::LogLineFormat(level, prefix, fmt, ##__VA_ARGS__)
#else
#define XELOGCORE(level, fmt, ...) \
  do {                             \
  } while (false)
#endif  // ENABLE_LOGGING

#define XELOGE(fmt, ...) \
  XELOGCORE(xe::LogLevel::LOG_LEVEL_ERROR, '!', fmt, ##__VA_ARGS__)
#define XELOGW(fmt, ...) \
  XELOGCORE(xe::LogLevel::LOG_LEVEL_WARNING, 'w', fmt, ##__VA_ARGS__)
#define XELOGI(fmt, ...) \
  XELOGCORE(xe::LogLevel::LOG_LEVEL_INFO, 'i', fmt, ##__VA_ARGS__)
#define XELOGD(fmt, ...) \
  XELOGCORE(xe::LogLevel::LOG_LEVEL_DEBUG, 'd', fmt, ##__VA_ARGS__)

#define XELOGCPU(fmt, ...) \
  XELOGCORE(xe::LogLevel::LOG_LEVEL_INFO, 'C', fmt, ##__VA_ARGS__)
#define XELOGAPU(fmt, ...) \
  XELOGCORE(xe::LogLevel::LOG_LEVEL_INFO, 'A', fmt, ##__VA_ARGS__)
#define XELOGGPU(fmt, ...) \
  XELOGCORE(xe::LogLevel::LOG_LEVEL_INFO, 'G', fmt, ##__VA_ARGS__)
#define XELOGKERNEL(fmt, ...) \
  XELOGCORE(xe::LogLevel::LOG_LEVEL_INFO, 'K', fmt, ##__VA_ARGS__)
#define XELOGFS(fmt, ...) \
  XELOGCORE(xe::LogLevel::LOG_LEVEL_INFO, 'F', fmt, ##__VA_ARGS__)

}  // namespace xe

#endif  // XENIA_BASE_LOGGING_H_
