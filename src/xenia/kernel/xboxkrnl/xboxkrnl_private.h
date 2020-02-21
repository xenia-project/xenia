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
#define DECLARE_REGISTER_EXPORTS(n)                                   \
  void Register##n##Exports(xe::cpu::ExportResolver* export_resolver, \
                            KernelState* kernel_state)
DECLARE_REGISTER_EXPORTS(Audio);
DECLARE_REGISTER_EXPORTS(AudioXma);
DECLARE_REGISTER_EXPORTS(Crypt);
DECLARE_REGISTER_EXPORTS(Debug);
DECLARE_REGISTER_EXPORTS(Error);
DECLARE_REGISTER_EXPORTS(Hal);
DECLARE_REGISTER_EXPORTS(Hid);
DECLARE_REGISTER_EXPORTS(Io);
DECLARE_REGISTER_EXPORTS(Memory);
DECLARE_REGISTER_EXPORTS(Misc);
DECLARE_REGISTER_EXPORTS(Module);
DECLARE_REGISTER_EXPORTS(Ob);
DECLARE_REGISTER_EXPORTS(Rtl);
DECLARE_REGISTER_EXPORTS(String);
DECLARE_REGISTER_EXPORTS(Threading);
DECLARE_REGISTER_EXPORTS(Usbcam);
DECLARE_REGISTER_EXPORTS(Video);
DECLARE_REGISTER_EXPORTS(XConfig);
#undef DECLARE_REGISTER_EXPORTS

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XBOXKRNL_XBOXKRNL_PRIVATE_H_
