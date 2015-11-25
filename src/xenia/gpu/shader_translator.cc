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

  xe::ui::spirv::SpirvEmitter e;
  auto glsl_std_450 = e.ImportExtendedInstructions("GLSL.std.450");
  auto fn = e.MakeMainEntry();
  auto float_1_0 = e.MakeFloatConstant(1.0f);
  auto acos = e.CreateExtendedInstructionCall(
      spv::Decoration::Invariant, e.MakeFloatType(32), glsl_std_450,
      static_cast<int>(spv::GLSLstd450::Acos), {{float_1_0}});
  e.MakeReturn(true);

  std::vector<uint32_t> words;
  e.Serialize(words);

  auto disasm_result2 = spirv_disasm.Disassemble(words.data(), words.size());

  return;
}

}  // namespace gpu
}  // namespace xe
