/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace util {

const uint8_t* GetXeKey(uint32_t idx, bool devkit = false);

void HmacSha(const uint8_t* key, uint32_t key_size_in, const uint8_t* inp_1,
             uint32_t inp_1_size, const uint8_t* inp_2, uint32_t inp_2_size,
             const uint8_t* inp_3, uint32_t inp_3_size, uint8_t* out,
             uint32_t out_size);

void RC4(const uint8_t* key, uint32_t key_size_in, const uint8_t* data,
         uint32_t data_size, uint8_t* out, uint32_t out_size);

}  // namespace util
}  // namespace kernel
}  // namespace xe
