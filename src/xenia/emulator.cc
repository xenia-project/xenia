/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/emulator.h"

#include <algorithm>
#include <cinttypes>

#include "config.h"
#include "third_party/fmt/include/fmt/format.h"
#include "xenia/apu/audio_system.h"
#include "xenia/base/assert.h"
#include "xenia/base/byte_stream.h"
#include "xenia/base/clock.h"
#include "xenia/base/cvar.h"
#include "xenia/base/debugging.h"
#include "xenia/base/exception_handler.h"
#include "xenia/base/literals.h"
#include "xenia/base/logging.h"
#include "xenia/base/mapped_memory.h"
#include "xenia/base/platform.h"
#include "xenia/base/string.h"
#include "xenia/cpu/backend/code_cache.h"
#include "xenia/cpu/backend/null_backend.h"
#include "xenia/cpu/cpu_flags.h"
#include "xenia/cpu/thread_state.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/hid/input_driver.h"
#include "xenia/hid/input_system.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/util/gameinfo_utils.h"
#include "xenia/kernel/util/xdbf_utils.h"
#include "xenia/kernel/xam/xam_module.h"
#include "xenia/kernel/xbdm/xbdm_module.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_module.h"
#include "xenia/memory.h"
#include "xenia/ui/file_picker.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/window.h"
#include "xenia/ui/windowed_app_context.h"
#include "xenia/vfs/devices/disc_image_device.h"
#include "xenia/vfs/devices/host_path_device.h"
#include "xenia/vfs/devices/null_device.h"
#include "xenia/vfs/devices/stfs_container_device.h"

#if XE_ARCH_AMD64
#include "xenia/cpu/backend/x64/x64_backend.h"
#endif  // XE_ARCH

DECLARE_int32(user_language);

DEFINE_double(time_scalar, 1.0,
              "Scalar used to speed or slow time (1x, 2x, 1/2x, etc).",
              "General");
DEFINE_string(
    launch_module, "",
    "Executable to launch from the .iso or the package instead of default.xex "
    "or the module specified by the game. Leave blank to launch the default "
    "module.",
    "General");
DEFINE_bool(allow_game_relative_writes, false,
            "Not useful to non-developers. Allows code to write to paths "
            "relative to game://. Used for "
            "generating test data to compare with original hardware. ",
            "General");

