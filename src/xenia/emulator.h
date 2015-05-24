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

#include <string>

#include "xenia/debug/debugger.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/memory.h"
#include "xenia/ui/main_window.h"
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
class InputSystem;
}  // namespace hid
namespace ui {
class MainWindow;
}  // namespace ui
}  // namespace xe

namespace xe {

class Emulator {
 public:
  Emulator(const std::wstring& command_line);
  ~Emulator();

  const std::wstring& command_line() const { return command_line_; }

  ui::MainWindow* main_window() const { return main_window_.get(); }

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
  kernel::fs::FileSystem* file_system() const { return file_system_.get(); }

  kernel::KernelState* kernel_state() const { return kernel_state_.get(); }

  X_STATUS Setup();

  // TODO(benvanik): raw binary.
  X_STATUS LaunchXexFile(const std::wstring& path);
  X_STATUS LaunchDiscImage(const std::wstring& path);
  X_STATUS LaunchSTFSTitle(const std::wstring& path);

 private:
  X_STATUS CompleteLaunch(const std::wstring& path,
                          const std::string& module_path);

  std::wstring command_line_;

  std::unique_ptr<ui::MainWindow> main_window_;

  std::unique_ptr<Memory> memory_;

  std::unique_ptr<debug::Debugger> debugger_;

  std::unique_ptr<cpu::Processor> processor_;
  std::unique_ptr<apu::AudioSystem> audio_system_;
  std::unique_ptr<gpu::GraphicsSystem> graphics_system_;
  std::unique_ptr<hid::InputSystem> input_system_;

  std::unique_ptr<cpu::ExportResolver> export_resolver_;
  std::unique_ptr<kernel::fs::FileSystem> file_system_;

  std::unique_ptr<kernel::KernelState> kernel_state_;
};

}  // namespace xe

#endif  // XENIA_EMULATOR_H_
