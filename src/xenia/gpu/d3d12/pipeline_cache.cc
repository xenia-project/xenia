/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/d3d12/pipeline_cache.h"

#include <cinttypes>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/hlsl_shader_translator.h"

namespace xe {
namespace gpu {
namespace d3d12 {

PipelineCache::PipelineCache(RegisterFile* register_file,
                             ui::d3d12::D3D12Context* context)
    : register_file_(register_file), context_(context) {
  shader_translator_.reset(new HlslShaderTranslator());
}

PipelineCache::~PipelineCache() { Shutdown(); }

void PipelineCache::Shutdown() {
  ClearCache();
}

D3D12Shader* PipelineCache::LoadShader(ShaderType shader_type,
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
  D3D12Shader* shader = new D3D12Shader(shader_type, data_hash, host_address,
                                        dword_count);
  shader_map_.insert({data_hash, shader});

  return shader;
}

PipelineCache::UpdateStatus PipelineCache::ConfigurePipeline(
    D3D12Shader* vertex_shader, D3D12Shader* pixel_shader,
    PrimitiveType primitive_type) {
#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES
  return UpdateState(vertex_shader, pixel_shader, primitive_type);
}

void PipelineCache::ClearCache() {
  // Destroy all shaders.
  for (auto it : shader_map_) {
    delete it.second;
  }
  shader_map_.clear();
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

bool PipelineCache::TranslateShader(D3D12Shader* shader,
                                    xenos::xe_gpu_program_cntl_t cntl) {
  // Perform translation.
  // If this fails the shader will be marked as invalid and ignored later.
  if (!shader_translator_->Translate(shader, cntl)) {
    XELOGE("Shader translation failed; marking shader as ignored");
    return false;
  }

  // Prepare the shader for use (creates the Shader Model bytecode).
  // It could still fail at this point.
  if (!shader->Prepare()) {
    XELOGE("Shader preparation failed; marking shader as ignored");
    return false;
  }

  if (shader->is_valid()) {
    XELOGGPU("Generated %s shader (%db) - hash %.16" PRIX64 ":\n%s\n",
             shader->type() == ShaderType::kVertex ? "vertex" : "pixel",
             shader->ucode_dword_count() * 4, shader->ucode_data_hash(),
             shader->ucode_disassembly().c_str());
  }

  // Dump shader files if desired.
  if (!FLAGS_dump_shaders.empty()) {
    shader->Dump(FLAGS_dump_shaders, "d3d12");
  }

  return shader->is_valid();
}

PipelineCache::UpdateStatus PipelineCache::UpdateState(
    D3D12Shader* vertex_shader, D3D12Shader* pixel_shader,
    PrimitiveType primitive_type) {
  bool mismatch = false;

  // Reset hash so we can build it up.
  XXH64_reset(&hash_state_, 0);

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
  status = UpdateShaderStages(vertex_shader, pixel_shader, primitive_type);
  CHECK_UPDATE_STATUS(status, mismatch, "Unable to update shader stages");

#undef CHECK_UPDATE_STATUS

  return mismatch ? UpdateStatus::kMismatch : UpdateStatus::kCompatible;
}

PipelineCache::UpdateStatus PipelineCache::UpdateShaderStages(
    D3D12Shader* vertex_shader, D3D12Shader* pixel_shader,
    PrimitiveType primitive_type) {
  auto& regs = update_shader_stages_regs_;

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
  dirty |= regs.vertex_shader != vertex_shader;
  dirty |= regs.pixel_shader != pixel_shader;
  dirty |= regs.primitive_type != primitive_type;
  regs.vertex_shader = vertex_shader;
  regs.pixel_shader = pixel_shader;
  regs.primitive_type = primitive_type;
  XXH64_update(&hash_state_, &regs, sizeof(regs));
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  xenos::xe_gpu_program_cntl_t sq_program_cntl;
  sq_program_cntl.dword_0 = regs.sq_program_cntl;

  if (!vertex_shader->is_translated() &&
      !TranslateShader(vertex_shader, sq_program_cntl)) {
    XELOGE("Failed to translate the vertex shader!");
    return UpdateStatus::kError;
  }

  if (pixel_shader && !pixel_shader->is_translated() &&
      !TranslateShader(pixel_shader, sq_program_cntl)) {
    XELOGE("Failed to translate the pixel shader!");
    return UpdateStatus::kError;
  }

  return UpdateStatus::kMismatch;
}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe
