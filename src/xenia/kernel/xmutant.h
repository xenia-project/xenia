/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XMUTANT_H_
#define XENIA_KERNEL_XMUTANT_H_

#include "xenia/base/threading.h"
#include "xenia/kernel/xobject.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

class XMutant : public XObject {
 public:
  static const Type kType = kTypeMutant;

  explicit XMutant(KernelState* kernel_state);
  ~XMutant() override;

  void Initialize(bool initial_owner);
  void InitializeNative(void* native_ptr, X_DISPATCH_HEADER* header);

  X_STATUS ReleaseMutant(uint32_t priority_increment, bool abandon, bool wait);

  xe::threading::WaitHandle* GetWaitHandle() override { return mutant_.get(); }

 private:
  std::unique_ptr<xe::threading::Mutant> mutant_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XMUTANT_H_
