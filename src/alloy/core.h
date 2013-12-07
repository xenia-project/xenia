/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_CORE_H_
#define ALLOY_CORE_H_

// TODO(benvanik): move the common stuff into here?
#include <xenia/common.h>

#include <alloy/arena.h>
#include <alloy/mutex.h>
#include <alloy/string_buffer.h>


namespace alloy {

typedef struct XECACHEALIGN vec128_s {
  union {
    struct {
      float     x;
      float     y;
      float     z;
      float     w;
    };
    struct {
      uint32_t  ix;
      uint32_t  iy;
      uint32_t  iz;
      uint32_t  iw;
    };
    float       f4[4];
    uint32_t    i4[4];
    uint16_t    s8[8];
    uint8_t     b16[16];
    struct {
      uint64_t  low;
      uint64_t  high;
    };
  };
} vec128_t;

}  // namespace alloy


#endif  // ALLOY_CORE_H_
