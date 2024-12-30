/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <atomic>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "xenia/app/discord/discord_presence.h"
#include "xenia/app/emulator_window.h"
#include "xenia/base/assert.h"
#include "xenia/base/cvar.h"
#include "xenia/base/debugging.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/base/profiling.h"
#include "xenia/base/threading.h"
#include "xenia/config.h"
#include "xenia/debug/ui/debug_window.h"
#include "xenia/emulator.h"
#include "xenia/kernel/xam/xam_module.h"
#include "xenia/ui/file_picker.h"
#include "xenia/ui/window.h"
#include "xenia/ui/window_listener.h"
#include "xenia/ui/windowed_app.h"
#include "xenia/ui/windowed_app_context.h"
#include "xenia/vfs/devices/host_path_device.h"

// Available audio systems:
#include "xenia/apu/nop/nop_audio_system.h"
#if !XE_PLATFORM_ANDROID
#include "xenia/apu/sdl/sdl_audio_system.h"
#endif  // !XE_PLATFORM_ANDROID
#if XE_PLATFORM_WIN32
#include "xenia/apu/xaudio2/xaudio2_audio_system.h"
#endif  // XE_PLATFORM_WIN32

// Available graphics systems:
#include "xenia/gpu/null/null_graphics_system.h"
#include "xenia/gpu/vulkan/vulkan_graphics_system.h"
#if XE_PLATFORM_WIN32
#include "xenia/gpu/d3d12/d3d12_graphics_system.h"
#endif  // XE_PLATFORM_WIN32

// Available input drivers:
#include "xenia/hid/nop/nop_hid.h"
#if !XE_PLATFORM_ANDROID
#include "xenia/hid/sdl/sdl_hid.h"
#endif  // !XE_PLATFORM_ANDROID
#if XE_PLATFORM_WIN32
#include "xenia/hid/winkey/winkey_hid.h"
#include "xenia/hid/xinput/xinput_hid.h"
#endif  // XE_PLATFORM_WIN32

DEFINE_string(apu, "any", "Audio system. Use: [any, nop, sdl, xaudio2]", "APU");
DEFINE_string(gpu, "any", "Graphics system. Use: [any, d3d12, vulkan, null]",
              "GPU");
DEFINE_string(hid, "any", "Input system. Use: [any, nop, sdl, winkey, xinput]",
              "HID");

DEFINE_path(
    storage_root, "",
    "Root path for persistent internal data storage (config, etc.), or empty "
    "to use the path preferred for the OS, such as the documents folder, or "
    "the emulator executable directory if portable.txt is present in it.",
    "Storage");
DEFINE_path(
    content_root, "",
    "Root path for guest content storage (saves, etc.), or empty to use the "
    "content folder under the storage root.",
    "Storage");
DEFINE_path(
    cache_root, "",
    "Root path for files used to speed up certain parts of the emulator or the "
    "game. These files may be persistent, but they can be deleted without "
    "major side effects such as progress loss. If empty, the cache folder "
    "under the storage root, or, if available, the cache directory preferred "
    "for the OS, will be used.",
    "Storage");

DEFINE_bool(mount_scratch, false, "Enable scratch mount", "Storage");

DEFINE_bool(mount_cache, true, "Enable cache mount", "Storage");
UPDATE_from_bool(mount_cache, 2024, 8, 31, 20, false);

DEFINE_transient_path(target, "",
                      "Specifies the target .xex or .iso to execute.",
                      "General");
DEFINE_transient_bool(portable, true,
                      "Specifies if Xenia should run in portable mode.",
                      "General");

DECLARE_bool(debug);

DEFINE_bool(discord, true, "Enable Discord rich presence", "General");

DECLARE_bool(widescreen);

namespace xe {
namespace app {

class EmulatorApp final : public xe::ui::WindowedApp {
 public:
  static std::unique_ptr<xe::ui::WindowedApp> Create(
      xe::ui::WindowedAppContext& app_context) {
    return std::unique_ptr<xe::ui::WindowedApp>(new EmulatorApp(app_context));
  }

  ~EmulatorApp();

  bool OnInitialize() override;

 protected:
  void OnDestroy() override;

 private:
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
    void Add(const std::string_view name, std::function<bool()> is_available,
             std::function<std::unique_ptr<T>(Args...)> instantiate) {
      creators_.push_back({std::string(name), is_available, instantiate});
    }

