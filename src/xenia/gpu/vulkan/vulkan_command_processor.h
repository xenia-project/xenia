/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_VULKAN_VULKAN_COMMAND_PROCESSOR_H_
#define XENIA_GPU_VULKAN_VULKAN_COMMAND_PROCESSOR_H_

#include <atomic>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include "xenia/base/threading.h"
#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/register_file.h"
#include "xenia/gpu/spirv_shader_translator.h"
#include "xenia/gpu/vulkan/vulkan_shader.h"
#include "xenia/gpu/xenos.h"
#include "xenia/kernel/xthread.h"
#include "xenia/memory.h"
#include "xenia/ui/vulkan/vulkan_context.h"

namespace xe {
namespace gpu {
namespace vulkan {

class VulkanGraphicsSystem;

class VulkanCommandProcessor : public CommandProcessor {
 public:
  VulkanCommandProcessor(VulkanGraphicsSystem* graphics_system,
                         kernel::KernelState* kernel_state);
  ~VulkanCommandProcessor() override;

  void ClearCaches() override;

 private:
  enum class UpdateStatus {
    kCompatible,
    kMismatch,
    kError,
  };

  bool SetupContext() override;
  void ShutdownContext() override;

  void MakeCoherent() override;
  void PrepareForWait() override;
  void ReturnFromWait() override;

  void PerformSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                   uint32_t frontbuffer_height) override;

  Shader* LoadShader(ShaderType shader_type, uint32_t guest_address,
                     const uint32_t* host_address,
                     uint32_t dword_count) override;

  bool IssueDraw(PrimitiveType prim_type, uint32_t index_count,
                 IndexBufferInfo* index_buffer_info) override;
  UpdateStatus UpdateShaders(PrimitiveType prim_type);
  UpdateStatus UpdateRenderTargets();
  UpdateStatus UpdateState(PrimitiveType prim_type);
  UpdateStatus UpdateViewportState();
  UpdateStatus UpdateRasterizerState(PrimitiveType prim_type);
  UpdateStatus UpdateBlendState();
  UpdateStatus UpdateDepthStencilState();
  UpdateStatus PopulateIndexBuffer(IndexBufferInfo* index_buffer_info);
  UpdateStatus PopulateVertexBuffers();
  UpdateStatus PopulateSamplers();
  UpdateStatus PopulateSampler(const Shader::TextureBinding& texture_binding);
  bool IssueCopy() override;

  SpirvShaderTranslator shader_translator_;

 private:
  bool SetShadowRegister(uint32_t* dest, uint32_t register_name);
  bool SetShadowRegister(float* dest, uint32_t register_name);
  struct UpdateRenderTargetsRegisters {
    uint32_t rb_modecontrol;
    uint32_t rb_surface_info;
    uint32_t rb_color_info;
    uint32_t rb_color1_info;
    uint32_t rb_color2_info;
    uint32_t rb_color3_info;
    uint32_t rb_color_mask;
    uint32_t rb_depthcontrol;
    uint32_t rb_stencilrefmask;
    uint32_t rb_depth_info;

    UpdateRenderTargetsRegisters() { Reset(); }
    void Reset() { std::memset(this, 0, sizeof(*this)); }
  } update_render_targets_regs_;
  struct UpdateViewportStateRegisters {
    // uint32_t pa_cl_clip_cntl;
    uint32_t rb_surface_info;
    uint32_t pa_cl_vte_cntl;
    uint32_t pa_su_sc_mode_cntl;
    uint32_t pa_sc_window_offset;
    uint32_t pa_sc_window_scissor_tl;
    uint32_t pa_sc_window_scissor_br;
    float pa_cl_vport_xoffset;
    float pa_cl_vport_yoffset;
    float pa_cl_vport_zoffset;
    float pa_cl_vport_xscale;
    float pa_cl_vport_yscale;
    float pa_cl_vport_zscale;

    UpdateViewportStateRegisters() { Reset(); }
    void Reset() { std::memset(this, 0, sizeof(*this)); }
  } update_viewport_state_regs_;
  struct UpdateRasterizerStateRegisters {
    uint32_t pa_su_sc_mode_cntl;
    uint32_t pa_sc_screen_scissor_tl;
    uint32_t pa_sc_screen_scissor_br;
    uint32_t multi_prim_ib_reset_index;
    PrimitiveType prim_type;

    UpdateRasterizerStateRegisters() { Reset(); }
    void Reset() { std::memset(this, 0, sizeof(*this)); }
  } update_rasterizer_state_regs_;
  struct UpdateBlendStateRegisters {
    uint32_t rb_blendcontrol[4];
    float rb_blend_rgba[4];

    UpdateBlendStateRegisters() { Reset(); }
    void Reset() { std::memset(this, 0, sizeof(*this)); }
  } update_blend_state_regs_;
  struct UpdateDepthStencilStateRegisters {
    uint32_t rb_depthcontrol;
    uint32_t rb_stencilrefmask;

    UpdateDepthStencilStateRegisters() { Reset(); }
    void Reset() { std::memset(this, 0, sizeof(*this)); }
  } update_depth_stencil_state_regs_;
  struct UpdateShadersRegisters {
    PrimitiveType prim_type;
    uint32_t pa_su_sc_mode_cntl;
    uint32_t sq_program_cntl;
    uint32_t sq_context_misc;
    VulkanShader* vertex_shader;
    VulkanShader* pixel_shader;

    UpdateShadersRegisters() { Reset(); }
    void Reset() {
      sq_program_cntl = 0;
      vertex_shader = pixel_shader = nullptr;
    }
  } update_shaders_regs_;
};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_VULKAN_COMMAND_PROCESSOR_H_
