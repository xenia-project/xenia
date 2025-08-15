/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "xenia/gpu/metal/metal_command_processor.h"
#include "xenia/ui/metal/metal_presenter.h"
#include "xenia/ui/metal/metal_util.h"

namespace xe {
namespace gpu {
namespace metal {

MetalCommandProcessor::MetalCommandProcessor(
    MetalGraphicsSystem* graphics_system, kernel::KernelState* kernel_state)
        : CommandProcessor(graphics_system, kernel_state) {}

MetalCommandProcessor::~MetalCommandProcessor() = default;

void MetalCommandProcessor::TracePlaybackWroteMemory(uint32_t base_ptr,
                                                            uint32_t length) {}

void MetalCommandProcessor::RestoreEdramSnapshot(const void* snapshot) {}

bool MetalCommandProcessor::IssueCopy() { return false; }

bool MetalCommandProcessor::SetupContext() { 
    if (!CommandProcessor::SetupContext()) {
    XELOGE("Failed to initialize base command processor context");
    return false;
    }

    const ui::metal::MetalProvider& provider = GetMetalProvider();
    device_ = provider.GetDevice();
    command_queue_ = provider.GetCommandQueue();

    shared_memory_ = std::make_unique<MetalSharedMemory>(*this, *memory_);
    if (!shared_memory_->Initialize()) {
        XELOGE("Failed to initialize shared memory");
        return false;
    }

    XELOGD("MetalCommandProcessor::SetupContext() Completed Successfully");
    return true;
 }

void MetalCommandProcessor::ShutdownContext() {}

void MetalCommandProcessor::IssueSwap(
    uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
    uint32_t frontbuffer_height) {}

Shader* MetalCommandProcessor::LoadShader(
    xenos::ShaderType shader_type, uint32_t guest_address,
    const uint32_t* host_address, uint32_t dword_count) {
  return nullptr;
}

bool MetalCommandProcessor::IssueDraw(
    xenos::PrimitiveType primitive_type, uint32_t index_count,
    IndexBufferInfo* index_buffer_info, bool major_mode_explicit) {
  return false;
}

}
}
}