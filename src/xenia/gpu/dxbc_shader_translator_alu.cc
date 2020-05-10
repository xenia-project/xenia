/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/dxbc_shader_translator.h"

#include "third_party/dxbc/d3d12TokenizedProgramFormat.hpp"

#include "xenia/base/assert.h"
#include "xenia/base/math.h"

namespace xe {
namespace gpu {
using namespace ucode;

void DxbcShaderTranslator::ProcessVectorAluOperation(
    const ParsedAluInstruction& instr, uint32_t& result_swizzle,
    bool& predicate_written) {
  result_swizzle = DxbcSrc::kXYZW;
  predicate_written = false;

  uint32_t used_result_components =
      instr.vector_and_constant_result.GetUsedResultComponents();
  if (!used_result_components &&
      !AluVectorOpHasSideEffects(instr.vector_opcode)) {
    return;
  }

  // Load operands.
  // A small shortcut, operands of cube are the same, but swizzled.
  uint32_t operand_count;
  if (instr.vector_opcode == AluVectorOpcode::kCube) {
    operand_count = 1;
  } else {
    operand_count = instr.vector_operand_count;
  }
  uint32_t operand_needed_components[3];
  for (uint32_t i = 0; i < operand_count; ++i) {
    operand_needed_components[i] = ucode::GetAluVectorOpNeededSourceComponents(
        instr.vector_opcode, i + 1, used_result_components);
  }
  // .zzxy - don't need the first component.
  if (instr.vector_opcode == AluVectorOpcode::kCube) {
    operand_needed_components[0] &= 0b1101;
  }
  DxbcSrc operands[3]{DxbcSrc::LF(0.0f), DxbcSrc::LF(0.0f), DxbcSrc::LF(0.0f)};
  uint32_t operand_temps = 0;
  for (uint32_t i = 0; i < operand_count; ++i) {
    bool operand_temp_pushed = false;
    operands[i] =
        LoadOperand(instr.vector_operands[i], operand_needed_components[i],
                    operand_temp_pushed);
    operand_temps += uint32_t(operand_temp_pushed);
  }
  // Don't return without PopSystemTemp(operand_temps) from now on!

  DxbcDest per_component_dest(
      DxbcDest::R(system_temp_result_, used_result_components));
  switch (instr.vector_opcode) {
    case AluVectorOpcode::kAdd:
      DxbcOpAdd(per_component_dest, operands[0], operands[1]);
      break;
    case AluVectorOpcode::kMul:
    case AluVectorOpcode::kMad: {
      bool is_mad = instr.vector_opcode == AluVectorOpcode::kMad;
      if (is_mad) {
        DxbcOpMAd(per_component_dest, operands[0], operands[1], operands[2]);
      } else {
        DxbcOpMul(per_component_dest, operands[0], operands[1]);
      }
      // Shader Model 3: 0 or denormal * anything = 0.
      // FIXME(Triang3l): Signed zero needs research and handling.
      uint32_t absolute_different =
          used_result_components &
          ~instr.vector_operands[0].GetAbsoluteIdenticalComponents(
              instr.vector_operands[1]);
      if (absolute_different) {
        uint32_t is_zero_temp = PushSystemTemp();
        DxbcOpMin(DxbcDest::R(is_zero_temp, absolute_different),
                  operands[0].Abs(), operands[1].Abs());
        // min isn't required to flush denormals, eq is.
        DxbcOpEq(DxbcDest::R(is_zero_temp, absolute_different),
                 DxbcSrc::R(is_zero_temp), DxbcSrc::LF(0.0f));
        DxbcOpMovC(DxbcDest::R(system_temp_result_, absolute_different),
                   DxbcSrc::R(is_zero_temp),
                   is_mad ? operands[2] : DxbcSrc::LF(0.0f),
                   DxbcSrc::R(system_temp_result_));
        // Release is_zero_temp.
        PopSystemTemp();
      }
    } break;

    case AluVectorOpcode::kMax:
    case AluVectorOpcode::kMin: {
      // max is commonly used as mov.
      uint32_t identical = instr.vector_operands[0].GetIdenticalComponents(
                               instr.vector_operands[1]) &
                           used_result_components;
      uint32_t different = used_result_components & ~identical;
      if (different) {
        // Shader Model 3 NaN behavior (a op b ? a : b, not fmax/fmin).
        if (instr.vector_opcode == AluVectorOpcode::kMin) {
          DxbcOpLT(DxbcDest::R(system_temp_result_, different), operands[0],
                   operands[1]);
        } else {
          DxbcOpGE(DxbcDest::R(system_temp_result_, different), operands[0],
                   operands[1]);
        }
        DxbcOpMovC(DxbcDest::R(system_temp_result_, different),
                   DxbcSrc::R(system_temp_result_), operands[0], operands[1]);
      }
      if (identical) {
        DxbcOpMov(DxbcDest::R(system_temp_result_, identical), operands[0]);
      }
    } break;

    case AluVectorOpcode::kSeq:
      DxbcOpEq(per_component_dest, operands[0], operands[1]);
      DxbcOpAnd(per_component_dest, DxbcSrc::R(system_temp_result_),
                DxbcSrc::LF(1.0f));
      break;
    case AluVectorOpcode::kSgt:
      DxbcOpLT(per_component_dest, operands[1], operands[0]);
      DxbcOpAnd(per_component_dest, DxbcSrc::R(system_temp_result_),
                DxbcSrc::LF(1.0f));
      break;
    case AluVectorOpcode::kSge:
      DxbcOpGE(per_component_dest, operands[0], operands[1]);
      DxbcOpAnd(per_component_dest, DxbcSrc::R(system_temp_result_),
                DxbcSrc::LF(1.0f));
      break;
    case AluVectorOpcode::kSne:
      DxbcOpNE(per_component_dest, operands[0], operands[1]);
      DxbcOpAnd(per_component_dest, DxbcSrc::R(system_temp_result_),
                DxbcSrc::LF(1.0f));
      break;

    case AluVectorOpcode::kFrc:
      DxbcOpFrc(per_component_dest, operands[0]);
      break;
    case AluVectorOpcode::kTrunc:
      DxbcOpRoundZ(per_component_dest, operands[0]);
      break;
    case AluVectorOpcode::kFloor:
      DxbcOpRoundNI(per_component_dest, operands[0]);
      break;

    case AluVectorOpcode::kCndEq:
      DxbcOpEq(per_component_dest, operands[0], DxbcSrc::LF(0.0f));
      DxbcOpMovC(per_component_dest, DxbcSrc::R(system_temp_result_),
                 operands[1], operands[2]);
      break;
    case AluVectorOpcode::kCndGe:
      DxbcOpGE(per_component_dest, operands[0], DxbcSrc::LF(0.0f));
      DxbcOpMovC(per_component_dest, DxbcSrc::R(system_temp_result_),
                 operands[1], operands[2]);
      break;
    case AluVectorOpcode::kCndGt:
      DxbcOpLT(per_component_dest, DxbcSrc::LF(0.0f), operands[0]);
      DxbcOpMovC(per_component_dest, DxbcSrc::R(system_temp_result_),
                 operands[1], operands[2]);
      break;

    case AluVectorOpcode::kDp4:
    case AluVectorOpcode::kDp3:
    case AluVectorOpcode::kDp2Add: {
      uint32_t component_count;
      if (instr.vector_opcode == AluVectorOpcode::kDp2Add) {
        component_count = 2;
      } else if (instr.vector_opcode == AluVectorOpcode::kDp3) {
        component_count = 3;
      } else {
        component_count = 4;
      }
      result_swizzle = DxbcSrc::kXXXX;
      uint32_t absolute_different =
          uint32_t((1 << component_count) - 1) &
          ~instr.vector_operands[0].GetAbsoluteIdenticalComponents(
              instr.vector_operands[1]);
      if (absolute_different) {
        // Shader Model 3: 0 or denormal * anything = 0.
        // FIXME(Triang3l): Signed zero needs research and handling.
        // Add component products only if non-zero. For dp4, 16 scalar
        // operations in the worst case (as opposed to always 20 for
        // eq/movc/eq/movc/dp4 or min/eq/movc/movc/dp4 for preparing operands
        // for dp4).
        DxbcOpMul(DxbcDest::R(system_temp_result_, 0b0001),
                  operands[0].SelectFromSwizzled(0),
                  operands[1].SelectFromSwizzled(0));
        if (absolute_different & 0b0001) {
          DxbcOpMin(DxbcDest::R(system_temp_result_, 0b0010),
                    operands[0].SelectFromSwizzled(0).Abs(),
                    operands[1].SelectFromSwizzled(0).Abs());
          DxbcOpEq(DxbcDest::R(system_temp_result_, 0b0010),
                   DxbcSrc::R(system_temp_result_, DxbcSrc::kYYYY),
                   DxbcSrc::LF(0.0f));
          DxbcOpMovC(DxbcDest::R(system_temp_result_, 0b0001),
                     DxbcSrc::R(system_temp_result_, DxbcSrc::kYYYY),
                     DxbcSrc::LF(0.0f),
                     DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX));
        }
        for (uint32_t i = 1; i < component_count; ++i) {
          bool component_different = (absolute_different & (1 << i)) != 0;
          DxbcOpMAd(DxbcDest::R(system_temp_result_,
                                component_different ? 0b0010 : 0b0001),
                    operands[0].SelectFromSwizzled(i),
                    operands[1].SelectFromSwizzled(i),
                    DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX));
          if (component_different) {
            DxbcOpMin(DxbcDest::R(system_temp_result_, 0b0100),
                      operands[0].SelectFromSwizzled(i).Abs(),
                      operands[1].SelectFromSwizzled(i).Abs());
            DxbcOpEq(DxbcDest::R(system_temp_result_, 0b0100),
                     DxbcSrc::R(system_temp_result_, DxbcSrc::kZZZZ),
                     DxbcSrc::LF(0.0f));
            DxbcOpMovC(DxbcDest::R(system_temp_result_, 0b0001),
                       DxbcSrc::R(system_temp_result_, DxbcSrc::kZZZZ),
                       DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX),
                       DxbcSrc::R(system_temp_result_, DxbcSrc::kYYYY));
          }
        }
      } else {
        if (component_count == 2) {
          DxbcOpDP2(DxbcDest::R(system_temp_result_, 0b0001), operands[0],
                    operands[1]);
        } else if (component_count == 3) {
          DxbcOpDP3(DxbcDest::R(system_temp_result_, 0b0001), operands[0],
                    operands[1]);
        } else {
          assert_true(component_count == 4);
          DxbcOpDP4(DxbcDest::R(system_temp_result_, 0b0001), operands[0],
                    operands[1]);
        }
      }
      if (component_count == 2) {
        // Add the third operand. Since floating-point addition isn't
        // associative, even though adding this in multiply-add for the first
        // component would be faster, it's safer to add here, in the end.
        DxbcOpAdd(DxbcDest::R(system_temp_result_, 0b0001),
                  DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX),
                  operands[2].SelectFromSwizzled(0));
      }
    } break;

    case AluVectorOpcode::kCube: {
      // operands[0] is .z_xy.
      // Result is T coordinate, S coordinate, 2 * major axis, face ID.
      constexpr uint32_t kCubeX = 2, kCubeY = 3, kCubeZ = 0;
      DxbcSrc cube_x_src(operands[0].SelectFromSwizzled(kCubeX));
      DxbcSrc cube_y_src(operands[0].SelectFromSwizzled(kCubeY));
      DxbcSrc cube_z_src(operands[0].SelectFromSwizzled(kCubeZ));
      // result.xy = bool2(abs(z) >= abs(x), abs(z) >= abs(y))
      DxbcOpGE(DxbcDest::R(system_temp_result_, 0b0011), cube_z_src.Abs(),
               operands[0].SwizzleSwizzled(kCubeX | (kCubeY << 2)).Abs());
      // result.x = abs(z) >= abs(x) && abs(z) >= abs(y)
      DxbcOpAnd(DxbcDest::R(system_temp_result_, 0b0001),
                DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX),
                DxbcSrc::R(system_temp_result_, DxbcSrc::kYYYY));
      DxbcDest tc_dest(DxbcDest::R(system_temp_result_, 0b0001));
      DxbcDest sc_dest(DxbcDest::R(system_temp_result_, 0b0010));
      DxbcDest ma_dest(DxbcDest::R(system_temp_result_, 0b0100));
      DxbcDest id_dest(DxbcDest::R(system_temp_result_, 0b1000));
      DxbcOpIf(true, DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX));
      {
        // Z is the major axis.
        // z < 0 needed for SC and ID, but the last to use is ID.
        uint32_t ma_neg_component = (used_result_components & 0b1000) ? 3 : 1;
        if (used_result_components & 0b1010) {
          DxbcOpLT(DxbcDest::R(system_temp_result_, 1 << ma_neg_component),
                   cube_z_src, DxbcSrc::LF(0.0f));
        }
        if (used_result_components & 0b0001) {
          DxbcOpMov(tc_dest, -cube_y_src);
        }
        if (used_result_components & 0b0010) {
          DxbcOpMovC(sc_dest,
                     DxbcSrc::R(system_temp_result_).Select(ma_neg_component),
                     -cube_x_src, cube_x_src);
        }
        if (used_result_components & 0b0100) {
          DxbcOpMul(ma_dest, DxbcSrc::LF(2.0f), cube_z_src);
        }
        if (used_result_components & 0b1000) {
          DxbcOpMovC(id_dest,
                     DxbcSrc::R(system_temp_result_).Select(ma_neg_component),
                     DxbcSrc::LF(5.0f), DxbcSrc::LF(4.0f));
        }
      }
      DxbcOpElse();
      {
        // result.x = abs(y) >= abs(x)
        DxbcOpGE(DxbcDest::R(system_temp_result_, 0b0001), cube_y_src.Abs(),
                 cube_x_src.Abs());
        DxbcOpIf(true, DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX));
        {
          // Y is the major axis.
          // y < 0 needed for TC and ID, but the last to use is ID.
          uint32_t ma_neg_component = (used_result_components & 0b1000) ? 3 : 0;
          if (used_result_components & 0b1001) {
            DxbcOpLT(DxbcDest::R(system_temp_result_, 1 << ma_neg_component),
                     cube_y_src, DxbcSrc::LF(0.0f));
          }
          if (used_result_components & 0b0001) {
            DxbcOpMovC(tc_dest,
                       DxbcSrc::R(system_temp_result_).Select(ma_neg_component),
                       -cube_z_src, cube_z_src);
          }
          if (used_result_components & 0b0010) {
            DxbcOpMov(sc_dest, cube_x_src);
          }
          if (used_result_components & 0b0100) {
            DxbcOpMul(ma_dest, DxbcSrc::LF(2.0f), cube_y_src);
          }
          if (used_result_components & 0b1000) {
            DxbcOpMovC(id_dest,
                       DxbcSrc::R(system_temp_result_).Select(ma_neg_component),
                       DxbcSrc::LF(3.0f), DxbcSrc::LF(2.0f));
          }
        }
        DxbcOpElse();
        {
          // X is the major axis.
          // x < 0 needed for SC and ID, but the last to use is ID.
          uint32_t ma_neg_component = (used_result_components & 0b1000) ? 3 : 1;
          if (used_result_components & 0b1010) {
            DxbcOpLT(DxbcDest::R(system_temp_result_, 1 << ma_neg_component),
                     cube_x_src, DxbcSrc::LF(0.0f));
          }
          if (used_result_components & 0b0001) {
            DxbcOpMov(tc_dest, -cube_y_src);
          }
          if (used_result_components & 0b0010) {
            DxbcOpMovC(sc_dest,
                       DxbcSrc::R(system_temp_result_).Select(ma_neg_component),
                       cube_z_src, -cube_z_src);
          }
          if (used_result_components & 0b0100) {
            DxbcOpMul(ma_dest, DxbcSrc::LF(2.0f), cube_x_src);
          }
          if (used_result_components & 0b1000) {
            DxbcOpAnd(id_dest,
                      DxbcSrc::R(system_temp_result_).Select(ma_neg_component),
                      DxbcSrc::LF(1.0f));
          }
        }
        DxbcOpEndIf();
      }
      DxbcOpEndIf();
    } break;

    case AluVectorOpcode::kMax4: {
      result_swizzle = DxbcSrc::kXXXX;
      // Find max of all different components of the first operand.
      // FIXME(Triang3l): Not caring about NaN because no info about the
      // correct order, just using SM4 max here, which replaces them with the
      // non-NaN component (however, there's one nice thing about it is that it
      // may be compiled into max3 + max on GCN).
      uint32_t remaining_components = 0;
      for (uint32_t i = 0; i < 4; ++i) {
        remaining_components |= 1 << ((operands[0].swizzle_ >> (i * 2)) & 3);
      }
      uint32_t unique_component_0;
      xe::bit_scan_forward(remaining_components, &unique_component_0);
      remaining_components &= ~uint32_t(1 << unique_component_0);
      if (remaining_components) {
        uint32_t unique_component_1;
        xe::bit_scan_forward(remaining_components, &unique_component_1);
        remaining_components &= ~uint32_t(1 << unique_component_1);
        DxbcOpMax(DxbcDest::R(system_temp_result_, 0b0001),
                  operands[0].Select(unique_component_0),
                  operands[0].Select(unique_component_1));
        while (remaining_components) {
          uint32_t unique_component;
          xe::bit_scan_forward(remaining_components, &unique_component);
          remaining_components &= ~uint32_t(1 << unique_component);
          DxbcOpMax(DxbcDest::R(system_temp_result_, 0b0001),
                    DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX),
                    operands[0].Select(unique_component));
        }
      } else {
        DxbcOpMov(DxbcDest::R(system_temp_result_, 0b0001),
                  operands[0].Select(unique_component_0));
      }
    } break;

    case AluVectorOpcode::kSetpEqPush:
      predicate_written = true;
      result_swizzle = DxbcSrc::kXXXX;
      // result.xy = src0.xw == 0.0 (x only if needed).
      DxbcOpEq(DxbcDest::R(system_temp_result_,
                           used_result_components ? 0b0011 : 0b0010),
               operands[0].SwizzleSwizzled(0b1100), DxbcSrc::LF(0.0f));
      // result.zw = src1.xw == 0.0 (z only if needed).
      DxbcOpEq(DxbcDest::R(system_temp_result_,
                           used_result_components ? 0b1100 : 0b1000),
               operands[1].SwizzleSwizzled(0b11000000), DxbcSrc::LF(0.0f));
      // p0 = src0.w == 0.0 && src1.w == 0.0
      DxbcOpAnd(DxbcDest::R(system_temp_ps_pc_p0_a0_, 0b0100),
                DxbcSrc::R(system_temp_result_, DxbcSrc::kYYYY),
                DxbcSrc::R(system_temp_result_, DxbcSrc::kWWWW));
      if (used_result_components) {
        // result = (src0.x == 0.0 && src1.x == 0.0) ? 0.0 : src0.x + 1.0
        DxbcOpAnd(DxbcDest::R(system_temp_result_, 0b0001),
                  DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX),
                  DxbcSrc::R(system_temp_result_, DxbcSrc::kZZZZ));
        // If the condition is true, 1 will be added to make it 0.
        DxbcOpMovC(DxbcDest::R(system_temp_result_, 0b0001),
                   DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX),
                   DxbcSrc::LF(-1.0f), operands[0].SelectFromSwizzled(0));
        DxbcOpAdd(DxbcDest::R(system_temp_result_, 0b0001),
                  DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX),
                  DxbcSrc::LF(1.0f));
      }
      break;
    case AluVectorOpcode::kSetpNePush:
      predicate_written = true;
      result_swizzle = DxbcSrc::kXXXX;
      // result.xy = src0.xw == 0.0 (x only if needed).
      DxbcOpEq(DxbcDest::R(system_temp_result_,
                           used_result_components ? 0b0011 : 0b0010),
               operands[0].SwizzleSwizzled(0b1100), DxbcSrc::LF(0.0f));
      // result.zw = src1.xw != 0.0 (z only if needed).
      DxbcOpNE(DxbcDest::R(system_temp_result_,
                           used_result_components ? 0b1100 : 0b1000),
               operands[1].SwizzleSwizzled(0b11000000), DxbcSrc::LF(0.0f));
      // p0 = src0.w == 0.0 && src1.w != 0.0
      DxbcOpAnd(DxbcDest::R(system_temp_ps_pc_p0_a0_, 0b0100),
                DxbcSrc::R(system_temp_result_, DxbcSrc::kYYYY),
                DxbcSrc::R(system_temp_result_, DxbcSrc::kWWWW));
      if (used_result_components) {
        // result = (src0.x == 0.0 && src1.x != 0.0) ? 0.0 : src0.x + 1.0
        DxbcOpAnd(DxbcDest::R(system_temp_result_, 0b0001),
                  DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX),
                  DxbcSrc::R(system_temp_result_, DxbcSrc::kZZZZ));
        // If the condition is true, 1 will be added to make it 0.
        DxbcOpMovC(DxbcDest::R(system_temp_result_, 0b0001),
                   DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX),
                   DxbcSrc::LF(-1.0f), operands[0].SelectFromSwizzled(0));
        DxbcOpAdd(DxbcDest::R(system_temp_result_, 0b0001),
                  DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX),
                  DxbcSrc::LF(1.0f));
      }
      break;
    case AluVectorOpcode::kSetpGtPush:
      predicate_written = true;
      result_swizzle = DxbcSrc::kXXXX;
      // result.xy = src0.xw == 0.0 (x only if needed).
      DxbcOpEq(DxbcDest::R(system_temp_result_,
                           used_result_components ? 0b0011 : 0b0010),
               operands[0].SwizzleSwizzled(0b1100), DxbcSrc::LF(0.0f));
      // result.zw = src1.xw > 0.0 (z only if needed).
      DxbcOpLT(DxbcDest::R(system_temp_result_,
                           used_result_components ? 0b1100 : 0b1000),
               DxbcSrc::LF(0.0f), operands[1].SwizzleSwizzled(0b11000000));
      // p0 = src0.w == 0.0 && src1.w > 0.0
      DxbcOpAnd(DxbcDest::R(system_temp_ps_pc_p0_a0_, 0b0100),
                DxbcSrc::R(system_temp_result_, DxbcSrc::kYYYY),
                DxbcSrc::R(system_temp_result_, DxbcSrc::kWWWW));
      if (used_result_components) {
        // result = (src0.x == 0.0 && src1.x > 0.0) ? 0.0 : src0.x + 1.0
        DxbcOpAnd(DxbcDest::R(system_temp_result_, 0b0001),
                  DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX),
                  DxbcSrc::R(system_temp_result_, DxbcSrc::kZZZZ));
        // If the condition is true, 1 will be added to make it 0.
        DxbcOpMovC(DxbcDest::R(system_temp_result_, 0b0001),
                   DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX),
                   DxbcSrc::LF(-1.0f), operands[0].SelectFromSwizzled(0));
        DxbcOpAdd(DxbcDest::R(system_temp_result_, 0b0001),
                  DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX),
                  DxbcSrc::LF(1.0f));
      }
      break;
    case AluVectorOpcode::kSetpGePush:
      predicate_written = true;
      result_swizzle = DxbcSrc::kXXXX;
      // result.xy = src0.xw == 0.0 (x only if needed).
      DxbcOpEq(DxbcDest::R(system_temp_result_,
                           used_result_components ? 0b0011 : 0b0010),
               operands[0].SwizzleSwizzled(0b1100), DxbcSrc::LF(0.0f));
      // result.zw = src1.xw >= 0.0 (z only if needed).
      DxbcOpGE(DxbcDest::R(system_temp_result_,
                           used_result_components ? 0b1100 : 0b1000),
               operands[1].SwizzleSwizzled(0b11000000), DxbcSrc::LF(0.0f));
      // p0 = src0.w == 0.0 && src1.w >= 0.0
      DxbcOpAnd(DxbcDest::R(system_temp_ps_pc_p0_a0_, 0b0100),
                DxbcSrc::R(system_temp_result_, DxbcSrc::kYYYY),
                DxbcSrc::R(system_temp_result_, DxbcSrc::kWWWW));
      if (used_result_components) {
        // result = (src0.x == 0.0 && src1.x >= 0.0) ? 0.0 : src0.x + 1.0
        DxbcOpAnd(DxbcDest::R(system_temp_result_, 0b0001),
                  DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX),
                  DxbcSrc::R(system_temp_result_, DxbcSrc::kZZZZ));
        // If the condition is true, 1 will be added to make it 0.
        DxbcOpMovC(DxbcDest::R(system_temp_result_, 0b0001),
                   DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX),
                   DxbcSrc::LF(-1.0f), operands[0].SelectFromSwizzled(0));
        DxbcOpAdd(DxbcDest::R(system_temp_result_, 0b0001),
                  DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX),
                  DxbcSrc::LF(1.0f));
      }
      break;

    case AluVectorOpcode::kKillEq:
      result_swizzle = DxbcSrc::kXXXX;
      DxbcOpEq(DxbcDest::R(system_temp_result_), operands[0], operands[1]);
      DxbcOpOr(DxbcDest::R(system_temp_result_, 0b0011),
               DxbcSrc::R(system_temp_result_, 0b0100),
               DxbcSrc::R(system_temp_result_, 0b1110));
      DxbcOpOr(DxbcDest::R(system_temp_result_, 0b0001),
               DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX),
               DxbcSrc::R(system_temp_result_, DxbcSrc::kYYYY));
      if (edram_rov_used_) {
        DxbcOpRetC(true, DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX));
      } else {
        DxbcOpDiscard(true, DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX));
      }
      if (used_result_components) {
        DxbcOpAnd(DxbcDest::R(system_temp_result_, 0b0001),
                  DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX),
                  DxbcSrc::LF(1.0f));
      }
      break;
    case AluVectorOpcode::kKillGt:
      result_swizzle = DxbcSrc::kXXXX;
      DxbcOpLT(DxbcDest::R(system_temp_result_), operands[1], operands[0]);
      DxbcOpOr(DxbcDest::R(system_temp_result_, 0b0011),
               DxbcSrc::R(system_temp_result_, 0b0100),
               DxbcSrc::R(system_temp_result_, 0b1110));
      DxbcOpOr(DxbcDest::R(system_temp_result_, 0b0001),
               DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX),
               DxbcSrc::R(system_temp_result_, DxbcSrc::kYYYY));
      if (edram_rov_used_) {
        DxbcOpRetC(true, DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX));
      } else {
        DxbcOpDiscard(true, DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX));
      }
      if (used_result_components) {
        DxbcOpAnd(DxbcDest::R(system_temp_result_, 0b0001),
                  DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX),
                  DxbcSrc::LF(1.0f));
      }
      break;
    case AluVectorOpcode::kKillGe:
      result_swizzle = DxbcSrc::kXXXX;
      DxbcOpGE(DxbcDest::R(system_temp_result_), operands[0], operands[1]);
      DxbcOpOr(DxbcDest::R(system_temp_result_, 0b0011),
               DxbcSrc::R(system_temp_result_, 0b0100),
               DxbcSrc::R(system_temp_result_, 0b1110));
      DxbcOpOr(DxbcDest::R(system_temp_result_, 0b0001),
               DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX),
               DxbcSrc::R(system_temp_result_, DxbcSrc::kYYYY));
      if (edram_rov_used_) {
        DxbcOpRetC(true, DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX));
      } else {
        DxbcOpDiscard(true, DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX));
      }
      if (used_result_components) {
        DxbcOpAnd(DxbcDest::R(system_temp_result_, 0b0001),
                  DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX),
                  DxbcSrc::LF(1.0f));
      }
      break;
    case AluVectorOpcode::kKillNe:
      result_swizzle = DxbcSrc::kXXXX;
      DxbcOpNE(DxbcDest::R(system_temp_result_), operands[0], operands[1]);
      DxbcOpOr(DxbcDest::R(system_temp_result_, 0b0011),
               DxbcSrc::R(system_temp_result_, 0b0100),
               DxbcSrc::R(system_temp_result_, 0b1110));
      DxbcOpOr(DxbcDest::R(system_temp_result_, 0b0001),
               DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX),
               DxbcSrc::R(system_temp_result_, DxbcSrc::kYYYY));
      if (edram_rov_used_) {
        DxbcOpRetC(true, DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX));
      } else {
        DxbcOpDiscard(true, DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX));
      }
      if (used_result_components) {
        DxbcOpAnd(DxbcDest::R(system_temp_result_, 0b0001),
                  DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX),
                  DxbcSrc::LF(1.0f));
      }
      break;

    case AluVectorOpcode::kDst:
      if (used_result_components & 0b0001) {
        DxbcOpMov(DxbcDest::R(system_temp_result_, 0b0001), DxbcSrc::LF(1.0f));
      }
      if (used_result_components & 0b0010) {
        // Shader Model 3: 0 or denormal * anything = 0.
        // FIXME(Triang3l): Signed zero needs research and handling.
        DxbcOpMul(DxbcDest::R(system_temp_result_, 0b0010),
                  operands[0].SelectFromSwizzled(1),
                  operands[1].SelectFromSwizzled(1));
        if (!(instr.vector_operands[0].GetAbsoluteIdenticalComponents(
                  instr.vector_operands[1]) &
              0b0010)) {
          DxbcOpMin(DxbcDest::R(system_temp_result_, 0b0100),
                    operands[0].SelectFromSwizzled(1).Abs(),
                    operands[1].SelectFromSwizzled(1).Abs());
          // min isn't required to flush denormals, eq is.
          DxbcOpEq(DxbcDest::R(system_temp_result_, 0b0100),
                   DxbcSrc::R(system_temp_result_, DxbcSrc::kZZZZ),
                   DxbcSrc::LF(0.0f));
          DxbcOpMovC(DxbcDest::R(system_temp_result_, 0b0010),
                     DxbcSrc::R(system_temp_result_, DxbcSrc::kZZZZ),
                     DxbcSrc::LF(0.0f),
                     DxbcSrc::R(system_temp_result_, DxbcSrc::kYYYY));
        }
      }
      if (used_result_components & 0b0100) {
        DxbcOpMov(DxbcDest::R(system_temp_result_, 0b0100),
                  operands[0].SelectFromSwizzled(2));
      }
      if (used_result_components & 0b1000) {
        DxbcOpMov(DxbcDest::R(system_temp_result_, 0b1000),
                  operands[1].SelectFromSwizzled(2));
      }
      break;

    case AluVectorOpcode::kMaxA:
      DxbcOpAdd(DxbcDest::R(system_temp_ps_pc_p0_a0_, 0b1000),
                operands[0].SelectFromSwizzled(3), DxbcSrc::LF(0.5f));
      DxbcOpRoundNI(DxbcDest::R(system_temp_ps_pc_p0_a0_, 0b1000),
                    DxbcSrc::R(system_temp_ps_pc_p0_a0_, DxbcSrc::kWWWW));
      DxbcOpMax(DxbcDest::R(system_temp_ps_pc_p0_a0_, 0b1000),
                DxbcSrc::R(system_temp_ps_pc_p0_a0_, DxbcSrc::kWWWW),
                DxbcSrc::LF(-256.0f));
      DxbcOpMin(DxbcDest::R(system_temp_ps_pc_p0_a0_, 0b1000),
                DxbcSrc::R(system_temp_ps_pc_p0_a0_, DxbcSrc::kWWWW),
                DxbcSrc::LF(255.0f));
      DxbcOpFToI(DxbcDest::R(system_temp_ps_pc_p0_a0_, 0b1000),
                 DxbcSrc::R(system_temp_ps_pc_p0_a0_, DxbcSrc::kWWWW));
      if (used_result_components) {
        uint32_t identical = instr.vector_operands[0].GetIdenticalComponents(
                                 instr.vector_operands[1]) &
                             used_result_components;
        uint32_t different = used_result_components & ~identical;
        if (different) {
          // Shader Model 3 NaN behavior (a op b ? a : b, not fmax/fmin).
          DxbcOpGE(DxbcDest::R(system_temp_result_, different), operands[0],
                   operands[1]);
          DxbcOpMovC(DxbcDest::R(system_temp_result_, different),
                     DxbcSrc::R(system_temp_result_), operands[0], operands[1]);
        }
        if (identical) {
          DxbcOpMov(DxbcDest::R(system_temp_result_, identical), operands[0]);
        }
      }
      break;

    default:
      assert_unhandled_case(instr.vector_opcode);
      EmitTranslationError("Unknown ALU vector operation");
      DxbcOpMov(DxbcDest::R(system_temp_result_), DxbcSrc::LF(0.0f));
  }

  PopSystemTemp(operand_temps);
}

