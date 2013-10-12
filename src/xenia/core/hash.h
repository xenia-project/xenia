/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CORE_HASH_H_
#define XENIA_CORE_HASH_H_

#include <xenia/common.h>


uint64_t xe_hash64(const void* data, size_t length, uint64_t seed = 0);


#endif  // XENIA_CORE_HASH_H_
