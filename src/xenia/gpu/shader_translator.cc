/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/shader_translator.h"

#include <string>

#include "third_party/spirv-tools/include/libspirv/libspirv.h"
#include "xenia/ui/spirv/spirv_assembler.h"
#include "xenia/ui/spirv/spirv_disassembler.h"
#include "xenia/ui/spirv/spirv_emitter.h"

namespace xe {
namespace gpu {

TranslatedShader::TranslatedShader(ShaderType shader_type,
                                   uint64_t ucode_data_hash,
                                   const uint32_t* ucode_words,
                                   size_t ucode_word_count)
    : shader_type_(shader_type), ucode_data_hash_(ucode_data_hash_) {
  ucode_data_.resize(ucode_word_count);
  std::memcpy(ucode_data_.data(), ucode_words,
              ucode_word_count * sizeof(uint32_t));
}

TranslatedShader::~TranslatedShader() = default;

ShaderTranslator::ShaderTranslator() {
  // HACK(benvanik): in-progress test code just to make sure things compile.
  const std::string spirv_source = R"(
OpCapability Shader
%1 = OpExtInstImport "GLSL.std.450"
OpMemoryModel Logical Simple
OpEntryPoint Vertex %2 "main"
%3 = OpTypeVoid
%4 = OpTypeFunction %3
%2 = OpFunction %3 None %4
%5 = OpLabel
OpReturn
OpFunctionEnd
)";

  xe::ui::spirv::SpirvAssembler spirv_asm;
  auto asm_result = spirv_asm.Assemble(spirv_source);

  xe::ui::spirv::SpirvDisassembler spirv_disasm;
  auto disasm_result =
      spirv_disasm.Disassemble(asm_result->words(), asm_result->word_count());

  return;
}

std::unique_ptr<TranslatedShader> ShaderTranslator::Translate(
    ShaderType shader_type, uint64_t ucode_data_hash,
    const uint32_t* ucode_words, size_t ucode_word_count) {
  auto translated_shader = std::make_unique<TranslatedShader>(
      shader_type, ucode_data_hash, ucode_words, ucode_word_count);

  // run over once to generate info:
  //   vertex fetch
  //    fetch constant
  //    unnormalized [0-1] or [-1,1] if signed
  //    signed (integer formats only)
  //    round index (vs. floor)
  //    exp adjust [-32, 31] - all data multiplied by 2^(adj)
  //    stride words
  //    offset words
  //   texture fetch
  //    fetch constant
  //    unnormalized (0-N) or (0-1)
  //   alloc/export types/counts
  //    vs: output (16), position, point size
  //    ps: color (4), depth

  xe::ui::spirv::SpirvEmitter e;
  auto glsl_std_450 = e.ImportExtendedInstructions("GLSL.std.450");
  auto fn = e.MakeMainEntry();
  auto float_1_0 = e.MakeFloatConstant(1.0f);
  auto acos = e.CreateExtendedInstructionCall(
      spv::Decoration::Invariant, e.MakeFloatType(32), glsl_std_450,
      static_cast<int>(spv::GLSLstd450::Acos), {{float_1_0}});
  e.MakeReturn(true);

  std::vector<uint32_t> spirv_words;
  e.Serialize(spirv_words);

  auto disasm_result = xe::ui::spirv::SpirvDisassembler().Disassemble(
      spirv_words.data(), spirv_words.size());

  return translated_shader;
}

}  // namespace gpu
}  // namespace xe
