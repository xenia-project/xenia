/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CONFIG_H_
#define XENIA_CONFIG_H_


// Enable compile-time and runtime-time assertions.
#define XE_OPTION_ENABLE_ASSERTS        1

// Enable general logging.
#define XE_OPTION_ENABLE_LOGGING        1
#define XE_OPTION_LOG_ERROR             1
#define XE_OPTION_LOG_WARNING           1
#define XE_OPTION_LOG_INFO              1
#define XE_OPTION_LOG_DEBUG             1
#define XE_OPTION_LOG_CPU               1
#define XE_OPTION_LOG_SDB               0
#define XE_OPTION_LOG_APU               1
#define XE_OPTION_LOG_GPU               1
#define XE_OPTION_LOG_KERNEL            1
#define XE_OPTION_LOG_FS                1


// TODO(benvanik): make this a runtime option
#define XE_OPTION_OPTIMIZED             0


#endif  // XENIA_CONFIG_H_

