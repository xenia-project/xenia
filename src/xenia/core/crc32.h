/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CORE_CRC32_H_
#define XENIA_CORE_CRC32_H_

#include <xenia/common.h>


uint32_t xe_crc32(const void* data, size_t length, uint32_t previous_crc = 0);


#endif  // XENIA_CORE_CRC32_H_
