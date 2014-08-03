/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_XENUMERATOR_H_
#define XENIA_KERNEL_XBOXKRNL_XENUMERATOR_H_

#include <xenia/kernel/xobject.h>

#include <xenia/xbox.h>


namespace xe {
namespace kernel {


class XEnumerator : public XObject {
public:
  XEnumerator(KernelState* kernel_state);
  virtual ~XEnumerator();

  void Initialize();

private:
};


}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_XBOXKRNL_XENUMERATOR_H_
