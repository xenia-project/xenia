/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_MODULES_H_
#define XENIA_KERNEL_XBOXKRNL_MODULES_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/xbox.h>


namespace xe {
namespace kernel {


X_STATUS xeExGetXConfigSetting(
    uint16_t category, uint16_t setting, void* buffer, uint16_t buffer_size,
    uint16_t* required_size);

int xeXexCheckExecutablePriviledge(uint32_t privilege);

int xeXexGetModuleHandle(const char* module_name,
                         X_HANDLE* module_handle_ptr);


}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_XBOXKRNL_MODULES_H_
