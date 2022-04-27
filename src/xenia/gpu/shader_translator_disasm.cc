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
    case InstructionStorageTarget::kInterpolator:
      out->Append('o');
      uses_storage_index = true;
      break;
    case InstructionStorageTarget::kPosition:
      out->Append("oPos");
      break;
    case InstructionStorageTarget::kPointSizeEdgeFlagKillVertex:
      out->Append("oPts");
      break;
    case InstructionStorageTarget::kExportAddress:
      out->Append("eA");
      break;
    case InstructionStorageTarget::kExportData:
      out->Append("eM");
      uses_storage_index = true;
      break;
    case InstructionStorageTarget::kColor:
      out->Append("oC");
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
      case InstructionStorageAddressingMode::kAbsolute:
        out->AppendFormat("{}", result.storage_index);
        break;
      case InstructionStorageAddressingMode::kAddressRegisterRelative:
        out->AppendFormat("[{}+a0]", result.storage_index);
        break;
      case InstructionStorageAddressingMode::kLoopRelative:
        out->AppendFormat("[{}+aL]", result.storage_index);
        break;
    }
  }
  // Not using GetUsedWriteMask/IsStandardSwizzle because they filter out
  // components not having any runtime effect, but those components are still
  // present in the microcode.
  if (!result.original_write_mask) {
    out->Append("._");
  } else if (result.original_write_mask != 0b1111 ||
             result.components[0] != SwizzleSource::kX ||
             result.components[1] != SwizzleSource::kY ||
             result.components[2] != SwizzleSource::kZ ||
             result.components[3] != SwizzleSource::kW) {
    out->Append('.');
    for (int i = 0; i < 4; ++i) {
      if (result.original_write_mask & (1 << i)) {
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
    case InstructionStorageSource::kTextureFetchConstant:
    case InstructionStorageSource::kVertexFetchConstant:
      assert_always();
      break;
  }
  if (op.is_absolute_value) {
    out->Append("_abs");
  }
  switch (op.storage_addressing_mode) {
    case InstructionStorageAddressingMode::kAbsolute:
      if (op.is_absolute_value) {
        out->AppendFormat("[{}]", op.storage_index);
      } else {
        out->AppendFormat("{}", op.storage_index);
      }
      break;
    case InstructionStorageAddressingMode::kAddressRegisterRelative:
      out->AppendFormat("[{}+a0]", op.storage_index);
      break;
    case InstructionStorageAddressingMode::kLoopRelative:
      out->AppendFormat("[{}+aL]", op.storage_index);
      break;
  }
  if (!op.IsStandardSwizzle()) {
    out->Append('.');
    if (op.component_count == 1) {
      out->Append(GetCharForSwizzle(op.components[0]));
    } else if (op.component_count == 2) {
      out->Append(GetCharForSwizzle(op.components[0]));
      out->Append(GetCharForSwizzle(op.components[1]));
    } else {
      for (uint32_t j = 0; j < op.component_count; ++j) {
        out->Append(GetCharForSwizzle(op.components[j]));
      }
    }
  }
}

void ParsedExecInstruction::Disassemble(StringBuffer* out) const {
  switch (type) {
    case Type::kUnconditional:
      out->AppendFormat("      {}", opcode_name);
      break;
    case Type::kPredicated:
      out->Append(condition ? " (p0) " : "(!p0) ");
      out->AppendFormat("{}", opcode_name);
      break;
    case Type::kConditional:
      out->AppendFormat("      {} {}b{}", opcode_name, condition ? "" : "!",
                        bool_constant_index);
      break;
  }
  if (is_yield) {
    if (type == Type::kConditional) {
      // For `exec` or `(p0) exec` (but not `cexec`), "unexpected token ','" if
      // preceded by a comma.
      out->Append(',');
    }
    out->Append(" Yield=true");
  }
  if (!is_predicate_clean) {
    out->Append("    // PredicateClean=false");
  }
  out->Append('\n');
}

void ParsedLoopStartInstruction::Disassemble(StringBuffer* out) const {
  out->Append("      loop ");
  out->AppendFormat("i{}, L{}", loop_constant_index, loop_skip_address);
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
  out->AppendFormat("endloop i{}, L{}", loop_constant_index, loop_body_address);
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
      out->AppendFormat("b{}, ", bool_constant_index);
      break;
  }
  out->AppendFormat("L{}", target_address);
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
      out->AppendFormat("b{}, ", bool_constant_index);
      break;
  }
  out->AppendFormat("L{}", target_address);
  out->Append('\n');
}

