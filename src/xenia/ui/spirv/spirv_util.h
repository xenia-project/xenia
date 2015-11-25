/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_SPIRV_SPIRV_UTIL_H_
#define XENIA_UI_SPIRV_SPIRV_UTIL_H_

#include "third_party/spirv/GLSL.std.450.h"
#include "third_party/spirv/spirv.h"

// Forward declarations from SPIRV-Tools so we don't pollute /so/ much.
struct spv_binary_t;
typedef spv_binary_t* spv_binary;
struct spv_context_t;
typedef spv_context_t* spv_context;
struct spv_diagnostic_t;
typedef spv_diagnostic_t* spv_diagnostic;
struct spv_text_t;
typedef spv_text_t* spv_text;

namespace xe {
namespace ui {
namespace spirv {

//

}  // namespace spirv
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_SPIRV_SPIRV_UTIL_H_
