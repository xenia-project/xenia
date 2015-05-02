/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_PRIVATE_H_
#define XENIA_KERNEL_XAM_PRIVATE_H_

#include "xenia/cpu/export_resolver.h"
#include "xenia/kernel/xam_ordinals.h"

namespace xe {
namespace kernel {

class KernelState;

namespace xam {
// Registration functions, one per file.
void RegisterContentExports(xe::cpu::ExportResolver* export_resolver,
                            KernelState* state);
void RegisterInfoExports(xe::cpu::ExportResolver* export_resolver,
                         KernelState* state);
void RegisterInputExports(xe::cpu::ExportResolver* export_resolver,
                          KernelState* state);
void RegisterMsgExports(xe::cpu::ExportResolver* export_resolver,
                        KernelState* state);
void RegisterNetExports(xe::cpu::ExportResolver* export_resolver,
                        KernelState* state);
void RegisterNotifyExports(xe::cpu::ExportResolver* export_resolver,
                           KernelState* state);
void RegisterUIExports(xe::cpu::ExportResolver* export_resolver,
                       KernelState* state);
void RegisterUserExports(xe::cpu::ExportResolver* export_resolver,
                         KernelState* state);
void RegisterVideoExports(xe::cpu::ExportResolver* export_resolver,
                          KernelState* state);
void RegisterVoiceExports(xe::cpu::ExportResolver* export_resolver,
                          KernelState* state);
}  // namespace xam

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_PRIVATE_H_
