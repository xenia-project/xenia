/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xdb/debug_target.h>

namespace xdb {

bool DebugTarget::InitializeFileSystem(const std::wstring& path) {
  file_system_.reset(new xe::kernel::fs::FileSystem());

  // Infer the type (stfs/iso/etc) from the path.
  auto file_system_type = file_system_->InferType(path);

  // Setup the file system exactly like the emulator does - this way we can
  // access all the same files.
  if (file_system_->InitializeFromPath(file_system_type, path)) {
    XELOGE("Unable to initialize filesystem from path");
    return false;
  }

  return true;
}

Module* DebugTarget::GetModule(uint16_t module_id) {
  std::lock_guard<std::mutex> lock(object_lock_);
  auto it = module_map_.find(module_id);
  return it != module_map_.end() ? it->second : nullptr;
}

Thread* DebugTarget::GetThread(uint16_t thread_id) {
  std::lock_guard<std::mutex> lock(object_lock_);
  auto it = thread_map_.find(thread_id);
  return it != thread_map_.end() ? it->second : nullptr;
}

}  // namespace xdb
