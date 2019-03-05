/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/compiler/passes/conditional_group_subpass.h"

#include "xenia/cpu/compiler/compiler.h"

namespace xe {
namespace cpu {
namespace compiler {
namespace passes {

ConditionalGroupSubpass::ConditionalGroupSubpass() : CompilerPass() {}

ConditionalGroupSubpass::~ConditionalGroupSubpass() = default;

}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe
