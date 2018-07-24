/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D12_PIPELINE_CACHE_H_
#define XENIA_GPU_D3D12_PIPELINE_CACHE_H_

#include <unordered_map>

#include "third_party/xxhash/xxhash.h"

#include "xenia/gpu/d3d12/d3d12_shader.h"
#include "xenia/gpu/register_file.h"
#include "xenia/gpu/shader_translator.h"
#include "xenia/gpu/xenos.h"
#include "xenia/ui/d3d12/d3d12_context.h"

namespace xe {
namespace gpu {
namespace d3d12 {

class PipelineCache {
 public:
  enum class UpdateStatus {
    kCompatible,
    kMismatch,
    kError,
  };

  PipelineCache(RegisterFile* register_file, ui::d3d12::D3D12Context* context);
  ~PipelineCache();

  void Shutdown();

  D3D12Shader* LoadShader(ShaderType shader_type, uint32_t guest_address,
                          const uint32_t* host_address, uint32_t dword_count);

  UpdateStatus ConfigurePipeline(D3D12Shader* vertex_shader,
                                 D3D12Shader* pixel_shader,
                                 PrimitiveType primitive_type);

  void ClearCache();

 private:
  bool SetShadowRegister(uint32_t* dest, uint32_t register_name);
  bool SetShadowRegister(float* dest, uint32_t register_name);

  bool TranslateShader(D3D12Shader* shader, xenos::xe_gpu_program_cntl_t cntl);

  UpdateStatus UpdateState(D3D12Shader* vertex_shader,
                           D3D12Shader* pixel_shader,
                           PrimitiveType primitive_type);

  UpdateStatus UpdateShaderStages(D3D12Shader* vertex_shader,
                                  D3D12Shader* pixel_shader,
                                  PrimitiveType primitive_type);

  RegisterFile* register_file_ = nullptr;
  ui::d3d12::D3D12Context* context_ = nullptr;

  // Reusable shader translator.
  std::unique_ptr<ShaderTranslator> shader_translator_ = nullptr;
  // All loaded shaders mapped by their guest hash key.
  std::unordered_map<uint64_t, D3D12Shader*> shader_map_;

  // Hash state used to incrementally produce pipeline hashes during update.
  // By the time the full update pass has run the hash will represent the
  // current state in a way that can uniquely identify the produced
  // ID3D12PipelineState.
  XXH64_state_t hash_state_;

  struct UpdateShaderStagesRegisters {
    PrimitiveType primitive_type;
    uint32_t pa_su_sc_mode_cntl;
    uint32_t sq_program_cntl;
    D3D12Shader* vertex_shader;
    D3D12Shader* pixel_shader;

    UpdateShaderStagesRegisters() { Reset(); }
    void Reset() { std::memset(this, 0, sizeof(*this)); }
  } update_shader_stages_regs_;
};

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_D3D12_PIPELINE_CACHE_H_