bool DxbcShaderTranslator::ProcessScalarAluOperation(
    const ParsedAluInstruction& instr, bool& predicate_written) {
  predicate_written = false;

  if (instr.scalar_opcode == ucode::AluScalarOpcode::kRetainPrev &&
      !instr.scalar_result.GetUsedWriteMask()) {
    return false;
  }

  DxbcSourceOperand dxbc_operands[3];
  // Whether the operand is the same as any previous operand, and thus is loaded
  // only once.
  bool operands_duplicate[3] = {};
  uint32_t operand_lengths[3];
  for (uint32_t i = 0; i < uint32_t(instr.scalar_operand_count); ++i) {
    const InstructionOperand& operand = instr.scalar_operands[i];
    for (uint32_t j = 0; j < i; ++j) {
      if (operand.GetIdenticalComponents(instr.scalar_operands[j]) == 0b1111) {
        operands_duplicate[i] = true;
        dxbc_operands[i] = dxbc_operands[j];
        break;
      }
    }
    if (!operands_duplicate[i]) {
      LoadDxbcSourceOperand(operand, dxbc_operands[i]);
    }
    operand_lengths[i] = DxbcSourceOperandLength(dxbc_operands[i]);
  }

  // So the same code can be used for instructions with the same format.
  static const uint32_t kCoreOpcodes[] = {
      D3D10_SB_OPCODE_ADD,
      D3D10_SB_OPCODE_ADD,
      D3D10_SB_OPCODE_MUL,
      D3D10_SB_OPCODE_MUL,
      D3D10_SB_OPCODE_MUL,
      D3D10_SB_OPCODE_MAX,
      D3D10_SB_OPCODE_MIN,
      D3D10_SB_OPCODE_EQ,
      D3D10_SB_OPCODE_LT,
      D3D10_SB_OPCODE_GE,
      D3D10_SB_OPCODE_NE,
      D3D10_SB_OPCODE_FRC,
      D3D10_SB_OPCODE_ROUND_Z,
      D3D10_SB_OPCODE_ROUND_NI,
      D3D10_SB_OPCODE_EXP,
      D3D10_SB_OPCODE_LOG,
      D3D10_SB_OPCODE_LOG,
      D3D11_SB_OPCODE_RCP,
      D3D11_SB_OPCODE_RCP,
      D3D11_SB_OPCODE_RCP,
      D3D10_SB_OPCODE_RSQ,
      D3D10_SB_OPCODE_RSQ,
      D3D10_SB_OPCODE_RSQ,
      D3D10_SB_OPCODE_MAX,
      D3D10_SB_OPCODE_MAX,
      D3D10_SB_OPCODE_ADD,
      D3D10_SB_OPCODE_ADD,
      D3D10_SB_OPCODE_EQ,
      D3D10_SB_OPCODE_NE,
      D3D10_SB_OPCODE_LT,
      D3D10_SB_OPCODE_GE,
      0,
      0,
      0,
      0,
      D3D10_SB_OPCODE_EQ,
      D3D10_SB_OPCODE_LT,
      D3D10_SB_OPCODE_GE,
      D3D10_SB_OPCODE_NE,
      D3D10_SB_OPCODE_EQ,
      D3D10_SB_OPCODE_SQRT,
      0,
      D3D10_SB_OPCODE_MUL,
      D3D10_SB_OPCODE_MUL,
      D3D10_SB_OPCODE_ADD,
      D3D10_SB_OPCODE_ADD,
      D3D10_SB_OPCODE_ADD,
      D3D10_SB_OPCODE_ADD,
      D3D10_SB_OPCODE_SINCOS,
      D3D10_SB_OPCODE_SINCOS,
  };

  bool translated = true;
  switch (instr.scalar_opcode) {
    case AluScalarOpcode::kAdds:
    case AluScalarOpcode::kSubs: {
      bool subtract = instr.scalar_opcode == AluScalarOpcode::kSubs;
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(
              kCoreOpcodes[uint32_t(instr.scalar_opcode)]) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
              3 + operand_lengths[0] +
              DxbcSourceOperandLength(dxbc_operands[0], subtract)));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 1, subtract);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
    } break;

    case AluScalarOpcode::kAddsPrev:
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(
              kCoreOpcodes[uint32_t(instr.scalar_opcode)]) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      break;

    case AluScalarOpcode::kMuls: {
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                 3 + 2 * operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 1);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      if (instr.scalar_operands[0].components[0] !=
          instr.scalar_operands[0].components[1]) {
        // Reproduce Shader Model 3 multiplication behavior (0 * anything = 0).
        uint32_t is_subnormal_temp = PushSystemTemp();
        // Get the non-NaN multiplicand closer to zero to check if any of them
        // is zero.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MIN) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                3 +
                2 * DxbcSourceOperandLength(dxbc_operands[0], false, true)));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
        shader_code_.push_back(is_subnormal_temp);
        UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0, false, true);
        UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 1, false, true);
        ++stat_.instruction_count;
        ++stat_.float_instruction_count;
        // Check if any multiplicand is zero (min isn't required to flush
        // denormals in the result).
        shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_EQ) |
                               ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
        shader_code_.push_back(is_subnormal_temp);
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
        shader_code_.push_back(is_subnormal_temp);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(0);
        ++stat_.instruction_count;
        ++stat_.float_instruction_count;
        // Zero the result if any multiplicand is zero.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
        shader_code_.push_back(system_temp_ps_pc_p0_a0_);
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
        shader_code_.push_back(is_subnormal_temp);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(0);
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
        shader_code_.push_back(system_temp_ps_pc_p0_a0_);
        ++stat_.instruction_count;
        ++stat_.movc_instruction_count;
        // Release is_subnormal_temp.
        PopSystemTemp();
      }
    } break;

    case AluScalarOpcode::kMulsPrev: {
      // Reproduce Shader Model 3 multiplication behavior (0 * anything = 0).
      uint32_t is_subnormal_temp = PushSystemTemp();
      // Get the non-NaN multiplicand closer to zero to check if any of them is
      // zero.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MIN) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
              6 + DxbcSourceOperandLength(dxbc_operands[0], false, true)));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(is_subnormal_temp);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0, false, true);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1) |
          ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
      shader_code_.push_back(ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(
          D3D10_SB_OPERAND_MODIFIER_ABS));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Do the multiplication.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Check if any multiplicand is zero (min isn't required to flush
      // denormals in the result).
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_EQ) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(is_subnormal_temp);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(is_subnormal_temp);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Zero the result if any multiplicand is zero.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(is_subnormal_temp);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
      // Release is_subnormal_temp.
      PopSystemTemp();
    } break;

    case AluScalarOpcode::kMulsPrev2: {
      // Implemented like MUL_LIT in the R600 ISA documentation, where src0 is
      // src0.x, src1 is ps, and src2 is src0.y.
      // Check if -FLT_MAX needs to be written - if any of the following
      // checks pass.
      uint32_t minus_max_mask = PushSystemTemp();
      // ps == -FLT_MAX || ps == -Infinity (as ps <= -FLT_MAX)
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_GE) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(minus_max_mask);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0xFF7FFFFFu);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // isnan(ps)
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_NE) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0010, 1));
      shader_code_.push_back(minus_max_mask);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // src0.y <= 0.0
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_GE) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
      shader_code_.push_back(minus_max_mask);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 1);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // isnan(src0.y)
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_NE) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                 3 + 2 * operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
      shader_code_.push_back(minus_max_mask);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 1);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 1);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // minus_max_mask = any(minus_max_mask)
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
      shader_code_.push_back(minus_max_mask);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, kSwizzleXYZW, 1));
      shader_code_.push_back(minus_max_mask);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_TEMP, 0b01001110, 1));
      shader_code_.push_back(minus_max_mask);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_OR) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(minus_max_mask);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(minus_max_mask);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
      shader_code_.push_back(minus_max_mask);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;
      // Calculate the product for the regular path of the instruction.
      // ps = src0.x * ps
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Write -FLT_MAX if needed.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(minus_max_mask);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0xFF7FFFFFu);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
      // Release minus_max_mask.
      PopSystemTemp();
    } break;

    case AluScalarOpcode::kMaxs:
    case AluScalarOpcode::kMins: {
      // max is commonly used as mov.
      if (instr.scalar_operands[0].components[0] ==
          instr.scalar_operands[0].components[1]) {
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3 +
                                                         operand_lengths[0]));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
        shader_code_.push_back(system_temp_ps_pc_p0_a0_);
        UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
        ++stat_.instruction_count;
        ++stat_.mov_instruction_count;
      } else {
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(
                kCoreOpcodes[uint32_t(instr.scalar_opcode)]) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                3 + 2 * operand_lengths[0]));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
        shader_code_.push_back(system_temp_ps_pc_p0_a0_);
        UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
        UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 1);
        ++stat_.instruction_count;
        ++stat_.float_instruction_count;
      }
    } break;

    case AluScalarOpcode::kSeqs:
    case AluScalarOpcode::kSgts:
    case AluScalarOpcode::kSges:
    case AluScalarOpcode::kSnes:
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(
              kCoreOpcodes[uint32_t(instr.scalar_opcode)]) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      if (instr.scalar_opcode != AluScalarOpcode::kSgts) {
        // lt in DXBC, not gt.
        UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      }
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      if (instr.scalar_opcode == AluScalarOpcode::kSgts) {
        UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      }
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Convert 0xFFFFFFFF to 1.0f.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0x3F800000);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;
      break;

    case AluScalarOpcode::kFrcs:
    case AluScalarOpcode::kTruncs:
    case AluScalarOpcode::kFloors:
    case AluScalarOpcode::kExp:
    case AluScalarOpcode::kLog:
    case AluScalarOpcode::kRcp:
    case AluScalarOpcode::kRsq:
    case AluScalarOpcode::kSqrt:
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(
              kCoreOpcodes[uint32_t(instr.scalar_opcode)]) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      break;

    case AluScalarOpcode::kLogc:
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_LOG) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Clamp -Infinity to -FLT_MAX.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAX) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0xFF7FFFFFu);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      break;

    case AluScalarOpcode::kRcpc:
    case AluScalarOpcode::kRsqc:
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(
              kCoreOpcodes[uint32_t(instr.scalar_opcode)]) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Clamp -Infinity to -FLT_MAX.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAX) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0xFF7FFFFFu);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Clamp +Infinity to +FLT_MAX.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MIN) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0x7F7FFFFFu);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      break;

    case AluScalarOpcode::kRcpf:
    case AluScalarOpcode::kRsqf: {
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(
              kCoreOpcodes[uint32_t(instr.scalar_opcode)]) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Change Infinity to positive or negative zero (the sign of zero has
      // effect on some instructions, such as rcp itself).
      uint32_t isinf_and_sign = PushSystemTemp();
      // Separate the value into the magnitude and the sign bit.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0011, 1));
      shader_code_.push_back(isinf_and_sign);
      shader_code_.push_back(
          EncodeVectorReplicatedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_IMMEDIATE32, kSwizzleXYZW, 0));
      shader_code_.push_back(0x7FFFFFFFu);
      shader_code_.push_back(0x80000000u);
      shader_code_.push_back(0);
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;
      // Check if the magnitude is infinite.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_IEQ) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(isinf_and_sign);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(isinf_and_sign);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0x7F800000u);
      ++stat_.instruction_count;
      ++stat_.int_instruction_count;
      // Zero ps if the magnitude is infinite (the signed zero is already in Y
      // of isinf_and_sign).
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(isinf_and_sign);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1));
      shader_code_.push_back(isinf_and_sign);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
      // Release isinf_and_sign.
      PopSystemTemp();
    } break;

    case AluScalarOpcode::kMaxAs:
    case AluScalarOpcode::kMaxAsf:
      // The `a0 = int(clamp(round(src0.x), -256.0, 255.0))` part.
      //
      // See AluVectorOpcode::kMaxA handling for details regarding rounding and
      // clamping.
      //
      // a0 = round(src0.x) (towards the nearest integer via floor(src0.x + 0.5)
      // for maxas and towards -Infinity for maxasf).
      if (instr.scalar_opcode == AluScalarOpcode::kMaxAs) {
        // a0 = src0.x + 0.5
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5 +
                                                         operand_lengths[0]));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
        shader_code_.push_back(system_temp_ps_pc_p0_a0_);
        UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(0x3F000000u);
        ++stat_.instruction_count;
        ++stat_.float_instruction_count;
        // a0 = floor(src0.x + 0.5)
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ROUND_NI) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
        shader_code_.push_back(system_temp_ps_pc_p0_a0_);
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
        shader_code_.push_back(system_temp_ps_pc_p0_a0_);
        ++stat_.instruction_count;
        ++stat_.float_instruction_count;
      } else {
        // a0 = floor(src0.x)
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ROUND_NI) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3 +
                                                         operand_lengths[0]));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
        shader_code_.push_back(system_temp_ps_pc_p0_a0_);
        UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
        ++stat_.instruction_count;
        ++stat_.float_instruction_count;
      }
      // a0 = max(round(src0.x), -256.0)
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAX) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0xC3800000u);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // a0 = clamp(round(src0.x), -256.0, 255.0)
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MIN) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0x437F0000u);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // a0 = int(clamp(floor(src0.x + 0.5), -256.0, 255.0))
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_FTOI) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b1000, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 3, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      ++stat_.instruction_count;
      ++stat_.conversion_instruction_count;
      // The `ps = max(src0.x, src0.y)` part.
      if (instr.scalar_operands[0].components[0] ==
          instr.scalar_operands[0].components[1]) {
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3 +
                                                         operand_lengths[0]));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
        shader_code_.push_back(system_temp_ps_pc_p0_a0_);
        UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
        ++stat_.instruction_count;
        ++stat_.mov_instruction_count;
      } else {
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAX) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                3 + 2 * operand_lengths[0]));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
        shader_code_.push_back(system_temp_ps_pc_p0_a0_);
        UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
        UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 1);
        ++stat_.instruction_count;
        ++stat_.float_instruction_count;
      }
      break;

    case AluScalarOpcode::kSubsPrev:
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(
              kCoreOpcodes[uint32_t(instr.scalar_opcode)]) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(6 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1) |
          ENCODE_D3D10_SB_OPERAND_EXTENDED(1));
      shader_code_.push_back(ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(
          D3D10_SB_OPERAND_MODIFIER_NEG));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      break;

    case AluScalarOpcode::kSetpEq:
    case AluScalarOpcode::kSetpNe:
    case AluScalarOpcode::kSetpGt:
    case AluScalarOpcode::kSetpGe:
      predicate_written = true;
      // Set p0 to whether the comparison with zero passes.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(
              kCoreOpcodes[uint32_t(instr.scalar_opcode)]) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      if (instr.scalar_opcode != AluScalarOpcode::kSetpGt) {
        // lt in DXBC, not gt.
        UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      }
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      if (instr.scalar_opcode == AluScalarOpcode::kSetpGt) {
        UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      }
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Set ps to 0.0 if the comparison passes or to 1.0 if it fails.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0x3F800000);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
      break;

    case AluScalarOpcode::kSetpInv:
      predicate_written = true;
      // Compare src0 to 0.0 (taking denormals into account, for instance) to
      // know what to set ps to in case src0 is not 1.0.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_EQ) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Assuming src0 is not 1.0 (this case will be handled later), set ps to
      // src0, except when it's zero - in this case, set ps to 1.0.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0x3F800000);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
      // Set p0 to whether src0 is 1.0.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_EQ) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0x3F800000);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // If src0 is 1.0, set ps to zero.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 2, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      ++stat_.instruction_count;
      ++stat_.movc_instruction_count;
      break;

    case AluScalarOpcode::kSetpPop:
      predicate_written = true;
      // ps = src0 - 1.0
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0xBF800000u);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Set p0 to whether (src0 - 1.0) is 0.0 or smaller.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_GE) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // If (src0 - 1.0) is 0.0 or smaller, set ps to 0.0 (already has
      // (src0 - 1.0), so clamping to zero is enough).
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAX) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      break;

    case AluScalarOpcode::kSetpClr:
      predicate_written = true;
      // ps = FLT_MAX
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0x7F7FFFFF);
      ++stat_.instruction_count;
      ++stat_.mov_instruction_count;
      // p0 = false
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.mov_instruction_count;
      break;

    case AluScalarOpcode::kSetpRstr:
      predicate_written = true;
      // Copy src0 to ps.
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      ++stat_.instruction_count;
      ++stat_.mov_instruction_count;
      // Set p0 to whether src0 is zero.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_EQ) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0100, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      break;

    case AluScalarOpcode::kKillsEq:
    case AluScalarOpcode::kKillsGt:
    case AluScalarOpcode::kKillsGe:
    case AluScalarOpcode::kKillsNe:
    case AluScalarOpcode::kKillsOne:
      // ps = src0.x op 0.0 (or src0.x == 1.0 for kills_one)
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(
              kCoreOpcodes[uint32_t(instr.scalar_opcode)]) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5 + operand_lengths[0]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      if (instr.scalar_opcode != AluScalarOpcode::kKillsGt) {
        // lt in DXBC, not gt.
        UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      }
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(
          instr.scalar_opcode == AluScalarOpcode::kKillsOne ? 0x3F800000 : 0);
      if (instr.scalar_opcode == AluScalarOpcode::kKillsGt) {
        UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      }
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      // Convert 0xFFFFFFFF to 1.0f.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_AND) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      shader_code_.push_back(
          EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
      shader_code_.push_back(0x3F800000);
      ++stat_.instruction_count;
      ++stat_.uint_instruction_count;
      // Discard.
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(
                                 edram_rov_used_ ? D3D10_SB_OPCODE_RETC
                                                 : D3D10_SB_OPCODE_DISCARD) |
                             ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(
                                 D3D10_SB_INSTRUCTION_TEST_NONZERO) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
      shader_code_.push_back(
          EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      ++stat_.instruction_count;
      if (edram_rov_used_) {
        ++stat_.dynamic_flow_control_count;
      }
      break;

    case AluScalarOpcode::kMulsc0:
    case AluScalarOpcode::kMulsc1: {
      shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) |
                             ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                                 3 + operand_lengths[0] + operand_lengths[1]));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      UseDxbcSourceOperand(dxbc_operands[1], kSwizzleXYZW, 0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
      if (!(instr.scalar_operands[0].GetAbsoluteIdenticalComponents(
                instr.scalar_operands[1]) &
            0b0001)) {
        // Reproduce Shader Model 3 multiplication behavior (0 * anything = 0).
        uint32_t is_subnormal_temp = PushSystemTemp();
        // Get the non-NaN multiplicand closer to zero to check if any of them
        // is zero.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MIN) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
                3 + DxbcSourceOperandLength(dxbc_operands[0], false, true) +
                DxbcSourceOperandLength(dxbc_operands[1], false, true)));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
        shader_code_.push_back(is_subnormal_temp);
        UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0, false, true);
        UseDxbcSourceOperand(dxbc_operands[1], kSwizzleXYZW, 0, false, true);
        ++stat_.instruction_count;
        ++stat_.float_instruction_count;
        // Check if any multiplicand is zero (min isn't required to flush
        // denormals in the result).
        shader_code_.push_back(ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_EQ) |
                               ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
        shader_code_.push_back(is_subnormal_temp);
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
        shader_code_.push_back(is_subnormal_temp);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(0);
        ++stat_.instruction_count;
        ++stat_.float_instruction_count;
        // Zero the result if any multiplicand is zero.
        shader_code_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOVC) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(9));
        shader_code_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
        shader_code_.push_back(system_temp_ps_pc_p0_a0_);
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
        shader_code_.push_back(is_subnormal_temp);
        shader_code_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_IMMEDIATE32, 0));
        shader_code_.push_back(0);
        shader_code_.push_back(
            EncodeVectorSelectOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1));
        shader_code_.push_back(system_temp_ps_pc_p0_a0_);
        ++stat_.instruction_count;
        ++stat_.movc_instruction_count;
        // Release is_subnormal_temp.
        PopSystemTemp();
      }
    } break;

    case AluScalarOpcode::kAddsc0:
    case AluScalarOpcode::kAddsc1:
    case AluScalarOpcode::kSubsc0:
    case AluScalarOpcode::kSubsc1: {
      bool subtract = instr.scalar_opcode == AluScalarOpcode::kSubsc0 ||
                      instr.scalar_opcode == AluScalarOpcode::kSubsc1;
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(
              kCoreOpcodes[uint32_t(instr.scalar_opcode)]) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(
              3 + operand_lengths[0] +
              DxbcSourceOperandLength(dxbc_operands[1], subtract)));
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      UseDxbcSourceOperand(dxbc_operands[1], kSwizzleXYZW, 0, subtract);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
    } break;

    case AluScalarOpcode::kSin:
    case AluScalarOpcode::kCos: {
      shader_code_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_SINCOS) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4 + operand_lengths[0]));
      // sincos ps, null, src0.x for sin
      // sincos null, ps, src0.x for cos
      if (instr.scalar_opcode != AluScalarOpcode::kSin) {
        shader_code_.push_back(
            EncodeZeroComponentOperand(D3D10_SB_OPERAND_TYPE_NULL, 0));
      }
      shader_code_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_TEMP, 0b0001, 1));
      shader_code_.push_back(system_temp_ps_pc_p0_a0_);
      if (instr.scalar_opcode != AluScalarOpcode::kCos) {
        shader_code_.push_back(
            EncodeZeroComponentOperand(D3D10_SB_OPERAND_TYPE_NULL, 0));
      }
      UseDxbcSourceOperand(dxbc_operands[0], kSwizzleXYZW, 0);
      ++stat_.instruction_count;
      ++stat_.float_instruction_count;
    } break;

    case AluScalarOpcode::kRetainPrev:
      // No changes, but translated successfully (just write the old ps).
      break;

    default:
      assert_unhandled_case(instr.scalar_opcode);
      translated = false;
      break;
  }

  for (uint32_t i = 0; i < uint32_t(instr.scalar_operand_count); ++i) {
    UnloadDxbcSourceOperand(dxbc_operands[instr.scalar_operand_count - 1 - i]);
  }

  return translated;
}

