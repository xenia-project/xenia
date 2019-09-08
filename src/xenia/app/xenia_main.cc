/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/discord/discord_presence.h"
#include "xenia/app/emulator_window.h"
#include "xenia/base/cvar.h"
#include "xenia/base/debugging.h"
#include "xenia/base/logging.h"
#include "xenia/base/main.h"
#include "xenia/base/profiling.h"
#include "xenia/base/threading.h"
#include "xenia/config.h"
#include "xenia/debug/ui/debug_window.h"
#include "xenia/emulator.h"
#include "xenia/ui/file_picker.h"
#include "xenia/vfs/devices/host_path_device.h"

// Available audio systems:
#include "xenia/apu/nop/nop_audio_system.h"
#if XE_PLATFORM_WIN32
#include "xenia/apu/xaudio2/xaudio2_audio_system.h"
#endif  // XE_PLATFORM_WIN32

// Available graphics systems:
#include "xenia/gpu/null/null_graphics_system.h"
#include "xenia/gpu/vk/vulkan_graphics_system.h"
#include "xenia/gpu/vulkan/vulkan_graphics_system.h"
#if XE_PLATFORM_WIN32
#include "xenia/gpu/d3d12/d3d12_graphics_system.h"
#endif  // XE_PLATFORM_WIN32

// Available input drivers:
#include "xenia/hid/nop/nop_hid.h"
#if XE_PLATFORM_WIN32
#include "xenia/hid/winkey/winkey_hid.h"
#include "xenia/hid/xinput/xinput_hid.h"
#endif  // XE_PLATFORM_WIN32

#include "third_party/xbyak/xbyak/xbyak_util.h"

DEFINE_string(apu, "any", "Audio system. Use: [any, nop, xaudio2]", "APU");
DEFINE_string(gpu, "any",
              "Graphics system. Use: [any, d3d12, vulkan, vk, null]", "GPU");
DEFINE_string(hid, "any", "Input system. Use: [any, nop, winkey, xinput]",
              "HID");

DEFINE_bool(fullscreen, false, "Toggles fullscreen", "GPU");

DEFINE_string(content_root, "", "Root path for content (save/etc) storage.",
              "Storage");

DEFINE_bool(mount_scratch, false, "Enable scratch mount", "Storage");
DEFINE_bool(mount_cache, false, "Enable cache mount", "Storage");

DEFINE_transient_string(target, "",
                        "Specifies the target .xex or .iso to execute.",
                        "General");
DECLARE_bool(debug);

DEFINE_bool(discord, true, "Enable Discord rich presence", "General");

namespace xe {
namespace app {

template <typename T, typename... Args>
class Factory {
 private:
  struct Creator {
    std::string name;
    std::function<bool()> is_available;
    std::function<std::unique_ptr<T>(Args...)> instantiate;
  };

  std::vector<Creator> creators_;

 public:
  void Add(const std::string& name, std::function<bool()> is_available,
           std::function<std::unique_ptr<T>(Args...)> instantiate) {
    creators_.push_back({name, is_available, instantiate});
  }

  void Add(const std::string& name,
           std::function<std::unique_ptr<T>(Args...)> instantiate) {
    Add(name, []() { return true; }, instantiate);
  }

  template <typename DT>
  void Add(const std::string& name) {
    Add(name, DT::IsAvailable, [](Args... args) {
      return std::make_unique<DT>(std::forward<Args>(args)...);
    });
  }

  std::unique_ptr<T> Create(const std::string& name, Args... args) {
    if (!name.empty() && name != "any") {
      auto it = std::find_if(
          creators_.cbegin(), creators_.cend(),
          [&name](const auto& f) { return name.compare(f.name) == 0; });
      if (it != creators_.cend() && (*it).is_available()) {
        return (*it).instantiate(std::forward<Args>(args)...);
      }
      return nullptr;
    } else {
      for (const auto& creator : creators_) {
        if (!creator.is_available()) continue;
        auto instance = creator.instantiate(std::forward<Args>(args)...);
        if (!instance) continue;
        return instance;
      }
      return nullptr;
    }
  }

  std::vector<std::unique_ptr<T>> CreateAll(const std::string& name,
                                            Args... args) {
    std::vector<std::unique_ptr<T>> instances;
    if (!name.empty() && name != "any") {
      auto it = std::find_if(
          creators_.cbegin(), creators_.cend(),
          [&name](const auto& f) { return name.compare(f.name) == 0; });
      if (it != creators_.cend() && (*it).is_available()) {
        auto instance = (*it).instantiate(std::forward<Args>(args)...);
        if (instance) {
          instances.emplace_back(std::move(instance));
        }
      }
    } else {
      for (const auto& creator : creators_) {
        if (!creator.is_available()) continue;
        auto instance = creator.instantiate(std::forward<Args>(args)...);
        if (instance) {
          instances.emplace_back(std::move(instance));
        }
      }
    }
    return instances;
  }
};

std::unique_ptr<apu::AudioSystem> CreateAudioSystem(cpu::Processor* processor) {
  Factory<apu::AudioSystem, cpu::Processor*> factory;
#if XE_PLATFORM_WIN32
  factory.Add<apu::xaudio2::XAudio2AudioSystem>("xaudio2");
#endif  // XE_PLATFORM_WIN32
  factory.Add<apu::nop::NopAudioSystem>("nop");
  return factory.Create(cvars::apu, processor);
}

std::unique_ptr<gpu::GraphicsSystem> CreateGraphicsSystem() {
  Factory<gpu::GraphicsSystem> factory;
#if XE_PLATFORM_WIN32
  factory.Add<gpu::d3d12::D3D12GraphicsSystem>("d3d12");
#endif  // XE_PLATFORM_WIN32
  // Abandoned Vulkan graphics system.
  factory.Add<gpu::vulkan::VulkanGraphicsSystem>("vulkan");
  // New Vulkan graphics system.
  // TODO(Triang3l): Move this higher when it's more ready, then drop the old
  // Vulkan graphics system.
  factory.Add<gpu::vk::VulkanGraphicsSystem>("vk");
  factory.Add<gpu::null::NullGraphicsSystem>("null");
  return factory.Create(cvars::gpu);
}

std::vector<std::unique_ptr<hid::InputDriver>> CreateInputDrivers(
    ui::Window* window) {
  std::vector<std::unique_ptr<hid::InputDriver>> drivers;
  if (cvars::hid.compare("nop") == 0) {
    drivers.emplace_back(xe::hid::nop::Create(window));
  } else {
    Factory<hid::InputDriver, ui::Window*> factory;
#if XE_PLATFORM_WIN32
    factory.Add("xinput", xe::hid::xinput::Create);
    // WinKey input driver should always be the last input driver added!
    factory.Add("winkey", xe::hid::winkey::Create);
#endif  // XE_PLATFORM_WIN32
    for (auto& driver : factory.CreateAll(cvars::hid, window)) {
      if (XSUCCEEDED(driver->Setup())) {
        drivers.emplace_back(std::move(driver));
      }
    }
    if (drivers.empty()) {
      // Fallback to nop if none created.
      drivers.emplace_back(xe::hid::nop::Create(window));
    }
  }
  return drivers;
}

int xenia_main(const std::vector<std::wstring>& args) {
  Profiler::Initialize();
  Profiler::ThreadEnter("main");

  // Figure out where content should go.
  std::wstring content_root = xe::to_wstring(cvars::content_root);
  std::wstring config_folder;

  if (content_root.empty()) {
    auto base_path = xe::filesystem::GetExecutableFolder();
    base_path = xe::to_absolute_path(base_path);

    auto portable_path = xe::join_paths(base_path, L"portable.txt");
    if (xe::filesystem::PathExists(portable_path)) {
      content_root = xe::join_paths(base_path, L"content");
      config_folder = base_path;
    } else {
      content_root = xe::filesystem::GetUserFolder();
#if defined(XE_PLATFORM_WIN32)
      content_root = xe::join_paths(content_root, L"Xenia");
#elif defined(XE_PLATFORM_LINUX)
      content_root = xe::join_paths(content_root, L"Xenia");
#else
#warning Unhandled platform for content root.
      content_root = xe::join_paths(content_root, L"Xenia");
#endif
      config_folder = content_root;
      content_root = xe::join_paths(content_root, L"content");
    }
  }
  content_root = xe::to_absolute_path(content_root);

  XELOGI("Content root: %S", content_root.c_str());
  config::SetupConfig(config_folder);

  if (cvars::discord) {
    discord::DiscordPresence::Initialize();
    discord::DiscordPresence::NotPlaying();
  }

  // Create the emulator but don't initialize so we can setup the window.
  auto emulator = std::make_unique<Emulator>(L"", content_root);

  // Main emulator display window.
  auto emulator_window = EmulatorWindow::Create(emulator.get());

  // Setup and initialize all subsystems. If we can't do something
  // (unsupported system, memory issues, etc) this will fail early.
  X_STATUS result =
      emulator->Setup(emulator_window->window(), CreateAudioSystem,
                      CreateGraphicsSystem, CreateInputDrivers);
  if (XFAILED(result)) {
    XELOGE("Failed to setup emulator: %.8X", result);
    return 1;
  }

  if (cvars::mount_scratch) {
    auto scratch_device = std::make_unique<xe::vfs::HostPathDevice>(
        "\\SCRATCH", L"scratch", false);
    if (!scratch_device->Initialize()) {
      XELOGE("Unable to scan scratch path");
    } else {
      if (!emulator->file_system()->RegisterDevice(std::move(scratch_device))) {
        XELOGE("Unable to register scratch path");
      } else {
        emulator->file_system()->RegisterSymbolicLink("scratch:", "\\SCRATCH");
      }
    }
  }

  if (cvars::mount_cache) {
    auto cache0_device =
        std::make_unique<xe::vfs::HostPathDevice>("\\CACHE0", L"cache0", false);
    if (!cache0_device->Initialize()) {
      XELOGE("Unable to scan cache0 path");
    } else {
      if (!emulator->file_system()->RegisterDevice(std::move(cache0_device))) {
        XELOGE("Unable to register cache0 path");
      } else {
        emulator->file_system()->RegisterSymbolicLink("cache0:", "\\CACHE0");
      }
    }

    auto cache1_device =
        std::make_unique<xe::vfs::HostPathDevice>("\\CACHE1", L"cache1", false);
    if (!cache1_device->Initialize()) {
      XELOGE("Unable to scan cache1 path");
    } else {
      if (!emulator->file_system()->RegisterDevice(std::move(cache1_device))) {
        XELOGE("Unable to register cache1 path");
      } else {
        emulator->file_system()->RegisterSymbolicLink("cache1:", "\\CACHE1");
      }
    }
  }

  // Set a debug handler.
  // This will respond to debugging requests so we can open the debug UI.
  std::unique_ptr<xe::debug::ui::DebugWindow> debug_window;
  if (cvars::debug) {
    emulator->processor()->set_debug_listener_request_handler(
        [&](xe::cpu::Processor* processor) {
          if (debug_window) {
            return debug_window.get();
          }
          emulator_window->loop()->PostSynchronous([&]() {
            debug_window = xe::debug::ui::DebugWindow::Create(
                emulator.get(), emulator_window->loop());
            debug_window->window()->on_closed.AddListener(
                [&](xe::ui::UIEvent* e) {
                  emulator->processor()->set_debug_listener(nullptr);
                  emulator_window->loop()->Post(
                      [&]() { debug_window.reset(); });
                });
          });
          return debug_window.get();
        });
  }

  auto evt = xe::threading::Event::CreateAutoResetEvent(false);
  emulator->on_launch.AddListener([&](auto title_id, const auto& game_title) {
    if (cvars::discord) {
      discord::DiscordPresence::PlayingTitle(
          game_title.empty() ? L"Unknown Title" : game_title);
    }
    emulator_window->UpdateTitle();
    evt->Set();
  });

  emulator->on_terminate.AddListener([&]() {
    if (cvars::discord) {
      discord::DiscordPresence::NotPlaying();
    }
  });

  emulator_window->window()->on_closing.AddListener([&](ui::UIEvent* e) {
    // This needs to shut down before the graphics context.
    Profiler::Shutdown();
  });

  bool exiting = false;
  emulator_window->loop()->on_quit.AddListener([&](ui::UIEvent* e) {
    exiting = true;
    evt->Set();

    if (cvars::discord) {
      discord::DiscordPresence::Shutdown();
    }

    // TODO(DrChat): Remove this code and do a proper exit.
    XELOGI("Cheap-skate exit!");
    exit(0);
  });

  // Enable the main menu now that the emulator is properly loaded
  emulator_window->window()->EnableMainMenu();

  // Grab path from the flag or unnamed argument.
  std::wstring path;
  if (!cvars::target.empty()) {
    path = xe::to_wstring(cvars::target);
  }

  // Toggles fullscreen
  if (cvars::fullscreen) emulator_window->ToggleFullscreen();

  if (!path.empty()) {
    // Normalize the path and make absolute.
    std::wstring abs_path = xe::to_absolute_path(path);
    result = emulator->LaunchPath(abs_path);
    if (XFAILED(result)) {
      xe::FatalError("Failed to launch target: %.8X", result);
      emulator.reset();
      emulator_window.reset();
      return 1;
    }
  }

  // Now, we're going to use the main thread to drive events related to
  // emulation.
  while (!exiting) {
    xe::threading::Wait(evt.get(), false);

    while (true) {
      emulator->WaitUntilExit();
      if (emulator->TitleRequested()) {
        emulator->LaunchNextTitle();
      } else {
        break;
      }
    }
  }

  debug_window.reset();
  emulator.reset();

  if (cvars::discord) {
    discord::DiscordPresence::Shutdown();
  }

  Profiler::Dump();
  Profiler::Shutdown();
  emulator_window.reset();
  return 0;
}

}  // namespace app
}  // namespace xe

DEFINE_ENTRY_POINT(L"xenia", xe::app::xenia_main, "[Path to .iso/.xex]",
                   "target");
