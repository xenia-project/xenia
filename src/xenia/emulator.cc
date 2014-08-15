/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/emulator.h>

#include <poly/poly.h>
#include <xdb/protocol.h>
#include <xenia/apu/apu.h>
#include <xenia/cpu/cpu.h>
#include <xenia/cpu/xenon_memory.h>
#include <xenia/gpu/gpu.h>
#include <xenia/hid/hid.h>
#include <xenia/kernel/kernel.h>
#include <xenia/kernel/kernel_state.h>
#include <xenia/kernel/modules.h>
#include <xenia/kernel/fs/filesystem.h>
#include <xenia/ui/window.h>


using namespace xe;
using namespace xe::apu;
using namespace xe::cpu;
using namespace xe::gpu;
using namespace xe::hid;
using namespace xe::kernel;
using namespace xe::kernel::fs;
using namespace xe::ui;


Emulator::Emulator(const xechar_t* command_line) :
    main_window_(0),
    memory_(0),
    processor_(0),
    audio_system_(0), graphics_system_(0), input_system_(0),
    export_resolver_(0), file_system_(0),
    kernel_state_(0), xam_(0), xboxkrnl_(0) {
  XEIGNORE(xestrcpy(command_line_, XECOUNT(command_line_), command_line));
}

Emulator::~Emulator() {
  // Note that we delete things in the reverse order they were initialized.

  auto ev = xdb::protocol::ProcessExitEvent::Append(memory()->trace_base());
  if (ev) {
    ev->type = xdb::protocol::EventType::PROCESS_EXIT;
  }

  if (main_window_) {
    main_window_->Close();
  }

  debug_agent_.reset();

  delete xam_;
  delete xboxkrnl_;
  delete kernel_state_;

  delete file_system_;

  delete input_system_;

  // Give the systems time to shutdown before we delete them.
  graphics_system_->Shutdown();
  audio_system_->Shutdown();
  delete graphics_system_;
  delete audio_system_;

  delete processor_;

  delete export_resolver_;
}

X_STATUS Emulator::Setup() {
  X_STATUS result = X_STATUS_UNSUCCESSFUL;

  debug_agent_.reset(new DebugAgent(this));
  result = debug_agent_->Initialize();
  XEEXPECTZERO(result);

  // Create memory system first, as it is required for other systems.
  memory_ = new XenonMemory();
  XEEXPECTNOTNULL(memory_);
  result = memory_->Initialize();
  XEEXPECTZERO(result);
  memory_->set_trace_base(debug_agent_->trace_base());

  // Shared export resolver used to attach and query for HLE exports.
  export_resolver_ = new ExportResolver();
  XEEXPECTNOTNULL(export_resolver_);

  // Initialize the CPU.
  processor_ = new Processor(this);
  XEEXPECTNOTNULL(processor_);

  // Initialize the APU.
  audio_system_ = xe::apu::Create(this);
  XEEXPECTNOTNULL(audio_system_);

  // Initialize the GPU.
  graphics_system_ = xe::gpu::Create(this);
  XEEXPECTNOTNULL(graphics_system_);

  // Initialize the HID.
  input_system_ = xe::hid::Create(this);
  XEEXPECTNOTNULL(input_system_);

  // Setup the core components.
  result = processor_->Setup();
  XEEXPECTZERO(result);
  result = audio_system_->Setup();
  XEEXPECTZERO(result);
  result = graphics_system_->Setup();
  XEEXPECTZERO(result);
  result = input_system_->Setup();
  XEEXPECTZERO(result);

  // Bring up the virtual filesystem used by the kernel.
  file_system_ = new FileSystem();
  XEEXPECTNOTNULL(file_system_);

  // Shared kernel state.
  kernel_state_ = new KernelState(this);
  XEEXPECTNOTNULL(kernel_state_);

  // HLE kernel modules.
  xboxkrnl_ = new XboxkrnlModule(this, kernel_state_);
  XEEXPECTNOTNULL(xboxkrnl_);
  xam_ = new XamModule(this, kernel_state_);
  XEEXPECTNOTNULL(xam_);

  return result;

XECLEANUP:
  return result;
}

void Emulator::set_main_window(Window* window) {
  assert_null(main_window_);
  main_window_ = window;

  window->closed.AddListener([](UIEvent& e) {
    // TODO(benvanik): call module API to kill? this is a bad shutdown.
    exit(1);
  });
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
  auto last_slash = path.find_last_of(poly::path_separator);
  if (last_slash == std::string::npos) {
    // No slash found, whole thing is a file.
    file_name = path;
  } else {
    // Skip slash.
    file_name = path.substr(last_slash + 1);
  }

  // Launch the game.
  std::string fs_path = "game:\\" + poly::to_string(file_name);
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
  auto ev = xdb::protocol::ProcessStartEvent::Append(memory()->trace_base());
  if (ev) {
    ev->type = xdb::protocol::EventType::PROCESS_START;
    ev->membase = reinterpret_cast<uint64_t>(memory()->membase());
    auto path_length = poly::to_string(path)
                           .copy(ev->launch_path, sizeof(ev->launch_path) - 1);
    ev->launch_path[path_length] = 0;
  }

  return xboxkrnl_->LaunchModule(module_path.c_str());
}
