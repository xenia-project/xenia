/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_SPIRV_SHADER_TRANSLATOR_H_
#define XENIA_GPU_SPIRV_SHADER_TRANSLATOR_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "third_party/glslang/SPIRV/SpvBuilder.h"
#include "xenia/gpu/shader_translator.h"

namespace xe {
namespace gpu {

class SpirvShaderTranslator : public ShaderTranslator {
 public:
  SpirvShaderTranslator(bool supports_clip_distance = true,
                        bool supports_cull_distance = true);

 protected:
  void Reset() override;

  void StartTranslation() override;

  std::vector<uint8_t> CompleteTranslation() override;

 private:
  // TODO(Triang3l): Depth-only pixel shader.
  bool IsSpirvVertexOrTessEvalShader() const { return is_vertex_shader(); }
  bool IsSpirvVertexShader() const {
    return IsSpirvVertexOrTessEvalShader() &&
           host_vertex_shader_type() == Shader::HostVertexShaderType::kVertex;
  }
  bool IsSpirvTessEvalShader() const {
    return IsSpirvVertexOrTessEvalShader() &&
           host_vertex_shader_type() != Shader::HostVertexShaderType::kVertex;
  }
  bool IsSpirvFragmentShader() const { return is_pixel_shader(); }

  void StartVertexOrTessEvalShaderBeforeMain();
  void StartVertexOrTessEvalShaderInMain();
  void CompleteVertexOrTessEvalShaderInMain();
  void CompleteVertexOrTessEvalShaderAfterMain(spv::Instruction* entry_point);

  bool supports_clip_distance_;
  bool supports_cull_distance_;

  std::unique_ptr<spv::Builder> builder_;

  std::vector<spv::Id> id_vector_temp_;
  std::vector<unsigned int> uint_vector_temp_;

  spv::Id ext_inst_glsl_std_450_;

  spv::Id type_void_;
  spv::Id type_bool_;
  spv::Id type_int_;
  spv::Id type_int4_;
  spv::Id type_uint_;
  spv::Id type_float_;
  spv::Id type_float2_;
  spv::Id type_float3_;
  spv::Id type_float4_;

  spv::Id const_int_0_;
  spv::Id const_int4_0_;
  spv::Id const_float_0_;
  spv::Id const_float4_0_;

  // VS as VS only - int.
  spv::Id input_vertex_index_;
  // VS as TES only - int.
  spv::Id input_primitive_id_;

  enum OutputPerVertexMember : unsigned int {
    kOutputPerVertexMemberPosition,
    kOutputPerVertexMemberPointSize,
    kOutputPerVertexMemberClipDistance,
    kOutputPerVertexMemberCullDistance,
    kOutputPerVertexMemberCount,
  };
  spv::Id output_per_vertex_;

  spv::Function* function_main_;
  // bool.
  spv::Id var_main_predicate_;
  // int4.
  spv::Id var_main_address_relative_;
  // int.
  spv::Id var_main_address_absolute_;
  // float4[register_count()].
  spv::Id var_main_registers_;
  // VS only - float3 (special exports).
  spv::Id var_main_point_size_edge_flag_kill_vertex_;
  spv::Block* main_loop_header_;
  spv::Block* main_loop_continue_;
  spv::Block* main_loop_merge_;
  spv::Id main_loop_pc_next_;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_SPIRV_SHADER_TRANSLATOR_H_
