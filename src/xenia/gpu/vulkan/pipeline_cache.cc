/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/pipeline_cache.h"

#include "third_party/xxhash/xxhash.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/vulkan/vulkan_gpu_flags.h"

namespace xe {
namespace gpu {
namespace vulkan {

using xe::ui::vulkan::CheckResult;

PipelineCache::PipelineCache(RegisterFile* register_file,
                             ui::vulkan::VulkanDevice* device)
    : register_file_(register_file), device_(*device) {}

PipelineCache::~PipelineCache() {
  // Destroy all shaders.
  for (auto it : shader_map_) {
    delete it.second;
  }
  shader_map_.clear();
}

VulkanShader* PipelineCache::LoadShader(ShaderType shader_type,
                                        uint32_t guest_address,
                                        const uint32_t* host_address,
                                        uint32_t dword_count) {
  // Hash the input memory and lookup the shader.
  uint64_t data_hash = XXH64(host_address, dword_count * sizeof(uint32_t), 0);
  auto it = shader_map_.find(data_hash);
  if (it != shader_map_.end()) {
    // Shader has been previously loaded.
    return it->second;
  }

  // Always create the shader and stash it away.
  // We need to track it even if it fails translation so we know not to try
  // again.
  VulkanShader* shader =
      new VulkanShader(shader_type, data_hash, host_address, dword_count);
  shader_map_.insert({data_hash, shader});

  // Perform translation.
  // If this fails the shader will be marked as invalid and ignored later.
  if (!shader_translator_.Translate(shader)) {
    XELOGE("Shader translation failed; marking shader as ignored");
    return shader;
  }

  // Prepare the shader for use (creates our VkShaderModule).
  // It could still fail at this point.
  if (!shader->Prepare()) {
    XELOGE("Shader preparation failed; marking shader as ignored");
    return shader;
  }

  if (shader->is_valid()) {
    XELOGGPU("Generated %s shader at 0x%.8X (%db):\n%s",
             shader_type == ShaderType::kVertex ? "vertex" : "pixel",
             guest_address, dword_count * 4,
             shader->ucode_disassembly().c_str());
  }

  // Dump shader files if desired.
  if (!FLAGS_dump_shaders.empty()) {
    shader->Dump(FLAGS_dump_shaders, "vk");
  }

  return shader;
}

bool PipelineCache::ConfigurePipeline(VkCommandBuffer command_buffer,
                                      VkRenderPass render_pass,
                                      PrimitiveType primitive_type) {
  return false;
}

void PipelineCache::ClearCache() {
  // TODO(benvanik): caching.
}

bool PipelineCache::SetShadowRegister(uint32_t* dest, uint32_t register_name) {
  uint32_t value = register_file_->values[register_name].u32;
  if (*dest == value) {
    return false;
  }
  *dest = value;
  return true;
}

bool PipelineCache::SetShadowRegister(float* dest, uint32_t register_name) {
  float value = register_file_->values[register_name].f32;
  if (*dest == value) {
    return false;
  }
  *dest = value;
  return true;
}

PipelineCache::UpdateStatus PipelineCache::UpdateShaders(
    PrimitiveType prim_type) {
  auto& regs = update_shaders_regs_;

  // These are the constant base addresses/ranges for shaders.
  // We have these hardcoded right now cause nothing seems to differ.
  assert_true(register_file_->values[XE_GPU_REG_SQ_VS_CONST].u32 ==
                  0x000FF000 ||
              register_file_->values[XE_GPU_REG_SQ_VS_CONST].u32 == 0x00000000);
  assert_true(register_file_->values[XE_GPU_REG_SQ_PS_CONST].u32 ==
                  0x000FF100 ||
              register_file_->values[XE_GPU_REG_SQ_PS_CONST].u32 == 0x00000000);

  bool dirty = false;
  dirty |= SetShadowRegister(&regs.pa_su_sc_mode_cntl,
                             XE_GPU_REG_PA_SU_SC_MODE_CNTL);
  dirty |= SetShadowRegister(&regs.sq_program_cntl, XE_GPU_REG_SQ_PROGRAM_CNTL);
  dirty |= SetShadowRegister(&regs.sq_context_misc, XE_GPU_REG_SQ_CONTEXT_MISC);
  // dirty |= regs.vertex_shader != active_vertex_shader_;
  // dirty |= regs.pixel_shader != active_pixel_shader_;
  dirty |= regs.prim_type != prim_type;
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }
  // regs.vertex_shader = static_cast<VulkanShader*>(active_vertex_shader_);
  // regs.pixel_shader = static_cast<VulkanShader*>(active_pixel_shader_);
  regs.prim_type = prim_type;

  SCOPE_profile_cpu_f("gpu");

