/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_VFS_DEVICES_XCONTENT_STFS_CONTAINER_ENTRY_H_
#define XENIA_VFS_DEVICES_XCONTENT_STFS_CONTAINER_ENTRY_H_

#include "xenia/vfs/devices/xcontent_container_entry.h"

namespace xe {
namespace vfs {

class StfsContainerEntry : public XContentContainerEntry {
 public:
  StfsContainerEntry(Device* device, Entry* parent, const std::string_view path,
                     MappedMemory* data);
  ~StfsContainerEntry() override;

  static std::unique_ptr<StfsContainerEntry> Create(Device* device,
                                                    Entry* parent,
                                                    const std::string_view name,
                                                    MappedMemory* data);

  MappedMemory* data() const { return data_; }

  X_STATUS Open(uint32_t desired_access, File** out_file) override;

 private:
  bool DeleteEntryInternal(Entry* entry) override;

  MappedMemory* data_;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_DEVICES_XCONTENT_CONTAINER_ENTRY_H_
