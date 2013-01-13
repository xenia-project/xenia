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

#include <xenia/config.h>
#include <xenia/string.h>


#if XE_COMPILER(GNUC)
#define XE_LOG_LINE_ATTRIBUTE __attribute__ ((format (printf, 5, 6)))
#else
#define XE_LOG_LINE_ATTRIBUTE
#endif  // GNUC
void xe_log_line(const xechar_t* file_path, const uint32_t line_number,
                 const xechar_t* function_name, const xechar_t level_char,
                 const xechar_t* fmt, ...) XE_LOG_LINE_ATTRIBUTE;
#undef XE_LOG_LINE_ATTRIBUTE

#if XE_OPTION(ENABLE_LOGGING)
#define XELOGCORE(level, fmt, ...) xe_log_line( \
    XE_CURRENT_FILE, XE_CURRENT_LINE, XE_CURRENT_FUNCTION, level, \
    fmt, ##__VA_ARGS__)
#else
#define XELOGCORE(level, fmt, ...) XE_EMPTY_MACRO
#endif  // ENABLE_LOGGING

#if XE_OPTION(LOG_ERROR)
#define XELOGE(fmt, ...) XELOGCORE('!', fmt, ##__VA_ARGS__)
#else
#define XELOGE(fmt, ...) XE_EMPTY_MACRO
#endif
#if XE_OPTION(LOG_WARNING)
#define XELOGW(fmt, ...) XELOGCORE('w', fmt, ##__VA_ARGS__)
#else
#define XELOGW(fmt, ...) XE_EMPTY_MACRO
#endif
#if XE_OPTION(LOG_INFO)
#define XELOGI(fmt, ...) XELOGCORE('i', fmt, ##__VA_ARGS__)
#else
#define XELOGI(fmt, ...) XE_EMPTY_MACRO
#endif
#if XE_OPTION(LOG_DEBUG)
#define XELOGD(fmt, ...) XELOGCORE('d', fmt, ##__VA_ARGS__)
#else
#define XELOGD(fmt, ...) XE_EMPTY_MACRO
#endif
#if XE_OPTION(LOG_CPU)
#define XELOGCPU(fmt, ...) XELOGCORE('C', fmt, ##__VA_ARGS__)
#else
#define XELOGCPU(fmt, ...) XE_EMPTY_MACRO
#endif
#if XE_OPTION(LOG_GPU)
#define XELOGGPU(fmt, ...) XELOGCORE('G', fmt, ##__VA_ARGS__)
#else
#define XELOGGPU(fmt, ...) XE_EMPTY_MACRO
#endif
#if XE_OPTION(LOG_KERNEL)
#define XELOGKERNEL(fmt, ...) XELOGCORE('K', fmt, ##__VA_ARGS__)
#else
#define XELOGKERNEL(fmt, ...) XE_EMPTY_MACRO
#endif


#endif  // XENIA_LOGGING_H_
