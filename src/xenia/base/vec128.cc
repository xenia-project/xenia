/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cstddef>
#include <string>

#include "xenia/base/math.h"
#include "xenia/base/platform.h"
#include "xenia/base/vec128.h"

namespace xe {

inline std::string to_string(const vec128_t& value) {
  char buffer[128];
  std::snprintf(buffer, sizeof(buffer), "(%g, %g, %g, %g)", value.x, value.y,
                value.z, value.w);
  return std::string(buffer);
}

}  // namespace xe
