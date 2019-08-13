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

enum class LogCategory {
	CPU = 0,
	GPU,
	APU,
	KERNEL,
	HID,
	VFS,
	UI,
	ALWAYS_LOG
};

// Initializes the logging system and any outputs requested.
// Must be called on startup.
void InitializeLogging(const std::wstring& app_name);
void ShutdownLogging();

// Appends a line to the log with printf-style formatting.
void LogLineFormat(LogLevel log_level, const char prefix_char, const char* fmt,
                   ...);

void LogLineFormat2(LogLevel log_level, LogCategory log_category, const char prefix_char, const char* fmt,
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
void FatalError(const wchar_t* fmt, ...);
// Logs a fatal error and aborts the program.
void FatalError(const std::string& str);
void FatalError(const std::wstring& str);

#if XE_OPTION_ENABLE_LOGGING
#define XELOGCORE(level, prefix, fmt, ...) \
  xe::LogLineFormat(level, prefix, fmt, ##__VA_ARGS__)
#define XELOGCORE2(level, category, prefix, fmt, ...) \
  xe::LogLineFormat2(level, category, prefix, fmt, ##__VA_ARGS__)
#else
#define XELOGCORE(level, fmt, ...) \
  do {                             \
  } while (false)
#endif  // ENABLE_LOGGING

// -- BEGIN EXISTING (OLD) LOGGING FUNCTIONS --
// These functions have been kept for older modules that have yet to be converted over
// to the new functions. They are also used where a new logging function doesn't exist
// (e.g. generic logging).
#define XELOGE(fmt, ...) XELOGCORE(xe::LogLevel::Error, '!', fmt, ##__VA_ARGS__)
#define XELOGW(fmt, ...) XELOGCORE(xe::LogLevel::Warning, 'w', fmt, ##__VA_ARGS__)
#define XELOGI(fmt, ...) XELOGCORE(xe::LogLevel::Info, 'i', fmt, ##__VA_ARGS__)
#define XELOGD(fmt, ...) XELOGCORE(xe::LogLevel::Debug, 'd', fmt, ##__VA_ARGS__)

// -- BEGIN NEW LOGGING FUNCTIONS --
// [CPU]
#define XELOG_CPU_E(fmt, ...) XELOGCORE2(xe::LogLevel::Error, xe::LogCategory::CPU, '!', fmt, ##__VA_ARGS__)
#define XELOG_CPU_W(fmt, ...) XELOGCORE2(xe::LogLevel::Warning, xe::LogCategory::CPU, 'w', fmt, ##__VA_ARGS__)
#define XELOG_CPU_I(fmt, ...) XELOGCORE2(xe::LogLevel::Info, xe::LogCategory::CPU, 'i', fmt, ##__VA_ARGS__)
#define XELOG_CPU_D(fmt, ...) XELOGCORE2(xe::LogLevel::Debug, xe::LogCategory::CPU, 'd', fmt, ##__VA_ARGS__)
// [GPU]
#define XELOG_GPU_E(fmt, ...) XELOGCORE2(xe::LogLevel::Error, xe::LogCategory::GPU, '!', fmt, ##__VA_ARGS__)
#define XELOG_GPU_W(fmt, ...) XELOGCORE2(xe::LogLevel::Warning, xe::LogCategory::GPU, 'w', fmt, ##__VA_ARGS__)
#define XELOG_GPU_I(fmt, ...) XELOGCORE2(xe::LogLevel::Info, xe::LogCategory::GPU, 'i', fmt, ##__VA_ARGS__)
#define XELOG_GPU_D(fmt, ...) XELOGCORE2(xe::LogLevel::Debug, xe::LogCategory::GPU, 'd', fmt, ##__VA_ARGS__)
// [APU]
#define XELOG_APU_E(fmt, ...) XELOGCORE2(xe::LogLevel::Error, xe::LogCategory::APU, '!', fmt, ##__VA_ARGS__)
#define XELOG_APU_W(fmt, ...) XELOGCORE2(xe::LogLevel::Warning, xe::LogCategory::APU, 'w', fmt, ##__VA_ARGS__)
#define XELOG_APU_I(fmt, ...) XELOGCORE2(xe::LogLevel::Info, xe::LogCategory::APU, 'i', fmt, ##__VA_ARGS__)
#define XELOG_APU_D(fmt, ...) XELOGCORE2(xe::LogLevel::Debug, xe::LogCategory::APU, 'd', fmt, ##__VA_ARGS__)
// [KERNEL]
#define XELOG_KERNEL_E(fmt, ...) XELOGCORE2(xe::LogLevel::Error, xe::LogCategory::KERNEL, '!', fmt, ##__VA_ARGS__)
#define XELOG_KERNEL_W(fmt, ...) XELOGCORE2(xe::LogLevel::Warning, xe::LogCategory::KERNEL, 'w', fmt, ##__VA_ARGS__)
#define XELOG_KERNEL_I(fmt, ...) XELOGCORE2(xe::LogLevel::Info, xe::LogCategory::KERNEL, 'i', fmt, ##__VA_ARGS__)
#define XELOG_KERNEL_D(fmt, ...) XELOGCORE2(xe::LogLevel::Debug, xe::LogCategory::KERNEL, 'd', fmt, ##__VA_ARGS__)
// [HID]
#define XELOG_HID_E(fmt, ...) XELOGCORE2(xe::LogLevel::Error, xe::LogCategory::KERNEL, '!', fmt, ##__VA_ARGS__)
#define XELOG_HID_W(fmt, ...) XELOGCORE2(xe::LogLevel::Warning, xe::LogCategory::KERNEL, 'w', fmt, ##__VA_ARGS__)
#define XELOG_HID_I(fmt, ...) XELOGCORE2(xe::LogLevel::Info, xe::LogCategory::KERNEL, 'i', fmt, ##__VA_ARGS__)
#define XELOG_HID_D(fmt, ...) XELOGCORE2(xe::LogLevel::Debug, xe::LogCategory::KERNEL, 'd', fmt, ##__VA_ARGS__)
// [VFS]
#define XELOG_VFS_E(fmt, ...) XELOGCORE2(xe::LogLevel::Error, xe::LogCategory::VFS, '!', fmt, ##__VA_ARGS__)
#define XELOG_VFS_W(fmt, ...) XELOGCORE2(xe::LogLevel::Warning, xe::LogCategory::VFS, 'w', fmt, ##__VA_ARGS__)
#define XELOG_VFS_I(fmt, ...) XELOGCORE2(xe::LogLevel::Info, xe::LogCategory::VFS, 'i', fmt, ##__VA_ARGS__)
#define XELOG_VFS_D(fmt, ...) XELOGCORE2(xe::LogLevel::Debug, xe::LogCategory::VFS, 'd', fmt, ##__VA_ARGS__)
// [UI]
#define XELOG_UI_E(fmt, ...) XELOGCORE2(xe::LogLevel::Error, xe::LogCategory::UI, '!', fmt, ##__VA_ARGS__)
#define XELOG_UI_W(fmt, ...) XELOGCORE2(xe::LogLevel::Warning, xe::LogCategory::UI, 'w', fmt, ##__VA_ARGS__)
#define XELOG_UI_I(fmt, ...) XELOGCORE2(xe::LogLevel::Info, xe::LogCategory::UI, 'i', fmt, ##__VA_ARGS__)
#define XELOG_UI_D(fmt, ...) XELOGCORE2(xe::LogLevel::Debug, xe::LogCategory::UI, 'd', fmt, ##__VA_ARGS__)
// [ALWAYS_LOG]
#define XELOG_ALWAYS_LOG_E(fmt, ...) XELOGCORE2(xe::LogLevel::Error, xe::LogCategory::ALWAYS_LOG, '!', fmt, ##__VA_ARGS__)
#define XELOG_ALWAYS_LOG_W(fmt, ...) XELOGCORE2(xe::LogLevel::Warning, xe::LogCategory::ALWAYS_LOG, 'w', fmt, ##__VA_ARGS__)
#define XELOG_ALWAYS_LOG_I(fmt, ...) XELOGCORE2(xe::LogLevel::Info, xe::LogCategory::ALWAYS_LOG, 'i', fmt, ##__VA_ARGS__)
#define XELOG_ALWAYS_LOG_D(fmt, ...) XELOGCORE2(xe::LogLevel::Debug, xe::LogCategory::ALWAYS_LOG, 'd', fmt, ##__VA_ARGS__)
// -- END NEW LOGGING FUNCTIONS --

}  // namespace xe

#endif  // XENIA_BASE_LOGGING_H_