namespace xe {
using namespace xe::literals;

Emulator::GameConfigLoadCallback::GameConfigLoadCallback(Emulator& emulator)
    : emulator_(emulator) {
  emulator_.AddGameConfigLoadCallback(this);
}

Emulator::GameConfigLoadCallback::~GameConfigLoadCallback() {
  emulator_.RemoveGameConfigLoadCallback(this);
}

Emulator::Emulator(const std::filesystem::path& command_line,
                   const std::filesystem::path& storage_root,
                   const std::filesystem::path& content_root,
                   const std::filesystem::path& cache_root)
    : on_launch(),
      on_terminate(),
      on_exit(),
      command_line_(command_line),
      storage_root_(storage_root),
      content_root_(content_root),
      cache_root_(cache_root),
      title_name_(),
      title_version_(),
      display_window_(nullptr),
      memory_(),
      audio_system_(),
      graphics_system_(),
      input_system_(),
      export_resolver_(),
      file_system_(),
      kernel_state_(),
      main_thread_(),
      title_id_(std::nullopt),
      paused_(false),
      restoring_(false),
      restore_fence_() {}

Emulator::~Emulator() {
  // Note that we delete things in the reverse order they were initialized.

  // Give the systems time to shutdown before we delete them.
  if (graphics_system_) {
    graphics_system_->Shutdown();
  }
  if (audio_system_) {
    audio_system_->Shutdown();
  }

  input_system_.reset();
  graphics_system_.reset();
  audio_system_.reset();

  kernel_state_.reset();
  file_system_.reset();

  processor_.reset();

  export_resolver_.reset();

  ExceptionHandler::Uninstall(Emulator::ExceptionCallbackThunk, this);
}

X_STATUS Emulator::Setup(
    ui::Window* display_window, ui::ImGuiDrawer* imgui_drawer,
    bool require_cpu_backend,
    std::function<std::unique_ptr<apu::AudioSystem>(cpu::Processor*)>
        audio_system_factory,
    std::function<std::unique_ptr<gpu::GraphicsSystem>()>
        graphics_system_factory,
    std::function<std::vector<std::unique_ptr<hid::InputDriver>>(ui::Window*)>
        input_driver_factory) {
  X_STATUS result = X_STATUS_UNSUCCESSFUL;

  display_window_ = display_window;
  imgui_drawer_ = imgui_drawer;

  // Initialize clock.
  // 360 uses a 50MHz clock.
  Clock::set_guest_tick_frequency(50000000);
  // We could reset this with save state data/constant value to help replays.
  Clock::set_guest_system_time_base(Clock::QueryHostSystemTime());
  // This can be adjusted dynamically, as well.
  Clock::set_guest_time_scalar(cvars::time_scalar);

  // Before we can set thread affinity we must enable the process to use all
  // logical processors.
  xe::threading::EnableAffinityConfiguration();

  // Create memory system first, as it is required for other systems.
  memory_ = std::make_unique<Memory>();
  if (!memory_->Initialize()) {
    return false;
  }

  // Shared export resolver used to attach and query for HLE exports.
  export_resolver_ = std::make_unique<xe::cpu::ExportResolver>();

  std::unique_ptr<xe::cpu::backend::Backend> backend;
#if XE_ARCH_AMD64
  if (cvars::cpu == "x64") {
    backend.reset(new xe::cpu::backend::x64::X64Backend());
  }
#endif  // XE_ARCH
  if (cvars::cpu == "any") {
    if (!backend) {
#if XE_ARCH_AMD64
      backend.reset(new xe::cpu::backend::x64::X64Backend());
#endif  // XE_ARCH
    }
  }
  if (!backend && !require_cpu_backend) {
    backend.reset(new xe::cpu::backend::NullBackend());
  }

  // Initialize the CPU.
  processor_ = std::make_unique<xe::cpu::Processor>(memory_.get(),
                                                    export_resolver_.get());
  if (!processor_->Setup(std::move(backend))) {
    return X_STATUS_UNSUCCESSFUL;
  }

  // Initialize the APU.
  if (audio_system_factory) {
    audio_system_ = audio_system_factory(processor_.get());
    if (!audio_system_) {
      return X_STATUS_NOT_IMPLEMENTED;
    }
  }

  // Initialize the GPU.
  graphics_system_ = graphics_system_factory();
  if (!graphics_system_) {
    return X_STATUS_NOT_IMPLEMENTED;
  }

  // Initialize the HID.
  input_system_ = std::make_unique<xe::hid::InputSystem>(display_window_);
  if (!input_system_) {
    return X_STATUS_NOT_IMPLEMENTED;
  }
  if (input_driver_factory) {
    auto input_drivers = input_driver_factory(display_window_);
    for (size_t i = 0; i < input_drivers.size(); ++i) {
      auto& input_driver = input_drivers[i];
      input_driver->set_is_active_callback(
          []() -> bool { return !xe::kernel::xam::xeXamIsUIActive(); });
      input_system_->AddDriver(std::move(input_driver));
    }
  }

  result = input_system_->Setup();
  if (result) {
    return result;
  }

  // Bring up the virtual filesystem used by the kernel.
  file_system_ = std::make_unique<xe::vfs::VirtualFileSystem>();

  patcher_ = std::make_unique<xe::patcher::Patcher>(storage_root_);

  // Shared kernel state.
  kernel_state_ = std::make_unique<xe::kernel::KernelState>(this);

  // Setup the core components.
  result = graphics_system_->Setup(
      processor_.get(), kernel_state_.get(),
      display_window_ ? &display_window_->app_context() : nullptr,
      display_window_ != nullptr);
  if (result) {
    return result;
  }

  if (audio_system_) {
    result = audio_system_->Setup(kernel_state_.get());
    if (result) {
      return result;
    }
  }

#define LOAD_KERNEL_MODULE(t) \
  static_cast<void>(kernel_state_->LoadKernelModule<kernel::t>())
  // HLE kernel modules.
  LOAD_KERNEL_MODULE(xboxkrnl::XboxkrnlModule);
  LOAD_KERNEL_MODULE(xam::XamModule);
  LOAD_KERNEL_MODULE(xbdm::XbdmModule);
#undef LOAD_KERNEL_MODULE

  // Initialize emulator fallback exception handling last.
  ExceptionHandler::Install(Emulator::ExceptionCallbackThunk, this);

  return result;
}

X_STATUS Emulator::TerminateTitle() {
  if (!is_title_open()) {
    return X_STATUS_UNSUCCESSFUL;
  }

  kernel_state_->TerminateTitle();
  title_id_ = std::nullopt;
  title_name_ = "";
  title_version_ = "";
  on_terminate();
  return X_STATUS_SUCCESS;
}

const std::unique_ptr<vfs::Device> Emulator::CreateVfsDeviceBasedOnPath(
    const std::filesystem::path& path, const std::string_view mount_path) {
  if (!path.has_extension()) {
    return std::make_unique<vfs::StfsContainerDevice>(mount_path, path);
  }
  auto extension = xe::utf8::lower_ascii(xe::path_to_utf8(path.extension()));
  if (extension == ".xex" || extension == ".elf" || extension == ".exe") {
    auto parent_path = path.parent_path();
    return std::make_unique<vfs::HostPathDevice>(
        mount_path, parent_path, !cvars::allow_game_relative_writes);
  } else {
    return std::make_unique<vfs::DiscImageDevice>(mount_path, path);
  }
}

X_STATUS Emulator::MountPath(const std::filesystem::path& path,
                             const std::string_view mount_path) {
  auto device = CreateVfsDeviceBasedOnPath(path, mount_path);
  if (!device->Initialize()) {
    xe::FatalError("Unable to mount {}; file not found or corrupt.");
    return X_STATUS_NO_SUCH_FILE;
  }
  if (!file_system_->RegisterDevice(std::move(device))) {
    xe::FatalError("Unable to register {}.");
    return X_STATUS_NO_SUCH_FILE;
  }

  file_system_->UnregisterSymbolicLink("d:");
  file_system_->UnregisterSymbolicLink("game:");
  // Create symlinks to the device.
  file_system_->RegisterSymbolicLink("game:", mount_path);
  file_system_->RegisterSymbolicLink("d:", mount_path);
  return X_STATUS_SUCCESS;
}

X_STATUS Emulator::LaunchPath(const std::filesystem::path& path) {
  // Launch based on file type.
  // This is a silly guess based on file extension.
  if (!path.has_extension()) {
    // Likely an STFS container.
    MountPath(path, "\\Device\\Cdrom0");
    return LaunchStfsContainer(path);
  };
  auto extension = xe::utf8::lower_ascii(xe::path_to_utf8(path.extension()));
  if (extension == ".xex" || extension == ".elf" || extension == ".exe") {
    // Treat as a naked xex file.
    MountPath(path, "\\Device\\Harddisk0\\Partition1");
    return LaunchXexFile(path);
  } else {
    // Assume a disc image.
    MountPath(path, "\\Device\\Cdrom0");
    return LaunchDiscImage(path);
  }
}

X_STATUS Emulator::LaunchXexFile(const std::filesystem::path& path) {
  // We create a virtual filesystem pointing to its directory and symlink
  // that to the game filesystem.
  // e.g., /my/files/foo.xex will get a local fs at:
  // \\Device\\Harddisk0\\Partition1
  // and then get that symlinked to game:\, so
  // -> game:\foo.xex
  // Get just the filename (foo.xex).
  auto file_name = path.filename();

  // Launch the game.
  auto fs_path = "game:\\" + xe::path_to_utf8(file_name);
  return CompleteLaunch(path, fs_path);
}

X_STATUS Emulator::LaunchDiscImage(const std::filesystem::path& path) {
  auto module_path(FindLaunchModule());
  return CompleteLaunch(path, module_path);
}

X_STATUS Emulator::LaunchStfsContainer(const std::filesystem::path& path) {
  auto module_path(FindLaunchModule());
  return CompleteLaunch(path, module_path);
}

X_STATUS Emulator::InstallContentPackage(const std::filesystem::path& path) {
  std::unique_ptr<vfs::StfsContainerDevice> device =
      std::make_unique<vfs::StfsContainerDevice>("", path);
  if (!device->Initialize()) {
    XELOGE("Failed to initialize device");
    return X_STATUS_INVALID_PARAMETER;
  }

  std::filesystem::path installation_path =
      content_root() / fmt::format("{:08X}", device->title_id()) /
      fmt::format("{:08X}", device->content_type()) / path.filename();

  std::filesystem::path header_path =
      content_root() / fmt::format("{:08X}", device->title_id()) / "Headers" /
      fmt::format("{:08X}", device->content_type()) / path.filename();

  if (std::filesystem::exists(installation_path)) {
    // TODO(Gliniak): Popup
    // Do you want to overwrite already existing data?
  } else {
    std::error_code error_code;
    std::filesystem::create_directories(installation_path, error_code);
    if (error_code) {
      return error_code.value();
    }
  }

  vfs::VirtualFileSystem::ExtractContentHeader(device.get(), header_path);

  return vfs::VirtualFileSystem::ExtractContentFiles(device.get(),
                                                     installation_path);
}

void Emulator::Pause() {
  if (paused_) {
    return;
  }
  paused_ = true;

  // Don't hold the lock on this (so any waits follow through)
  graphics_system_->Pause();
  audio_system_->Pause();

  auto lock = global_critical_region::AcquireDirect();
  auto threads =
      kernel_state()->object_table()->GetObjectsByType<kernel::XThread>(
          kernel::XObject::Type::Thread);
  auto current_thread = kernel::XThread::IsInThread()
                            ? kernel::XThread::GetCurrentThread()
                            : nullptr;
  for (auto thread : threads) {
    // Don't pause ourself or host threads.
    if (thread == current_thread || !thread->can_debugger_suspend()) {
      continue;
    }

    if (thread->is_running()) {
      thread->thread()->Suspend(nullptr);
    }
  }

  XELOGD("! EMULATOR PAUSED !");
}

void Emulator::Resume() {
  if (!paused_) {
    return;
  }
  paused_ = false;
  XELOGD("! EMULATOR RESUMED !");

  graphics_system_->Resume();
  audio_system_->Resume();

  auto threads =
      kernel_state()->object_table()->GetObjectsByType<kernel::XThread>(
          kernel::XObject::Type::Thread);
  for (auto thread : threads) {
    if (!thread->can_debugger_suspend()) {
      // Don't pause host threads.
      continue;
    }

    if (thread->is_running()) {
      thread->thread()->Resume(nullptr);
    }
  }
}

bool Emulator::SaveToFile(const std::filesystem::path& path) {
  Pause();

  filesystem::CreateEmptyFile(path);
  auto map = MappedMemory::Open(path, MappedMemory::Mode::kReadWrite, 0, 2_GiB);
  if (!map) {
    return false;
  }

  // Save the emulator state to a file
  ByteStream stream(map->data(), map->size());
  stream.Write(kEmulatorSaveSignature);
  stream.Write(title_id_.has_value());
  if (title_id_.has_value()) {
    stream.Write(title_id_.value());
  }

  // It's important we don't hold the global lock here! XThreads need to step
  // forward (possibly through guarded regions) without worry!
  processor_->Save(&stream);
  graphics_system_->Save(&stream);
  audio_system_->Save(&stream);
  kernel_state_->Save(&stream);
  memory_->Save(&stream);
  map->Close(stream.offset());

  Resume();
  return true;
}

bool Emulator::RestoreFromFile(const std::filesystem::path& path) {
  // Restore the emulator state from a file
  auto map = MappedMemory::Open(path, MappedMemory::Mode::kReadWrite);
  if (!map) {
    return false;
  }

  restoring_ = true;

  // Terminate any loaded titles.
  Pause();
  kernel_state_->TerminateTitle();

  auto lock = global_critical_region::AcquireDirect();
  ByteStream stream(map->data(), map->size());
  if (stream.Read<uint32_t>() != kEmulatorSaveSignature) {
    return false;
  }

  auto has_title_id = stream.Read<bool>();
  std::optional<uint32_t> title_id;
  if (!has_title_id) {
    title_id = {};
  } else {
    title_id = stream.Read<uint32_t>();
  }
  if (title_id_.has_value() != title_id.has_value() ||
      title_id_.value() != title_id.value()) {
    // Swapping between titles is unsupported at the moment.
    assert_always();
    return false;
  }

  if (!processor_->Restore(&stream)) {
    XELOGE("Could not restore processor!");
    return false;
  }
  if (!graphics_system_->Restore(&stream)) {
    XELOGE("Could not restore graphics system!");
    return false;
  }
  if (!audio_system_->Restore(&stream)) {
    XELOGE("Could not restore audio system!");
    return false;
  }
  if (!kernel_state_->Restore(&stream)) {
    XELOGE("Could not restore kernel state!");
    return false;
  }
  if (!memory_->Restore(&stream)) {
    XELOGE("Could not restore memory!");
    return false;
  }

  // Update the main thread.
  auto threads =
      kernel_state_->object_table()->GetObjectsByType<kernel::XThread>();
  for (auto thread : threads) {
    if (thread->main_thread()) {
      main_thread_ = thread;
      break;
    }
  }

  Resume();

  restore_fence_.Signal();
  restoring_ = false;

  return true;
}

bool Emulator::TitleRequested() {
  auto xam = kernel_state()->GetKernelModule<kernel::xam::XamModule>("xam.xex");
  return xam->loader_data().launch_data_present;
}

void Emulator::LaunchNextTitle() {
  auto xam = kernel_state()->GetKernelModule<kernel::xam::XamModule>("xam.xex");
  auto next_title = xam->loader_data().launch_path;

  // Swap disk doesn't require reloading
  // This function should be purged?
  CompleteLaunch("", next_title);
}

const std::filesystem::path Emulator::GetNewDiscPath(
    std::string window_message) {
  std::filesystem::path path = "";

  auto file_picker = xe::ui::FilePicker::Create();
  file_picker->set_mode(ui::FilePicker::Mode::kOpen);
  file_picker->set_type(ui::FilePicker::Type::kFile);
  file_picker->set_multi_selection(false);
  file_picker->set_title(!window_message.empty() ? window_message
                                                 : "Select Content Package");
  file_picker->set_extensions({
      {"Supported Files", "*.iso;*.xex;*.xcp;*.*"},
      {"Disc Image (*.iso)", "*.iso"},
      {"Xbox Executable (*.xex)", "*.xex"},
      {"All Files (*.*)", "*.*"},
  });

  if (file_picker->Show()) {
    auto selected_files = file_picker->selected_files();
    if (!selected_files.empty()) {
      path = selected_files[0];
    }
  }
  return path;
}

bool Emulator::ExceptionCallbackThunk(Exception* ex, void* data) {
  return reinterpret_cast<Emulator*>(data)->ExceptionCallback(ex);
}

bool Emulator::ExceptionCallback(Exception* ex) {
  // Check to see if the exception occurred in guest code.
  auto code_cache = processor()->backend()->code_cache();
  auto code_base = code_cache->execute_base_address();
  auto code_end = code_base + code_cache->total_size();

  if (!processor()->is_debugger_attached() && debugging::IsDebuggerAttached()) {
    // If Xenia's debugger isn't attached but another one is, pass it to that
    // debugger.
    return false;
  } else if (processor()->is_debugger_attached()) {
    // Let the debugger handle this exception. It may decide to continue past
    // it (if it was a stepping breakpoint, etc).
    return processor()->OnUnhandledException(ex);
  }

  if (!(ex->pc() >= code_base && ex->pc() < code_end)) {
    // Didn't occur in guest code. Let it pass.
    return false;
  }

  // Within range. Pause the emulator and eat the exception.
  Pause();

  // Dump information into the log.
  auto current_thread = kernel::XThread::GetCurrentThread();
  assert_not_null(current_thread);

  auto guest_function = code_cache->LookupFunction(ex->pc());
  assert_not_null(guest_function);

  auto context = current_thread->thread_state()->context();

  std::string crash_msg;
  crash_msg.append("==== CRASH DUMP ====\n");
  crash_msg.append(fmt::format("Thread ID (Host: 0x{:08X} / Guest: 0x{:08X})\n",
         current_thread->thread()->system_id(), current_thread->thread_id()));
  crash_msg.append(fmt::format("Thread Handle: 0x{:08X}\n", current_thread->handle()));
  crash_msg.append(fmt::format("PC: 0x{:08X}\n",
         guest_function->MapMachineCodeToGuestAddress(ex->pc())));
  crash_msg.append("Registers:\n");
  for (int i = 0; i < 32; i++) {
    crash_msg.append(fmt::format(" r{:<3} = {:016X}\n", i, context->r[i]));
  }
  for (int i = 0; i < 32; i++) {
    crash_msg.append(fmt::format(" f{:<3} = {:016X} = (double){} = (float){}\n", i,
           *reinterpret_cast<uint64_t*>(&context->f[i]), context->f[i],
           *(float*)&context->f[i]));
  }
  for (int i = 0; i < 128; i++) {
    crash_msg.append(fmt::format(" v{:<3} = [0x{:08X}, 0x{:08X}, 0x{:08X}, 0x{:08X}]\n", i,
           context->v[i].u32[0], context->v[i].u32[1], context->v[i].u32[2],
           context->v[i].u32[3]));
  }
  XELOGE("{}", crash_msg);
  std::string crash_dlg = fmt::format(
              "The guest has crashed.\n\n"
              "Xenia has now paused itself.\n\n"
              "{}", crash_msg);
  // Display a dialog telling the user the guest has crashed.
  if (display_window_ && imgui_drawer_) {
    display_window_->app_context().CallInUIThreadSynchronous([this, &crash_dlg]() {
      xe::ui::ImGuiDialog::ShowMessageBox(
          imgui_drawer_, "Uh-oh!", crash_dlg);
    });
  }

  // Now suspend ourself (we should be a guest thread).
  current_thread->Suspend(nullptr);

  // We should not arrive here!
  assert_always();
  return false;
}

void Emulator::WaitUntilExit() {
  while (true) {
    if (main_thread_) {
      xe::threading::Wait(main_thread_->thread(), false);
    }

    if (restoring_) {
      restore_fence_.Wait();
    } else {
      // Not restoring and the thread exited. We're finished.
      break;
    }
  }

  on_exit();
}

void Emulator::AddGameConfigLoadCallback(GameConfigLoadCallback* callback) {
  assert_not_null(callback);
  // Game config load callbacks handling is entirely in the UI thread.
  assert_true(!display_window_ ||
              display_window_->app_context().IsInUIThread());
  // Check if already added.
  if (std::find(game_config_load_callbacks_.cbegin(),
                game_config_load_callbacks_.cend(),
                callback) != game_config_load_callbacks_.cend()) {
    return;
  }
  game_config_load_callbacks_.push_back(callback);
}

void Emulator::RemoveGameConfigLoadCallback(GameConfigLoadCallback* callback) {
  assert_not_null(callback);
  // Game config load callbacks handling is entirely in the UI thread.
  assert_true(!display_window_ ||
              display_window_->app_context().IsInUIThread());
  auto it = std::find(game_config_load_callbacks_.cbegin(),
                      game_config_load_callbacks_.cend(), callback);
  if (it == game_config_load_callbacks_.cend()) {
    return;
  }
  if (game_config_load_callback_loop_next_index_ != SIZE_MAX) {
    // Actualize the next callback index after the erasure from the vector.
    size_t existing_index =
        size_t(std::distance(game_config_load_callbacks_.cbegin(), it));
    if (game_config_load_callback_loop_next_index_ > existing_index) {
      --game_config_load_callback_loop_next_index_;
    }
  }
  game_config_load_callbacks_.erase(it);
}

std::string Emulator::FindLaunchModule() {
  std::string path("game:\\");

  if (!cvars::launch_module.empty()) {
    return path + cvars::launch_module;
  }

  std::string default_module("default.xex");

  auto gameinfo_entry(file_system_->ResolvePath(path + "GameInfo.bin"));
  if (gameinfo_entry) {
    vfs::File* file = nullptr;
    X_STATUS result =
        gameinfo_entry->Open(vfs::FileAccess::kGenericRead, &file);
    if (XSUCCEEDED(result)) {
      std::vector<uint8_t> buffer(gameinfo_entry->size());
      size_t bytes_read = 0;
      result = file->ReadSync(buffer.data(), buffer.size(), 0, &bytes_read);
      if (XSUCCEEDED(result)) {
        kernel::util::GameInfo info(buffer);
        if (info.is_valid()) {
          XELOGI("Found virtual title {}", info.virtual_title_id());

          const std::string xna_id("584E07D1");
          auto xna_id_entry(file_system_->ResolvePath(path + xna_id));
          if (xna_id_entry) {
            default_module = xna_id + "\\" + info.module_name();
          } else {
            XELOGE("Could not find fixed XNA path {}", xna_id);
          }
        }
      }
    }
  }

  return path + default_module;
}

static std::string format_version(xex2_version version) {
  // fmt::format doesn't like bit fields
  uint32_t major, minor, build, qfe;
  major = version.major;
  minor = version.minor;
  build = version.build;
  qfe = version.qfe;
  if (qfe) {
    return fmt::format("{}.{}.{}.{}", major, minor, build, qfe);
  }
  if (build) {
    return fmt::format("{}.{}.{}", major, minor, build);
  }
  return fmt::format("{}.{}", major, minor);
}

X_STATUS Emulator::CompleteLaunch(const std::filesystem::path& path,
                                  const std::string_view module_path) {
  // Making changes to the UI (setting the icon) and executing game config
  // load callbacks which expect to be called from the UI thread.
  assert_true(display_window_->app_context().IsInUIThread());

  // Setup NullDevices for raw HDD partition accesses
  // Cache/STFC code baked into games tries reading/writing to these
  // By using a NullDevice that just returns success to all IO requests it
  // should allow games to believe cache/raw disk was accessed successfully

  // NOTE: this should probably be moved to xenia_main.cc, but right now we
  // need to register the \Device\Harddisk0\ NullDevice _after_ the
  // \Device\Harddisk0\Partition1 HostPathDevice, otherwise requests to
  // Partition1 will go to this. Registering during CompleteLaunch allows us
  // to make sure any HostPathDevices are ready beforehand. (see comment above
  // cache:\ device registration for more info about why)
  auto null_paths = {std::string("\\Partition0"), std::string("\\Cache0"),
                     std::string("\\Cache1")};
  auto null_device =
      std::make_unique<vfs::NullDevice>("\\Device\\Harddisk0", null_paths);
  if (null_device->Initialize()) {
    file_system_->RegisterDevice(std::move(null_device));
  }

  // Reset state.
  title_id_ = std::nullopt;
  title_name_ = "";
  title_version_ = "";
  display_window_->SetIcon(nullptr, 0);

  // Allow xam to request module loads.
  auto xam = kernel_state()->GetKernelModule<kernel::xam::XamModule>("xam.xex");

  XELOGI("Loading module {}", module_path);
  auto module = kernel_state_->LoadUserModule(module_path);
  if (!module) {
    XELOGE("Failed to load user module {}", xe::path_to_utf8(path));
    return X_STATUS_NOT_FOUND;
  }

  X_RESULT result = kernel_state_->ApplyTitleUpdate(module);
  if (XFAILED(result)) {
    XELOGE("Failed to apply title update! Cannot run module {}",
           xe::path_to_utf8(path));
    return result;
  }

  result = kernel_state_->FinishLoadingUserModule(module);
  if (XFAILED(result)) {
    XELOGE("Failed to initialize user module {}", xe::path_to_utf8(path));
    return result;
  }
  // Grab the current title ID.
  xex2_opt_execution_info* info = nullptr;
  uint32_t workspace_address = 0;
  module->GetOptHeader(XEX_HEADER_EXECUTION_INFO, &info);

  kernel_state_->memory()
      ->LookupHeapByType(false, 0x1000)
      ->Alloc(module->workspace_size(), 0x1000,
              kMemoryAllocationReserve | kMemoryAllocationCommit,
              kMemoryProtectRead | kMemoryProtectWrite, false,
              &workspace_address);

  if (!info) {
    title_id_ = 0;
  } else {
    title_id_ = info->title_id;
    auto title_version = info->version();
    if (title_version.value != 0) {
      title_version_ = format_version(title_version);
    }
  }

  // Try and load the resource database (xex only).
  if (module->title_id()) {
    auto title_id = fmt::format("{:08X}", module->title_id());

    // Load the per-game configuration file and make sure updates are handled
    // by the callbacks.
    config::LoadGameConfig(title_id);
    assert_true(game_config_load_callback_loop_next_index_ == SIZE_MAX);
    game_config_load_callback_loop_next_index_ = 0;
    while (game_config_load_callback_loop_next_index_ <
           game_config_load_callbacks_.size()) {
      game_config_load_callbacks_[game_config_load_callback_loop_next_index_++]
          ->PostGameConfigLoad();
    }
    game_config_load_callback_loop_next_index_ = SIZE_MAX;

    const kernel::util::XdbfGameData db = kernel_state_->module_xdbf(module);
    if (db.is_valid()) {
      XLanguage language =
          db.GetExistingLanguage(static_cast<XLanguage>(cvars::user_language));
      title_name_ = db.title(language);
      XELOGI("Title name: {}", title_name_);

      // Show achievments data
      XELOGI("-------------------- ACHIEVEMENTS --------------------");
      const std::vector<kernel::util::XdbfAchievementTableEntry>
          achievement_list = db.GetAchievements();
      for (const kernel::util::XdbfAchievementTableEntry& entry :
           achievement_list) {
        std::string label = db.GetStringTableEntry(language, entry.label_id);
        std::string desc =
            db.GetStringTableEntry(language, entry.description_id);

        XELOGI("{} - {} - {} - {}", entry.id, label, desc, entry.gamerscore);
      }
      XELOGI("----------------- END OF ACHIEVEMENTS ----------------");

      auto icon_block = db.icon();
      if (icon_block) {
        display_window_->SetIcon(icon_block.buffer, icon_block.size);
      }
    }
  }

  // Initializing the shader storage in a blocking way so the user doesn't
  // miss the initial seconds - for instance, sound from an intro video may
  // start playing before the video can be seen if doing this in parallel with
  // the main thread.
  on_shader_storage_initialization(true);
  graphics_system_->InitializeShaderStorage(cache_root_, title_id_.value(),
                                            true);
  on_shader_storage_initialization(false);

  auto main_thread = kernel_state_->LaunchModule(module);
  if (!main_thread) {
    return X_STATUS_UNSUCCESSFUL;
  }
  main_thread_ = main_thread;
  on_launch(title_id_.value(), title_name_);

  return X_STATUS_SUCCESS;
}

}  // namespace xe
