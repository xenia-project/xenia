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
void xeObSplitName(X_ANSI_STRING input_string,
                   X_ANSI_STRING* leading_path_component,
                   X_ANSI_STRING* remaining_path_components,
                   PPCContext* context);
uint32_t xeObHashObjectName(X_ANSI_STRING* ElementName, PPCContext* context);
uint32_t xeObCreateObject(X_OBJECT_TYPE* object_factory,
                          X_OBJECT_ATTRIBUTES* optional_attributes,
                          uint32_t object_size_sans_headers,
                          uint32_t* out_object, cpu::ppc::PPCContext* context);
}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XBOXKRNL_XBOXKRNL_OB_H_
