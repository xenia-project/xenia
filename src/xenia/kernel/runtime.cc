/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/runtime.h>

#include <xenia/kernel/modules/modules.h>
#include <xenia/kernel/modules/xboxkrnl/fs/filesystem.h>


using namespace xe;
using namespace xe::cpu;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl::fs;


Runtime::Runtime(shared_ptr<cpu::Processor> processor,
                 const xechar_t* command_line) {
  memory_ = processor->memory();
  processor_ = processor;
  XEIGNORE(xestrcpy(command_line_, XECOUNT(command_line_), command_line));
  export_resolver_ = shared_ptr<ExportResolver>(new ExportResolver());

  filesystem_ = shared_ptr<FileSystem>(new FileSystem());

  xboxkrnl_ = auto_ptr<xboxkrnl::XboxkrnlModule>(
      new xboxkrnl::XboxkrnlModule(this));
  xam_ = auto_ptr<xam::XamModule>(
      new xam::XamModule(this));
}

Runtime::~Runtime() {
  xe_memory_release(memory_);
}

const xechar_t* Runtime::command_line() {
  return command_line_;
}

xe_memory_ref Runtime::memory() {
  return xe_memory_retain(memory_);
}

shared_ptr<cpu::Processor> Runtime::processor() {
  return processor_;
}

shared_ptr<ExportResolver> Runtime::export_resolver() {
  return export_resolver_;
}

shared_ptr<FileSystem> Runtime::filesystem() {
  return filesystem_;
}

int Runtime::LaunchXexFile(const xechar_t* path) {
  // We create a virtual filesystem pointing to its directory and symlink
  // that to the game filesystem.
  // e.g., /my/files/foo.xex will get a local fs at:
  // \\Device\\Harddisk0\\Partition1
  // and then get that symlinked to game:\, so
  // -> game:\foo.xex

  int result_code = 0;

  // Get just the filename (foo.xex).
  const xechar_t* file_name = xestrrchr(path, XE_PATH_SEPARATOR);
  if (file_name) {
    // Skip slash.
    file_name++;
  } else {
    // No slash found, whole thing is a file.
    file_name = path;
  }

  // Get the parent path of the file.
  xechar_t parent_path[XE_MAX_PATH];
  XEIGNORE(xestrcpy(parent_path, XECOUNT(parent_path), path));
  parent_path[file_name - path] = 0;

  // Register the local directory in the virtual filesystem.
  result_code = filesystem_->RegisterHostPathDevice(
      "\\Device\\Harddisk1\\Partition0", parent_path);
  if (result_code) {
    XELOGE("Unable to mount local directory");
    return result_code;
  }

  // Create symlinks to the device.
  filesystem_->CreateSymbolicLink(
      "game:", "\\Device\\Harddisk1\\Partition0");
  filesystem_->CreateSymbolicLink(
      "d:", "\\Device\\Harddisk1\\Partition0");

  // Get the file name of the module to load from the filesystem.
  char fs_path[XE_MAX_PATH];
  XEIGNORE(xestrcpya(fs_path, XECOUNT(fs_path), "game:\\"));
  char* fs_path_ptr = fs_path + xestrlena(fs_path);
  *fs_path_ptr = 0;
#if XE_WCHAR
  XEIGNORE(xestrnarrow(fs_path_ptr, XECOUNT(fs_path), file_name));
#else
  XEIGNORE(xestrcpya(fs_path_ptr, XECOUNT(fs_path), file_name));
#endif

  // Launch the game.
  return xboxkrnl_->LaunchModule(fs_path);
}

int Runtime::LaunchDiscImage(const xechar_t* path) {
  int result_code = 0;

  // Register the disc image in the virtual filesystem.
  result_code = filesystem_->RegisterDiscImageDevice(
      "\\Device\\Cdrom0", path);
  if (result_code) {
    XELOGE("Unable to mount disc image");
    return result_code;
  }

  // Create symlinks to the device.
  filesystem_->CreateSymbolicLink(
      "game:",
      "\\Device\\Cdrom0");
  filesystem_->CreateSymbolicLink(
      "d:",
      "\\Device\\Cdrom0");

  // Launch the game.
  return xboxkrnl_->LaunchModule("game:\\default.xex");
}
