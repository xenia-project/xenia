/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_SPIRV_SHADER_TRANSLATOR_H_
#define XENIA_GPU_SPIRV_SHADER_TRANSLATOR_H_

#include <memory>
#include <string>
#include <vector>

#include "third_party/glslang-spirv/SpvBuilder.h"
#include "third_party/spirv/GLSL.std.450.hpp11"
#include "xenia/gpu/shader_translator.h"
#include "xenia/gpu/spirv/compiler.h"
#include "xenia/ui/spirv/spirv_disassembler.h"
#include "xenia/ui/spirv/spirv_validator.h"

namespace xe {
namespace gpu {

// Push constants embedded within the command buffer.
// The total size of this struct must be <= 128b (as that's the commonly
// supported size).
struct SpirvPushConstants {
  // Accessible to vertex shader only:
  float window_scale[4];  // sx,sy, ?, ?
  float vtx_fmt[4];

  // Accessible to fragment shader only:
  float alpha_test[4];  // alpha test enable, func, ref, ?
  uint32_t ps_param_gen;
};
static_assert(sizeof(SpirvPushConstants) <= 128,
              "Push constants must fit <= 128b");
constexpr uint32_t kSpirvPushConstantVertexRangeOffset = 0;
constexpr uint32_t kSpirvPushConstantVertexRangeSize = (sizeof(float) * 4) * 2;
constexpr uint32_t kSpirvPushConstantFragmentRangeOffset =
    kSpirvPushConstantVertexRangeSize;
constexpr uint32_t kSpirvPushConstantFragmentRangeSize =
    (sizeof(float) * 4) + sizeof(uint32_t);
constexpr uint32_t kSpirvPushConstantsSize = sizeof(SpirvPushConstants);

class SpirvShaderTranslator : public ShaderTranslator {
 public:
  SpirvShaderTranslator();
  ~SpirvShaderTranslator() override;

 protected:
  void StartTranslation() override;
  std::vector<uint8_t> CompleteTranslation() override;
  void PostTranslation(Shader* shader) override;

  void PreProcessControlFlowInstruction(
      uint32_t cf_index, const ucode::ControlFlowInstruction& instr) override;
  void ProcessLabel(uint32_t cf_index) override;
  void ProcessControlFlowInstructionBegin(uint32_t cf_index) override;
  void ProcessControlFlowInstructionEnd(uint32_t cf_index) override;
  void ProcessControlFlowNopInstruction() override;
  void ProcessExecInstructionBegin(const ParsedExecInstruction& instr) override;
  void ProcessExecInstructionEnd(const ParsedExecInstruction& instr) override;
  void ProcessLoopStartInstruction(
      const ParsedLoopStartInstruction& instr) override;
  void ProcessLoopEndInstruction(
      const ParsedLoopEndInstruction& instr) override;
  void ProcessCallInstruction(const ParsedCallInstruction& instr) override;
  void ProcessReturnInstruction(const ParsedReturnInstruction& instr) override;
  void ProcessJumpInstruction(const ParsedJumpInstruction& instr) override;
  void ProcessAllocInstruction(const ParsedAllocInstruction& instr) override;
  void ProcessVertexFetchInstruction(
      const ParsedVertexFetchInstruction& instr) override;
  void ProcessTextureFetchInstruction(
      const ParsedTextureFetchInstruction& instr) override;
  void ProcessAluInstruction(const ParsedAluInstruction& instr) override;

 private:
  spv::Function* CreateCubeFunction();

  void ProcessVectorAluInstruction(const ParsedAluInstruction& instr);
  void ProcessScalarAluInstruction(const ParsedAluInstruction& instr);

  // Creates a call to the given GLSL intrinsic.
  spv::Id SpirvShaderTranslator::CreateGlslStd450InstructionCall(
      spv::Decoration precision, spv::Id result_type,
      spv::GLSLstd450 instruction_ordinal, std::vector<spv::Id> args);

  // Loads an operand into a value.
  // The value returned will be in the form described in the operand (number of
  // components, etc).
  spv::Id LoadFromOperand(const InstructionOperand& op);
  // Stores a value based on the specified result information.
  // The value will be transformed into the appropriate form for the result and
  // the proper components will be selected.
  void StoreToResult(spv::Id source_value_id, const InstructionResult& result);

  xe::ui::spirv::SpirvDisassembler disassembler_;
  xe::ui::spirv::SpirvValidator validator_;
  xe::gpu::spirv::Compiler compiler_;

  // True if there's an open predicated block
  bool open_predicated_block_ = false;
  bool predicated_block_cond_ = false;
  spv::Block* predicated_block_end_ = nullptr;

  // TODO(benvanik): replace with something better, make reusable, etc.
  std::unique_ptr<spv::Builder> builder_;
  spv::Id glsl_std_450_instruction_set_ = 0;

  // Generated function
  spv::Function* translated_main_ = nullptr;
  spv::Function* cube_function_ = nullptr;

  // Types.
  spv::Id float_type_ = 0, bool_type_ = 0, int_type_ = 0, uint_type_ = 0;
  spv::Id vec2_uint_type_ = 0;
  spv::Id vec2_float_type_ = 0, vec3_float_type_ = 0, vec4_float_type_ = 0;
  spv::Id vec4_uint_type_ = 0;
  spv::Id vec2_bool_type_ = 0, vec3_bool_type_ = 0, vec4_bool_type_ = 0;
  spv::Id image_1d_type_ = 0, image_2d_type_ = 0, image_3d_type_ = 0,
          image_cube_type_ = 0;

  // Constants.
  spv::Id vec4_float_zero_ = 0, vec4_float_one_ = 0;

  // Array of AMD registers.
  // These values are all pointers.
  spv::Id registers_ptr_ = 0, registers_type_ = 0;
  spv::Id consts_ = 0, a0_ = 0, aL_ = 0, p0_ = 0;
  spv::Id ps_ = 0, pv_ = 0;  // IDs of previous results
  spv::Id pos_ = 0;
  spv::Id push_consts_ = 0;
  spv::Id interpolators_ = 0;
  spv::Id vertex_idx_ = 0;
  spv::Id frag_outputs_ = 0, frag_depth_ = 0;
  spv::Id samplers_ = 0;
  spv::Id tex_[4] = {0};  // Images {1D, 2D, 3D, Cube}

  // SPIR-V IDs that are part of the in/out interface.
  std::vector<spv::Id> interface_ids_;

  // Map of {binding -> {offset -> spv input}}
  std::map<uint32_t, std::map<uint32_t, spv::Id>> vertex_binding_map_;

  struct CFBlock {
    spv::Block* block = nullptr;
    bool prev_dominates = true;
  };
  std::map<uint32_t, CFBlock> cf_blocks_;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_SPIRV_SHADER_TRANSLATOR_H_
