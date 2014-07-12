/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_LOGGING_H_
#define XENIA_LOGGING_H_

#include <cstdint>

#include <xenia/platform.h>
#include <xenia/config.h>
#include <xenia/string.h>


#if XE_COMPILER_GNUC
#define XE_LOG_LINE_ATTRIBUTE __attribute__ ((format (printf, 5, 6)))
#else
#define XE_LOG_LINE_ATTRIBUTE
#endif  // GNUC
void xe_log_line(const char* file_path, const uint32_t line_number,
                 const char* function_name, const char level_char,
                 const char* fmt, ...) XE_LOG_LINE_ATTRIBUTE;
#undef XE_LOG_LINE_ATTRIBUTE
void xe_handle_fatal(
    const char* file_path, const uint32_t line_number,
    const char* function_name, const char* fmt, ...);

#if XE_OPTION_ENABLE_LOGGING
#define XELOGCORE(level, fmt, ...) xe_log_line( \
    __FILE__, __LINE__, __FUNCTION__, level, \
    fmt, ##__VA_ARGS__)
#else
#define XELOGCORE(level, fmt, ...) XE_EMPTY_MACRO
#endif  // ENABLE_LOGGING

#define XEFATAL(fmt, ...) do { \
    xe_handle_fatal(__FILE__, __LINE__, __FUNCTION__, \
                    fmt, ##__VA_ARGS__); \
  } while (false);

#if XE_OPTION_LOG_ERROR
#define XELOGE(fmt, ...) XELOGCORE('!', fmt, ##__VA_ARGS__)
#else
#define XELOGE(fmt, ...) XE_EMPTY_MACRO
#endif
#if XE_OPTION_LOG_WARNING
#define XELOGW(fmt, ...) XELOGCORE('w', fmt, ##__VA_ARGS__)
#else
#define XELOGW(fmt, ...) XE_EMPTY_MACRO
#endif
#if XE_OPTION_LOG_INFO
#define XELOGI(fmt, ...) XELOGCORE('i', fmt, ##__VA_ARGS__)
#else
#define XELOGI(fmt, ...) XE_EMPTY_MACRO
#endif
#if XE_OPTION_LOG_DEBUG
#define XELOGD(fmt, ...) XELOGCORE('d', fmt, ##__VA_ARGS__)
#else
#define XELOGD(fmt, ...) XE_EMPTY_MACRO
#endif
#if XE_OPTION_LOG_CPU
#define XELOGCPU(fmt, ...) XELOGCORE('C', fmt, ##__VA_ARGS__)
#else
#define XELOGCPU(fmt, ...) XE_EMPTY_MACRO
#endif
#if XE_OPTION_LOG_SDB
#define XELOGSDB(fmt, ...) XELOGCORE('S', fmt, ##__VA_ARGS__)
#else
#define XELOGSDB(fmt, ...) XE_EMPTY_MACRO
#endif
#if XE_OPTION_LOG_APU
#define XELOGAPU(fmt, ...) XELOGCORE('A', fmt, ##__VA_ARGS__)
#else
#define XELOGAPU(fmt, ...) XE_EMPTY_MACRO
#endif
#if XE_OPTION_LOG_GPU
#define XELOGGPU(fmt, ...) XELOGCORE('G', fmt, ##__VA_ARGS__)
#else
#define XELOGGPU(fmt, ...) XE_EMPTY_MACRO
#endif
#if XE_OPTION_LOG_KERNEL
#define XELOGKERNEL(fmt, ...) XELOGCORE('K', fmt, ##__VA_ARGS__)
#else
#define XELOGKERNEL(fmt, ...) XE_EMPTY_MACRO
#endif
#if XE_OPTION_LOG_FS
#define XELOGFS(fmt, ...) XELOGCORE('F', fmt, ##__VA_ARGS__)
#else
#define XELOGFS(fmt, ...) XE_EMPTY_MACRO
#endif


#endif  // XENIA_LOGGING_H_
