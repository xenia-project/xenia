/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

 #ifndef XENIA_GPU_METAL_COMMAND_PROCESSOR_H
 #define XENIA_GPU_METAL_COMMAND_PROCESSOR_H

 #include "third_party/metal-shader-converter/include/metal_irconverter/metal_irconverter.h"

#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/metal/metal_graphics_system.h"
#include "xenia/gpu/metal/metal_shared_memory.h"
#include "xenia/ui/metal/metal_provider.h"


namespace xe {
namespace gpu {
namespace metal {

class MetalCommandProcessor : public CommandProcessor {
public:
    explicit MetalCommandProcessor(MetalGraphicsSystem* graphics_system,
                                    kernel::KernelState* kernel_state);
    ~MetalCommandProcessor();

    void TracePlaybackWroteMemory(uint32_t base_ptr, uint32_t length) override;

    void RestoreEdramSnapshot(const void* snapshot) override;

    ui::metal::MetalProvider& GetMetalProvider() const {
        return *static_cast<ui::metal::MetalProvider*>(
            graphics_system_->provider());
    }

protected:
    bool SetupContext() override;
    void ShutdownContext() override;

    void IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                    uint32_t frontbuffer_height) override;

    Shader* LoadShader(xenos::ShaderType shader_type, uint32_t guest_address,
                     const uint32_t* host_address,
                     uint32_t dword_count) override;

    bool IssueDraw(xenos::PrimitiveType primitive_type, uint32_t index_count,
                    IndexBufferInfo* index_buffer_info,
                    bool major_mode_explicit) override;
    bool IssueCopy() override;

private:
    MTL::Device* device_ = nullptr;
    MTL::CommandQueue* command_queue_ = nullptr;
    MTL::CommandBuffer* current_command_buffer_ = nullptr;
    MTL::RenderCommandEncoder* current_render_encoder_ = nullptr;
    std::unique_ptr<MetalSharedMemory> shared_memory_ = nullptr;
};

}
}
}

#endif