/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_EMULATOR_H_
#define XENIA_EMULATOR_H_

#include <functional>
#include <string>

#include "xenia/base/exception_handler.h"
#include "xenia/debug/debugger.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/memory.h"
#include "xenia/vfs/virtual_file_system.h"
#include "xenia/xbox.h"

namespace xe {
namespace apu {
class AudioSystem;
}  // namespace apu
namespace cpu {
class ExportResolver;
class Processor;
class ThreadState;
}  // namespace cpu
namespace gpu {
class GraphicsSystem;
}  // namespace gpu
namespace hid {
class InputDriver;
class InputSystem;
}  // namespace hid
namespace ui {
class Window;
}  // namespace ui
}  // namespace xe

namespace xe {

class Emulator {
 public:
  explicit Emulator(const std::wstring& command_line);
  ~Emulator();

  const std::wstring& command_line() const { return command_line_; }

  ui::Window* display_window() const { return display_window_; }

  Memory* memory() const { return memory_.get(); }

  debug::Debugger* debugger() const { return debugger_.get(); }

  cpu::Processor* processor() const { return processor_.get(); }
  apu::AudioSystem* audio_system() const { return audio_system_.get(); }
  gpu::GraphicsSystem* graphics_system() const {
    return graphics_system_.get();
  }
  hid::InputSystem* input_system() const { return input_system_.get(); }

  cpu::ExportResolver* export_resolver() const {
    return export_resolver_.get();
  }
  vfs::VirtualFileSystem* file_system() const { return file_system_.get(); }

  kernel::KernelState* kernel_state() const { return kernel_state_.get(); }

  X_STATUS Setup(
      ui::Window* display_window,
      std::function<std::unique_ptr<apu::AudioSystem>(cpu::Processor*)>
          audio_system_factory,
      std::function<std::unique_ptr<gpu::GraphicsSystem>()>
          graphics_system_factory,
      std::function<std::vector<std::unique_ptr<hid::InputDriver>>(ui::Window*)>
          input_driver_factory);

  X_STATUS LaunchPath(std::wstring path);
  X_STATUS LaunchXexFile(std::wstring path);
  X_STATUS LaunchDiscImage(std::wstring path);
  X_STATUS LaunchStfsContainer(std::wstring path);

  void Pause();
  void Resume();

  bool SaveToFile(const std::wstring& path);
  bool RestoreFromFile(const std::wstring& path);

  void WaitUntilExit();

 private:
  static bool ExceptionCallbackThunk(Exception* ex, void* data);
  bool ExceptionCallback(Exception* ex);

  X_STATUS CompleteLaunch(const std::wstring& path,
                          const std::string& module_path);

  std::wstring command_line_;

  ui::Window* display_window_;

  std::unique_ptr<Memory> memory_;

  std::unique_ptr<debug::Debugger> debugger_;

  std::unique_ptr<cpu::Processor> processor_;
  std::unique_ptr<apu::AudioSystem> audio_system_;
  std::unique_ptr<gpu::GraphicsSystem> graphics_system_;
  std::unique_ptr<hid::InputSystem> input_system_;

  std::unique_ptr<cpu::ExportResolver> export_resolver_;
  std::unique_ptr<vfs::VirtualFileSystem> file_system_;

  std::unique_ptr<kernel::KernelState> kernel_state_;
  threading::Thread* main_thread_ = nullptr;

  bool paused_ = false;
  bool restoring_ = false;
  threading::Fence restore_fence_;  // Fired on restore finish.
};

}  // namespace xe

#endif  // XENIA_EMULATOR_H_
