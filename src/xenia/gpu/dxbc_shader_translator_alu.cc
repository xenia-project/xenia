/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/dxbc_shader_translator.h"

#include <cfloat>
#include <cmath>

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
  // .zzxy - don't need duplicated Z.
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
      // Not using DXBC mad to prevent fused multiply-add (mul followed by add
      // may be optimized into non-fused mad by the driver in the identical
      // operands case also).
      DxbcOpMul(per_component_dest, operands[0], operands[1]);
      uint32_t multiplicands_different =
          used_result_components &
          ~instr.vector_operands[0].GetIdenticalComponents(
              instr.vector_operands[1]);
      if (multiplicands_different) {
        // Shader Model 3: +-0 or denormal * anything = +0.
        uint32_t is_zero_temp = PushSystemTemp();
        DxbcOpMin(DxbcDest::R(is_zero_temp, multiplicands_different),
                  operands[0].Abs(), operands[1].Abs());
        // min isn't required to flush denormals, eq is.
        DxbcOpEq(DxbcDest::R(is_zero_temp, multiplicands_different),
                 DxbcSrc::R(is_zero_temp), DxbcSrc::LF(0.0f));
        // Not replacing true `0 + term` with movc of the term because +0 + -0
        // should result in +0, not -0.
        DxbcOpMovC(DxbcDest::R(system_temp_result_, multiplicands_different),
                   DxbcSrc::R(is_zero_temp), DxbcSrc::LF(0.0f),
                   DxbcSrc::R(system_temp_result_));
        // Release is_zero_temp.
        PopSystemTemp();
      }
      if (instr.vector_opcode == AluVectorOpcode::kMad) {
        DxbcOpAdd(per_component_dest, DxbcSrc::R(system_temp_result_),
                  operands[2]);
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
      uint32_t different = uint32_t((1 << component_count) - 1) &
                           ~instr.vector_operands[0].GetIdenticalComponents(
                               instr.vector_operands[1]);
      for (uint32_t i = 0; i < component_count; ++i) {
        DxbcOpMul(DxbcDest::R(system_temp_result_, i ? 0b0010 : 0b0001),
                  operands[0].SelectFromSwizzled(i),
                  operands[1].SelectFromSwizzled(i));
        if ((different & (1 << i)) != 0) {
          // Shader Model 3: +-0 or denormal * anything = +0 (also not replacing
          // true `0 + term` with movc of the term because +0 + -0 should result
          // in +0, not -0).
          DxbcOpMin(DxbcDest::R(system_temp_result_, 0b0100),
                    operands[0].SelectFromSwizzled(i).Abs(),
                    operands[1].SelectFromSwizzled(i).Abs());
          DxbcOpEq(DxbcDest::R(system_temp_result_, 0b0100),
                   DxbcSrc::R(system_temp_result_, DxbcSrc::kZZZZ),
                   DxbcSrc::LF(0.0f));
          DxbcOpMovC(DxbcDest::R(system_temp_result_, i ? 0b0010 : 0b0001),
                     DxbcSrc::R(system_temp_result_, DxbcSrc::kZZZZ),
                     DxbcSrc::LF(0.0f),
                     DxbcSrc::R(system_temp_result_,
                                i ? DxbcSrc::kYYYY : DxbcSrc::kXXXX));
        }
        if (i) {
          // Not using DXBC dp# to avoid fused multiply-add, PC GPUs are scalar
          // as of 2020 anyway, and not using mad for the same reason (mul
          // followed by add may be optimized into non-fused mad by the driver
          // in the identical operands case also).
          DxbcOpAdd(DxbcDest::R(system_temp_result_, 0b0001),
                    DxbcSrc::R(system_temp_result_, DxbcSrc::kXXXX),
                    DxbcSrc::R(system_temp_result_, DxbcSrc::kYYYY));
        }
      }
      if (component_count == 2) {
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
        DxbcOpMul(DxbcDest::R(system_temp_result_, 0b0010),
                  operands[0].SelectFromSwizzled(1),
                  operands[1].SelectFromSwizzled(1));
        if (!(instr.vector_operands[0].GetIdenticalComponents(
                  instr.vector_operands[1]) &
              0b0010)) {
          // Shader Model 3: +-0 or denormal * anything = +0.
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
          // Shader Model 3 NaN behavior (a >= b ? a : b, not fmax).
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

void DxbcShaderTranslator::ProcessScalarAluOperation(
    const ParsedAluInstruction& instr, bool& predicate_written) {
  predicate_written = false;

  if (instr.scalar_opcode == ucode::AluScalarOpcode::kRetainPrev) {
    return;
  }

  // Load operands.
  DxbcSrc operands_loaded[2]{DxbcSrc::LF(0.0f), DxbcSrc::LF(0.0f)};
  uint32_t operand_temps = 0;
  for (uint32_t i = 0; i < instr.scalar_operand_count; ++i) {
    bool operand_temp_pushed = false;
    operands_loaded[i] =
        LoadOperand(instr.scalar_operands[i],
                    (1 << instr.scalar_operands[i].component_count) - 1,
                    operand_temp_pushed);
    operand_temps += uint32_t(operand_temp_pushed);
  }
  // Don't return without PopSystemTemp(operand_temps) from now on!
  DxbcSrc operand_0_a(operands_loaded[0].SelectFromSwizzled(0));
  DxbcSrc operand_0_b(operands_loaded[0].SelectFromSwizzled(1));
  DxbcSrc operand_1(operands_loaded[1].SelectFromSwizzled(0));

  DxbcDest ps_dest(DxbcDest::R(system_temp_ps_pc_p0_a0_, 0b0001));
  DxbcSrc ps_src(DxbcSrc::R(system_temp_ps_pc_p0_a0_, DxbcSrc::kXXXX));
  switch (instr.scalar_opcode) {
    case AluScalarOpcode::kAdds:
      DxbcOpAdd(ps_dest, operand_0_a, operand_0_b);
      break;
    case AluScalarOpcode::kAddsPrev:
      DxbcOpAdd(ps_dest, operand_0_a, ps_src);
      break;
    case AluScalarOpcode::kMuls:
      DxbcOpMul(ps_dest, operand_0_a, operand_0_b);
      if (instr.scalar_operands[0].components[0] !=
          instr.scalar_operands[0].components[1]) {
        // Shader Model 3: +-0 or denormal * anything = +0.
        uint32_t is_zero_temp = PushSystemTemp();
        DxbcOpMin(DxbcDest::R(is_zero_temp, 0b0001), operand_0_a.Abs(),
                  operand_0_b.Abs());
        // min isn't required to flush denormals, eq is.
        DxbcOpEq(DxbcDest::R(is_zero_temp, 0b0001),
                 DxbcSrc::R(is_zero_temp, DxbcSrc::kXXXX), DxbcSrc::LF(0.0f));
        DxbcOpMovC(ps_dest, DxbcSrc::R(is_zero_temp, DxbcSrc::kXXXX),
                   DxbcSrc::LF(0.0f), ps_src);
        // Release is_zero_temp.
        PopSystemTemp();
      }
      break;
    case AluScalarOpcode::kMulsPrev:
    case AluScalarOpcode::kMulsPrev2: {
      uint32_t test_temp = PushSystemTemp();
      if (instr.scalar_opcode == AluScalarOpcode::kMulsPrev2) {
        // Check if need to select the src0.a * ps case.
        // ps != -FLT_MAX.
        DxbcOpNE(DxbcDest::R(test_temp, 0b0001), ps_src, DxbcSrc::LF(-FLT_MAX));
        // isfinite(ps), or |ps| <= FLT_MAX, or -|ps| >= -FLT_MAX, since
        // -FLT_MAX is already loaded to an SGPR, this is also false if it's
        // NaN.
        DxbcOpGE(DxbcDest::R(test_temp, 0b0010), -ps_src.Abs(),
                 DxbcSrc::LF(-FLT_MAX));
        DxbcOpAnd(DxbcDest::R(test_temp, 0b0001),
                  DxbcSrc::R(test_temp, DxbcSrc::kXXXX),
                  DxbcSrc::R(test_temp, DxbcSrc::kYYYY));
        // isfinite(src0.b).
        DxbcOpGE(DxbcDest::R(test_temp, 0b0010), -operand_0_b.Abs(),
                 DxbcSrc::LF(-FLT_MAX));
        DxbcOpAnd(DxbcDest::R(test_temp, 0b0001),
                  DxbcSrc::R(test_temp, DxbcSrc::kXXXX),
                  DxbcSrc::R(test_temp, DxbcSrc::kYYYY));
        // src0.b > 0 (need !(src0.b <= 0), but src0.b has already been checked
        // for NaN).
        DxbcOpLT(DxbcDest::R(test_temp, 0b0010), DxbcSrc::LF(0.0f),
                 operand_0_b);
        DxbcOpAnd(DxbcDest::R(test_temp, 0b0001),
                  DxbcSrc::R(test_temp, DxbcSrc::kXXXX),
                  DxbcSrc::R(test_temp, DxbcSrc::kYYYY));
        DxbcOpIf(true, DxbcSrc::R(test_temp, DxbcSrc::kXXXX));
      }
      // Shader Model 3: +-0 or denormal * anything = +0.
      DxbcOpMin(DxbcDest::R(test_temp, 0b0001), operand_0_a.Abs(),
                ps_src.Abs());
      // min isn't required to flush denormals, eq is.
      DxbcOpEq(DxbcDest::R(test_temp, 0b0001),
               DxbcSrc::R(test_temp, DxbcSrc::kXXXX), DxbcSrc::LF(0.0f));
      DxbcOpMul(ps_dest, operand_0_a, ps_src);
      DxbcOpMovC(ps_dest, DxbcSrc::R(test_temp, DxbcSrc::kXXXX),
                 DxbcSrc::LF(0.0f), ps_src);
      if (instr.scalar_opcode == AluScalarOpcode::kMulsPrev2) {
        DxbcOpElse();
        DxbcOpMov(ps_dest, DxbcSrc::LF(-FLT_MAX));
        DxbcOpEndIf();
      }
      // Release test_temp.
      PopSystemTemp();
    } break;

    case AluScalarOpcode::kMaxs:
    case AluScalarOpcode::kMins:
      // max is commonly used as mov.
      if (instr.scalar_operands[0].components[0] ==
          instr.scalar_operands[0].components[1]) {
        DxbcOpMov(ps_dest, operand_0_a);
      } else {
        // Shader Model 3 NaN behavior (a op b ? a : b, not fmax/fmin).
        if (instr.scalar_opcode == AluScalarOpcode::kMins) {
          DxbcOpLT(ps_dest, operand_0_a, operand_0_b);
        } else {
          DxbcOpGE(ps_dest, operand_0_a, operand_0_b);
        }
        DxbcOpMovC(ps_dest, ps_src, operand_0_a, operand_0_b);
      }
      break;

    case AluScalarOpcode::kSeqs:
      DxbcOpEq(ps_dest, operand_0_a, DxbcSrc::LF(0.0f));
      DxbcOpAnd(ps_dest, ps_src, DxbcSrc::LF(1.0f));
      break;
    case AluScalarOpcode::kSgts:
      DxbcOpLT(ps_dest, DxbcSrc::LF(0.0f), operand_0_a);
      DxbcOpAnd(ps_dest, ps_src, DxbcSrc::LF(1.0f));
      break;
    case AluScalarOpcode::kSges:
      DxbcOpGE(ps_dest, operand_0_a, DxbcSrc::LF(0.0f));
      DxbcOpAnd(ps_dest, ps_src, DxbcSrc::LF(1.0f));
      break;
    case AluScalarOpcode::kSnes:
      DxbcOpNE(ps_dest, operand_0_a, DxbcSrc::LF(0.0f));
      DxbcOpAnd(ps_dest, ps_src, DxbcSrc::LF(1.0f));
      break;

    case AluScalarOpcode::kFrcs:
      DxbcOpFrc(ps_dest, operand_0_a);
      break;
    case AluScalarOpcode::kTruncs:
      DxbcOpRoundZ(ps_dest, operand_0_a);
      break;
    case AluScalarOpcode::kFloors:
      DxbcOpRoundNI(ps_dest, operand_0_a);
      break;

    case AluScalarOpcode::kExp:
      DxbcOpExp(ps_dest, operand_0_a);
      break;
    case AluScalarOpcode::kLogc: {
      DxbcOpLog(ps_dest, operand_0_a);
      uint32_t is_neg_infinity_temp = PushSystemTemp();
      DxbcOpEq(DxbcDest::R(is_neg_infinity_temp, 0b0001), ps_src,
               DxbcSrc::LF(-INFINITY));
      DxbcOpMovC(ps_dest, DxbcSrc::R(is_neg_infinity_temp, DxbcSrc::kXXXX),
                 DxbcSrc::LF(-FLT_MAX), ps_src);
      // Release is_neg_infinity_temp.
      PopSystemTemp();
    } break;
    case AluScalarOpcode::kLog:
      DxbcOpLog(ps_dest, operand_0_a);
      break;
    case AluScalarOpcode::kRcpc:
    case AluScalarOpcode::kRsqc: {
      if (instr.scalar_opcode == AluScalarOpcode::kRsqc) {
        DxbcOpRSq(ps_dest, operand_0_a);
      } else {
        DxbcOpRcp(ps_dest, operand_0_a);
      }
      uint32_t is_infinity_temp = PushSystemTemp();
      DxbcOpEq(DxbcDest::R(is_infinity_temp, 0b0001), ps_src.Abs(),
               DxbcSrc::LF(INFINITY));
      // If +-Infinity (0x7F800000 or 0xFF800000), add -1 (0xFFFFFFFF) to turn
      // into +-FLT_MAX (0x7F7FFFFF or 0xFF7FFFFF).
      DxbcOpIAdd(ps_dest, ps_src, DxbcSrc::R(is_infinity_temp, DxbcSrc::kXXXX));
      // Release is_infinity_temp.
      PopSystemTemp();
    } break;
    case AluScalarOpcode::kRcpf:
    case AluScalarOpcode::kRsqf: {
      if (instr.scalar_opcode == AluScalarOpcode::kRsqf) {
        DxbcOpRSq(ps_dest, operand_0_a);
      } else {
        DxbcOpRcp(ps_dest, operand_0_a);
      }
      uint32_t is_not_infinity_temp = PushSystemTemp();
      DxbcOpNE(DxbcDest::R(is_not_infinity_temp, 0b0001), ps_src.Abs(),
               DxbcSrc::LF(INFINITY));
      // Keep the sign bit if infinity.
      DxbcOpOr(DxbcDest::R(is_not_infinity_temp, 0b0001),
               DxbcSrc::R(is_not_infinity_temp, DxbcSrc::kXXXX),
               DxbcSrc::LU(uint32_t(1) << 31));
      DxbcOpAnd(ps_dest, ps_src,
                DxbcSrc::R(is_not_infinity_temp, DxbcSrc::kXXXX));
      // Release is_not_infinity_temp.
      PopSystemTemp();
    } break;
    case AluScalarOpcode::kRcp:
      DxbcOpRcp(ps_dest, operand_0_a);
      break;
    case AluScalarOpcode::kRsq:
      DxbcOpRSq(ps_dest, operand_0_a);
      break;

    case AluScalarOpcode::kMaxAs:
    case AluScalarOpcode::kMaxAsf:
      if (instr.scalar_opcode == AluScalarOpcode::kMaxAsf) {
        DxbcOpRoundNI(DxbcDest::R(system_temp_ps_pc_p0_a0_, 0b1000),
                      operand_0_a);
      } else {
        DxbcOpAdd(DxbcDest::R(system_temp_ps_pc_p0_a0_, 0b1000), operand_0_a,
                  DxbcSrc::LF(0.5f));
        DxbcOpRoundNI(DxbcDest::R(system_temp_ps_pc_p0_a0_, 0b1000),
                      DxbcSrc::R(system_temp_ps_pc_p0_a0_, DxbcSrc::kWWWW));
      }
      DxbcOpMax(DxbcDest::R(system_temp_ps_pc_p0_a0_, 0b1000),
                DxbcSrc::R(system_temp_ps_pc_p0_a0_, DxbcSrc::kWWWW),
                DxbcSrc::LF(-256.0f));
      DxbcOpMin(DxbcDest::R(system_temp_ps_pc_p0_a0_, 0b1000),
                DxbcSrc::R(system_temp_ps_pc_p0_a0_, DxbcSrc::kWWWW),
                DxbcSrc::LF(255.0f));
      DxbcOpFToI(DxbcDest::R(system_temp_ps_pc_p0_a0_, 0b1000),
                 DxbcSrc::R(system_temp_ps_pc_p0_a0_, DxbcSrc::kWWWW));
      if (instr.scalar_operands[0].components[0] ==
          instr.scalar_operands[0].components[1]) {
        DxbcOpMov(ps_dest, operand_0_a);
      } else {
        // Shader Model 3 NaN behavior (a >= b ? a : b, not fmax).
        DxbcOpGE(ps_dest, operand_0_a, operand_0_b);
        DxbcOpMovC(ps_dest, ps_src, operand_0_a, operand_0_b);
      }
      break;

    case AluScalarOpcode::kSubs:
      DxbcOpAdd(ps_dest, operand_0_a, -operand_0_b);
      break;
    case AluScalarOpcode::kSubsPrev:
      DxbcOpAdd(ps_dest, operand_0_a, -ps_src);
      break;

    case AluScalarOpcode::kSetpEq:
      predicate_written = true;
      DxbcOpEq(DxbcDest::R(system_temp_ps_pc_p0_a0_, 0b0100), operand_0_a,
               DxbcSrc::LF(0.0f));
      DxbcOpMovC(ps_dest, DxbcSrc::R(system_temp_ps_pc_p0_a0_, DxbcSrc::kZZZZ),
                 DxbcSrc::LF(0.0f), DxbcSrc::LF(1.0f));
      break;
    case AluScalarOpcode::kSetpNe:
      predicate_written = true;
      DxbcOpNE(DxbcDest::R(system_temp_ps_pc_p0_a0_, 0b0100), operand_0_a,
               DxbcSrc::LF(0.0f));
      DxbcOpMovC(ps_dest, DxbcSrc::R(system_temp_ps_pc_p0_a0_, DxbcSrc::kZZZZ),
                 DxbcSrc::LF(0.0f), DxbcSrc::LF(1.0f));
      break;
    case AluScalarOpcode::kSetpGt:
      predicate_written = true;
      DxbcOpLT(DxbcDest::R(system_temp_ps_pc_p0_a0_, 0b0100), DxbcSrc::LF(0.0f),
               operand_0_a);
      DxbcOpMovC(ps_dest, DxbcSrc::R(system_temp_ps_pc_p0_a0_, DxbcSrc::kZZZZ),
                 DxbcSrc::LF(0.0f), DxbcSrc::LF(1.0f));
      break;
    case AluScalarOpcode::kSetpGe:
      predicate_written = true;
      DxbcOpGE(DxbcDest::R(system_temp_ps_pc_p0_a0_, 0b0100), operand_0_a,
               DxbcSrc::LF(0.0f));
      DxbcOpMovC(ps_dest, DxbcSrc::R(system_temp_ps_pc_p0_a0_, DxbcSrc::kZZZZ),
                 DxbcSrc::LF(0.0f), DxbcSrc::LF(1.0f));
      break;
    case AluScalarOpcode::kSetpInv:
      predicate_written = true;
      // Calculate ps as if src0.a != 1.0 (the false predicate value case).
      DxbcOpEq(ps_dest, operand_0_a, DxbcSrc::LF(0.0f));
      DxbcOpMovC(ps_dest, ps_src, DxbcSrc::LF(1.0f), operand_0_a);
      // Set the predicate to src0.a == 1.0, and, if it's true, zero ps.
      DxbcOpEq(DxbcDest::R(system_temp_ps_pc_p0_a0_, 0b0100), operand_0_a,
               DxbcSrc::LF(1.0f));
      DxbcOpMovC(ps_dest, DxbcSrc::R(system_temp_ps_pc_p0_a0_, DxbcSrc::kZZZZ),
                 DxbcSrc::LF(0.0f), ps_src);
      break;
    case AluScalarOpcode::kSetpPop:
      predicate_written = true;
      DxbcOpAdd(ps_dest, operand_0_a, DxbcSrc::LF(-1.0f));
      DxbcOpGE(DxbcDest::R(system_temp_ps_pc_p0_a0_, 0b0100), DxbcSrc::LF(0.0f),
               ps_src);
      DxbcOpMovC(ps_dest, DxbcSrc::R(system_temp_ps_pc_p0_a0_, DxbcSrc::kZZZZ),
                 DxbcSrc::LF(0.0f), ps_src);
      break;
    case AluScalarOpcode::kSetpClr:
      predicate_written = true;
      DxbcOpMov(ps_dest, DxbcSrc::LF(FLT_MAX));
      DxbcOpMov(DxbcDest::R(system_temp_ps_pc_p0_a0_, 0b0100), DxbcSrc::LU(0));
      break;
    case AluScalarOpcode::kSetpRstr:
      predicate_written = true;
      DxbcOpEq(DxbcDest::R(system_temp_ps_pc_p0_a0_, 0b0100), operand_0_a,
               DxbcSrc::LF(0.0f));
      // Just copying src0.a to ps (since it's set to 0 if it's 0) could work,
      // but flush denormals and zero sign just for safety.
      DxbcOpMovC(ps_dest, DxbcSrc::R(system_temp_ps_pc_p0_a0_, DxbcSrc::kZZZZ),
                 DxbcSrc::LF(0.0f), operand_0_a);
      break;

    case AluScalarOpcode::kKillsEq:
      DxbcOpEq(ps_dest, operand_0_a, DxbcSrc::LF(0.0f));
      if (edram_rov_used_) {
        DxbcOpRetC(true, ps_src);
      } else {
        DxbcOpDiscard(true, ps_src);
      }
      DxbcOpAnd(ps_dest, ps_src, DxbcSrc::LF(1.0f));
      break;
    case AluScalarOpcode::kKillsGt:
      DxbcOpLT(ps_dest, DxbcSrc::LF(0.0f), operand_0_a);
      if (edram_rov_used_) {
        DxbcOpRetC(true, ps_src);
      } else {
        DxbcOpDiscard(true, ps_src);
      }
      DxbcOpAnd(ps_dest, ps_src, DxbcSrc::LF(1.0f));
      break;
    case AluScalarOpcode::kKillsGe:
      DxbcOpGE(ps_dest, operand_0_a, DxbcSrc::LF(0.0f));
      if (edram_rov_used_) {
        DxbcOpRetC(true, ps_src);
      } else {
        DxbcOpDiscard(true, ps_src);
      }
      DxbcOpAnd(ps_dest, ps_src, DxbcSrc::LF(1.0f));
      break;
    case AluScalarOpcode::kKillsNe:
      DxbcOpNE(ps_dest, operand_0_a, DxbcSrc::LF(0.0f));
      if (edram_rov_used_) {
        DxbcOpRetC(true, ps_src);
      } else {
        DxbcOpDiscard(true, ps_src);
      }
      DxbcOpAnd(ps_dest, ps_src, DxbcSrc::LF(1.0f));
      break;
    case AluScalarOpcode::kKillsOne:
      DxbcOpEq(ps_dest, operand_0_a, DxbcSrc::LF(1.0f));
      if (edram_rov_used_) {
        DxbcOpRetC(true, ps_src);
      } else {
        DxbcOpDiscard(true, ps_src);
      }
      DxbcOpAnd(ps_dest, ps_src, DxbcSrc::LF(1.0f));
      break;

    case AluScalarOpcode::kSqrt:
      DxbcOpSqRt(ps_dest, operand_0_a);
      break;

    case AluScalarOpcode::kMulsc0:
    case AluScalarOpcode::kMulsc1:
      DxbcOpMul(ps_dest, operand_0_a, operand_1);
      if (!(instr.scalar_operands[0].GetIdenticalComponents(
                instr.scalar_operands[1]) &
            0b0001)) {
        // Shader Model 3: +-0 or denormal * anything = +0.
        uint32_t is_zero_temp = PushSystemTemp();
        DxbcOpMin(DxbcDest::R(is_zero_temp, 0b0001), operand_0_a.Abs(),
                  operand_1.Abs());
        // min isn't required to flush denormals, eq is.
        DxbcOpEq(DxbcDest::R(is_zero_temp, 0b0001),
                 DxbcSrc::R(is_zero_temp, DxbcSrc::kXXXX), DxbcSrc::LF(0.0f));
        DxbcOpMovC(ps_dest, DxbcSrc::R(is_zero_temp, DxbcSrc::kXXXX),
                   DxbcSrc::LF(0.0f), ps_src);
        // Release is_zero_temp.
        PopSystemTemp();
      }
      break;
    case AluScalarOpcode::kAddsc0:
    case AluScalarOpcode::kAddsc1:
      DxbcOpAdd(ps_dest, operand_0_a, operand_1);
      break;
    case AluScalarOpcode::kSubsc0:
    case AluScalarOpcode::kSubsc1:
      DxbcOpAdd(ps_dest, operand_0_a, -operand_1);
      break;

    case AluScalarOpcode::kSin:
      DxbcOpSinCos(ps_dest, DxbcDest::Null(), operand_0_a);
      break;
    case AluScalarOpcode::kCos:
      DxbcOpSinCos(DxbcDest::Null(), ps_dest, operand_0_a);
      break;

    default:
      assert_unhandled_case(instr.scalar_opcode);
      EmitTranslationError("Unknown ALU scalar operation");
      DxbcOpMov(ps_dest, DxbcSrc::LF(0.0f));
  }

  PopSystemTemp(operand_temps);
}

void DxbcShaderTranslator::ProcessAluInstruction(
    const ParsedAluInstruction& instr) {
  if (instr.IsNop()) {
    // Don't even disassemble or update predication.
    return;
  }

  if (emit_source_map_) {
    instruction_disassembly_buffer_.Reset();
    instr.Disassemble(&instruction_disassembly_buffer_);
  }
  UpdateInstructionPredicationAndEmitDisassembly(instr.is_predicated,
                                                 instr.predicate_condition);

  // Whether the instruction has changed the predicate, and it needs to be
  // checked again later.
  bool predicate_written_vector = false;
  uint32_t vector_result_swizzle = DxbcSrc::kXYZW;
  ProcessVectorAluOperation(instr, vector_result_swizzle,
                            predicate_written_vector);
  bool predicate_written_scalar = false;
  ProcessScalarAluOperation(instr, predicate_written_scalar);

  StoreResult(instr.vector_and_constant_result,
              DxbcSrc::R(system_temp_result_, vector_result_swizzle),
              instr.GetMemExportStreamConstant() != UINT32_MAX);
  StoreResult(instr.scalar_result,
              DxbcSrc::R(system_temp_ps_pc_p0_a0_, DxbcSrc::kXXXX));

  if (predicate_written_vector || predicate_written_scalar) {
    cf_exec_predicate_written_ = true;
    CloseInstructionPredication();
  }
}

}  // namespace gpu
}  // namespace xe
