/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XDB_DEBUG_TARGET_H_
#define XDB_DEBUG_TARGET_H_

#include <xenia/kernel/fs/filesystem.h>

namespace xdb {

class DebugTarget {
 public:
  virtual ~DebugTarget() = default;

  xe::kernel::fs::FileSystem* file_system() const { return file_system_.get(); }

 protected:
  DebugTarget() = default;

  bool InitializeFileSystem(const std::wstring& path);

  std::unique_ptr<xe::kernel::fs::FileSystem> file_system_;
};

}  // namespace xdb

#endif  // XDB_DEBUG_TARGET_H_
