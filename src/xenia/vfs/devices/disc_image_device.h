/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_VFS_DEVICES_DISC_IMAGE_DEVICE_H_
#define XENIA_VFS_DEVICES_DISC_IMAGE_DEVICE_H_

#include <memory>
#include <string>

#include "xenia/base/mapped_memory.h"
#include "xenia/vfs/device.h"

namespace xe {
namespace vfs {

class GDFX;

class DiscImageDevice : public Device {
 public:
  DiscImageDevice(const std::string& path, const std::wstring& local_path);
  ~DiscImageDevice() override;

  int Init();

  std::unique_ptr<Entry> ResolvePath(const char* path) override;

 private:
  std::wstring local_path_;
  std::unique_ptr<MappedMemory> mmap_;
  GDFX* gdfx_;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_DEVICES_DISC_IMAGE_DEVICE_H_
