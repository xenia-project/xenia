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

#include "xenia/gpu/gl4/command_processor.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/gpu/register_file.h"
#include "xenia/kernel/objects/xthread.h"
#include "xenia/ui/gl/gl_context.h"

namespace xe {
namespace gpu {
namespace gl4 {

class GL4GraphicsSystem : public GraphicsSystem {
 public:
  GL4GraphicsSystem(Emulator* emulator);
  ~GL4GraphicsSystem() override;

  std::unique_ptr<ui::GraphicsContext> CreateContext(
      ui::Window* target_window) override;

  X_STATUS Setup(cpu::Processor* processor, ui::Loop* target_loop,
                 ui::Window* target_window) override;
  void Shutdown() override;

  RegisterFile* register_file() { return &register_file_; }
  CommandProcessor* command_processor() const {
    return command_processor_.get();
  }

  void InitializeRingBuffer(uint32_t ptr, uint32_t page_count) override;
  void EnableReadPointerWriteBack(uint32_t ptr, uint32_t block_size) override;

  void RequestFrameTrace() override;
  void BeginTracing() override;
  void EndTracing() override;
  void PlayTrace(const uint8_t* trace_data, size_t trace_size,
                 TracePlaybackMode playback_mode) override;
  void ClearCaches() override;

 private:
  void MarkVblank();
  void Swap(xe::ui::UIEvent& e);
  uint64_t ReadRegister(uint32_t addr);
  void WriteRegister(uint32_t addr, uint64_t value);

  static uint64_t MMIOReadRegisterThunk(void* ppc_context,
                                        GL4GraphicsSystem* gs, uint32_t addr) {
    return gs->ReadRegister(addr);
  }
  static void MMIOWriteRegisterThunk(void* ppc_context, GL4GraphicsSystem* gs,
                                     uint32_t addr, uint64_t value) {
    gs->WriteRegister(addr, value);
  }

  RegisterFile register_file_;
  std::unique_ptr<CommandProcessor> command_processor_;

  xe::ui::gl::GLContext* display_context_ = nullptr;

  std::atomic<bool> worker_running_;
  kernel::object_ref<kernel::XHostThread> worker_thread_;
};

}  // namespace gl4
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_GL4_GL4_GRAPHICS_SYSTEM_H_