void ParsedAllocInstruction::Disassemble(StringBuffer* out) const {
  out->Append("      alloc ");
  switch (type) {
    case AllocType::kNone:
      break;
    case AllocType::kVsPosition:
      out->Append("position");
      break;
    case AllocType::kVsInterpolators:  // or AllocType::kPsColors
      if (is_vertex_shader) {
        out->Append("interpolators");
      } else {
        out->Append("colors");
      }
      break;
    case AllocType::kMemory:
      out->AppendFormat("export = {}", count);
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
  out->Append(opcode_name);
  out->Append(' ');
  DisassembleResultOperand(result, out);
  if (!is_mini_fetch) {
    out->Append(", ");
    DisassembleSourceOperand(operands[0], out);
    out->AppendFormat(", vf{}", 95 - operands[1].storage_index);
    if (attributes.is_index_rounded) {
      out->Append(", RoundIndex=true");
    }
  }

  if (attributes.exp_adjust) {
    out->AppendFormat(", ExpAdjust={}", attributes.exp_adjust);
  }
  if (attributes.offset) {
    out->AppendFormat(", Offset={}", attributes.offset);
  }
  if (attributes.data_format != xenos::VertexFormat::kUndefined) {
    out->AppendFormat(
        ", DataFormat={}",
        kVertexFetchDataFormats[static_cast<int>(attributes.data_format)].name);
  }
  if (!is_mini_fetch && attributes.stride) {
    out->AppendFormat(", Stride={}", attributes.stride);
  }
  if (attributes.is_signed) {
    out->Append(", Signed=true");
  }
  if (attributes.is_integer) {
    out->Append(", NumFormat=integer");
  }
  if (attributes.prefetch_count) {
    out->AppendFormat(", PrefetchCount={}", attributes.prefetch_count + 1);
  }

  out->Append('\n');
}

void ParsedTextureFetchInstruction::Disassemble(StringBuffer* out) const {
  static const char* kTextureFilterNames[] = {
      "point",
      "linear",
      "basemap",
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
    out->AppendFormat("tf{}", operands[1].storage_index);
  }

  if (!attributes.fetch_valid_only) {
    out->Append(", FetchValidOnly=false");
  }
  if (attributes.unnormalized_coordinates) {
    out->Append(", UnnormalizedTextureCoords=true");
  }
  if (attributes.mag_filter != xenos::TextureFilter::kUseFetchConst) {
    out->AppendFormat(
        ", MagFilter={}",
        kTextureFilterNames[static_cast<int>(attributes.mag_filter)]);
  }
  if (attributes.min_filter != xenos::TextureFilter::kUseFetchConst) {
    out->AppendFormat(
        ", MinFilter={}",
        kTextureFilterNames[static_cast<int>(attributes.min_filter)]);
  }
  if (attributes.mip_filter != xenos::TextureFilter::kUseFetchConst) {
    out->AppendFormat(
        ", MipFilter={}",
        kTextureFilterNames[static_cast<int>(attributes.mip_filter)]);
  }
  if (attributes.aniso_filter != xenos::AnisoFilter::kUseFetchConst) {
    out->AppendFormat(
        ", AnisoFilter={}",
        kAnisoFilterNames[static_cast<int>(attributes.aniso_filter)]);
  }
  if (attributes.vol_mag_filter != xenos::TextureFilter::kUseFetchConst) {
    out->AppendFormat(
        ", VolMagFilter={}",
        kTextureFilterNames[static_cast<int>(attributes.vol_mag_filter)]);
  }
  if (attributes.vol_min_filter != xenos::TextureFilter::kUseFetchConst) {
    out->AppendFormat(
        ", VolMinFilter={}",
        kTextureFilterNames[static_cast<int>(attributes.vol_min_filter)]);
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
  if (attributes.lod_bias != 0.0f) {
    out->AppendFormat(", LODBias={:g}", attributes.lod_bias);
  }
  int component_count = xenos::GetFetchOpDimensionComponentCount(dimension);
  if (attributes.offset_x != 0.0f) {
    out->AppendFormat(", OffsetX={:g}", attributes.offset_x);
  }
  if (component_count > 1 && attributes.offset_y != 0.0f) {
    out->AppendFormat(", OffsetY={:g}", attributes.offset_y);
  }
  if (component_count > 2 && attributes.offset_z != 0.0f) {
    out->AppendFormat(", OffsetZ={:g}", attributes.offset_z);
  }

  out->Append('\n');
}

void ParsedAluInstruction::Disassemble(StringBuffer* out) const {
  bool is_vector_op_default_nop = IsVectorOpDefaultNop();
  bool is_scalar_op_default_nop = IsScalarOpDefaultNop();
  if (is_vector_op_default_nop && is_scalar_op_default_nop) {
    out->Append("   ");
    if (is_predicated) {
      out->Append(predicate_condition ? " (p0) " : "(!p0) ");
    } else {
      out->Append("      ");
    }
    out->Append("nop\n");
    return;
  }
  if (!is_vector_op_default_nop) {
    out->Append("   ");
    if (is_predicated) {
      out->Append(predicate_condition ? " (p0) " : "(!p0) ");
    } else {
      out->Append("      ");
    }
    out->Append(vector_opcode_name);
    if (vector_and_constant_result.is_clamped) {
      out->Append("_sat");
    }
    out->Append(' ');
    DisassembleResultOperand(vector_and_constant_result, out);
    for (uint32_t i = 0; i < vector_operand_count; ++i) {
      out->Append(", ");
      DisassembleSourceOperand(vector_operands[i], out);
    }
    out->Append('\n');
  }
  if (!is_scalar_op_default_nop) {
    out->Append(is_vector_op_default_nop ? "   " : "              + ");
    if (is_predicated) {
      out->Append(predicate_condition ? " (p0) " : "(!p0) ");
    } else {
      out->Append("      ");
    }
    out->Append(scalar_opcode_name);
    if (scalar_result.is_clamped) {
      out->Append("_sat");
    }
    out->Append(' ');
    DisassembleResultOperand(scalar_result, out);
    for (uint32_t i = 0; i < scalar_operand_count; ++i) {
      out->Append(", ");
      DisassembleSourceOperand(scalar_operands[i], out);
    }
    out->Append('\n');
  }
}

}  // namespace gpu
}  // namespace xe
