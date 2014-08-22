/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef POLY_LOGGING_H_
#define POLY_LOGGING_H_

#include <cstdint>

#include <poly/string.h>

namespace poly {

#define POLY_OPTION_ENABLE_LOGGING 1
#define POLY_OPTION_LOG_ERROR 1
#define POLY_OPTION_LOG_WARNING 1
#define POLY_OPTION_LOG_INFO 1
#define POLY_OPTION_LOG_DEBUG 1

#define POLY_EMPTY_MACRO \
  do {                   \
  } while (false)

#if XE_COMPILER_GNUC
#define POLY_LOG_LINE_ATTRIBUTE __attribute__((format(printf, 5, 6)))
#else
#define POLY_LOG_LINE_ATTRIBUTE
#endif  // GNUC
void log_line(const char* file_path, const uint32_t line_number,
              const char level_char, const char* fmt,
              ...) POLY_LOG_LINE_ATTRIBUTE;
#undef POLY_LOG_LINE_ATTRIBUTE

void handle_fatal(const char* file_path, const uint32_t line_number,
                  const char* fmt, ...);

#if POLY_OPTION_ENABLE_LOGGING
#define PLOGCORE(level, fmt, ...) \
  poly::log_line(__FILE__, __LINE__, level, fmt, ##__VA_ARGS__)
#else
#define PLOGCORE(level, fmt, ...) POLY_EMPTY_MACRO
#endif  // ENABLE_LOGGING

#define PFATAL(fmt, ...)                                        \
  do {                                                          \
    poly::handle_fatal(__FILE__, __LINE__, fmt, ##__VA_ARGS__); \
  } while (false)

#if POLY_OPTION_LOG_ERROR
#define PLOGE(fmt, ...) PLOGCORE('!', fmt, ##__VA_ARGS__)
#else
#define PLOGE(fmt, ...) POLY_EMPTY_MACRO
#endif
#if POLY_OPTION_LOG_WARNING
#define PLOGW(fmt, ...) PLOGCORE('w', fmt, ##__VA_ARGS__)
#else
#define PLOGW(fmt, ...) POLY_EMPTY_MACRO
#endif
#if POLY_OPTION_LOG_INFO
#define PLOGI(fmt, ...) PLOGCORE('i', fmt, ##__VA_ARGS__)
#else
#define PLOGI(fmt, ...) POLY_EMPTY_MACRO
#endif
#if POLY_OPTION_LOG_DEBUG
#define PLOGD(fmt, ...) PLOGCORE('d', fmt, ##__VA_ARGS__)
#else
#define PLOGD(fmt, ...) POLY_EMPTY_MACRO
#endif

}  // namespace poly

#endif  // POLY_LOGGING_H_