    void Add(const std::string_view name,
             std::function<std::unique_ptr<T>(Args...)> instantiate) {
      auto always_available = []() { return true; };
      Add(name, always_available, instantiate);
    }

    template <typename DT>
    void Add(const std::string_view name) {
      Add(name, DT::IsAvailable, [](Args... args) {
        return std::make_unique<DT>(std::forward<Args>(args)...);
      });
    }

    std::unique_ptr<T> Create(const std::string_view name, Args... args) {
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

    std::vector<std::unique_ptr<T>> CreateAll(const std::string_view name,
                                              Args... args) {
      std::vector<std::unique_ptr<T>> instances;

      // "Any" path
      if (name.empty() || name == "any") {
        for (const auto& creator : creators_) {
          if (!creator.is_available()) {
            continue;
          }

          // Skip xinput for "any" and use SDL
          if (creator.name.compare("xinput") == 0) {
            continue;
          }

          auto instance = creator.instantiate(std::forward<Args>(args)...);
          if (instance) {
            instances.emplace_back(std::move(instance));
          }
        }
        return instances;
      }

      // "Specified" path. Winkey is always added on windows.
      if (name != "winkey") {
        auto it = std::find_if(
            creators_.cbegin(), creators_.cend(),
            [&name](const auto& f) { return name.compare(f.name) == 0; });

        if (it != creators_.cend() && (*it).is_available()) {
          auto instance = (*it).instantiate(std::forward<Args>(args)...);
          if (instance) {
            instances.emplace_back(std::move(instance));
          }
        }
      }

      // Always add winkey for passthrough.
      auto it = std::find_if(
          creators_.cbegin(), creators_.cend(),
          [&name](const auto& f) { return f.name.compare("winkey") == 0; });
      if (it != creators_.cend() && (*it).is_available()) {
        auto instance = (*it).instantiate(std::forward<Args>(args)...);
        if (instance) {
          instances.emplace_back(std::move(instance));
        }
      }
      return instances;
    }
  };

  class DebugWindowClosedListener final : public xe::ui::WindowListener {
   public:
    explicit DebugWindowClosedListener(EmulatorApp& emulator_app)
        : emulator_app_(emulator_app) {}

    void OnClosing(xe::ui::UIEvent& e) override;

   private:
    EmulatorApp& emulator_app_;
  };

  explicit EmulatorApp(xe::ui::WindowedAppContext& app_context);

  static std::unique_ptr<apu::AudioSystem> CreateAudioSystem(
      cpu::Processor* processor);
  static std::unique_ptr<gpu::GraphicsSystem> CreateGraphicsSystem();
  static std::vector<std::unique_ptr<hid::InputDriver>> CreateInputDrivers(
      ui::Window* window);

  void EmulatorThread();
  void ShutdownEmulatorThreadFromUIThread();

  DebugWindowClosedListener debug_window_closed_listener_;

  std::unique_ptr<Emulator> emulator_;
  std::unique_ptr<EmulatorWindow> emulator_window_;

  // Created on demand, used by the emulator.
  std::unique_ptr<xe::debug::ui::DebugWindow> debug_window_;

