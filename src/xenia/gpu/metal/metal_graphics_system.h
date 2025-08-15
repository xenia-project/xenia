/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_METAL_GRAPHICS_SYSTEM_H
#define XENIA_GPU_METAL_GRAPHICS_SYSTEM_H

#include <memory>

#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/graphics_system.h"

#include "third_party/metal-cpp/Metal/Metal.hpp"

namespace xe {
namespace gpu {
namespace metal {
class MetalGraphicsSystem : public GraphicsSystem {
    public:
        MetalGraphicsSystem();
        ~MetalGraphicsSystem();

        static bool IsAvailable();

        std::string name() const override;

        X_STATUS Setup(cpu::Processor* processor, kernel::KernelState* kernel_state,
                     ui::WindowedAppContext* app_context,
                     bool is_surface_required) override;
    protected:
            std::unique_ptr<CommandProcessor> CreateCommandProcessor() override;
};

} // namespace metal
} // namespace gpu
} // namespace xe

#endif // XENIA_GPU_METAL_GRAPHICS_SYSTEM_H
