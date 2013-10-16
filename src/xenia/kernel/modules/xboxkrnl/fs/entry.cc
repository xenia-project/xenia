/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/modules/xboxkrnl/fs/entry.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl::fs;


Entry::Entry(Type type, Device* device, const char* path) :
    type_(type),
    device_(device) {
  path_ = xestrdupa(path);
  // TODO(benvanik): last index of \, unless \ at end, then before that
  name_ = NULL;
}

Entry::~Entry() {
  xe_free(path_);
  xe_free(name_);
}

Entry::Type Entry::type() {
  return type_;
}

Device* Entry::device() {
  return device_;
}

const char* Entry::path() {
  return path_;
}

const char* Entry::name() {
  return name_;
}


MemoryMapping::MemoryMapping(uint8_t* address, size_t length) :
    address_(address), length_(length) {
}

MemoryMapping::~MemoryMapping() {
}

uint8_t* MemoryMapping::address() {
  return address_;
}

size_t MemoryMapping::length() {
  return length_;
}


FileEntry::FileEntry(Device* device, const char* path) :
    Entry(kTypeFile, device, path) {
}

FileEntry::~FileEntry() {
}


DirectoryEntry::DirectoryEntry(Device* device, const char* path) :
    Entry(kTypeDirectory, device, path) {
}

DirectoryEntry::~DirectoryEntry() {
}
