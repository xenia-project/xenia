/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_PRIVATE_H_
#define XENIA_KERNEL_XBOXKRNL_PRIVATE_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/kernel/xboxkrnl_ordinals.h>


namespace xe {
namespace kernel {

class KernelState;

namespace xboxkrnl {
// Registration functions, one per file.
void RegisterAudioExports(ExportResolver* export_resolver, KernelState* state);
void RegisterDebugExports(ExportResolver* export_resolver, KernelState* state);
void RegisterHalExports(ExportResolver* export_resolver, KernelState* state);
void RegisterIoExports(ExportResolver* export_resolver, KernelState* state);
void RegisterMemoryExports(ExportResolver* export_resolver, KernelState* state);
void RegisterMiscExports(ExportResolver* export_resolver, KernelState* state);
void RegisterModuleExports(ExportResolver* export_resolver, KernelState* state);
void RegisterNtExports(ExportResolver* export_resolver, KernelState* state);
void RegisterObExports(ExportResolver* export_resolver, KernelState* state);
void RegisterRtlExports(ExportResolver* export_resolver, KernelState* state);
void RegisterStringExports(ExportResolver* export_resolver, KernelState* state);
void RegisterThreadingExports(ExportResolver* export_resolver,
                              KernelState* state);
void RegisterUsbcamExports(ExportResolver* export_resolver, KernelState* state);
void RegisterVideoExports(ExportResolver* export_resolver, KernelState* state);
}  // namespace xboxkrnl

}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_XBOXKRNL_PRIVATE_H_
