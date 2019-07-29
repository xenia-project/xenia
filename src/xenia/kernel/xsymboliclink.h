/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XSYMBOLICLINK_H_
#define XENIA_KERNEL_XSYMBOLICLINK_H_

#include <memory>
#include <unordered_map>

#include "xenia/base/mutex.h"
#include "xenia/base/threading.h"
#include "xenia/kernel/xobject.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

class XSymbolicLink : public XObject {
 public:
  static const Type kType = kTypeSymbolicLink;

  explicit XSymbolicLink(KernelState* kernel_state);
  ~XSymbolicLink() override;

  void Initialize(const std::string& path, const std::string& target);

  bool Save(ByteStream* stream) override;
  static object_ref<XSymbolicLink> Restore(KernelState* kernel_state,
                                           ByteStream* stream);

  const std ::string& path() const { return path_; }
  const std ::string& target() const { return target_; }

 private:
  XSymbolicLink();

  std::string path_;
  std::string target_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XSYMBOLICLINK_H_
