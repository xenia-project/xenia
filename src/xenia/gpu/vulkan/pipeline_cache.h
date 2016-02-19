/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_VULKAN_PIPELINE_CACHE_H_
#define XENIA_GPU_VULKAN_PIPELINE_CACHE_H_

#include <unordered_map>

#include "xenia/gpu/register_file.h"
#include "xenia/gpu/spirv_shader_translator.h"
#include "xenia/gpu/vulkan/vulkan_shader.h"
#include "xenia/gpu/xenos.h"
#include "xenia/ui/spirv/spirv_disassembler.h"
#include "xenia/ui/vulkan/vulkan.h"
#include "xenia/ui/vulkan/vulkan_device.h"

namespace xe {
namespace gpu {
namespace vulkan {

// Configures and caches pipelines based on render state.
// This is responsible for properly setting all state required for a draw
// including shaders, various blend/etc options, and input configuration.
class PipelineCache {
 public:
  PipelineCache(RegisterFile* register_file, ui::vulkan::VulkanDevice* device);
  ~PipelineCache();

  // Loads a shader from the cache, possibly translating it.
  VulkanShader* LoadShader(ShaderType shader_type, uint32_t guest_address,
                           const uint32_t* host_address, uint32_t dword_count);

  // Configures a pipeline using the current render state and the given render
  // pass. If a previously available pipeline is available it will be used,
  // otherwise a new one may be created. Any state that can be set dynamically
  // in the command buffer is issued at this time.
  // Returns whether the pipeline could be successfully created.
  bool ConfigurePipeline(VkCommandBuffer command_buffer,
                         VkRenderPass render_pass, VulkanShader* vertex_shader,
                         VulkanShader* pixel_shader,
                         PrimitiveType primitive_type);

  // Currently configured pipeline layout, if any.
  VkPipelineLayout current_pipeline_layout() const { return nullptr; }

  // Clears all cached content.
  void ClearCache();

 private:
  // TODO(benvanik): geometry shader cache.
  // TODO(benvanik): translated shader cache.
  // TODO(benvanik): pipeline layouts.
  // TODO(benvanik): pipeline cache.

  RegisterFile* register_file_ = nullptr;
  VkDevice device_ = nullptr;

  SpirvShaderTranslator shader_translator_;
  xe::ui::spirv::SpirvDisassembler disassembler_;
  // All loaded shaders mapped by their guest hash key.
  std::unordered_map<uint64_t, VulkanShader*> shader_map_;

 private:
  enum class UpdateStatus {
    kCompatible,
    kMismatch,
    kError,
  };

  UpdateStatus UpdateShaders(PrimitiveType prim_type);
  UpdateStatus UpdateRenderTargets();
  UpdateStatus UpdateState(PrimitiveType prim_type);
  UpdateStatus UpdateViewportState();
  UpdateStatus UpdateRasterizerState(PrimitiveType prim_type);
  UpdateStatus UpdateBlendState();
  UpdateStatus UpdateDepthStencilState();

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

#endif  // XENIA_GPU_VULKAN_PIPELINE_CACHE_H_
