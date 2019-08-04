/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_XBOXKRNL_PRIVATE_H_
#define XENIA_KERNEL_XBOXKRNL_XBOXKRNL_PRIVATE_H_

#include "xenia/cpu/export_resolver.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_ordinals.h"

namespace xe {
namespace kernel {
namespace xboxkrnl {

xe::cpu::Export* RegisterExport_xboxkrnl(xe::cpu::Export* export_entry);

// Registration functions, one per file.
void RegisterAudioExports(xe::cpu::ExportResolver* export_resolver,
                          KernelState* kernel_state);
void RegisterAudioXmaExports(xe::cpu::ExportResolver* export_resolver,
                             KernelState* kernel_state);
void RegisterCryptExports(xe::cpu::ExportResolver* export_resolver,
                          KernelState* kernel_state);
void RegisterDebugExports(xe::cpu::ExportResolver* export_resolver,
                          KernelState* kernel_state);
void RegisterErrorExports(xe::cpu::ExportResolver* export_resolver,
                          KernelState* kernel_state);
void RegisterHalExports(xe::cpu::ExportResolver* export_resolver,
                        KernelState* kernel_state);
void RegisterHidExports(xe::cpu::ExportResolver* export_resolver,
                        KernelState* kernel_state);
void RegisterIoExports(xe::cpu::ExportResolver* export_resolver,
                       KernelState* kernel_state);
void RegisterMemoryExports(xe::cpu::ExportResolver* export_resolver,
                           KernelState* kernel_state);
void RegisterMiscExports(xe::cpu::ExportResolver* export_resolver,
                         KernelState* kernel_state);
void RegisterModuleExports(xe::cpu::ExportResolver* export_resolver,
                           KernelState* kernel_state);
void RegisterObExports(xe::cpu::ExportResolver* export_resolver,
                       KernelState* kernel_state);
void RegisterRtlExports(xe::cpu::ExportResolver* export_resolver,
                        KernelState* kernel_state);
void RegisterStringExports(xe::cpu::ExportResolver* export_resolver,
                           KernelState* kernel_state);
void RegisterThreadingExports(xe::cpu::ExportResolver* export_resolver,
                              KernelState* kernel_state);
void RegisterUsbcamExports(xe::cpu::ExportResolver* export_resolver,
                           KernelState* kernel_state);
void RegisterVideoExports(xe::cpu::ExportResolver* export_resolver,
                          KernelState* kernel_state);

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XBOXKRNL_XBOXKRNL_PRIVATE_H_
