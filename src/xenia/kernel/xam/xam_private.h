/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_XAM_PRIVATE_H_
#define XENIA_KERNEL_XAM_XAM_PRIVATE_H_

#include "xenia/cpu/export_resolver.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xam/xam_ordinals.h"

namespace xe {
namespace kernel {
namespace xam {

xe::cpu::Export* RegisterExport_xam(xe::cpu::Export* export_entry);

// Registration functions, one per file.
void RegisterAvatarExports(xe::cpu::ExportResolver* export_resolver,
                           KernelState* kernel_state);
void RegisterContentExports(xe::cpu::ExportResolver* export_resolver,
                            KernelState* kernel_state);
void RegisterInfoExports(xe::cpu::ExportResolver* export_resolver,
                         KernelState* kernel_state);
void RegisterInputExports(xe::cpu::ExportResolver* export_resolver,
                          KernelState* kernel_state);
void RegisterMsgExports(xe::cpu::ExportResolver* export_resolver,
                        KernelState* kernel_state);
void RegisterNetExports(xe::cpu::ExportResolver* export_resolver,
                        KernelState* kernel_state);
void RegisterNotifyExports(xe::cpu::ExportResolver* export_resolver,
                           KernelState* kernel_state);
void RegisterNuiExports(xe::cpu::ExportResolver* export_resolver,
                        KernelState* kernel_state);
void RegisterUIExports(xe::cpu::ExportResolver* export_resolver,
                       KernelState* kernel_state);
void RegisterUserExports(xe::cpu::ExportResolver* export_resolver,
                         KernelState* kernel_state);
void RegisterVideoExports(xe::cpu::ExportResolver* export_resolver,
                          KernelState* kernel_state);
void RegisterVoiceExports(xe::cpu::ExportResolver* export_resolver,
                          KernelState* kernel_state);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_XAM_PRIVATE_H_
