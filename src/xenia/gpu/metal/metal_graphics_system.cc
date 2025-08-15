/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

 #include "xenia/gpu/metal/metal_graphics_system.h"

#include <algorithm>

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/gpu/metal/metal_command_processor.h"
#include "xenia/gpu/draw_util.h"
#include "xenia/ui/metal/metal_util.h"
#include "xenia/xbox.h"

 namespace xe {
 namespace gpu {
 namespace metal {

 MetalGraphicsSystem::MetalGraphicsSystem() {}

 MetalGraphicsSystem::~MetalGraphicsSystem() {}

 bool MetalGraphicsSystem::IsAvailable() {
     return xe::ui::metal::MetalProvider::IsMetalAPIAvailable();
 }

 std::string MetalGraphicsSystem::name() const {
     return "MetalGraphicsSystem";
 }

 X_STATUS MetalGraphicsSystem::Setup(cpu::Processor* processor, kernel::KernelState* kernel_state,
                                     ui::WindowedAppContext* app_context,
                                     bool is_surface_required) {
    provider_ = xe::ui::metal::MetalProvider::Create();
    return GraphicsSystem::Setup(processor, kernel_state, app_context, is_surface_required);
 }

 std::unique_ptr<CommandProcessor> MetalGraphicsSystem::CreateCommandProcessor() {
     return std::unique_ptr<CommandProcessor>(
        new MetalCommandProcessor(this, kernel_state_)
     );
 }

 }  // namespace metal
 }  // namespace gpu
 }  // namespace xe
