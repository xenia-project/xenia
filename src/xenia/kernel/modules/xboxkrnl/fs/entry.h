/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_MODULES_XBOXKRNL_FS_ENTRY_H_
#define XENIA_KERNEL_MODULES_XBOXKRNL_FS_ENTRY_H_

#include <xenia/common.h>
#include <xenia/core.h>


namespace xe {
namespace kernel {
namespace xboxkrnl {
namespace fs {


class Device;


class Entry {
public:
  enum Type {
    kTypeFile,
    kTypeDirectory,
  };

  Entry(Type type, Device* device, const char* path);
  virtual ~Entry();

  Type type();
  Device* device();
  const char* path();
  const char* name();

private:
  Type      type_;
  Device*   device_;
  char*     path_;
  char*     name_;
};


class MemoryMapping {
public:
  MemoryMapping(uint8_t* address, size_t length);
  virtual ~MemoryMapping();

  uint8_t* address();
  size_t length();

private:
  uint8_t*    address_;
  size_t      length_;
};


class FileEntry : public Entry {
public:
  FileEntry(Device* device, const char* path);
  virtual ~FileEntry();

  //virtual void Query() = 0;

  virtual MemoryMapping* CreateMemoryMapping(
      xe_file_mode file_mode, const size_t offset, const size_t length) = 0;
};


class DirectoryEntry : public Entry {
public:
  DirectoryEntry(Device* device, const char* path);
  virtual ~DirectoryEntry();

  //virtual void Query() = 0;
};


}  // namespace fs
}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_MODULES_XBOXKRNL_FS_ENTRY_H_
