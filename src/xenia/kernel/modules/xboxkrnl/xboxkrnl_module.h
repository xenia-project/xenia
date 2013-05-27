/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_MODULES_XBOXKRNL_MODULE_IMPL_H_
#define XENIA_KERNEL_MODULES_XBOXKRNL_MODULE_IMPL_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/kernel/xbox.h>


namespace xe {
namespace kernel {
namespace xboxkrnl {


int xeXexCheckExecutablePriviledge(uint32_t privilege);

int xeXexGetModuleHandle(const char* module_name,
                         X_HANDLE* module_handle_ptr);


}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_MODULES_XBOXKRNL_MODULE_IMPL_H_
