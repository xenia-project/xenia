/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_XBOXKRNL_MODULES_H_
#define XENIA_KERNEL_XBOXKRNL_XBOXKRNL_MODULES_H_

#include "xenia/kernel/util/shim_utils.h"

namespace xe {
namespace kernel {
namespace xboxkrnl {

dword_result_t XexGetModuleHandle(std::string module_name,
                                  xe::be<uint32_t>* hmodule_ptr);

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XBOXKRNL_XBOXKRNL_MODULES_H_
