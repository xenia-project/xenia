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

// Initializes the logging system and any outputs requested.
// Must be called on startup.
void InitializeLogging(const std::wstring& app_name);

// Appends a line to the log with printf-style formatting.
void LogLineFormat(const char level_char, const char* fmt, ...);
void LogLineVarargs(const char level_char, const char* fmt, va_list args);
// Appends a line to the log.
void LogLine(const char level_char, const char* str,
             size_t str_length = std::string::npos);
void LogLine(const char level_char, const std::string& str);

// Logs a fatal error with printf-style formatting and aborts the program.
void FatalError(const char* fmt, ...);
// Logs a fatal error and aborts the program.
void FatalError(const std::string& str);

#if XE_OPTION_ENABLE_LOGGING
#define XELOGCORE(level, fmt, ...) xe::LogLineFormat(level, fmt, ##__VA_ARGS__)
#else
#define XELOGCORE(level, fmt, ...) \
  do {                             \
  } while (false)
#endif  // ENABLE_LOGGING

#define XELOGE(fmt, ...) XELOGCORE('!', fmt, ##__VA_ARGS__)
#define XELOGW(fmt, ...) XELOGCORE('w', fmt, ##__VA_ARGS__)
#define XELOGI(fmt, ...) XELOGCORE('i', fmt, ##__VA_ARGS__)
#define XELOGD(fmt, ...) XELOGCORE('d', fmt, ##__VA_ARGS__)

#define XELOGCPU(fmt, ...) XELOGCORE('C', fmt, ##__VA_ARGS__)
#define XELOGAPU(fmt, ...) XELOGCORE('A', fmt, ##__VA_ARGS__)
#define XELOGGPU(fmt, ...) XELOGCORE('G', fmt, ##__VA_ARGS__)
#define XELOGKERNEL(fmt, ...) XELOGCORE('K', fmt, ##__VA_ARGS__)
#define XELOGFS(fmt, ...) XELOGCORE('F', fmt, ##__VA_ARGS__)

}  // namespace xe

#endif  // XENIA_BASE_LOGGING_H_
