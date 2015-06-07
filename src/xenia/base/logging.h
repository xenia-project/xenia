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

#include "xenia/base/string.h"

namespace xe {

#define XE_OPTION_ENABLE_LOGGING 1
#define XE_OPTION_LOG_ERROR 1
#define XE_OPTION_LOG_WARNING 1
#define XE_OPTION_LOG_INFO 1
#define XE_OPTION_LOG_DEBUG 1
#define XE_OPTION_LOG_CPU 1
#define XE_OPTION_LOG_APU 1
#define XE_OPTION_LOG_GPU 1
#define XE_OPTION_LOG_KERNEL 1
#define XE_OPTION_LOG_FS 1

#define XE_EMPTY_MACRO \
  do {                 \
  } while (false)

#if XE_COMPILER_GNUC
#define XE_LOG_LINE_ATTRIBUTE __attribute__((format(printf, 5, 6)))
#else
#define XE_LOG_LINE_ATTRIBUTE
#endif  // XE_COMPILER_GNUC
void log_line(const char level_char, const char* fmt,
              ...) XE_LOG_LINE_ATTRIBUTE;
#undef XE_LOG_LINE_ATTRIBUTE

void handle_fatal(const char* fmt, ...);

#if XE_OPTION_ENABLE_LOGGING
#define XELOGCORE(level, fmt, ...) xe::log_line(level, fmt, ##__VA_ARGS__)
#else
#define XELOGCORE(level, fmt, ...) XE_EMPTY_MACRO
#endif  // ENABLE_LOGGING

#define XEFATAL(fmt, ...)                 \
  do {                                    \
    xe::handle_fatal(fmt, ##__VA_ARGS__); \
  } while (false)

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

}  // namespace xe

#endif  // XENIA_LOGGING_H_
