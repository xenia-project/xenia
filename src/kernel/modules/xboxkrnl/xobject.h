/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_MODULES_XBOXKRNL_XOBJECT_H_
#define XENIA_KERNEL_MODULES_XBOXKRNL_XOBJECT_H_

#include "kernel/modules/xboxkrnl/kernel_state.h"

#include <xenia/kernel/xbox.h>


namespace xe {
namespace kernel {
  class Runtime;
}
}


namespace xe {
namespace kernel {
namespace xboxkrnl {


class XObject {
public:
  enum Type {
    kTypeModule   = 0x00000001,
    kTypeThread   = 0x00000002,
  };

  XObject(KernelState* kernel_state, Type type);
  virtual ~XObject();

  KernelState* kernel_state();

  Type type();
  X_HANDLE handle();

  void Retain();
  void Release();

protected:
  Runtime* runtime();
  xe_memory_ref memory(); // unretained

private:
  KernelState*  kernel_state_;

  volatile int32_t ref_count_;

  Type          type_;
  X_HANDLE      handle_;
};


}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_MODULES_XBOXKRNL_XOBJECT_H_
