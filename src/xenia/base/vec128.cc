/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cstddef>
#include <string>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/math.h"
#include "xenia/base/platform.h"
#include "xenia/base/vec128.h"

namespace xe {

std::string to_string(const vec128_t& value) {
  return fmt::format("({}, {}, {}, {})", value.x, value.y, value.z, value.w);
}

}  // namespace xe
