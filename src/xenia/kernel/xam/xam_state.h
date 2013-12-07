/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_XAM_STATE_H_
#define XENIA_KERNEL_XAM_XAM_STATE_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/export_resolver.h>
#include <xenia/kernel/kernel_module.h>


namespace xe {
namespace kernel {
namespace xam {


class XamState {
public:
  XamState(Emulator* emulator);
  ~XamState();

  Emulator* emulator() const { return emulator_; }
  Memory* memory() const { return memory_; }

private:
  Emulator*       emulator_;
  Memory*         memory_;
  ExportResolver* export_resolver_;
};


}  // namespace xam
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_XAM_XAM_STATE_H_
