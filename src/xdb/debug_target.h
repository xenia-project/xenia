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

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <xdb/cursor.h>
#include <xenia/kernel/fs/filesystem.h>

namespace xdb {

class DebugTarget {
 public:
  virtual ~DebugTarget() = default;

  xe::kernel::fs::FileSystem* file_system() const { return file_system_.get(); }

  virtual std::unique_ptr<Cursor> CreateCursor() = 0;

  Module* GetModule(uint16_t module_id);
  Thread* GetThread(uint16_t thread_id);

 protected:
  DebugTarget() = default;

  bool InitializeFileSystem(const std::wstring& path);

  std::unique_ptr<xe::kernel::fs::FileSystem> file_system_;

  std::mutex object_lock_;
  std::vector<std::unique_ptr<Module>> modules_;
  std::vector<std::unique_ptr<Thread>> threads_;
  std::unordered_map<uint16_t, Module*> module_map_;
  std::unordered_map<uint16_t, Thread*> thread_map_;
};

}  // namespace xdb

#endif  // XDB_DEBUG_TARGET_H_
