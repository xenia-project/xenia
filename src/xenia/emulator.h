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

#include <xenia/common.h>
#include <xenia/core.h>
#include <xenia/xbox.h>
#include <xenia/cpu/xenon_memory.h>


XEDECLARECLASS1(xe, ExportResolver);
XEDECLARECLASS2(xe, apu, AudioSystem);
XEDECLARECLASS2(xe, cpu, Processor);
XEDECLARECLASS2(xe, debug, DebugServer);
XEDECLARECLASS2(xe, gpu, GraphicsSystem);
XEDECLARECLASS2(xe, hid, InputSystem);
XEDECLARECLASS2(xe, kernel, KernelState);
XEDECLARECLASS2(xe, kernel, XamModule);
XEDECLARECLASS2(xe, kernel, XboxkrnlModule);
XEDECLARECLASS3(xe, kernel, fs, FileSystem);
XEDECLARECLASS2(xe, ui, Window);


namespace xe {


class Emulator {
public:
  Emulator(const xechar_t* command_line);
  ~Emulator();

  const xechar_t* command_line() const { return command_line_; }

  ui::Window* main_window() const { return main_window_; }
  void set_main_window(ui::Window* window);

  cpu::XenonMemory* memory() const { return memory_; }

  debug::DebugServer* debug_server() const { return debug_server_; }

  cpu::Processor* processor() const { return processor_; }
  apu::AudioSystem* audio_system() const { return audio_system_; }
  gpu::GraphicsSystem* graphics_system() const { return graphics_system_; }
  hid::InputSystem* input_system() const { return input_system_; }

  ExportResolver* export_resolver() const { return export_resolver_; }
  kernel::fs::FileSystem* file_system() const { return file_system_; }

  kernel::XboxkrnlModule* xboxkrnl() const { return xboxkrnl_; }
  kernel::XamModule* xam() const { return xam_; }

  X_STATUS Setup();

  // TODO(benvanik): raw binary.
  X_STATUS LaunchXexFile(const xechar_t* path);
  X_STATUS LaunchDiscImage(const xechar_t* path);
  X_STATUS LaunchSTFSTitle(const xechar_t* path);

private:
  xechar_t                command_line_[poly::max_path];

  ui::Window*             main_window_;

  cpu::XenonMemory*       memory_;

  debug::DebugServer*     debug_server_;

  cpu::Processor*         processor_;
  apu::AudioSystem*       audio_system_;
  gpu::GraphicsSystem*    graphics_system_;
  hid::InputSystem*       input_system_;

  ExportResolver*         export_resolver_;
  kernel::fs::FileSystem* file_system_;

  kernel::KernelState*    kernel_state_;
  kernel::XamModule*      xam_;
  kernel::XboxkrnlModule* xboxkrnl_;
};


}  // namespace xe


#endif  // XENIA_EMULATOR_H_
