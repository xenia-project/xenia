/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xsymboliclink.h"

#include "xenia/base/byte_stream.h"
#include "xenia/kernel/kernel_state.h"

namespace xe {
namespace kernel {

XSymbolicLink::XSymbolicLink(KernelState* kernel_state)
    : XObject(kernel_state, kObjectType), path_(), target_() {}

XSymbolicLink::XSymbolicLink() : XObject(kObjectType), path_(), target_() {}

XSymbolicLink::~XSymbolicLink() {}

void XSymbolicLink::Initialize(const std::string_view path,
                               const std::string_view target) {
  path_ = std::string(path);
  target_ = std::string(target);
  // TODO(gibbed): kernel_state_->RegisterSymbolicLink(this);
}

bool XSymbolicLink::Save(ByteStream* stream) {
  if (!SaveObject(stream)) {
    return false;
  }
  stream->Write(path_);
  stream->Write(target_);
  return true;
}

object_ref<XSymbolicLink> XSymbolicLink::Restore(KernelState* kernel_state,
                                                 ByteStream* stream) {
  auto symlink = new XSymbolicLink();
  symlink->kernel_state_ = kernel_state;
  if (!symlink->RestoreObject(stream)) {
    delete symlink;
    return nullptr;
  }

  auto path = stream->Read<std::string>();
  auto target = stream->Read<std::string>();
  symlink->Initialize(path, target);

  return object_ref<XSymbolicLink>(symlink);
}

}  // namespace kernel
}  // namespace xe
