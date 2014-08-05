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

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/kernel/xam_ordinals.h>


namespace xe {
namespace kernel {

class KernelState;

namespace xam {
// Registration functions, one per file.
void RegisterContentExports(ExportResolver* export_resolver, KernelState* state);
void RegisterInfoExports(ExportResolver* export_resolver, KernelState* state);
void RegisterInputExports(ExportResolver* export_resolver, KernelState* state);
void RegisterMsgExports(ExportResolver* export_resolver, KernelState* state);
void RegisterNetExports(ExportResolver* export_resolver, KernelState* state);
void RegisterNotifyExports(ExportResolver* export_resolver, KernelState* state);
void RegisterUIExports(ExportResolver* export_resolver, KernelState* state);
void RegisterUserExports(ExportResolver* export_resolver, KernelState* state);
void RegisterVideoExports(ExportResolver* export_resolver, KernelState* state);
void RegisterVoiceExports(ExportResolver* export_resolver, KernelState* state);
}  // namespace xam

}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_XAM_PRIVATE_H_
