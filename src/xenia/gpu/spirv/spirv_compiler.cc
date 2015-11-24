/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/spirv/spirv_compiler.h"

#include "third_party/spirv-tools/include/libspirv/libspirv.h"
#include "xenia/gpu/spirv/spv_assembler.h"
#include "xenia/gpu/spirv/spv_disassembler.h"
#include "xenia/gpu/spirv/spv_emitter.h"

namespace xe {
namespace gpu {
namespace spirv {

SpirvCompiler::SpirvCompiler() {
  // HACK(benvanik): in-progress test code just to make sure things compile.
  const std::string spirv = R"(
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

  SpvAssembler spv_asm;
  auto asm_result = spv_asm.Assemble(spirv);

  SpvDisassembler spv_disasm;
  auto disasm_result =
      spv_disasm.Disassemble(asm_result->words(), asm_result->word_count());

  SpvEmitter e;
  auto glsl_std_450 = e.ImportExtendedInstructions("GLSL.std.450");
  auto fn = e.MakeMainEntry();
  auto float_1_0 = e.MakeFloatConstant(1.0f);
  auto acos = e.CreateExtendedInstructionCall(
      spv::Decoration::Invariant, e.MakeFloatType(32), glsl_std_450,
      static_cast<int>(spv::GLSLstd450::Acos), {{float_1_0}});
  e.MakeReturn(true);

  std::vector<uint32_t> words;
  e.Serialize(words);

  auto disasm_result2 = spv_disasm.Disassemble(words.data(), words.size());

  return;
}

}  // namespace spirv
}  // namespace gpu
}  // namespace xe
