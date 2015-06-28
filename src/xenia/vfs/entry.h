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

#include "xenia/base/filesystem.h"
#include "xenia/base/mapped_memory.h"
#include "xenia/base/string_buffer.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
class KernelState;
class XFile;
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

enum FileAttributeFlags : uint32_t {
  kFileAttributeNone = 0x0000,
  kFileAttributeReadOnly = 0x0001,
  kFileAttributeHidden = 0x0002,
  kFileAttributeSystem = 0x0004,
  kFileAttributeDirectory = 0x0010,
  kFileAttributeArchive = 0x0020,
  kFileAttributeDevice = 0x0040,
  kFileAttributeNormal = 0x0080,
  kFileAttributeTemporary = 0x0100,
  kFileAttributeCompressed = 0x0800,
  kFileAttributeEncrypted = 0x4000,
};

class Entry {
 public:
  virtual ~Entry();

  void Dump(xe::StringBuffer* string_buffer, int indent);

  Device* device() const { return device_; }
  const std::string& path() const { return path_; }
  const std::string& absolute_path() const { return absolute_path_; }
  const std::string& name() const { return name_; }
  uint32_t attributes() const { return attributes_; }
  size_t size() const { return size_; }
  size_t allocation_size() const { return allocation_size_; }
  uint64_t create_timestamp() const { return create_timestamp_; }
  uint64_t access_timestamp() const { return access_timestamp_; }
  uint64_t write_timestamp() const { return write_timestamp_; }

  bool is_read_only() const;

  Entry* GetChild(std::string name);

  size_t child_count() const { return children_.size(); }
  Entry* IterateChildren(const xe::filesystem::WildcardEngine& engine,
                         size_t* current_index);

  virtual X_STATUS Open(KernelState* kernel_state, Mode mode, bool async,
                        XFile** out_file) = 0;

  virtual bool can_map() const { return false; }
  virtual std::unique_ptr<MappedMemory> OpenMapped(MappedMemory::Mode mode,
                                                   size_t offset = 0,
                                                   size_t length = 0) {
    return nullptr;
  }

 protected:
  Entry(Device* device, const std::string& path);

  Device* device_;
  std::string path_;
  std::string absolute_path_;
  std::string name_;
  uint32_t attributes_;  // FileAttributeFlags
  size_t size_;
  size_t allocation_size_;
  uint64_t create_timestamp_;
  uint64_t access_timestamp_;
  uint64_t write_timestamp_;
  std::vector<std::unique_ptr<Entry>> children_;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_ENTRY_H_
