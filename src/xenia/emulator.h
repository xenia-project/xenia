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


XEDECLARECLASS1(xe, ExportResolver);
XEDECLARECLASS2(xe, apu, AudioSystem);
XEDECLARECLASS2(xe, cpu, Processor);
XEDECLARECLASS2(xe, dbg, Debugger);
XEDECLARECLASS2(xe, gpu, GraphicsSystem);
XEDECLARECLASS2(xe, hid, InputSystem);
XEDECLARECLASS3(xe, kernel, xam, XamModule);
XEDECLARECLASS3(xe, kernel, xboxkrnl, XboxkrnlModule);
XEDECLARECLASS4(xe, kernel, xboxkrnl, fs, FileSystem);


namespace xe {


class Emulator {
public:
  Emulator(const xechar_t* command_line);
  ~Emulator();

  const xechar_t* command_line() const { return command_line_; }
  Memory* memory() const { return memory_; }

  dbg::Debugger* debugger() const { return debugger_; }

  cpu::Processor* processor() const { return processor_; }
  apu::AudioSystem* audio_system() const { return audio_system_; }
  gpu::GraphicsSystem* graphics_system() const { return graphics_system_; }
  hid::InputSystem* input_system() const { return input_system_; }

  ExportResolver* export_resolver() const { return export_resolver_; }
  kernel::xboxkrnl::fs::FileSystem* file_system() const { return file_system_; }

  X_STATUS Setup();

  // TODO(benvanik): raw binary.
  X_STATUS LaunchXexFile(const xechar_t* path);
  X_STATUS LaunchDiscImage(const xechar_t* path);

private:
  xechar_t          command_line_[XE_MAX_PATH];
  Memory*           memory_;

  dbg::Debugger*          debugger_;

  cpu::Processor*         processor_;
  apu::AudioSystem*       audio_system_;
  gpu::GraphicsSystem*    graphics_system_;
  hid::InputSystem*       input_system_;

  ExportResolver*         export_resolver_;
  kernel::xboxkrnl::fs::FileSystem* file_system_;

  kernel::xboxkrnl::XboxkrnlModule* xboxkrnl_;
  kernel::xam::XamModule*           xam_;
};


}  // namespace xe


#endif  // XENIA_EMULATOR_H_
