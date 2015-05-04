/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/emulator.h"

#include "xenia/apu/apu.h"
#include "xenia/base/assert.h"
#include "xenia/base/string.h"
#include "xenia/cpu/cpu.h"
#include "xenia/gpu/gpu.h"
#include "xenia/hid/hid.h"
#include "xenia/kernel/kernel.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/modules.h"
#include "xenia/kernel/fs/filesystem.h"
#include "xenia/memory.h"
#include "xenia/ui/main_window.h"

namespace xe {

using namespace xe::apu;
using namespace xe::cpu;
using namespace xe::gpu;
using namespace xe::hid;
using namespace xe::kernel;
using namespace xe::kernel::fs;
using namespace xe::ui;

Emulator::Emulator(const std::wstring& command_line)
    : command_line_(command_line) {}

Emulator::~Emulator() {
  // Note that we delete things in the reverse order they were initialized.

  xam_.reset();
  xboxkrnl_.reset();
  kernel_state_.reset();

  file_system_.reset();

  input_system_.reset();

  // Give the systems time to shutdown before we delete them.
  graphics_system_->Shutdown();
  audio_system_->Shutdown();
  graphics_system_.reset();
  audio_system_.reset();

  processor_.reset();

  export_resolver_.reset();

  // Kill the window last, as until the graphics system/etc is dead it's needed.
  main_window_.reset();
}

X_STATUS Emulator::Setup() {
  X_STATUS result = X_STATUS_UNSUCCESSFUL;

  // Create the main window. Other parts will hook into this.
  main_window_ = std::make_unique<ui::MainWindow>(this);
  main_window_->Start();

  // Create memory system first, as it is required for other systems.
  memory_ = std::make_unique<Memory>();
  result = memory_->Initialize();
  if (result) {
    return result;
  }

  // Shared export resolver used to attach and query for HLE exports.
  export_resolver_ = std::make_unique<xe::cpu::ExportResolver>();

  // Initialize the CPU.
  processor_ =
      std::make_unique<Processor>(memory_.get(), export_resolver_.get());

  // Initialize the APU.
  audio_system_ = std::move(xe::apu::Create(this));
  if (!audio_system_) {
    return X_STATUS_NOT_IMPLEMENTED;
  }

  // Initialize the GPU.
  graphics_system_ = std::move(xe::gpu::Create());
  if (!graphics_system_) {
    return X_STATUS_NOT_IMPLEMENTED;
  }

  // Initialize the HID.
  input_system_ = std::move(xe::hid::Create(this));
  if (!input_system_) {
    return X_STATUS_NOT_IMPLEMENTED;
  }

  // Setup the core components.
  result = processor_->Setup();
  if (result) {
    return result;
  }
  result = audio_system_->Setup();
  if (result) {
    return result;
  }
  result = graphics_system_->Setup(processor_.get(), main_window_->loop(),
                                   main_window_.get());
  if (result) {
    return result;
  }
  result = input_system_->Setup();
  if (result) {
    return result;
  }

  // Bring up the virtual filesystem used by the kernel.
  file_system_ = std::make_unique<FileSystem>();

  // Shared kernel state.
  kernel_state_ = std::make_unique<KernelState>(this);

  // HLE kernel modules.
  xboxkrnl_ = std::make_unique<XboxkrnlModule>(this, kernel_state_.get());
  xam_ = std::make_unique<XamModule>(this, kernel_state_.get());

  return result;
}

X_STATUS Emulator::LaunchXexFile(const std::wstring& path) {
  // We create a virtual filesystem pointing to its directory and symlink
  // that to the game filesystem.
  // e.g., /my/files/foo.xex will get a local fs at:
  // \\Device\\Harddisk0\\Partition1
  // and then get that symlinked to game:\, so
  // -> game:\foo.xex

  int result_code =
      file_system_->InitializeFromPath(FileSystemType::XEX_FILE, path);
  if (result_code) {
    return X_STATUS_INVALID_PARAMETER;
  }

  // Get just the filename (foo.xex).
  std::wstring file_name;
  auto last_slash = path.find_last_of(xe::path_separator);
  if (last_slash == std::string::npos) {
    // No slash found, whole thing is a file.
    file_name = path;
  } else {
    // Skip slash.
    file_name = path.substr(last_slash + 1);
  }

  // Launch the game.
  std::string fs_path = "game:\\" + xe::to_string(file_name);
  return CompleteLaunch(path, fs_path);
}

X_STATUS Emulator::LaunchDiscImage(const std::wstring& path) {
  int result_code =
      file_system_->InitializeFromPath(FileSystemType::DISC_IMAGE, path);
  if (result_code) {
    return X_STATUS_INVALID_PARAMETER;
  }

  // Launch the game.
  return CompleteLaunch(path, "game:\\default.xex");
}

X_STATUS Emulator::LaunchSTFSTitle(const std::wstring& path) {
  int result_code =
      file_system_->InitializeFromPath(FileSystemType::STFS_TITLE, path);
  if (result_code) {
    return X_STATUS_INVALID_PARAMETER;
  }

  // Launch the game.
  return CompleteLaunch(path, "game:\\default.xex");
}

X_STATUS Emulator::CompleteLaunch(const std::wstring& path,
                                  const std::string& module_path) {
  return xboxkrnl_->LaunchModule(module_path.c_str());
}

}  // namespace xe
