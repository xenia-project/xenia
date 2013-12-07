/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_XENON_RUNTIME_H_
#define XENIA_CPU_XENON_RUNTIME_H_

#include <alloy/runtime/runtime.h>

#include <xenia/core.h>
#include <xenia/cpu/xenon_thread_state.h>

XEDECLARECLASS1(xe, ExportResolver);


namespace xe {
namespace cpu {

class XenonThreadState;


class XenonRuntime : public alloy::runtime::Runtime {
public:
  XenonRuntime(Memory* memory, ExportResolver* export_resolver);
  virtual ~XenonRuntime();

  ExportResolver* export_resolver() const { return export_resolver_; }

  virtual int Initialize(alloy::backend::Backend* backend = 0);

private:
  ExportResolver* export_resolver_;
};


}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_XENON_RUNTIME_H_
