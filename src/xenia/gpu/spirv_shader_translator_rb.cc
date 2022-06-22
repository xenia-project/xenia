/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/spirv_shader_translator.h"

#include <cstdint>
#include <memory>

#include "third_party/glslang/SPIRV/GLSL.std.450.h"
#include "xenia/base/assert.h"

namespace xe {
namespace gpu {

spv::Id SpirvShaderTranslator::PreClampedFloat32To7e3(
    spv::Builder& builder, spv::Id f32_scalar, spv::Id ext_inst_glsl_std_450) {
  // https://github.com/Microsoft/DirectXTex/blob/master/DirectXTex/DirectXTexConvert.cpp
  // Assuming the value is already clamped to [0, 31.875].

  spv::Id type_uint = builder.makeUintType(32);

  // Need the source as uint for bit operations.
  {
    spv::Id source_type = builder.getTypeId(f32_scalar);
    assert_true(builder.isScalarType(source_type));
    if (!builder.isUintType(source_type)) {
      f32_scalar = builder.createUnaryOp(spv::OpBitcast, type_uint, f32_scalar);
    }
  }

  // The denormal 7e3 case.
  // denormal_biased_f32 = (f32 & 0x7FFFFF) | 0x800000
  spv::Id denormal_biased_f32;
  {
    spv::Instruction* denormal_insert_instruction = new spv::Instruction(
        builder.getUniqueId(), type_uint, spv::OpBitFieldInsert);
    denormal_insert_instruction->addIdOperand(f32_scalar);
    denormal_insert_instruction->addIdOperand(builder.makeUintConstant(1));
    denormal_insert_instruction->addIdOperand(builder.makeUintConstant(23));
    denormal_insert_instruction->addIdOperand(builder.makeUintConstant(9));
    builder.getBuildPoint()->addInstruction(
        std::unique_ptr<spv::Instruction>(denormal_insert_instruction));
    denormal_biased_f32 = denormal_insert_instruction->getResultId();
  }
  // denormal_biased_f32_shift_amount = min(125 - (f32 >> 23), 24)
  // Not allowing the shift to overflow as that's undefined in SPIR-V.
  spv::Id denormal_biased_f32_shift_amount;
  {
    spv::Instruction* denormal_shift_amount_instruction =
        new spv::Instruction(builder.getUniqueId(), type_uint, spv::OpExtInst);
    denormal_shift_amount_instruction->addIdOperand(ext_inst_glsl_std_450);
    denormal_shift_amount_instruction->addImmediateOperand(GLSLstd450UMin);
    denormal_shift_amount_instruction->addIdOperand(builder.createBinOp(
        spv::OpISub, type_uint, builder.makeUintConstant(125),
        builder.createBinOp(spv::OpShiftRightLogical, type_uint, f32_scalar,
                            builder.makeUintConstant(23))));
    denormal_shift_amount_instruction->addIdOperand(
        builder.makeUintConstant(24));
    builder.getBuildPoint()->addInstruction(
        std::unique_ptr<spv::Instruction>(denormal_shift_amount_instruction));
    denormal_biased_f32_shift_amount =
        denormal_shift_amount_instruction->getResultId();
  }
  // denormal_biased_f32 =
  //     ((f32 & 0x7FFFFF) | 0x800000) >> min(125 - (f32 >> 23), 24)
  denormal_biased_f32 = builder.createBinOp(spv::OpShiftRightLogical, type_uint,
                                            denormal_biased_f32,
                                            denormal_biased_f32_shift_amount);

  // The normal 7e3 case.
  // Bias the exponent.
  // normal_biased_f32 = f32 - (124 << 23)
  spv::Id normal_biased_f32 =
      builder.createBinOp(spv::OpISub, type_uint, f32_scalar,
                          builder.makeUintConstant(UINT32_C(124) << 23));

  // Select the needed conversion depending on whether the number is too small
  // to be represented as normalized 7e3.
  spv::Id biased_f32 = builder.createTriOp(
      spv::OpSelect, type_uint,
      builder.createBinOp(spv::OpULessThan, builder.makeBoolType(), f32_scalar,
                          builder.makeUintConstant(0x3E800000)),
      denormal_biased_f32, normal_biased_f32);

  // Build the 7e3 number rounding to the nearest even.
  // ((biased_f32 + 0x7FFF + ((biased_f32 >> 16) & 1)) >> 16) & 0x3FF
  return builder.createTriOp(
      spv::OpBitFieldUExtract, type_uint,
      builder.createBinOp(
          spv::OpIAdd, type_uint,
          builder.createBinOp(spv::OpIAdd, type_uint, biased_f32,
                              builder.makeUintConstant(0x7FFF)),
          builder.createTriOp(spv::OpBitFieldUExtract, type_uint, biased_f32,
                              builder.makeUintConstant(16),
                              builder.makeUintConstant(1))),
      builder.makeUintConstant(16), builder.makeUintConstant(10));
}

spv::Id SpirvShaderTranslator::UnclampedFloat32To7e3(
    spv::Builder& builder, spv::Id f32_scalar, spv::Id ext_inst_glsl_std_450) {
  spv::Id type_float = builder.makeFloatType(32);

  // Need the source as float for clamping.
  {
    spv::Id source_type = builder.getTypeId(f32_scalar);
    assert_true(builder.isScalarType(source_type));
    if (!builder.isFloatType(source_type)) {
      f32_scalar =
          builder.createUnaryOp(spv::OpBitcast, type_float, f32_scalar);
    }
  }

  {
    spv::Instruction* clamp_instruction =
        new spv::Instruction(builder.getUniqueId(), type_float, spv::OpExtInst);
    clamp_instruction->addIdOperand(ext_inst_glsl_std_450);
    clamp_instruction->addImmediateOperand(GLSLstd450NClamp);
    clamp_instruction->addIdOperand(f32_scalar);
    clamp_instruction->addIdOperand(builder.makeFloatConstant(0.0f));
    clamp_instruction->addIdOperand(builder.makeFloatConstant(31.875f));
    builder.getBuildPoint()->addInstruction(
        std::unique_ptr<spv::Instruction>(clamp_instruction));
    f32_scalar = clamp_instruction->getResultId();
  }

  return PreClampedFloat32To7e3(builder, f32_scalar, ext_inst_glsl_std_450);
}

spv::Id SpirvShaderTranslator::Float7e3To32(spv::Builder& builder,
                                            spv::Id f10_uint_scalar,
                                            uint32_t f10_shift,
                                            bool result_as_uint,
                                            spv::Id ext_inst_glsl_std_450) {
  // https://github.com/Microsoft/DirectXTex/blob/master/DirectXTex/DirectXTexConvert.cpp

  assert_true(builder.isUintType(builder.getTypeId(f10_uint_scalar)));
  assert_true(f10_shift <= (32 - 10));

  spv::Id type_bool = builder.makeBoolType();
  spv::Id type_int = builder.makeIntType(32);
  spv::Id type_uint = builder.makeUintType(32);

  spv::Id f10_unbiased_exponent = builder.createTriOp(
      spv::OpBitFieldUExtract, type_uint, f10_uint_scalar,
      builder.makeUintConstant(f10_shift + 7), builder.makeUintConstant(3));
  spv::Id f10_mantissa = builder.createTriOp(
      spv::OpBitFieldUExtract, type_uint, f10_uint_scalar,
      builder.makeUintConstant(f10_shift), builder.makeUintConstant(7));

  // The denormal nonzero 7e3 case.
  // denormal_mantissa_msb = findMSB(f10_mantissa)
  spv::Id denormal_mantissa_msb;
  {
    spv::Instruction* denormal_mantissa_msb_instruction =
        new spv::Instruction(builder.getUniqueId(), type_int, spv::OpExtInst);
    denormal_mantissa_msb_instruction->addIdOperand(ext_inst_glsl_std_450);
    denormal_mantissa_msb_instruction->addImmediateOperand(GLSLstd450FindUMsb);
    denormal_mantissa_msb_instruction->addIdOperand(f10_mantissa);
    builder.getBuildPoint()->addInstruction(
        std::unique_ptr<spv::Instruction>(denormal_mantissa_msb_instruction));
    denormal_mantissa_msb = denormal_mantissa_msb_instruction->getResultId();
  }
  denormal_mantissa_msb =
      builder.createUnaryOp(spv::OpBitcast, type_uint, denormal_mantissa_msb);
  // denormal_f32_unbiased_exponent = 1 - (7 - findMSB(f10_mantissa))
  // Or:
  // denormal_f32_unbiased_exponent = findMSB(f10_mantissa) - 6
  spv::Id denormal_f32_unbiased_exponent =
      builder.createBinOp(spv::OpISub, type_uint, denormal_mantissa_msb,
                          builder.makeUintConstant(6));
  // Normalize the mantissa.
  // denormal_f32_mantissa = f10_mantissa << (7 - findMSB(f10_mantissa))
  spv::Id denormal_f32_mantissa = builder.createBinOp(
      spv::OpShiftLeftLogical, type_uint, f10_mantissa,
      builder.createBinOp(spv::OpISub, type_uint, builder.makeUintConstant(7),
                          denormal_mantissa_msb));
  // If the 7e3 number is zero, make sure the float32 number is zero too.
  spv::Id f10_mantissa_is_nonzero = builder.createBinOp(
      spv::OpINotEqual, type_bool, f10_mantissa, builder.makeUintConstant(0));
  // Set the unbiased exponent to -124 for zero - 124 will be added later,
  // resulting in zero float32.
  denormal_f32_unbiased_exponent = builder.createTriOp(
      spv::OpSelect, type_uint, f10_mantissa_is_nonzero,
      denormal_f32_unbiased_exponent, builder.makeUintConstant(uint32_t(-124)));
  denormal_f32_mantissa =
      builder.createTriOp(spv::OpSelect, type_uint, f10_mantissa_is_nonzero,
                          denormal_f32_mantissa, builder.makeUintConstant(0));

  // Select the needed conversion depending on whether the number is normal.
  spv::Id f10_is_normal =
      builder.createBinOp(spv::OpINotEqual, type_bool, f10_unbiased_exponent,
                          builder.makeUintConstant(0));
  spv::Id f32_unbiased_exponent = builder.createTriOp(
      spv::OpSelect, type_uint, f10_is_normal, f10_unbiased_exponent,
      denormal_f32_unbiased_exponent);
  spv::Id f32_mantissa =
      builder.createTriOp(spv::OpSelect, type_uint, f10_is_normal, f10_mantissa,
                          denormal_f32_mantissa);

  // Bias the exponent and construct the build the float32 number.
  spv::Id f32_shifted;
  {
    spv::Instruction* f32_insert_instruction = new spv::Instruction(
        builder.getUniqueId(), type_uint, spv::OpBitFieldInsert);
    f32_insert_instruction->addIdOperand(f32_mantissa);
    f32_insert_instruction->addIdOperand(
        builder.createBinOp(spv::OpIAdd, type_uint, f32_unbiased_exponent,
                            builder.makeUintConstant(124)));
    f32_insert_instruction->addIdOperand(builder.makeUintConstant(7));
    f32_insert_instruction->addIdOperand(builder.makeUintConstant(8));
    builder.getBuildPoint()->addInstruction(
        std::unique_ptr<spv::Instruction>(f32_insert_instruction));
    f32_shifted = f32_insert_instruction->getResultId();
  }
  spv::Id f32 =
      builder.createBinOp(spv::OpShiftLeftLogical, type_uint, f32_shifted,
                          builder.makeUintConstant(23 - 7));

  if (!result_as_uint) {
    f32 = builder.createUnaryOp(spv::OpBitcast, builder.makeFloatType(32), f32);
  }

  return f32;
}

spv::Id SpirvShaderTranslator::PreClampedDepthTo20e4(
    spv::Builder& builder, spv::Id f32_scalar, bool round_to_nearest_even,
    bool remap_from_0_to_0_5, spv::Id ext_inst_glsl_std_450) {
  // CFloat24 from d3dref9.dll +
  // https://github.com/Microsoft/DirectXTex/blob/master/DirectXTex/DirectXTexConvert.cpp
  // Assuming the value is already clamped to [0, 2) (in all places, the depth
  // is written with saturation).

  uint32_t remap_bias = uint32_t(remap_from_0_to_0_5);

  spv::Id type_uint = builder.makeUintType(32);

  // Need the source as uint for bit operations.
  {
    spv::Id source_type = builder.getTypeId(f32_scalar);
    assert_true(builder.isScalarType(source_type));
    if (!builder.isUintType(source_type)) {
      f32_scalar = builder.createUnaryOp(spv::OpBitcast, type_uint, f32_scalar);
    }
  }

  // The denormal 20e4 case.
  // denormal_biased_f32 = (f32 & 0x7FFFFF) | 0x800000
  spv::Id denormal_biased_f32;
  {
    spv::Instruction* denormal_insert_instruction = new spv::Instruction(
        builder.getUniqueId(), type_uint, spv::OpBitFieldInsert);
    denormal_insert_instruction->addIdOperand(f32_scalar);
    denormal_insert_instruction->addIdOperand(builder.makeUintConstant(1));
    denormal_insert_instruction->addIdOperand(builder.makeUintConstant(23));
    denormal_insert_instruction->addIdOperand(builder.makeUintConstant(9));
    builder.getBuildPoint()->addInstruction(
        std::unique_ptr<spv::Instruction>(denormal_insert_instruction));
    denormal_biased_f32 = denormal_insert_instruction->getResultId();
  }
  // denormal_biased_f32_shift_amount = min(113 - (f32 >> 23), 24)
  // Not allowing the shift to overflow as that's undefined in SPIR-V.
  spv::Id denormal_biased_f32_shift_amount;
  {
    spv::Instruction* denormal_shift_amount_instruction =
        new spv::Instruction(builder.getUniqueId(), type_uint, spv::OpExtInst);
    denormal_shift_amount_instruction->addIdOperand(ext_inst_glsl_std_450);
    denormal_shift_amount_instruction->addImmediateOperand(GLSLstd450UMin);
    denormal_shift_amount_instruction->addIdOperand(builder.createBinOp(
        spv::OpISub, type_uint, builder.makeUintConstant(113 - remap_bias),
        builder.createBinOp(spv::OpShiftRightLogical, type_uint, f32_scalar,
                            builder.makeUintConstant(23))));
    denormal_shift_amount_instruction->addIdOperand(
        builder.makeUintConstant(24));
    builder.getBuildPoint()->addInstruction(
        std::unique_ptr<spv::Instruction>(denormal_shift_amount_instruction));
    denormal_biased_f32_shift_amount =
        denormal_shift_amount_instruction->getResultId();
  }
  // denormal_biased_f32 =
  //     ((f32 & 0x7FFFFF) | 0x800000) >> min(113 - (f32 >> 23), 24)
  denormal_biased_f32 = builder.createBinOp(spv::OpShiftRightLogical, type_uint,
                                            denormal_biased_f32,
                                            denormal_biased_f32_shift_amount);

  // The normal 20e4 case.
  // Bias the exponent.
  // normal_biased_f32 = f32 - (112 << 23)
  spv::Id normal_biased_f32 = builder.createBinOp(
      spv::OpISub, type_uint, f32_scalar,
      builder.makeUintConstant((UINT32_C(112) + remap_bias) << 23));

  // Select the needed conversion depending on whether the number is too small
  // to be represented as normalized 20e4.
  spv::Id biased_f32 = builder.createTriOp(
      spv::OpSelect, type_uint,
      builder.createBinOp(
          spv::OpULessThan, builder.makeBoolType(), f32_scalar,
          builder.makeUintConstant(0x38800000 - (remap_bias << 23))),
      denormal_biased_f32, normal_biased_f32);

  // Build the 20e4 number rounding to the nearest even or towards zero.
  if (round_to_nearest_even) {
    // biased_f32 += 3 + ((biased_f32 >> 3) & 1)
    biased_f32 = builder.createBinOp(
        spv::OpIAdd, type_uint,
        builder.createBinOp(spv::OpIAdd, type_uint, biased_f32,
                            builder.makeUintConstant(3)),
        builder.createTriOp(spv::OpBitFieldUExtract, type_uint, biased_f32,
                            builder.makeUintConstant(3),
                            builder.makeUintConstant(1)));
  }
  return builder.createTriOp(spv::OpBitFieldUExtract, type_uint, biased_f32,
                             builder.makeUintConstant(3),
                             builder.makeUintConstant(24));
}

spv::Id SpirvShaderTranslator::Depth20e4To32(spv::Builder& builder,
                                             spv::Id f24_uint_scalar,
                                             uint32_t f24_shift,
                                             bool remap_to_0_to_0_5,
                                             bool result_as_uint,
                                             spv::Id ext_inst_glsl_std_450) {
  // CFloat24 from d3dref9.dll +
  // https://github.com/Microsoft/DirectXTex/blob/master/DirectXTex/DirectXTexConvert.cpp

  assert_true(builder.isUintType(builder.getTypeId(f24_uint_scalar)));
  assert_true(f24_shift <= (32 - 24));

  uint32_t remap_bias = uint32_t(remap_to_0_to_0_5);

  spv::Id type_bool = builder.makeBoolType();
  spv::Id type_int = builder.makeIntType(32);
  spv::Id type_uint = builder.makeUintType(32);

  spv::Id f24_unbiased_exponent = builder.createTriOp(
      spv::OpBitFieldUExtract, type_uint, f24_uint_scalar,
      builder.makeUintConstant(f24_shift + 20), builder.makeUintConstant(4));
  spv::Id f24_mantissa = builder.createTriOp(
      spv::OpBitFieldUExtract, type_uint, f24_uint_scalar,
      builder.makeUintConstant(f24_shift), builder.makeUintConstant(20));

  // The denormal nonzero 20e4 case.
  // denormal_mantissa_msb = findMSB(f24_mantissa)
  spv::Id denormal_mantissa_msb;
  {
    spv::Instruction* denormal_mantissa_msb_instruction =
        new spv::Instruction(builder.getUniqueId(), type_int, spv::OpExtInst);
    denormal_mantissa_msb_instruction->addIdOperand(ext_inst_glsl_std_450);
    denormal_mantissa_msb_instruction->addImmediateOperand(GLSLstd450FindUMsb);
    denormal_mantissa_msb_instruction->addIdOperand(f24_mantissa);
    builder.getBuildPoint()->addInstruction(
        std::unique_ptr<spv::Instruction>(denormal_mantissa_msb_instruction));
    denormal_mantissa_msb = denormal_mantissa_msb_instruction->getResultId();
  }
  denormal_mantissa_msb =
      builder.createUnaryOp(spv::OpBitcast, type_uint, denormal_mantissa_msb);
  // denormal_f32_unbiased_exponent = 1 - (20 - findMSB(f24_mantissa))
  // Or:
  // denormal_f32_unbiased_exponent = findMSB(f24_mantissa) - 19
  spv::Id denormal_f32_unbiased_exponent =
      builder.createBinOp(spv::OpISub, type_uint, denormal_mantissa_msb,
                          builder.makeUintConstant(19));
  // Normalize the mantissa.
  // denormal_f32_mantissa = f24_mantissa << (20 - findMSB(f24_mantissa))
  spv::Id denormal_f32_mantissa = builder.createBinOp(
      spv::OpShiftLeftLogical, type_uint, f24_mantissa,
      builder.createBinOp(spv::OpISub, type_uint, builder.makeUintConstant(20),
                          denormal_mantissa_msb));
  // If the 20e4 number is zero, make sure the float32 number is zero too.
  spv::Id f24_mantissa_is_nonzero = builder.createBinOp(
      spv::OpINotEqual, type_bool, f24_mantissa, builder.makeUintConstant(0));
  // Set the unbiased exponent to -112 for zero - 112 will be added later,
  // resulting in zero float32.
  denormal_f32_unbiased_exponent = builder.createTriOp(
      spv::OpSelect, type_uint, f24_mantissa_is_nonzero,
      denormal_f32_unbiased_exponent,
      builder.makeUintConstant(uint32_t(-int32_t(112 - remap_bias))));
  denormal_f32_mantissa =
      builder.createTriOp(spv::OpSelect, type_uint, f24_mantissa_is_nonzero,
                          denormal_f32_mantissa, builder.makeUintConstant(0));

  // Select the needed conversion depending on whether the number is normal.
  spv::Id f24_is_normal =
      builder.createBinOp(spv::OpINotEqual, type_bool, f24_unbiased_exponent,
                          builder.makeUintConstant(0));
  spv::Id f32_unbiased_exponent = builder.createTriOp(
      spv::OpSelect, type_uint, f24_is_normal, f24_unbiased_exponent,
      denormal_f32_unbiased_exponent);
  spv::Id f32_mantissa =
      builder.createTriOp(spv::OpSelect, type_uint, f24_is_normal, f24_mantissa,
                          denormal_f32_mantissa);

  // Bias the exponent and construct the build the float32 number.
  spv::Id f32_shifted;
  {
    spv::Instruction* f32_insert_instruction = new spv::Instruction(
        builder.getUniqueId(), type_uint, spv::OpBitFieldInsert);
    f32_insert_instruction->addIdOperand(f32_mantissa);
    f32_insert_instruction->addIdOperand(
        builder.createBinOp(spv::OpIAdd, type_uint, f32_unbiased_exponent,
                            builder.makeUintConstant(112 - remap_bias)));
    f32_insert_instruction->addIdOperand(builder.makeUintConstant(20));
    f32_insert_instruction->addIdOperand(builder.makeUintConstant(8));
    builder.getBuildPoint()->addInstruction(
        std::unique_ptr<spv::Instruction>(f32_insert_instruction));
    f32_shifted = f32_insert_instruction->getResultId();
  }
  spv::Id f32 =
      builder.createBinOp(spv::OpShiftLeftLogical, type_uint, f32_shifted,
                          builder.makeUintConstant(23 - 20));

  if (!result_as_uint) {
    f32 = builder.createUnaryOp(spv::OpBitcast, builder.makeFloatType(32), f32);
  }

  return f32;
}

}  // namespace gpu
}  // namespace xe
