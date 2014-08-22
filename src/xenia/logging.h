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

#include <poly/logging.h>

#define XE_OPTION_ENABLE_LOGGING        1
#define XE_OPTION_LOG_ERROR             1
#define XE_OPTION_LOG_WARNING           1
#define XE_OPTION_LOG_INFO              1
#define XE_OPTION_LOG_DEBUG             1
#define XE_OPTION_LOG_CPU               1
#define XE_OPTION_LOG_APU               1
#define XE_OPTION_LOG_GPU               1
#define XE_OPTION_LOG_KERNEL            1
#define XE_OPTION_LOG_FS                1

#if XE_OPTION_LOG_ERROR
#define XELOGE PLOGE
#else
#define XELOGE(fmt, ...) POLY_EMPTY_MACRO
#endif
#if XE_OPTION_LOG_WARNING
#define XELOGW PLOGW
#else
#define XELOGW(fmt, ...) POLY_EMPTY_MACRO
#endif
#if XE_OPTION_LOG_INFO
#define XELOGI PLOGI
#else
#define XELOGI(fmt, ...) POLY_EMPTY_MACRO
#endif
#if XE_OPTION_LOG_DEBUG
#define XELOGD PLOGD
#else
#define XELOGD(fmt, ...) POLY_EMPTY_MACRO
#endif
#if XE_OPTION_LOG_CPU
#define XELOGCPU(fmt, ...) PLOGCORE('C', fmt, ##__VA_ARGS__)
#else
#define XELOGCPU(fmt, ...) POLY_EMPTY_MACRO
#endif
#if XE_OPTION_LOG_APU
#define XELOGAPU(fmt, ...) PLOGCORE('A', fmt, ##__VA_ARGS__)
#else
#define XELOGAPU(fmt, ...) POLY_EMPTY_MACRO
#endif
#if XE_OPTION_LOG_GPU
#define XELOGGPU(fmt, ...) PLOGCORE('G', fmt, ##__VA_ARGS__)
#else
#define XELOGGPU(fmt, ...) POLY_EMPTY_MACRO
#endif
#if XE_OPTION_LOG_KERNEL
#define XELOGKERNEL(fmt, ...) PLOGCORE('K', fmt, ##__VA_ARGS__)
#else
#define XELOGKERNEL(fmt, ...) POLY_EMPTY_MACRO
#endif
#if XE_OPTION_LOG_FS
#define XELOGFS(fmt, ...) PLOGCORE('F', fmt, ##__VA_ARGS__)
#else
#define XELOGFS(fmt, ...) POLY_EMPTY_MACRO
#endif


#endif  // XENIA_LOGGING_H_
