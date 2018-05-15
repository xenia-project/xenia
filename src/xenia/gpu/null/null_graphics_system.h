/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_NULL_NULL_GRAPHICS_SYSTEM_H_
#define XENIA_GPU_NULL_NULL_GRAPHICS_SYSTEM_H_

#include <memory>

#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/graphics_system.h"

namespace xe {
namespace gpu {
namespace null {

class NullGraphicsSystem : public GraphicsSystem {
 public:
  NullGraphicsSystem();
  ~NullGraphicsSystem() override;

  std::wstring name() const override { return L"null"; }
  SwapState* swap_state() override { return &swap_state_; }

  X_STATUS Setup(cpu::Processor* processor, kernel::KernelState* kernel_state,
                 std::unique_ptr<ui::GraphicsContext> context) override;
  void Shutdown() override;

 private:
  std::unique_ptr<CommandProcessor> CreateCommandProcessor() override;

  SwapState swap_state_ = {};
};

}  // namespace null
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_NULL_NULL_GRAPHICS_SYSTEM_H_