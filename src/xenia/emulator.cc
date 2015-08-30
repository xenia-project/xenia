/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/emulator.h"

#include <gflags/gflags.h>

#include "xenia/apu/audio_system.h"
#include "xenia/base/assert.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/hid/input_system.h"
#include "xenia/kernel/kernel.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/modules.h"
#include "xenia/memory.h"
#include "xenia/vfs/virtual_file_system.h"
#include "xenia/vfs/devices/disc_image_device.h"
#include "xenia/vfs/devices/host_path_device.h"
#include "xenia/vfs/devices/stfs_container_device.h"

DEFINE_double(time_scalar, 1.0,
              "Scalar used to speed or slow time (1x, 2x, 1/2x, etc).");

namespace xe {

Emulator::Emulator(const std::wstring& command_line)
    : command_line_(command_line) {}

Emulator::~Emulator() {
  // Note that we delete things in the reverse order they were initialized.

  if (debugger_) {
    // Kill the debugger first, so that we don't have it messing with things.
    debugger_->StopSession();
  }

  // Give the systems time to shutdown before we delete them.
  graphics_system_->Shutdown();
  audio_system_->Shutdown();

  input_system_.reset();
  graphics_system_.reset();
  audio_system_.reset();

  kernel_state_.reset();
  file_system_.reset();

  processor_.reset();

  debugger_.reset();

  export_resolver_.reset();
}

X_STATUS Emulator::Setup(ui::Window* display_window) {
  X_STATUS result = X_STATUS_UNSUCCESSFUL;

  display_window_ = display_window;

  // Initialize clock.
  // 360 uses a 50MHz clock.
  Clock::set_guest_tick_frequency(50000000);
  // We could reset this with save state data/constant value to help replays.
  Clock::set_guest_system_time_base(Clock::QueryHostSystemTime());
  // This can be adjusted dynamically, as well.
  Clock::set_guest_time_scalar(FLAGS_time_scalar);

  // Before we can set thread affinity we must enable the process to use all
  // logical processors.
  xe::threading::EnableAffinityConfiguration();

  // Create memory system first, as it is required for other systems.
  memory_ = std::make_unique<Memory>();
  result = memory_->Initialize();
  if (result) {
    return result;
  }

  // Shared export resolver used to attach and query for HLE exports.
  export_resolver_ = std::make_unique<xe::cpu::ExportResolver>();

  if (FLAGS_debug) {
    // Debugger first, as other parts hook into it.
    debugger_.reset(new debug::Debugger(this));

    // Create debugger first. Other types hook up to it.
    debugger_->StartSession();
  }

  // Initialize the CPU.
  processor_ = std::make_unique<xe::cpu::Processor>(
      memory_.get(), export_resolver_.get(), debugger_.get());

  // Initialize the APU.
  audio_system_ = xe::apu::AudioSystem::Create(processor_.get());
  if (!audio_system_) {
    return X_STATUS_NOT_IMPLEMENTED;
  }

  // Initialize the GPU.
  graphics_system_ = xe::gpu::GraphicsSystem::Create(this);
  if (!graphics_system_) {
    return X_STATUS_NOT_IMPLEMENTED;
  }
  display_window_->loop()->PostSynchronous([this]() {
    display_window_->set_context(
        graphics_system_->CreateContext(display_window_));
  });

  // Initialize the HID.
  input_system_ = xe::hid::InputSystem::Create(this);
  if (!input_system_) {
    return X_STATUS_NOT_IMPLEMENTED;
  }

  result = input_system_->Setup();
  if (result) {
    return result;
  }

  // Bring up the virtual filesystem used by the kernel.
  file_system_ = std::make_unique<xe::vfs::VirtualFileSystem>();

  // Shared kernel state.
  kernel_state_ = std::make_unique<xe::kernel::KernelState>(this);

  // Setup the core components.
  if (!processor_->Setup()) {
    return result;
  }
  result = graphics_system_->Setup(processor_.get(), display_window_->loop(),
                                   display_window_);
  if (result) {
    return result;
  }

  result = audio_system_->Setup(kernel_state_.get());
  if (result) {
    return result;
  }

  // HLE kernel modules.
  kernel_state_->LoadKernelModule<kernel::XboxkrnlModule>();
  kernel_state_->LoadKernelModule<kernel::XamModule>();

  // Finish initializing the display.
  display_window_->loop()->PostSynchronous([this]() {
    {
      xe::ui::GraphicsContextLock context_lock(display_window_->context());
      auto profiler_display =
          display_window_->context()->CreateProfilerDisplay();
      Profiler::set_display(std::move(profiler_display));
    }
    display_window_->MakeReady();
  });

  return result;
}

X_STATUS Emulator::LaunchPath(std::wstring path) {
  // Launch based on file type.
  // This is a silly guess based on file extension.
  auto last_slash = path.find_last_of(xe::kPathSeparator);
  auto last_dot = path.find_last_of('.');
  if (last_dot < last_slash) {
    last_dot = std::wstring::npos;
  }
  if (last_dot == std::wstring::npos) {
    // Likely an STFS container.
    return LaunchStfsContainer(path);
  } else if (path.substr(last_dot) == L".xex" ||
             path.substr(last_dot) == L".elf") {
    // Treat as a naked xex file.
    return LaunchXexFile(path);
  } else {
    // Assume a disc image.
    return LaunchDiscImage(path);
  }
}

X_STATUS Emulator::LaunchXexFile(std::wstring path) {
  // We create a virtual filesystem pointing to its directory and symlink
  // that to the game filesystem.
  // e.g., /my/files/foo.xex will get a local fs at:
  // \\Device\\Harddisk0\\Partition1
  // and then get that symlinked to game:\, so
  // -> game:\foo.xex

  auto mount_path = "\\Device\\Harddisk0\\Partition0";

  // Register the local directory in the virtual filesystem.
  auto parent_path = xe::find_base_path(path);
  auto device =
      std::make_unique<vfs::HostPathDevice>(mount_path, parent_path, true);
  if (!device->Initialize()) {
    XELOGE("Unable to scan host path");
    return X_STATUS_NO_SUCH_FILE;
  }
  if (!file_system_->RegisterDevice(std::move(device))) {
    XELOGE("Unable to register host path");
    return X_STATUS_NO_SUCH_FILE;
  }

  // Create symlinks to the device.
  file_system_->RegisterSymbolicLink("game:", mount_path);
  file_system_->RegisterSymbolicLink("d:", mount_path);

  // Get just the filename (foo.xex).
  auto file_name = xe::find_name_from_path(path);

  // Launch the game.
  std::string fs_path = "game:\\" + xe::to_string(file_name);
  return CompleteLaunch(path, fs_path);
}

X_STATUS Emulator::LaunchDiscImage(std::wstring path) {
  auto mount_path = "\\Device\\Cdrom0";

  // Register the disc image in the virtual filesystem.
  auto device = std::make_unique<vfs::DiscImageDevice>(mount_path, path);
  if (!device->Initialize()) {
    xe::FatalError("Unable to mount disc image; file not found or corrupt.");
    return X_STATUS_NO_SUCH_FILE;
  }
  if (!file_system_->RegisterDevice(std::move(device))) {
    xe::FatalError("Unable to register disc image.");
    return X_STATUS_NO_SUCH_FILE;
  }

  // Create symlinks to the device.
  file_system_->RegisterSymbolicLink("game:", mount_path);
  file_system_->RegisterSymbolicLink("d:", mount_path);

  // Launch the game.
  return CompleteLaunch(path, "game:\\default.xex");
}

X_STATUS Emulator::LaunchStfsContainer(std::wstring path) {
  auto mount_path = "\\Device\\Cdrom0";

  // Register the container in the virtual filesystem.
  auto device = std::make_unique<vfs::StfsContainerDevice>(mount_path, path);
  if (!device->Initialize()) {
    xe::FatalError(
        "Unable to mount STFS container; file not found or corrupt.");
    return X_STATUS_NO_SUCH_FILE;
  }
  if (!file_system_->RegisterDevice(std::move(device))) {
    xe::FatalError("Unable to register STFS container.");
    return X_STATUS_NO_SUCH_FILE;
  }

  file_system_->RegisterSymbolicLink("game:", mount_path);
  file_system_->RegisterSymbolicLink("d:", mount_path);

  // Launch the game.
  return CompleteLaunch(path, "game:\\default.xex");
}

X_STATUS Emulator::CompleteLaunch(const std::wstring& path,
                                  const std::string& module_path) {
  // Allow xam to request module loads.
  auto xam = kernel_state()->GetKernelModule<kernel::XamModule>("xam.xex");
  auto xboxkrnl =
      kernel_state()->GetKernelModule<kernel::XboxkrnlModule>("xboxkrnl.exe");

  int result = 0;
  auto next_module = module_path;
  while (next_module != "") {
    XELOGI("Launching module %s", next_module.c_str());
    result = xboxkrnl->LaunchModule(next_module.c_str());

    // Check xam and see if they want us to load another module.
    auto& loader_data = xam->loader_data();
    next_module = loader_data.launch_path;

    // And blank out the launch path to avoid an infinite loop.
    loader_data.launch_path = "";
  }

  if (result == 0) {
    return X_STATUS_SUCCESS;
  } else {
    return X_STATUS_UNSUCCESSFUL;
  }
}

}  // namespace xe
