/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_FS_ENTRY_H_
#define XENIA_KERNEL_FS_ENTRY_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/xbox.h>


XEDECLARECLASS2(xe, kernel, KernelState);
XEDECLARECLASS2(xe, kernel, XFile);
XEDECLARECLASS2(xe, kernel, XFileInfo);
XEDECLARECLASS2(xe, kernel, XDirectoryInfo);
XEDECLARECLASS2(xe, kernel, XVolumeInfo);
XEDECLARECLASS2(xe, kernel, XFileSystemAttributeInfo);


namespace xe {
namespace kernel {
namespace fs {

class Device;


class MemoryMapping {
public:
  MemoryMapping(uint8_t* address, size_t length);
  virtual ~MemoryMapping();

  uint8_t* address() const { return address_; }
  size_t length() const { return length_; }

private:
  uint8_t*    address_;
  size_t      length_;
};


class Entry {
public:
  enum Type {
    kTypeFile,
    kTypeDirectory,
  };

  Entry(Type type, Device* device, const char* path);
  virtual ~Entry();

  Type type() const { return type_; }
  Device* device() const { return device_; }
  const char* path() const { return path_; }
  const char* absolute_path() const { return absolute_path_; }
  const char* name() const { return name_; }

  virtual X_STATUS QueryInfo(XFileInfo* out_info) = 0;
  virtual X_STATUS QueryDirectory(XDirectoryInfo* out_info,
                                  size_t length, const char* file_name, bool restart) = 0;

  virtual bool can_map() { return false; }
  virtual MemoryMapping* CreateMemoryMapping(
      xe_file_mode file_mode, const size_t offset, const size_t length) {
    return NULL;
  }

  virtual X_STATUS Open(
      KernelState* kernel_state,
      uint32_t desired_access, bool async,
      XFile** out_file) = 0;

private:
  Type      type_;
  Device*   device_;
  char*     path_;
  char*     absolute_path_;
  char*     name_;
};


}  // namespace fs
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_FS_ENTRY_H_
