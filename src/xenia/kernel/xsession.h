/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XSESSION_H_
#define XENIA_KERNEL_XSESSION_H_

#include "xenia/base/byte_order.h"
#include "xenia/kernel/xobject.h"

namespace xe {
namespace kernel {
class XSession : public XObject {
 public:
  static const Type kType = Type::Session;

  XSession(KernelState* kernel_state);

  bool Initialize();

 private:
};
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XSESSION_H_