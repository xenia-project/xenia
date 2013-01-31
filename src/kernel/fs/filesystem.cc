/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/fs/filesystem.h>

#include "kernel/fs/devices/disc_image_device.h"
#include "kernel/fs/devices/local_directory_device.h"


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::fs;


FileSystem::FileSystem(xe_pal_ref pal) {
  pal_ = xe_pal_retain(pal);
}

FileSystem::~FileSystem() {
  xe_pal_release(pal_);
}

int FileSystem::RegisterDevice(const char* path, Device* device) {
  return 1;
}

int FileSystem::RegisterLocalDirectoryDevice(
    const char* path, const xechar_t* local_path) {
  Device* device = new LocalDirectoryDevice(pal_, local_path);
  return RegisterDevice(path, device);
}

int FileSystem::RegisterDiscImageDevice(
    const char* path, const xechar_t* local_path) {
  Device* device = new DiscImageDevice(pal_, local_path);
  return RegisterDevice(path, device);
}

int FileSystem::CreateSymbolicLink(const char* path, const char* target) {
  return 1;
}

int FileSystem::DeleteSymbolicLink(const char* path) {
  return 1;
}

Entry* FileSystem::ResolvePath(const char* path) {
  return NULL;
}
