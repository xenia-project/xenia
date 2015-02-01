/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_GL4_GL4_GRAPHICS_SYSTEM_H_
#define XENIA_GPU_GL4_GL4_GRAPHICS_SYSTEM_H_

#include <memory>

#include "xenia/common.h"
#include "xenia/gpu/gl4/command_processor.h"
#include "xenia/gpu/gl4/wgl_control.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/gpu/register_file.h"

namespace xe {
namespace gpu {
namespace gl4 {

class GL4GraphicsSystem : public GraphicsSystem {
 public:
  GL4GraphicsSystem(Emulator* emulator);
  ~GL4GraphicsSystem() override;

  X_STATUS Setup() override;
  void Shutdown() override;

  RegisterFile* register_file() { return &register_file_; }

  void InitializeRingBuffer(uint32_t ptr, uint32_t page_count) override;
  void EnableReadPointerWriteBack(uint32_t ptr, uint32_t block_size) override;

 private:
  void MarkVblank();
  void SwapHandler(const SwapParameters& swap_params);
  uint64_t ReadRegister(uint64_t addr);
  void WriteRegister(uint64_t addr, uint64_t value);

  static uint64_t MMIOReadRegisterThunk(GL4GraphicsSystem* gs, uint64_t addr) {
    return gs->ReadRegister(addr);
  }
  static void MMIOWriteRegisterThunk(GL4GraphicsSystem* gs, uint64_t addr,
                                     uint64_t value) {
    gs->WriteRegister(addr, value);
  }
  static void __stdcall VsyncCallbackThunk(GL4GraphicsSystem* gs, BOOLEAN) {
    gs->MarkVblank();
  }

  RegisterFile register_file_;
  std::unique_ptr<CommandProcessor> command_processor_;
  std::unique_ptr<WGLControl> control_;

  HANDLE timer_queue_;
  HANDLE vsync_timer_;
};

}  // namespace gl4
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_GL4_GL4_GRAPHICS_SYSTEM_H_
