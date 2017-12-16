/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/shader_translator.h"

#include <cstdarg>
#include <set>
#include <string>

#include "xenia/base/math.h"

namespace xe {
namespace gpu {

using namespace ucode;

void DisassembleResultOperand(const InstructionResult& result,
                              StringBuffer* out) {
  bool uses_storage_index = false;
  switch (result.storage_target) {
    case InstructionStorageTarget::kRegister:
      out->Append('r');
      uses_storage_index = true;
      break;
    case InstructionStorageTarget::kInterpolant:
      out->Append('o');
      uses_storage_index = true;
      break;
    case InstructionStorageTarget::kPosition:
      out->Append("oPos");
      break;
    case InstructionStorageTarget::kPointSize:
      out->Append("oPts");
      break;
    case InstructionStorageTarget::kColorTarget:
      out->AppendFormat("oC");
      uses_storage_index = true;
      break;
    case InstructionStorageTarget::kDepth:
      out->Append("oDepth");
      break;
    case InstructionStorageTarget::kNone:
      break;
  }
  if (uses_storage_index) {
    switch (result.storage_addressing_mode) {
      case InstructionStorageAddressingMode::kStatic:
        out->AppendFormat("%d", result.storage_index);
        break;
      case InstructionStorageAddressingMode::kAddressAbsolute:
        out->AppendFormat("[%d+a0]", result.storage_index);
        break;
      case InstructionStorageAddressingMode::kAddressRelative:
        out->AppendFormat("[%d+aL]", result.storage_index);
        break;
    }
  }
  if (!result.has_any_writes()) {
    out->Append("._");
  } else if (!result.is_standard_swizzle()) {
    out->Append('.');
    for (int i = 0; i < 4; ++i) {
      if (result.write_mask[i]) {
        out->Append(GetCharForSwizzle(result.components[i]));
      } else {
        out->Append('_');
      }
    }
  }
}

void DisassembleSourceOperand(const InstructionOperand& op, StringBuffer* out) {
  if (op.is_negated) {
    out->Append('-');
  }
  switch (op.storage_source) {
    case InstructionStorageSource::kRegister:
      out->Append('r');
      break;
    case InstructionStorageSource::kConstantFloat:
      out->Append('c');
      break;
    case InstructionStorageSource::kConstantInt:
      out->Append('i');
      break;
    case InstructionStorageSource::kConstantBool:
      out->Append('b');
      break;
    case InstructionStorageSource::kTextureFetchConstant:
    case InstructionStorageSource::kVertexFetchConstant:
      assert_always();
      break;
  }
  if (op.is_absolute_value) {
    out->Append("_abs");
  }
  switch (op.storage_addressing_mode) {
    case InstructionStorageAddressingMode::kStatic:
      if (op.is_absolute_value) {
        out->AppendFormat("[%d]", op.storage_index);
      } else {
        out->AppendFormat("%d", op.storage_index);
      }
      break;
    case InstructionStorageAddressingMode::kAddressAbsolute:
      out->AppendFormat("[%d+a0]", op.storage_index);
      break;
    case InstructionStorageAddressingMode::kAddressRelative:
      out->AppendFormat("[%d+aL]", op.storage_index);
      break;
  }
  if (!op.is_standard_swizzle()) {
    out->Append('.');
    if (op.component_count == 1) {
      out->Append(GetCharForSwizzle(op.components[0]));
    } else if (op.component_count == 2) {
      out->Append(GetCharForSwizzle(op.components[0]));
      out->Append(GetCharForSwizzle(op.components[1]));
    } else {
      for (int j = 0; j < op.component_count; ++j) {
        out->Append(GetCharForSwizzle(op.components[j]));
      }
    }
  }
}

void ParsedExecInstruction::Disassemble(StringBuffer* out) const {
  switch (type) {
    case Type::kUnconditional:
      out->AppendFormat("      %s ", opcode_name);
      break;
    case Type::kPredicated:
      out->Append(condition ? " (p0) " : "(!p0) ");
      out->AppendFormat("%s ", opcode_name);
      break;
    case Type::kConditional:
      out->AppendFormat("      %s ", opcode_name);
      if (!condition) {
        out->Append('!');
      }
      out->AppendFormat("b%u", bool_constant_index);
      break;
  }
  if (is_yield) {
    out->Append(", Yield=true");
  }
  if (!clean) {
    out->Append("  // PredicateClean=false");
  }
  out->Append('\n');
}

void ParsedLoopStartInstruction::Disassemble(StringBuffer* out) const {
  out->Append("      loop ");
  out->AppendFormat("i%u, L%u", loop_constant_index, loop_skip_address);
  if (is_repeat) {
    out->Append(", Repeat=true");
  }
  out->Append('\n');
}

void ParsedLoopEndInstruction::Disassemble(StringBuffer* out) const {
  if (is_predicated_break) {
    out->Append(predicate_condition ? " (p0) " : "(!p0) ");
  } else {
    out->Append("      ");
  }
  out->AppendFormat("endloop i%u, L%u", loop_constant_index, loop_body_address);
  out->Append('\n');
}

void ParsedCallInstruction::Disassemble(StringBuffer* out) const {
  switch (type) {
    case Type::kUnconditional:
      out->Append("      call ");
      break;
    case Type::kPredicated:
      out->Append(condition ? " (p0) " : "(!p0) ");
      out->Append("call ");
      break;
    case Type::kConditional:
      out->Append("      ccall ");
      if (!condition) {
        out->Append('!');
      }
      out->AppendFormat("b%u, ", bool_constant_index);
      break;
  }
  out->AppendFormat("L%u", target_address);
  out->Append('\n');
}

void ParsedReturnInstruction::Disassemble(StringBuffer* out) const {
  out->Append("      ret\n");
}

void ParsedJumpInstruction::Disassemble(StringBuffer* out) const {
  switch (type) {
    case Type::kUnconditional:
      out->Append("      jmp ");
      break;
    case Type::kPredicated:
      out->Append(condition ? " (p0) " : "(!p0) ");
      out->Append("jmp ");
      break;
    case Type::kConditional:
      out->Append("      cjmp ");
      if (!condition) {
        out->Append('!');
      }
      out->AppendFormat("b%u, ", bool_constant_index);
      break;
  }
  out->AppendFormat("L%u", target_address);
  out->Append('\n');
}

void ParsedAllocInstruction::Disassemble(StringBuffer* out) const {
  out->Append("      alloc ");
  switch (type) {
    case AllocType::kNone:
      break;
    case AllocType::kVsPosition:
      out->AppendFormat("position");
      break;
    case AllocType::kVsInterpolators:  // or AllocType::kPsColors
      if (is_vertex_shader) {
        out->Append("interpolators");
      } else {
        out->Append("colors");
      }
      break;
    case AllocType::kMemory:
      out->AppendFormat("export = %d", count);
      break;
  }
  out->Append('\n');
}

void ParsedVertexFetchInstruction::Disassemble(StringBuffer* out) const {
  static const struct {
    const char* name;
  } kVertexFetchDataFormats[0xff] = {
#define TYPE(id) {#id}
      {0},
      {0},
      {0},
      {0},
      {0},
      {0},
      TYPE(FMT_8_8_8_8),     // 6
      TYPE(FMT_2_10_10_10),  // 7
      {0},
      {0},
      {0},
      {0},
      {0},
      {0},
      {0},
      {0},
      TYPE(FMT_10_11_11),  // 16
      TYPE(FMT_11_11_10),  // 17
      {0},
      {0},
      {0},
      {0},
      {0},
      {0},
      {0},
      TYPE(FMT_16_16),        // 25
      TYPE(FMT_16_16_16_16),  // 26
      {0},
      {0},
      {0},
      {0},
      TYPE(FMT_16_16_FLOAT),        // 31
      TYPE(FMT_16_16_16_16_FLOAT),  // 32
      TYPE(FMT_32),                 // 33
      TYPE(FMT_32_32),              // 34
      TYPE(FMT_32_32_32_32),        // 35
      TYPE(FMT_32_FLOAT),           // 36
      TYPE(FMT_32_32_FLOAT),        // 37
      TYPE(FMT_32_32_32_32_FLOAT),  // 38
      {0},
      {0},
      {0},
      {0},
      {0},
      {0},
      {0},
      {0},
      {0},
      {0},
      {0},
      {0},
      {0},
      {0},
      {0},
      {0},
      {0},
      {0},
      TYPE(FMT_32_32_32_FLOAT),  // 57
#undef TYPE
  };
  out->Append("   ");
  if (is_predicated) {
    out->Append(predicate_condition ? " (p0) " : "(!p0) ");
  } else {
    out->Append("      ");
  }
  out->AppendFormat(opcode_name);
  out->Append(' ');
  DisassembleResultOperand(result, out);
  if (!is_mini_fetch) {
    out->Append(", ");
    DisassembleSourceOperand(operands[0], out);
    out->Append(", ");
    out->AppendFormat("vf%d", 95 - operands[1].storage_index);
  }

  if (attributes.is_index_rounded) {
    out->Append(", RoundIndex=true");
  }
  if (attributes.exp_adjust) {
    out->AppendFormat(", ExpAdjust=%d", attributes.exp_adjust);
  }
  if (attributes.offset) {
    out->AppendFormat(", Offset=%d", attributes.offset);
  }
  if (attributes.data_format != VertexFormat::kUndefined) {
    out->AppendFormat(
        ", DataFormat=%s",
        kVertexFetchDataFormats[static_cast<int>(attributes.data_format)]);
  }
  if (!is_mini_fetch && attributes.stride) {
    out->AppendFormat(", Stride=%d", attributes.stride);
  }
  if (attributes.is_signed) {
    out->Append(", Signed=true");
  }
  if (attributes.is_integer) {
    out->Append(", NumFormat=integer");
  }
  if (attributes.prefetch_count) {
    out->AppendFormat(", PrefetchCount=%u", attributes.prefetch_count + 1);
  }

  out->Append('\n');
}

void ParsedTextureFetchInstruction::Disassemble(StringBuffer* out) const {
  static const char* kTextureFilterNames[] = {
      "point",
      "linear",
      "BASEMAP",
      "keep",
  };
  static const char* kAnisoFilterNames[] = {
      "disabled", "max1to1",  "max2to1", "max4to1",
      "max8to1",  "max16to1", "keep",
  };

  out->Append("   ");
  if (is_predicated) {
    out->Append(predicate_condition ? " (p0) " : "(!p0) ");
  } else {
    out->Append("      ");
  }
  out->Append(opcode_name);
  out->Append(' ');
  bool needs_comma = false;
  if (has_result()) {
    DisassembleResultOperand(result, out);
    needs_comma = true;
  }
  if (needs_comma) {
    out->Append(", ");
  }
  DisassembleSourceOperand(operands[0], out);
  if (operand_count > 1) {
    if (needs_comma) {
      out->Append(", ");
    }
    out->AppendFormat("tf%u", operands[1].storage_index);
  }

  if (!attributes.fetch_valid_only) {
    out->Append(", FetchValidOnly=false");
  }
  if (attributes.unnormalized_coordinates) {
    out->Append(", UnnormalizedTextureCoords=true");
  }
  if (attributes.mag_filter != TextureFilter::kUseFetchConst) {
    out->AppendFormat(
        ", MagFilter=%s",
        kTextureFilterNames[static_cast<int>(attributes.mag_filter)]);
  }
  if (attributes.min_filter != TextureFilter::kUseFetchConst) {
    out->AppendFormat(
        ", MinFilter=%s",
        kTextureFilterNames[static_cast<int>(attributes.min_filter)]);
  }
  if (attributes.mip_filter != TextureFilter::kUseFetchConst) {
    out->AppendFormat(
        ", MipFilter=%s",
        kTextureFilterNames[static_cast<int>(attributes.mip_filter)]);
  }
  if (attributes.aniso_filter != AnisoFilter::kUseFetchConst) {
    out->AppendFormat(
        ", AnisoFilter=%s",
        kAnisoFilterNames[static_cast<int>(attributes.aniso_filter)]);
  }
  if (!attributes.use_computed_lod) {
    out->Append(", UseComputedLOD=false");
  }
  if (attributes.use_register_lod) {
    out->Append(", UseRegisterLOD=true");
  }
  if (attributes.use_register_gradients) {
    out->Append(", UseRegisterGradients=true");
  }
  int component_count = GetTextureDimensionComponentCount(dimension);
  if (attributes.offset_x != 0.0f) {
    out->AppendFormat(", OffsetX=%g", attributes.offset_x);
  }
  if (component_count > 1 && attributes.offset_y != 0.0f) {
    out->AppendFormat(", OffsetY=%g", attributes.offset_y);
  }
  if (component_count > 2 && attributes.offset_z != 0.0f) {
    out->AppendFormat(", OffsetZ=%g", attributes.offset_z);
  }

  out->Append('\n');
}

void ParsedAluInstruction::Disassemble(StringBuffer* out) const {
  if (is_nop()) {
    out->Append("         nop\n");
    return;
  }
  if (is_scalar_type() && is_paired) {
    out->Append("              + ");
  } else {
    out->Append("   ");
  }
  if (is_predicated) {
    out->Append(predicate_condition ? " (p0) " : "(!p0) ");
  } else {
    out->Append("      ");
  }
  out->Append(opcode_name);
  if (result.is_clamped) {
    out->Append("_sat");
  }
  out->Append(' ');

  DisassembleResultOperand(result, out);

  for (int i = 0; i < operand_count; ++i) {
    out->Append(", ");
    DisassembleSourceOperand(operands[i], out);
  }
  out->Append('\n');
}

}  // namespace gpu
}  // namespace xe
