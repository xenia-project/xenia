/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
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
#define DECLARE_REGISTER_EXPORTS(n)                                   \
  void Register##n##Exports(xe::cpu::ExportResolver* export_resolver, \
                            KernelState* kernel_state)
DECLARE_REGISTER_EXPORTS(Avatar);
DECLARE_REGISTER_EXPORTS(Content);
DECLARE_REGISTER_EXPORTS(Info);
DECLARE_REGISTER_EXPORTS(Input);
DECLARE_REGISTER_EXPORTS(Locale);
DECLARE_REGISTER_EXPORTS(Msg);
DECLARE_REGISTER_EXPORTS(Net);
DECLARE_REGISTER_EXPORTS(Notify);
DECLARE_REGISTER_EXPORTS(Nui);
DECLARE_REGISTER_EXPORTS(UI);
DECLARE_REGISTER_EXPORTS(User);
DECLARE_REGISTER_EXPORTS(Video);
DECLARE_REGISTER_EXPORTS(Voice);
#undef DECLARE_REGISTER_EXPORTS

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_XAM_PRIVATE_H_