void DxbcShaderTranslator::ProcessAluInstruction(
    const ParsedAluInstruction& instr) {
  if (instr.IsNop()) {
    return;
  }

  if (emit_source_map_) {
    instruction_disassembly_buffer_.Reset();
    instr.Disassemble(&instruction_disassembly_buffer_);
  }
  UpdateInstructionPredicationAndEmitDisassembly(instr.is_predicated,
                                                 instr.predicate_condition);

  // Whether the instruction has changed the predicate and it needs to be
  // checked again later.
  bool predicate_written_vector = false;
  // Whether the result is only in X and all components should be remapped to X
  // while storing.
  uint32_t vector_result_swizzle = DxbcSrc::kXYZW;
  ProcessVectorAluOperation(instr, vector_result_swizzle,
                            predicate_written_vector);
  bool predicate_written_scalar = false;
  bool store_scalar =
      ProcessScalarAluOperation(instr, predicate_written_scalar);

  StoreResult(instr.vector_and_constant_result,
              DxbcSrc::R(system_temp_result_, vector_result_swizzle),
              instr.GetMemExportStreamConstant() != UINT32_MAX);
  if (store_scalar) {
    StoreResult(instr.scalar_result,
                DxbcSrc::R(system_temp_ps_pc_p0_a0_, DxbcSrc::kXXXX));
  }

  if (predicate_written_vector || predicate_written_scalar) {
    cf_exec_predicate_written_ = true;
    CloseInstructionPredication();
  }
}

}  // namespace gpu
}  // namespace xe
