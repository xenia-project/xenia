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


namespace xe {
namespace kernel {
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


class FileEntry : public Entry {
public:
  FileEntry(Device* device, const char* path);
  virtual ~FileEntry();

  //virtual void Query();
};


class DirectoryEntry : public Entry {
public:
  DirectoryEntry(Device* device, const char* path);
  virtual ~DirectoryEntry();

  //virtual void Query();
};


}  // namespace fs
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_FS_ENTRY_H_
