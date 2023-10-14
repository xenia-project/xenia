/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Xenia Canary. All rights reserved. * Released under the BSD
 *license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_XBOXKRNL_OB_H_
#define XENIA_KERNEL_XBOXKRNL_XBOXKRNL_OB_H_

#include "xenia/kernel/util/shim_utils.h"

namespace xe {
namespace kernel {
namespace xboxkrnl {
void xeObDereferenceObject(PPCContext* context, uint32_t native_ptr);

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XBOXKRNL_XBOXKRNL_OB_H_