  return UpdateStatus::kMismatch;
}

PipelineCache::UpdateStatus PipelineCache::UpdateRenderTargets() {
  auto& regs = update_render_targets_regs_;

  bool dirty = false;
  dirty |= SetShadowRegister(&regs.rb_modecontrol, XE_GPU_REG_RB_MODECONTROL);
  dirty |= SetShadowRegister(&regs.rb_surface_info, XE_GPU_REG_RB_SURFACE_INFO);
  dirty |= SetShadowRegister(&regs.rb_color_info, XE_GPU_REG_RB_COLOR_INFO);
  dirty |= SetShadowRegister(&regs.rb_color1_info, XE_GPU_REG_RB_COLOR1_INFO);
  dirty |= SetShadowRegister(&regs.rb_color2_info, XE_GPU_REG_RB_COLOR2_INFO);
  dirty |= SetShadowRegister(&regs.rb_color3_info, XE_GPU_REG_RB_COLOR3_INFO);
  dirty |= SetShadowRegister(&regs.rb_color_mask, XE_GPU_REG_RB_COLOR_MASK);
  dirty |= SetShadowRegister(&regs.rb_depthcontrol, XE_GPU_REG_RB_DEPTHCONTROL);
  dirty |=
      SetShadowRegister(&regs.rb_stencilrefmask, XE_GPU_REG_RB_STENCILREFMASK);
  dirty |= SetShadowRegister(&regs.rb_depth_info, XE_GPU_REG_RB_DEPTH_INFO);
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  SCOPE_profile_cpu_f("gpu");

  return UpdateStatus::kMismatch;
}

PipelineCache::UpdateStatus PipelineCache::UpdateState(
    PrimitiveType prim_type) {
  bool mismatch = false;

#define CHECK_UPDATE_STATUS(status, mismatch, error_message) \
  {                                                          \
    if (status == UpdateStatus::kError) {                    \
      XELOGE(error_message);                                 \
      return status;                                         \
    } else if (status == UpdateStatus::kMismatch) {          \
      mismatch = true;                                       \
    }                                                        \
  }

  UpdateStatus status;
  status = UpdateViewportState();
  CHECK_UPDATE_STATUS(status, mismatch, "Unable to update viewport state");
  status = UpdateRasterizerState(prim_type);
  CHECK_UPDATE_STATUS(status, mismatch, "Unable to update rasterizer state");
  status = UpdateBlendState();
  CHECK_UPDATE_STATUS(status, mismatch, "Unable to update blend state");
  status = UpdateDepthStencilState();
  CHECK_UPDATE_STATUS(status, mismatch, "Unable to update depth/stencil state");

  return mismatch ? UpdateStatus::kMismatch : UpdateStatus::kCompatible;
}

PipelineCache::UpdateStatus PipelineCache::UpdateViewportState() {
  auto& regs = update_viewport_state_regs_;

  bool dirty = false;
  // dirty |= SetShadowRegister(&state_regs.pa_cl_clip_cntl,
  //     XE_GPU_REG_PA_CL_CLIP_CNTL);
  dirty |= SetShadowRegister(&regs.rb_surface_info, XE_GPU_REG_RB_SURFACE_INFO);
  dirty |= SetShadowRegister(&regs.pa_cl_vte_cntl, XE_GPU_REG_PA_CL_VTE_CNTL);
  dirty |= SetShadowRegister(&regs.pa_su_sc_mode_cntl,
                             XE_GPU_REG_PA_SU_SC_MODE_CNTL);
  dirty |= SetShadowRegister(&regs.pa_sc_window_offset,
                             XE_GPU_REG_PA_SC_WINDOW_OFFSET);
  dirty |= SetShadowRegister(&regs.pa_sc_window_scissor_tl,
                             XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL);
  dirty |= SetShadowRegister(&regs.pa_sc_window_scissor_br,
                             XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR);
  dirty |= SetShadowRegister(&regs.pa_cl_vport_xoffset,
                             XE_GPU_REG_PA_CL_VPORT_XOFFSET);
  dirty |= SetShadowRegister(&regs.pa_cl_vport_yoffset,
                             XE_GPU_REG_PA_CL_VPORT_YOFFSET);
  dirty |= SetShadowRegister(&regs.pa_cl_vport_zoffset,
                             XE_GPU_REG_PA_CL_VPORT_ZOFFSET);
  dirty |= SetShadowRegister(&regs.pa_cl_vport_xscale,
                             XE_GPU_REG_PA_CL_VPORT_XSCALE);
  dirty |= SetShadowRegister(&regs.pa_cl_vport_yscale,
                             XE_GPU_REG_PA_CL_VPORT_YSCALE);
  dirty |= SetShadowRegister(&regs.pa_cl_vport_zscale,
                             XE_GPU_REG_PA_CL_VPORT_ZSCALE);

  // Much of this state machine is extracted from:
  // https://github.com/freedreno/mesa/blob/master/src/mesa/drivers/dri/r200/r200_state.c
  // http://fossies.org/dox/MesaLib-10.3.5/fd2__gmem_8c_source.html
  // http://www.x.org/docs/AMD/old/evergreen_3D_registers_v2.pdf

  // http://www.x.org/docs/AMD/old/evergreen_3D_registers_v2.pdf
  // VTX_XY_FMT = true: the incoming X, Y have already been multiplied by 1/W0.
  //            = false: multiply the X, Y coordinates by 1/W0.
  // VTX_Z_FMT = true: the incoming Z has already been multiplied by 1/W0.
  //           = false: multiply the Z coordinate by 1/W0.
  // VTX_W0_FMT = true: the incoming W0 is not 1/W0. Perform the reciprocal to
  //                    get 1/W0.
  // draw_batcher_.set_vtx_fmt((regs.pa_cl_vte_cntl >> 8) & 0x1 ? 1.0f : 0.0f,
  //                          (regs.pa_cl_vte_cntl >> 9) & 0x1 ? 1.0f : 0.0f,
  //                          (regs.pa_cl_vte_cntl >> 10) & 0x1 ? 1.0f : 0.0f);

  // Done in VS, no need to flush state.
  // if ((regs.pa_cl_vte_cntl & (1 << 0)) > 0) {
  //  draw_batcher_.set_window_scalar(1.0f, 1.0f);
  //} else {
  //  draw_batcher_.set_window_scalar(1.0f / 2560.0f, -1.0f / 2560.0f);
  //}

  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  return UpdateStatus::kMismatch;
}

PipelineCache::UpdateStatus PipelineCache::UpdateRasterizerState(
    PrimitiveType prim_type) {
  auto& regs = update_rasterizer_state_regs_;

  bool dirty = false;
  dirty |= SetShadowRegister(&regs.pa_su_sc_mode_cntl,
                             XE_GPU_REG_PA_SU_SC_MODE_CNTL);
  dirty |= SetShadowRegister(&regs.pa_sc_screen_scissor_tl,
                             XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL);
  dirty |= SetShadowRegister(&regs.pa_sc_screen_scissor_br,
                             XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR);
  dirty |= SetShadowRegister(&regs.multi_prim_ib_reset_index,
                             XE_GPU_REG_VGT_MULTI_PRIM_IB_RESET_INDX);
  dirty |= regs.prim_type != prim_type;
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  regs.prim_type = prim_type;

  SCOPE_profile_cpu_f("gpu");

  return UpdateStatus::kMismatch;
}

PipelineCache::UpdateStatus PipelineCache::UpdateBlendState() {
  auto& reg_file = *register_file_;
  auto& regs = update_blend_state_regs_;

  // Alpha testing -- ALPHAREF, ALPHAFUNC, ALPHATESTENABLE
  // Deprecated in GL, implemented in shader.
  // if(ALPHATESTENABLE && frag_out.a [<=/ALPHAFUNC] ALPHAREF) discard;
  // uint32_t color_control = reg_file[XE_GPU_REG_RB_COLORCONTROL].u32;
  // draw_batcher_.set_alpha_test((color_control & 0x4) != 0,  //
  // ALPAHTESTENABLE
  //                             color_control & 0x7,         // ALPHAFUNC
  //                             reg_file[XE_GPU_REG_RB_ALPHA_REF].f32);

  bool dirty = false;
  dirty |=
      SetShadowRegister(&regs.rb_blendcontrol[0], XE_GPU_REG_RB_BLENDCONTROL_0);
  dirty |=
      SetShadowRegister(&regs.rb_blendcontrol[1], XE_GPU_REG_RB_BLENDCONTROL_1);
  dirty |=
      SetShadowRegister(&regs.rb_blendcontrol[2], XE_GPU_REG_RB_BLENDCONTROL_2);
  dirty |=
      SetShadowRegister(&regs.rb_blendcontrol[3], XE_GPU_REG_RB_BLENDCONTROL_3);
  dirty |= SetShadowRegister(&regs.rb_blend_rgba[0], XE_GPU_REG_RB_BLEND_RED);
  dirty |= SetShadowRegister(&regs.rb_blend_rgba[1], XE_GPU_REG_RB_BLEND_GREEN);
  dirty |= SetShadowRegister(&regs.rb_blend_rgba[2], XE_GPU_REG_RB_BLEND_BLUE);
  dirty |= SetShadowRegister(&regs.rb_blend_rgba[3], XE_GPU_REG_RB_BLEND_ALPHA);
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  SCOPE_profile_cpu_f("gpu");

  return UpdateStatus::kMismatch;
}

PipelineCache::UpdateStatus PipelineCache::UpdateDepthStencilState() {
  auto& regs = update_depth_stencil_state_regs_;

  bool dirty = false;
  dirty |= SetShadowRegister(&regs.rb_depthcontrol, XE_GPU_REG_RB_DEPTHCONTROL);
  dirty |=
      SetShadowRegister(&regs.rb_stencilrefmask, XE_GPU_REG_RB_STENCILREFMASK);
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  SCOPE_profile_cpu_f("gpu");

  return UpdateStatus::kMismatch;
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe
