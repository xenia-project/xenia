/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/vulkan_command_processor.h"

#include <algorithm>

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/sampler_info.h"
#include "xenia/gpu/texture_info.h"
#include "xenia/gpu/vulkan/vulkan_gpu_flags.h"
#include "xenia/gpu/vulkan/vulkan_graphics_system.h"
#include "xenia/gpu/xenos.h"

namespace xe {
namespace gpu {
namespace vulkan {

using namespace xe::gpu::xenos;

VulkanCommandProcessor::VulkanCommandProcessor(
    VulkanGraphicsSystem* graphics_system, kernel::KernelState* kernel_state)
    : CommandProcessor(graphics_system, kernel_state) {}

VulkanCommandProcessor::~VulkanCommandProcessor() = default;

void VulkanCommandProcessor::ClearCaches() { CommandProcessor::ClearCaches(); }

bool VulkanCommandProcessor::SetupContext() {
  if (!CommandProcessor::SetupContext()) {
    XELOGE("Unable to initialize base command processor context");
    return false;
  }

  return true;
}

void VulkanCommandProcessor::ShutdownContext() {
  CommandProcessor::ShutdownContext();
}

void VulkanCommandProcessor::MakeCoherent() {
  RegisterFile* regs = register_file_;
  auto status_host = regs->values[XE_GPU_REG_COHER_STATUS_HOST].u32;

  CommandProcessor::MakeCoherent();

  if (status_host & 0x80000000ul) {
    // scratch_buffer_.ClearCache();
  }
}

void VulkanCommandProcessor::PrepareForWait() {
  SCOPE_profile_cpu_f("gpu");

  CommandProcessor::PrepareForWait();

  // TODO(benvanik): fences and fancy stuff. We should figure out a way to
  // make interrupt callbacks from the GPU so that we don't have to do a full
  // synchronize here.
  // glFlush();
  // glFinish();

  context_->ClearCurrent();
}

void VulkanCommandProcessor::ReturnFromWait() {
  context_->MakeCurrent();

  CommandProcessor::ReturnFromWait();
}

void VulkanCommandProcessor::PerformSwap(uint32_t frontbuffer_ptr,
                                         uint32_t frontbuffer_width,
                                         uint32_t frontbuffer_height,
                                         uint32_t color_format) {
  // Ensure we issue any pending draws.
  // draw_batcher_.Flush(DrawBatcher::FlushMode::kMakeCoherent);

  // Need to finish to be sure the other context sees the right data.
  // TODO(benvanik): prevent this? fences?
  // glFinish();

  if (context_->WasLost()) {
    // We've lost the context due to a TDR.
    // TODO: Dump the current commands to a tracefile.
    assert_always();
  }

  // Remove any dead textures, etc.
  // texture_cache_.Scavenge();
}

Shader* VulkanCommandProcessor::LoadShader(ShaderType shader_type,
                                           uint32_t guest_address,
                                           const uint32_t* host_address,
                                           uint32_t dword_count) {
  // return shader_cache_.LookupOrInsertShader(shader_type, host_address,
  //                                          dword_count);
  return nullptr;
}

bool VulkanCommandProcessor::IssueDraw(PrimitiveType prim_type,
                                       uint32_t index_count,
                                       IndexBufferInfo* index_buffer_info) {
#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  // Skip all drawing for now - what did you expect? :)
  return true;

  bool draw_valid = false;
  // if (index_buffer_info) {
  //  draw_valid = draw_batcher_.BeginDrawElements(prim_type, index_count,
  //                                               index_buffer_info->format);
  //} else {
  //  draw_valid = draw_batcher_.BeginDrawArrays(prim_type, index_count);
  //}
  if (!draw_valid) {
    return false;
  }

  auto& regs = *register_file_;

  auto enable_mode =
      static_cast<ModeControl>(regs[XE_GPU_REG_RB_MODECONTROL].u32 & 0x7);
  if (enable_mode == ModeControl::kIgnore) {
    // Ignored.
    // draw_batcher_.DiscardDraw();
    return true;
  } else if (enable_mode == ModeControl::kCopy) {
    // Special copy handling.
    // draw_batcher_.DiscardDraw();
    return IssueCopy();
  }

#define CHECK_ISSUE_UPDATE_STATUS(status, mismatch, error_message)  \
  {                                                                 \
    if (status == UpdateStatus::kError) {                           \
      XELOGE(error_message);                                        \
      /*draw_batcher_.DiscardDraw();                             */ \
      return false;                                                 \
    } else if (status == UpdateStatus::kMismatch) {                 \
      mismatch = true;                                              \
    }                                                               \
  }

  UpdateStatus status;
  bool mismatch = false;
  status = UpdateShaders(prim_type);
  CHECK_ISSUE_UPDATE_STATUS(status, mismatch, "Unable to prepare draw shaders");
  status = UpdateRenderTargets();
  CHECK_ISSUE_UPDATE_STATUS(status, mismatch, "Unable to setup render targets");
  // if (!active_framebuffer_) {
  //  // No framebuffer, so nothing we do will actually have an effect.
  //  // Treat it as a no-op.
  //  // TODO(benvanik): if we have a vs export, still allow it to go.
  //  draw_batcher_.DiscardDraw();
  //  return true;
  //}

  status = UpdateState(prim_type);
  CHECK_ISSUE_UPDATE_STATUS(status, mismatch, "Unable to setup render state");
  status = PopulateSamplers();
  CHECK_ISSUE_UPDATE_STATUS(status, mismatch,
                            "Unable to prepare draw samplers");

  status = PopulateIndexBuffer(index_buffer_info);
  CHECK_ISSUE_UPDATE_STATUS(status, mismatch, "Unable to setup index buffer");
  status = PopulateVertexBuffers();
  CHECK_ISSUE_UPDATE_STATUS(status, mismatch, "Unable to setup vertex buffers");

  // if (!draw_batcher_.CommitDraw()) {
  //  return false;
  //}

  // draw_batcher_.Flush(DrawBatcher::FlushMode::kMakeCoherent);
  if (context_->WasLost()) {
    // This draw lost us the context. This typically isn't hit.
    assert_always();
    return false;
  }

  return true;
}

bool VulkanCommandProcessor::SetShadowRegister(uint32_t* dest,
                                               uint32_t register_name) {
  uint32_t value = register_file_->values[register_name].u32;
  if (*dest == value) {
    return false;
  }
  *dest = value;
  return true;
}

bool VulkanCommandProcessor::SetShadowRegister(float* dest,
                                               uint32_t register_name) {
  float value = register_file_->values[register_name].f32;
  if (*dest == value) {
    return false;
  }
  *dest = value;
  return true;
}

VulkanCommandProcessor::UpdateStatus VulkanCommandProcessor::UpdateShaders(
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
  dirty |= regs.vertex_shader != active_vertex_shader_;
  dirty |= regs.pixel_shader != active_pixel_shader_;
  dirty |= regs.prim_type != prim_type;
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }
  regs.vertex_shader = static_cast<VulkanShader*>(active_vertex_shader_);
  regs.pixel_shader = static_cast<VulkanShader*>(active_pixel_shader_);
  regs.prim_type = prim_type;