  // Refreshing the emulator - placed after its dependencies.
  std::atomic<bool> emulator_thread_quit_requested_;
  std::unique_ptr<xe::threading::Event> emulator_thread_event_;
  std::thread emulator_thread_;
};

void EmulatorApp::DebugWindowClosedListener::OnClosing(xe::ui::UIEvent& e) {
  EmulatorApp* emulator_app = &emulator_app_;
  emulator_app->emulator_->processor()->set_debug_listener(nullptr);
  emulator_app->debug_window_.reset();
}

EmulatorApp::EmulatorApp(xe::ui::WindowedAppContext& app_context)
    : xe::ui::WindowedApp(app_context, "xenia", "[Path to .iso/.xex]"),
      debug_window_closed_listener_(*this) {
  AddPositionalOption("target");
}

EmulatorApp::~EmulatorApp() {
  // Should be shut down from OnDestroy if OnInitialize has ever been done, but
  // for the most safety as a running thread may be destroyed only after
  // joining.
  ShutdownEmulatorThreadFromUIThread();
}

std::unique_ptr<apu::AudioSystem> EmulatorApp::CreateAudioSystem(
    cpu::Processor* processor) {
  Factory<apu::AudioSystem, cpu::Processor*> factory;
#if XE_PLATFORM_WIN32
  factory.Add<apu::xaudio2::XAudio2AudioSystem>("xaudio2");
#endif  // XE_PLATFORM_WIN32
#if !XE_PLATFORM_ANDROID
  factory.Add<apu::sdl::SDLAudioSystem>("sdl");
#endif  // !XE_PLATFORM_ANDROID
  factory.Add<apu::nop::NopAudioSystem>("nop");
  return factory.Create(cvars::apu, processor);
}

std::unique_ptr<gpu::GraphicsSystem> EmulatorApp::CreateGraphicsSystem() {
  // While Vulkan is supported by a large variety of operating systems (Windows,
  // GNU/Linux, Android, also via the MoltenVK translation layer on top of Metal
  // on macOS and iOS), please don't remove platform-specific GPU backends from
  // Xenia.
  //
  // Regardless of the operating system, having multiple options provides more
  // stability to users. In case of driver issues, users may try switching
  // between the available backends. For example, in June 2022, on Nvidia Ampere
  // (RTX 30xx), Xenia had synchronization issues that resulted in flickering,
  // most prominently in 4D5307E6, on Direct3D 12 - but the same issue was not
  // reproducible in the Vulkan backend, however, it used ImageSampleExplicitLod
  // with explicit gradients for cubemaps, which triggered a different driver
  // bug on Nvidia (every 1 out of 2x2 pixels receiving junk).
  //
  // Specifically on Microsoft platforms, there are a few reasons why supporting
  // Direct3D 12 is desirable rather than limiting Xenia to Vulkan only:
  // - Wider hardware support for Direct3D 12 on x86 Windows desktops.
  //   Direct3D 12 requires the minimum of Nvidia Fermi, or, with a pre-2021
  //   driver version, Intel HD Graphics 4200. Vulkan, however, is supported
  //   only starting with Nvidia Kepler and a much more recent Intel UHD
  //   Graphics generation.
  // - Wider hardware support on other kinds of Microsoft devices. The Xbox One
  //   and the Xbox Series X|S only support Direct3D as the GPU API in their UWP
  //   runtime, and only version 12 can be granted expanded resource access.
  //   Qualcomm, as of June 2022, also doesn't provide a Vulkan implementation
  //   for their Arm-based Windows devices, while Direct3D 12 is available.
  //   - Both older Intel GPUs and the Xbox One apparently, as well as earlier
  //     Windows 10 versions, also require Shader Model 5.1 DXBC shaders rather
  //     than Shader Model 6 DXIL ones, so a DXBC shader translator should be
  //     available in Xenia too, a DXIL one doesn't fully replace it.
  // - As of June 2022, AMD also refuses to implement the
  //   VK_EXT_fragment_shader_interlock Vulkan extension in their drivers, as
  //   well as its OpenGL counterpart, which is heavily utilized for accurate
  //   support of Xenos render target formats that don't have PC equivalents
  //   (8_8_8_8_GAMMA, 2_10_10_10_FLOAT, 16_16 and 16_16_16_16 with -32 to 32
  //   range, D24FS8) with correct blending. Direct3D 12, however, requires
  //   support for similar functionality (rasterizer-ordered views) on the
  //   feature level 12_1, and the AMD driver implements it on Direct3D, as well
  //   as raster order groups in their Metal driver.
  //
  // Additionally, different host GPU APIs receive feature support at different
  // paces. VK_EXT_fragment_shader_interlock first appeared in 2019, for
  // instance, while Xenia had been taking advantage of rasterizer-ordered views
  // on Direct3D 12 for over half a year at that point (they have existed in
  // Direct3D 12 since the first version).
  //
  // MoltenVK on top Metal also has its flaws and limitations. Metal, for
  // instance, as of June 2022, doesn't provide a switch for primitive restart,
  // while Vulkan does - so MoltenVK is not completely transparent to Xenia,
  // many of its issues that may be not very obvious (unlike when the Metal API
  // is used directly) should be taken into account in Xenia. Also, as of June
  // 2022, MoltenVK translates SPIR-V shaders into the C++-based Metal Shading
  // Language rather than AIR directly, which likely massively increases
  // pipeline object creation time - and Xenia translates shaders and creates
  // pipelines when they're first actually used for a draw command by the game,
  // thus it can't precompile anything that hasn't ever been encountered before
  // there's already no time to waste.
  //
  // Very old hardware (Direct3D 10 level) is also not supported by most Vulkan
  // drivers. However, in the future, Xenia may be ported to it using the
  // Direct3D 11 API with the feature level 10_1 or 10_0. OpenGL, however, had
  // been lagging behind Direct3D prior to versions 4.x, and didn't receive
  // compute shaders until a 4.2 extension (while 4.2 already corresponds
  // roughly to Direct3D 11 features) - and replacing Xenia compute shaders with
  // transform feedback / stream output is not always trivial (in particular,
  // will need to rely on GL_ARB_transform_feedback3 for skipping over memory
  // locations that shouldn't be overwritten).
  //
  // For maintainability, as much implementation code as possible should be
  // placed in `xe::gpu` and shared between the backends rather than duplicated
  // between them.
  Factory<gpu::GraphicsSystem> factory;
#if XE_PLATFORM_WIN32
  factory.Add<gpu::d3d12::D3D12GraphicsSystem>("d3d12");
#endif  // XE_PLATFORM_WIN32
  factory.Add<gpu::vulkan::VulkanGraphicsSystem>("vulkan");
  factory.Add<gpu::null::NullGraphicsSystem>("null");
  return factory.Create(cvars::gpu);
}

std::vector<std::unique_ptr<hid::InputDriver>> EmulatorApp::CreateInputDrivers(
    ui::Window* window) {
  std::vector<std::unique_ptr<hid::InputDriver>> drivers;
  if (cvars::hid.compare("nop") == 0) {
    drivers.emplace_back(
        xe::hid::nop::Create(window, EmulatorWindow::kZOrderHidInput));
  } else {
    Factory<hid::InputDriver, ui::Window*, size_t> factory;
#if XE_PLATFORM_WIN32
    factory.Add("xinput", xe::hid::xinput::Create);
#endif  // XE_PLATFORM_WIN32
#if !XE_PLATFORM_ANDROID
    factory.Add("sdl", xe::hid::sdl::Create);
#endif  // !XE_PLATFORM_ANDROID
#if XE_PLATFORM_WIN32
    // WinKey input driver should always be the last input driver added!
    factory.Add("winkey", xe::hid::winkey::Create);
#endif  // XE_PLATFORM_WIN32
    for (auto& driver : factory.CreateAll(cvars::hid, window,
                                          EmulatorWindow::kZOrderHidInput)) {
      if (XSUCCEEDED(driver->Setup())) {
        drivers.emplace_back(std::move(driver));
      }
    }
    if (drivers.empty()) {
      // Fallback to nop if none created.
      drivers.emplace_back(
          xe::hid::nop::Create(window, EmulatorWindow::kZOrderHidInput));
    }
  }
  return drivers;
}

bool EmulatorApp::OnInitialize() {
#if XE_ARCH_AMD64 == 1
  amd64::InitFeatureFlags();
#endif
  Profiler::Initialize();
  Profiler::ThreadEnter("Main");

  // Figure out where internal files and content should go.
  std::filesystem::path storage_root = cvars::storage_root;
  if (storage_root.empty()) {
    storage_root = xe::filesystem::GetExecutableFolder();
    if (!cvars::portable &&
        !std::filesystem::exists(storage_root / "portable.txt")) {
      storage_root = xe::filesystem::GetUserFolder();
#if defined(XE_PLATFORM_WIN32) || defined(XE_PLATFORM_GNU_LINUX)
      storage_root = storage_root / "Xenia";
#else
      // TODO(Triang3l): Point to the app's external storage "files" directory
      // on Android.
#warning Unhandled platform for the data root.
      storage_root = storage_root / "Xenia";
#endif
    }
  }
  storage_root = std::filesystem::absolute(storage_root);
  XELOGI("Storage root: {}", storage_root);

  config::SetupConfig(storage_root);

  std::filesystem::path content_root = cvars::content_root;
  if (content_root.empty()) {
    content_root = storage_root / "content";
  } else {
    // If content root isn't an absolute path, then it should be relative to the
    // storage root.
    if (!content_root.is_absolute()) {
      content_root = storage_root / content_root;
    }
  }
  content_root = std::filesystem::absolute(content_root);
  XELOGI("Content root: {}", content_root);

  std::filesystem::path cache_root = cvars::cache_root;
  if (cache_root.empty()) {
    cache_root = storage_root / "cache_host";
    // TODO(Triang3l): Point to the app's external storage "cache" directory on
    // Android.
  } else {
    // If content root isn't an absolute path, then it should be relative to the
    // storage root.
    if (!cache_root.is_absolute()) {
      cache_root = storage_root / cache_root;
    }
  }
  cache_root = std::filesystem::absolute(cache_root);
  XELOGI("Host cache root: {}", cache_root);

  if (cvars::discord) {
    discord::DiscordPresence::Initialize();
    discord::DiscordPresence::NotPlaying();
  }

  // Create the emulator but don't initialize so we can setup the window.
  emulator_ =
      std::make_unique<Emulator>("", storage_root, content_root, cache_root);

  // Determine window size based on user setting.
  auto res = xe::gpu::GraphicsSystem::GetInternalDisplayResolution();

  // Main emulator display window.
  emulator_window_ = EmulatorWindow::Create(emulator_.get(), app_context(),
                                            res.first, res.second);
  if (!emulator_window_) {
    XELOGE("Failed to create the main emulator window");
    return false;
  }

  // Setup the emulator and run its loop in a separate thread.
  emulator_thread_quit_requested_.store(false, std::memory_order_relaxed);
  emulator_thread_event_ = xe::threading::Event::CreateAutoResetEvent(false);
  assert_not_null(emulator_thread_event_);
  emulator_thread_ = std::thread(&EmulatorApp::EmulatorThread, this);

  return true;
}

void EmulatorApp::OnDestroy() {
  ShutdownEmulatorThreadFromUIThread();

  if (cvars::discord) {
    discord::DiscordPresence::Shutdown();
  }

  Profiler::Dump();
  // The profiler needs to shut down before the graphics context.
  Profiler::Shutdown();

  // Write all cvar overrides to the config.
  config::SaveConfig();

  // TODO(DrChat): Remove this code and do a proper exit.
  XELOGI("Cheap-skate exit!");
  std::quick_exit(EXIT_SUCCESS);
}

void EmulatorApp::EmulatorThread() {
  assert_not_null(emulator_thread_event_);

  xe::threading::set_name("Emulator");
  Profiler::ThreadEnter("Emulator");

  // Setup and initialize all subsystems. If we can't do something
  // (unsupported system, memory issues, etc) this will fail early.
  X_STATUS result = emulator_->Setup(
      emulator_window_->window(), emulator_window_->imgui_drawer(), true,
      CreateAudioSystem, CreateGraphicsSystem, CreateInputDrivers);
  if (XFAILED(result)) {
    XELOGE("Failed to setup emulator: {:08X}", result);
    app_context().RequestDeferredQuit();
    return;
  }

  app_context().CallInUIThread(
      [this]() { emulator_window_->SetupGraphicsSystemPresenterPainting(); });

  if (cvars::mount_scratch) {
    auto scratch_device = std::make_unique<xe::vfs::HostPathDevice>(
        "\\SCRATCH", "scratch", false);
    if (!scratch_device->Initialize()) {
      XELOGE("Unable to scan scratch path");
    } else {
      if (!emulator_->file_system()->RegisterDevice(
              std::move(scratch_device))) {
        XELOGE("Unable to register scratch path");
      } else {
        emulator_->file_system()->RegisterSymbolicLink("scratch:", "\\SCRATCH");
      }
    }
  }

  if (cvars::mount_cache) {
    auto cache0_device =
        std::make_unique<xe::vfs::HostPathDevice>("\\CACHE0", "cache0", false);
    if (!cache0_device->Initialize()) {
      XELOGE("Unable to scan cache0 path");
    } else {
      if (!emulator_->file_system()->RegisterDevice(std::move(cache0_device))) {
        XELOGE("Unable to register cache0 path");
      } else {
        emulator_->file_system()->RegisterSymbolicLink("cache0:", "\\CACHE0");
      }
    }

    auto cache1_device =
        std::make_unique<xe::vfs::HostPathDevice>("\\CACHE1", "cache1", false);
    if (!cache1_device->Initialize()) {
      XELOGE("Unable to scan cache1 path");
    } else {
      if (!emulator_->file_system()->RegisterDevice(std::move(cache1_device))) {
        XELOGE("Unable to register cache1 path");
      } else {
        emulator_->file_system()->RegisterSymbolicLink("cache1:", "\\CACHE1");
      }
    }

    // Some (older?) games try accessing cache:\ too
    // NOTE: this must be registered _after_ the cache0/cache1 devices, due to
    // substring/start_with logic inside VirtualFileSystem::ResolvePath, else
    // accesses to those devices will go here instead
    auto cache_device =
        std::make_unique<xe::vfs::HostPathDevice>("\\CACHE", "cache", false);
    if (!cache_device->Initialize()) {
      XELOGE("Unable to scan cache path");
    } else {
      if (!emulator_->file_system()->RegisterDevice(std::move(cache_device))) {
        XELOGE("Unable to register cache path");
      } else {
        emulator_->file_system()->RegisterSymbolicLink("cache:", "\\CACHE");
      }
    }
  }

  // Set a debug handler.
  // This will respond to debugging requests so we can open the debug UI.
  if (cvars::debug) {
    emulator_->processor()->set_debug_listener_request_handler(
        [this](xe::cpu::Processor* processor) {
          if (debug_window_) {
            return debug_window_.get();
          }
          app_context().CallInUIThreadSynchronous([this]() {
            debug_window_ = xe::debug::ui::DebugWindow::Create(emulator_.get(),
                                                               app_context());
            debug_window_->window()->AddListener(
                &debug_window_closed_listener_);
          });
          // If failed to enqueue the UI thread call, this will just be null.
          return debug_window_.get();
        });
  }

  emulator_->on_launch.AddListener([&](auto title_id, const auto& game_title) {
    if (cvars::discord) {
      discord::DiscordPresence::PlayingTitle(
          game_title.empty() ? "Unknown Title" : std::string(game_title));
    }
    app_context().CallInUIThread([this]() { emulator_window_->UpdateTitle(); });
    emulator_thread_event_->Set();
  });

  emulator_->on_shader_storage_initialization.AddListener(
      [this](bool initializing) {
        app_context().CallInUIThread([this, initializing]() {
          emulator_window_->SetInitializingShaderStorage(initializing);
        });
      });

  emulator_->on_patch_apply.AddListener([this]() {
    app_context().CallInUIThread([this]() { emulator_window_->UpdateTitle(); });
  });

  emulator_->on_terminate.AddListener([]() {
    if (cvars::discord) {
      discord::DiscordPresence::NotPlaying();
    }
  });

  // Enable emulator input now that the emulator is properly loaded.
  app_context().CallInUIThread(
      [this]() { emulator_window_->OnEmulatorInitialized(); });

  // Grab path from the flag or unnamed argument.
  std::filesystem::path path;
  if (!cvars::target.empty()) {
    path = cvars::target;
  }

  if (!path.empty()) {
    // Normalize the path and make absolute.
    auto abs_path = std::filesystem::absolute(path);

    result = app_context().CallInUIThread(
        [this, abs_path]() { return emulator_window_->RunTitle(abs_path); });
    if (XFAILED(result)) {
      xe::FatalError(fmt::format("Failed to launch target: {:08X}", result));
      app_context().RequestDeferredQuit();
      return;
    }
  }

  auto xam = emulator_->kernel_state()->GetKernelModule<kernel::xam::XamModule>(
      "xam.xex");

  if (xam) {
    xam->LoadLoaderData();

    if (xam->loader_data().launch_data_present) {
      const std::filesystem::path host_path = xam->loader_data().host_path;
      app_context().CallInUIThread([this, host_path]() {
        return emulator_window_->RunTitle(host_path);
      });
    }
  }

  // Now, we're going to use this thread to drive events related to emulation.
  while (!emulator_thread_quit_requested_.load(std::memory_order_relaxed)) {
    xe::threading::Wait(emulator_thread_event_.get(), false);
    emulator_->WaitUntilExit();
  }
}

void EmulatorApp::ShutdownEmulatorThreadFromUIThread() {
  // TODO(Triang3l): Proper shutdown of the emulator (relying on std::quick_exit
  // for now) - currently WaitUntilExit loops forever otherwise (plus possibly
  // lots of other things not shutting down correctly now). Some parts of the
  // code call the regular std::exit, which seems to be calling destructors (at
  // least on Linux), so the entire join is currently commented out.
#if 0
  // Same thread as the one created it, to make sure there's zero possibility of
  // a race with the creation of the emulator thread.
  assert_true(app_context().IsInUIThread());
  emulator_thread_quit_requested_.store(true, std::memory_order_relaxed);
  if (!emulator_thread_.joinable()) {
    return;
  }
  emulator_thread_event_->Set();
  emulator_thread_.join();
#endif
}

}  // namespace app
}  // namespace xe

XE_DEFINE_WINDOWED_APP(xenia, xe::app::EmulatorApp::Create);
