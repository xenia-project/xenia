/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_VFS_ENTRY_H_
#define XENIA_VFS_ENTRY_H_

#include <memory>
#include <string>

#include "xenia/base/mapped_memory.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
class KernelState;
class XFile;
struct X_FILE_NETWORK_OPEN_INFORMATION;
class X_FILE_DIRECTORY_INFORMATION;
}  // namespace kernel
}  // namespace xe

namespace xe {
namespace vfs {

using namespace xe::kernel;

class Device;

enum class Mode {
  READ,
  READ_WRITE,
  READ_APPEND,
};

class Entry {
 public:
  Entry(Device* device, const std::string& path);
  virtual ~Entry();

  Device* device() const { return device_; }
  const std::string& path() const { return path_; }
  const std::string& absolute_path() const { return absolute_path_; }
  const std::string& name() const { return name_; }

  bool is_read_only() const;

  virtual X_STATUS QueryInfo(X_FILE_NETWORK_OPEN_INFORMATION* out_info) = 0;
  virtual X_STATUS QueryDirectory(X_FILE_DIRECTORY_INFORMATION* out_info,
                                  size_t length, const char* file_name,
                                  bool restart) = 0;

  virtual X_STATUS Open(KernelState* kernel_state, Mode mode, bool async,
                        XFile** out_file) = 0;

  virtual bool can_map() const { return false; }
  virtual std::unique_ptr<MappedMemory> OpenMapped(MappedMemory::Mode mode,
                                                   size_t offset = 0,
                                                   size_t length = 0) {
    return nullptr;
  }

 private:
  Device* device_;
  std::string path_;
  std::string absolute_path_;
  std::string name_;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_ENTRY_H_