  SCOPE_profile_cpu_f("gpu");

  return UpdateStatus::kMismatch;
}

VulkanCommandProcessor::UpdateStatus
VulkanCommandProcessor::UpdateRenderTargets() {
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

VulkanCommandProcessor::UpdateStatus VulkanCommandProcessor::UpdateState(
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

VulkanCommandProcessor::UpdateStatus
VulkanCommandProcessor::UpdateViewportState() {
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

VulkanCommandProcessor::UpdateStatus
VulkanCommandProcessor::UpdateRasterizerState(PrimitiveType prim_type) {
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

VulkanCommandProcessor::UpdateStatus
VulkanCommandProcessor::UpdateBlendState() {
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

VulkanCommandProcessor::UpdateStatus
VulkanCommandProcessor::UpdateDepthStencilState() {
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

VulkanCommandProcessor::UpdateStatus
VulkanCommandProcessor::PopulateIndexBuffer(
    IndexBufferInfo* index_buffer_info) {
  auto& regs = *register_file_;
  if (!index_buffer_info || !index_buffer_info->guest_base) {
    // No index buffer or auto draw.
    return UpdateStatus::kCompatible;
  }
  auto& info = *index_buffer_info;

#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  // Min/max index ranges for clamping. This is often [0g,FFFF|FFFFFF].
  // All indices should be clamped to [min,max]. May be a way to do this in GL.
  uint32_t min_index = regs[XE_GPU_REG_VGT_MIN_VTX_INDX].u32;
  uint32_t max_index = regs[XE_GPU_REG_VGT_MAX_VTX_INDX].u32;
  assert_true(min_index == 0);
  assert_true(max_index == 0xFFFF || max_index == 0xFFFFFF);

  assert_true(info.endianness == Endian::k8in16 ||
              info.endianness == Endian::k8in32);

  trace_writer_.WriteMemoryRead(info.guest_base, info.length);

  return UpdateStatus::kCompatible;
}

VulkanCommandProcessor::UpdateStatus
VulkanCommandProcessor::PopulateVertexBuffers() {
#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  auto& regs = *register_file_;
  assert_not_null(active_vertex_shader_);

  for (const auto& vertex_binding : active_vertex_shader_->vertex_bindings()) {
    int r = XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 +
            (vertex_binding.fetch_constant / 3) * 6;
    const auto group = reinterpret_cast<xe_gpu_fetch_group_t*>(&regs.values[r]);
    const xe_gpu_vertex_fetch_t* fetch = nullptr;
    switch (vertex_binding.fetch_constant % 3) {
      case 0:
        fetch = &group->vertex_fetch_0;
        break;
      case 1:
        fetch = &group->vertex_fetch_1;
        break;
      case 2:
        fetch = &group->vertex_fetch_2;
        break;
    }
    assert_true(fetch->endian == 2);

    size_t valid_range = size_t(fetch->size * 4);

    trace_writer_.WriteMemoryRead(fetch->address << 2, valid_range);
  }

  return UpdateStatus::kCompatible;
}

VulkanCommandProcessor::UpdateStatus
VulkanCommandProcessor::PopulateSamplers() {
#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  bool mismatch = false;

  // VS and PS samplers are shared, but may be used exclusively.
  // We walk each and setup lazily.
  bool has_setup_sampler[32] = {false};

  // Vertex texture samplers.
  for (auto& texture_binding : active_vertex_shader_->texture_bindings()) {
    if (has_setup_sampler[texture_binding.fetch_constant]) {
      continue;
    }
    has_setup_sampler[texture_binding.fetch_constant] = true;
    auto status = PopulateSampler(texture_binding);
    if (status == UpdateStatus::kError) {
      return status;
    } else if (status == UpdateStatus::kMismatch) {
      mismatch = true;
    }
  }

  // Pixel shader texture sampler.
  for (auto& texture_binding : active_pixel_shader_->texture_bindings()) {
    if (has_setup_sampler[texture_binding.fetch_constant]) {
      continue;
    }
    has_setup_sampler[texture_binding.fetch_constant] = true;
    auto status = PopulateSampler(texture_binding);
    if (status == UpdateStatus::kError) {
      return UpdateStatus::kError;
    } else if (status == UpdateStatus::kMismatch) {
      mismatch = true;
    }
  }

  return mismatch ? UpdateStatus::kMismatch : UpdateStatus::kCompatible;
}

VulkanCommandProcessor::UpdateStatus VulkanCommandProcessor::PopulateSampler(
    const Shader::TextureBinding& texture_binding) {
  auto& regs = *register_file_;
  int r = XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 +
          texture_binding.fetch_constant * 6;
  auto group = reinterpret_cast<const xe_gpu_fetch_group_t*>(&regs.values[r]);
  auto& fetch = group->texture_fetch;

  // ?
  if (!fetch.type) {
    return UpdateStatus::kCompatible;
  }
  assert_true(fetch.type == 0x2);

  TextureInfo texture_info;
  if (!TextureInfo::Prepare(fetch, &texture_info)) {
    XELOGE("Unable to parse texture fetcher info");
    return UpdateStatus::kCompatible;  // invalid texture used
  }
  SamplerInfo sampler_info;
  if (!SamplerInfo::Prepare(fetch, texture_binding.fetch_instr,
                            &sampler_info)) {
    XELOGE("Unable to parse sampler info");
    return UpdateStatus::kCompatible;  // invalid texture used
  }

  trace_writer_.WriteMemoryRead(texture_info.guest_address,
                                texture_info.input_length);

  return UpdateStatus::kCompatible;
}

bool VulkanCommandProcessor::IssueCopy() {
  SCOPE_profile_cpu_f("gpu");
  return true;
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe
