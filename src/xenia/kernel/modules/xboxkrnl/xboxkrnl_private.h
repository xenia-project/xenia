/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_MODULES_XBOXKRNL_PRIVATE_H_
#define XENIA_KERNEL_MODULES_XBOXKRNL_PRIVATE_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/kernel/modules/xboxkrnl/xboxkrnl_ordinals.h>


namespace xe {
namespace kernel {
namespace xboxkrnl {

class KernelState;


// This is a global object initialized with the XboxkrnlModule.
// It references the current kernel state object that all kernel methods should
// be using to stash their variables.
extern KernelState* shared_kernel_state_;


// Registration functions, one per file.
void RegisterDebugExports(ExportResolver* export_resolver, KernelState* state);
void RegisterHalExports(ExportResolver* export_resolver, KernelState* state);
void RegisterMemoryExports(ExportResolver* export_resolver, KernelState* state);
void RegisterModuleExports(ExportResolver* export_resolver, KernelState* state);
void RegisterRtlExports(ExportResolver* export_resolver, KernelState* state);
void RegisterThreadingExports(ExportResolver* export_resolver,
                              KernelState* state);
void RegisterVideoExports(ExportResolver* export_resolver, KernelState* state);


}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_MODULES_XBOXKRNL_PRIVATE_H_
