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

void DxbcShaderTranslator::KillPixel(bool condition,
                                     const dxbc::Src& condition_src) {
  // Discard the pixel, but continue execution if other lanes in the quad need
  // this lane for derivatives. The driver may also perform early exiting
  // internally if all lanes are discarded if deemed beneficial.
  a_.OpDiscard(condition, condition_src);
  if (edram_rov_used_) {
    // Even though discarding disables all subsequent UAV/ROV writes, also skip
    // as much of the Render Backend emulation logic as possible by setting the
    // coverage and the mask of the written render targets to zero.
    a_.OpMov(dxbc::Dest::R(system_temp_rov_params_, 0b0001), dxbc::Src::LU(0));
  }
}

void DxbcShaderTranslator::ProcessVectorAluOperation(
    const ParsedAluInstruction& instr, uint32_t& result_swizzle,
    bool& predicate_written) {
  result_swizzle = dxbc::Src::kXYZW;
  predicate_written = false;

  uint32_t used_result_components =
      instr.vector_and_constant_result.GetUsedResultComponents();
  if (!used_result_components &&
      !ucode::GetAluVectorOpcodeInfo(instr.vector_opcode).changed_state) {
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
  dxbc::Src operands[3]{dxbc::Src::LF(0.0f), dxbc::Src::LF(0.0f),
                        dxbc::Src::LF(0.0f)};
  uint32_t operand_temps = 0;
  for (uint32_t i = 0; i < operand_count; ++i) {
    bool operand_temp_pushed = false;
    operands[i] =
        LoadOperand(instr.vector_operands[i], operand_needed_components[i],
                    operand_temp_pushed);
    operand_temps += uint32_t(operand_temp_pushed);
  }
  // Don't return without PopSystemTemp(operand_temps) from now on!

  dxbc::Dest per_component_dest(
      dxbc::Dest::R(system_temp_result_, used_result_components));
  switch (instr.vector_opcode) {
    case AluVectorOpcode::kAdd:
      a_.OpAdd(per_component_dest, operands[0], operands[1]);
      break;
    case AluVectorOpcode::kMul:
    case AluVectorOpcode::kMad: {
      // Not using DXBC mad to prevent fused multiply-add (mul followed by add
      // may be optimized into non-fused mad by the driver in the identical
      // operands case also).
      a_.OpMul(per_component_dest, operands[0], operands[1]);
      uint32_t multiplicands_different =
          used_result_components &
          ~instr.vector_operands[0].GetIdenticalComponents(
              instr.vector_operands[1]);
      if (multiplicands_different) {
        // Shader Model 3: +-0 or denormal * anything = +0.
        uint32_t is_zero_temp = PushSystemTemp();
        a_.OpMin(dxbc::Dest::R(is_zero_temp, multiplicands_different),
                 operands[0].Abs(), operands[1].Abs());
        // min isn't required to flush denormals, eq is.
        a_.OpEq(dxbc::Dest::R(is_zero_temp, multiplicands_different),
                dxbc::Src::R(is_zero_temp), dxbc::Src::LF(0.0f));
        // Not replacing true `0 + term` with movc of the term because +0 + -0
        // should result in +0, not -0.
        a_.OpMovC(dxbc::Dest::R(system_temp_result_, multiplicands_different),
                  dxbc::Src::R(is_zero_temp), dxbc::Src::LF(0.0f),
                  dxbc::Src::R(system_temp_result_));
        // Release is_zero_temp.
        PopSystemTemp();
      }
      if (instr.vector_opcode == AluVectorOpcode::kMad) {
        a_.OpAdd(per_component_dest, dxbc::Src::R(system_temp_result_),
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
          a_.OpLT(dxbc::Dest::R(system_temp_result_, different), operands[0],
                  operands[1]);
        } else {
          a_.OpGE(dxbc::Dest::R(system_temp_result_, different), operands[0],
                  operands[1]);
        }
        a_.OpMovC(dxbc::Dest::R(system_temp_result_, different),
                  dxbc::Src::R(system_temp_result_), operands[0], operands[1]);
      }
      if (identical) {
        a_.OpMov(dxbc::Dest::R(system_temp_result_, identical), operands[0]);
      }
    } break;

    case AluVectorOpcode::kSeq:
      a_.OpEq(per_component_dest, operands[0], operands[1]);
      a_.OpAnd(per_component_dest, dxbc::Src::R(system_temp_result_),
               dxbc::Src::LF(1.0f));
      break;
    case AluVectorOpcode::kSgt:
      a_.OpLT(per_component_dest, operands[1], operands[0]);
      a_.OpAnd(per_component_dest, dxbc::Src::R(system_temp_result_),
               dxbc::Src::LF(1.0f));
      break;
    case AluVectorOpcode::kSge:
      a_.OpGE(per_component_dest, operands[0], operands[1]);
      a_.OpAnd(per_component_dest, dxbc::Src::R(system_temp_result_),
               dxbc::Src::LF(1.0f));
      break;
    case AluVectorOpcode::kSne:
      a_.OpNE(per_component_dest, operands[0], operands[1]);
      a_.OpAnd(per_component_dest, dxbc::Src::R(system_temp_result_),
               dxbc::Src::LF(1.0f));
      break;

    case AluVectorOpcode::kFrc:
      a_.OpFrc(per_component_dest, operands[0]);
      break;
    case AluVectorOpcode::kTrunc:
      a_.OpRoundZ(per_component_dest, operands[0]);
      break;
    case AluVectorOpcode::kFloor:
      a_.OpRoundNI(per_component_dest, operands[0]);
      break;

    case AluVectorOpcode::kCndEq:
      a_.OpEq(per_component_dest, operands[0], dxbc::Src::LF(0.0f));
      a_.OpMovC(per_component_dest, dxbc::Src::R(system_temp_result_),
                operands[1], operands[2]);
      break;
    case AluVectorOpcode::kCndGe:
      a_.OpGE(per_component_dest, operands[0], dxbc::Src::LF(0.0f));
      a_.OpMovC(per_component_dest, dxbc::Src::R(system_temp_result_),
                operands[1], operands[2]);
      break;
    case AluVectorOpcode::kCndGt:
      a_.OpLT(per_component_dest, dxbc::Src::LF(0.0f), operands[0]);
      a_.OpMovC(per_component_dest, dxbc::Src::R(system_temp_result_),
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
      result_swizzle = dxbc::Src::kXXXX;
      uint32_t different = uint32_t((1 << component_count) - 1) &
                           ~instr.vector_operands[0].GetIdenticalComponents(
                               instr.vector_operands[1]);
      for (uint32_t i = 0; i < component_count; ++i) {
        a_.OpMul(dxbc::Dest::R(system_temp_result_, i ? 0b0010 : 0b0001),
                 operands[0].SelectFromSwizzled(i),
                 operands[1].SelectFromSwizzled(i));
        if ((different & (1 << i)) != 0) {
          // Shader Model 3: +-0 or denormal * anything = +0 (also not replacing
          // true `0 + term` with movc of the term because +0 + -0 should result
          // in +0, not -0).
          a_.OpMin(dxbc::Dest::R(system_temp_result_, 0b0100),
                   operands[0].SelectFromSwizzled(i).Abs(),
                   operands[1].SelectFromSwizzled(i).Abs());
          a_.OpEq(dxbc::Dest::R(system_temp_result_, 0b0100),
                  dxbc::Src::R(system_temp_result_, dxbc::Src::kZZZZ),
                  dxbc::Src::LF(0.0f));
          a_.OpMovC(dxbc::Dest::R(system_temp_result_, i ? 0b0010 : 0b0001),
                    dxbc::Src::R(system_temp_result_, dxbc::Src::kZZZZ),
                    dxbc::Src::LF(0.0f),
                    dxbc::Src::R(system_temp_result_,
                                 i ? dxbc::Src::kYYYY : dxbc::Src::kXXXX));
        }
        if (i) {
          // Not using DXBC dp# to avoid fused multiply-add, PC GPUs are scalar
          // as of 2020 anyway, and not using mad for the same reason (mul
          // followed by add may be optimized into non-fused mad by the driver
          // in the identical operands case also).
          a_.OpAdd(dxbc::Dest::R(system_temp_result_, 0b0001),
                   dxbc::Src::R(system_temp_result_, dxbc::Src::kXXXX),
                   dxbc::Src::R(system_temp_result_, dxbc::Src::kYYYY));
        }
      }
      if (component_count == 2) {
        a_.OpAdd(dxbc::Dest::R(system_temp_result_, 0b0001),
                 dxbc::Src::R(system_temp_result_, dxbc::Src::kXXXX),
                 operands[2].SelectFromSwizzled(0));
      }
    } break;

    case AluVectorOpcode::kCube: {
      // operands[0] is .z_xy.
      // Result is T coordinate, S coordinate, 2 * major axis, face ID.
      constexpr uint32_t kCubeX = 2, kCubeY = 3, kCubeZ = 0;
      dxbc::Src cube_x_src(operands[0].SelectFromSwizzled(kCubeX));
      dxbc::Src cube_y_src(operands[0].SelectFromSwizzled(kCubeY));
      dxbc::Src cube_z_src(operands[0].SelectFromSwizzled(kCubeZ));
      // result.xy = bool2(abs(z) >= abs(x), abs(z) >= abs(y))
      a_.OpGE(dxbc::Dest::R(system_temp_result_, 0b0011), cube_z_src.Abs(),
              operands[0].SwizzleSwizzled(kCubeX | (kCubeY << 2)).Abs());
      // result.x = abs(z) >= abs(x) && abs(z) >= abs(y)
      a_.OpAnd(dxbc::Dest::R(system_temp_result_, 0b0001),
               dxbc::Src::R(system_temp_result_, dxbc::Src::kXXXX),
               dxbc::Src::R(system_temp_result_, dxbc::Src::kYYYY));
      dxbc::Dest tc_dest(dxbc::Dest::R(system_temp_result_, 0b0001));
      dxbc::Dest sc_dest(dxbc::Dest::R(system_temp_result_, 0b0010));
      dxbc::Dest ma_dest(dxbc::Dest::R(system_temp_result_, 0b0100));
      dxbc::Dest id_dest(dxbc::Dest::R(system_temp_result_, 0b1000));
      a_.OpIf(true, dxbc::Src::R(system_temp_result_, dxbc::Src::kXXXX));
      {
        // Z is the major axis.
        // z < 0 needed for SC and ID, but the last to use is ID.
        uint32_t ma_neg_component = (used_result_components & 0b1000) ? 3 : 1;
        if (used_result_components & 0b1010) {
          a_.OpLT(dxbc::Dest::R(system_temp_result_, 1 << ma_neg_component),
                  cube_z_src, dxbc::Src::LF(0.0f));
        }
        if (used_result_components & 0b0001) {
          a_.OpMov(tc_dest, -cube_y_src);
        }
        if (used_result_components & 0b0010) {
          a_.OpMovC(sc_dest,
                    dxbc::Src::R(system_temp_result_).Select(ma_neg_component),
                    -cube_x_src, cube_x_src);
        }
        if (used_result_components & 0b0100) {
          a_.OpMul(ma_dest, dxbc::Src::LF(2.0f), cube_z_src);
        }
        if (used_result_components & 0b1000) {
          a_.OpMovC(id_dest,
                    dxbc::Src::R(system_temp_result_).Select(ma_neg_component),
                    dxbc::Src::LF(5.0f), dxbc::Src::LF(4.0f));
        }
      }
      a_.OpElse();
      {
        // result.x = abs(y) >= abs(x)
        a_.OpGE(dxbc::Dest::R(system_temp_result_, 0b0001), cube_y_src.Abs(),
                cube_x_src.Abs());
        a_.OpIf(true, dxbc::Src::R(system_temp_result_, dxbc::Src::kXXXX));
        {
          // Y is the major axis.
          // y < 0 needed for TC and ID, but the last to use is ID.
          uint32_t ma_neg_component = (used_result_components & 0b1000) ? 3 : 0;
          if (used_result_components & 0b1001) {
            a_.OpLT(dxbc::Dest::R(system_temp_result_, 1 << ma_neg_component),
                    cube_y_src, dxbc::Src::LF(0.0f));
          }
          if (used_result_components & 0b0001) {
            a_.OpMovC(
                tc_dest,
                dxbc::Src::R(system_temp_result_).Select(ma_neg_component),
                -cube_z_src, cube_z_src);
          }
          if (used_result_components & 0b0010) {
            a_.OpMov(sc_dest, cube_x_src);
          }
          if (used_result_components & 0b0100) {
            a_.OpMul(ma_dest, dxbc::Src::LF(2.0f), cube_y_src);
          }
          if (used_result_components & 0b1000) {
            a_.OpMovC(
                id_dest,
                dxbc::Src::R(system_temp_result_).Select(ma_neg_component),
                dxbc::Src::LF(3.0f), dxbc::Src::LF(2.0f));
          }
        }
        a_.OpElse();
        {
          // X is the major axis.
          // x < 0 needed for SC and ID, but the last to use is ID.
          uint32_t ma_neg_component = (used_result_components & 0b1000) ? 3 : 1;
          if (used_result_components & 0b1010) {
            a_.OpLT(dxbc::Dest::R(system_temp_result_, 1 << ma_neg_component),
                    cube_x_src, dxbc::Src::LF(0.0f));
          }
          if (used_result_components & 0b0001) {
            a_.OpMov(tc_dest, -cube_y_src);
          }
          if (used_result_components & 0b0010) {
            a_.OpMovC(
                sc_dest,
                dxbc::Src::R(system_temp_result_).Select(ma_neg_component),
                cube_z_src, -cube_z_src);
          }
          if (used_result_components & 0b0100) {
            a_.OpMul(ma_dest, dxbc::Src::LF(2.0f), cube_x_src);
          }
          if (used_result_components & 0b1000) {
            a_.OpAnd(id_dest,
                     dxbc::Src::R(system_temp_result_).Select(ma_neg_component),
                     dxbc::Src::LF(1.0f));
          }
        }
        a_.OpEndIf();
      }
      a_.OpEndIf();
    } break;

    case AluVectorOpcode::kMax4: {
      result_swizzle = dxbc::Src::kXXXX;
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
        a_.OpMax(dxbc::Dest::R(system_temp_result_, 0b0001),
                 operands[0].Select(unique_component_0),
                 operands[0].Select(unique_component_1));
        while (remaining_components) {
          uint32_t unique_component;
          xe::bit_scan_forward(remaining_components, &unique_component);
          remaining_components &= ~uint32_t(1 << unique_component);
          a_.OpMax(dxbc::Dest::R(system_temp_result_, 0b0001),
                   dxbc::Src::R(system_temp_result_, dxbc::Src::kXXXX),
                   operands[0].Select(unique_component));
        }
      } else {
        a_.OpMov(dxbc::Dest::R(system_temp_result_, 0b0001),
                 operands[0].Select(unique_component_0));
      }
    } break;

    case AluVectorOpcode::kSetpEqPush:
      predicate_written = true;
      result_swizzle = dxbc::Src::kXXXX;
      // result.xy = src0.xw == 0.0 (x only if needed).
      a_.OpEq(dxbc::Dest::R(system_temp_result_,
                            used_result_components ? 0b0011 : 0b0010),
              operands[0].SwizzleSwizzled(0b1100), dxbc::Src::LF(0.0f));
      // result.zw = src1.xw == 0.0 (z only if needed).
      a_.OpEq(dxbc::Dest::R(system_temp_result_,
                            used_result_components ? 0b1100 : 0b1000),
              operands[1].SwizzleSwizzled(0b11000000), dxbc::Src::LF(0.0f));
      // p0 = src0.w == 0.0 && src1.w == 0.0
      a_.OpAnd(dxbc::Dest::R(system_temp_ps_pc_p0_a0_, 0b0100),
               dxbc::Src::R(system_temp_result_, dxbc::Src::kYYYY),
               dxbc::Src::R(system_temp_result_, dxbc::Src::kWWWW));
      if (used_result_components) {
        // result = (src0.x == 0.0 && src1.x == 0.0) ? 0.0 : src0.x + 1.0
        a_.OpAnd(dxbc::Dest::R(system_temp_result_, 0b0001),
                 dxbc::Src::R(system_temp_result_, dxbc::Src::kXXXX),
                 dxbc::Src::R(system_temp_result_, dxbc::Src::kZZZZ));
        // If the condition is true, 1 will be added to make it 0.
        a_.OpMovC(dxbc::Dest::R(system_temp_result_, 0b0001),
                  dxbc::Src::R(system_temp_result_, dxbc::Src::kXXXX),
                  dxbc::Src::LF(-1.0f), operands[0].SelectFromSwizzled(0));
        a_.OpAdd(dxbc::Dest::R(system_temp_result_, 0b0001),
                 dxbc::Src::R(system_temp_result_, dxbc::Src::kXXXX),
                 dxbc::Src::LF(1.0f));
      }
      break;
    case AluVectorOpcode::kSetpNePush:
      predicate_written = true;
      result_swizzle = dxbc::Src::kXXXX;
      // result.xy = src0.xw == 0.0 (x only if needed).
      a_.OpEq(dxbc::Dest::R(system_temp_result_,
                            used_result_components ? 0b0011 : 0b0010),
              operands[0].SwizzleSwizzled(0b1100), dxbc::Src::LF(0.0f));
      // result.zw = src1.xw != 0.0 (z only if needed).
      a_.OpNE(dxbc::Dest::R(system_temp_result_,
                            used_result_components ? 0b1100 : 0b1000),
              operands[1].SwizzleSwizzled(0b11000000), dxbc::Src::LF(0.0f));
      // p0 = src0.w == 0.0 && src1.w != 0.0
      a_.OpAnd(dxbc::Dest::R(system_temp_ps_pc_p0_a0_, 0b0100),
               dxbc::Src::R(system_temp_result_, dxbc::Src::kYYYY),
               dxbc::Src::R(system_temp_result_, dxbc::Src::kWWWW));
      if (used_result_components) {
        // result = (src0.x == 0.0 && src1.x != 0.0) ? 0.0 : src0.x + 1.0
        a_.OpAnd(dxbc::Dest::R(system_temp_result_, 0b0001),
                 dxbc::Src::R(system_temp_result_, dxbc::Src::kXXXX),
                 dxbc::Src::R(system_temp_result_, dxbc::Src::kZZZZ));
        // If the condition is true, 1 will be added to make it 0.
        a_.OpMovC(dxbc::Dest::R(system_temp_result_, 0b0001),
                  dxbc::Src::R(system_temp_result_, dxbc::Src::kXXXX),
                  dxbc::Src::LF(-1.0f), operands[0].SelectFromSwizzled(0));
        a_.OpAdd(dxbc::Dest::R(system_temp_result_, 0b0001),
                 dxbc::Src::R(system_temp_result_, dxbc::Src::kXXXX),
                 dxbc::Src::LF(1.0f));
      }
      break;
    case AluVectorOpcode::kSetpGtPush:
      predicate_written = true;
      result_swizzle = dxbc::Src::kXXXX;
      // result.xy = src0.xw == 0.0 (x only if needed).
      a_.OpEq(dxbc::Dest::R(system_temp_result_,
                            used_result_components ? 0b0011 : 0b0010),
              operands[0].SwizzleSwizzled(0b1100), dxbc::Src::LF(0.0f));
      // result.zw = src1.xw > 0.0 (z only if needed).
      a_.OpLT(dxbc::Dest::R(system_temp_result_,
                            used_result_components ? 0b1100 : 0b1000),
              dxbc::Src::LF(0.0f), operands[1].SwizzleSwizzled(0b11000000));
      // p0 = src0.w == 0.0 && src1.w > 0.0
      a_.OpAnd(dxbc::Dest::R(system_temp_ps_pc_p0_a0_, 0b0100),
               dxbc::Src::R(system_temp_result_, dxbc::Src::kYYYY),
               dxbc::Src::R(system_temp_result_, dxbc::Src::kWWWW));
      if (used_result_components) {
        // result = (src0.x == 0.0 && src1.x > 0.0) ? 0.0 : src0.x + 1.0
        a_.OpAnd(dxbc::Dest::R(system_temp_result_, 0b0001),
                 dxbc::Src::R(system_temp_result_, dxbc::Src::kXXXX),
                 dxbc::Src::R(system_temp_result_, dxbc::Src::kZZZZ));
        // If the condition is true, 1 will be added to make it 0.
        a_.OpMovC(dxbc::Dest::R(system_temp_result_, 0b0001),
                  dxbc::Src::R(system_temp_result_, dxbc::Src::kXXXX),
                  dxbc::Src::LF(-1.0f), operands[0].SelectFromSwizzled(0));
        a_.OpAdd(dxbc::Dest::R(system_temp_result_, 0b0001),
                 dxbc::Src::R(system_temp_result_, dxbc::Src::kXXXX),
                 dxbc::Src::LF(1.0f));
      }
      break;
    case AluVectorOpcode::kSetpGePush:
      predicate_written = true;
      result_swizzle = dxbc::Src::kXXXX;
      // result.xy = src0.xw == 0.0 (x only if needed).
      a_.OpEq(dxbc::Dest::R(system_temp_result_,
                            used_result_components ? 0b0011 : 0b0010),
              operands[0].SwizzleSwizzled(0b1100), dxbc::Src::LF(0.0f));
      // result.zw = src1.xw >= 0.0 (z only if needed).
      a_.OpGE(dxbc::Dest::R(system_temp_result_,
                            used_result_components ? 0b1100 : 0b1000),
              operands[1].SwizzleSwizzled(0b11000000), dxbc::Src::LF(0.0f));
      // p0 = src0.w == 0.0 && src1.w >= 0.0
      a_.OpAnd(dxbc::Dest::R(system_temp_ps_pc_p0_a0_, 0b0100),
               dxbc::Src::R(system_temp_result_, dxbc::Src::kYYYY),
               dxbc::Src::R(system_temp_result_, dxbc::Src::kWWWW));
      if (used_result_components) {
        // result = (src0.x == 0.0 && src1.x >= 0.0) ? 0.0 : src0.x + 1.0
        a_.OpAnd(dxbc::Dest::R(system_temp_result_, 0b0001),
                 dxbc::Src::R(system_temp_result_, dxbc::Src::kXXXX),
                 dxbc::Src::R(system_temp_result_, dxbc::Src::kZZZZ));
        // If the condition is true, 1 will be added to make it 0.
        a_.OpMovC(dxbc::Dest::R(system_temp_result_, 0b0001),
                  dxbc::Src::R(system_temp_result_, dxbc::Src::kXXXX),
                  dxbc::Src::LF(-1.0f), operands[0].SelectFromSwizzled(0));
        a_.OpAdd(dxbc::Dest::R(system_temp_result_, 0b0001),
                 dxbc::Src::R(system_temp_result_, dxbc::Src::kXXXX),
                 dxbc::Src::LF(1.0f));
      }
      break;

    case AluVectorOpcode::kKillEq:
      result_swizzle = dxbc::Src::kXXXX;
      a_.OpEq(dxbc::Dest::R(system_temp_result_), operands[0], operands[1]);
      a_.OpOr(dxbc::Dest::R(system_temp_result_, 0b0011),
              dxbc::Src::R(system_temp_result_, 0b0100),
              dxbc::Src::R(system_temp_result_, 0b1110));
      a_.OpOr(dxbc::Dest::R(system_temp_result_, 0b0001),
              dxbc::Src::R(system_temp_result_, dxbc::Src::kXXXX),
              dxbc::Src::R(system_temp_result_, dxbc::Src::kYYYY));
      KillPixel(true, dxbc::Src::R(system_temp_result_, dxbc::Src::kXXXX));
      if (used_result_components) {
        a_.OpAnd(dxbc::Dest::R(system_temp_result_, 0b0001),
                 dxbc::Src::R(system_temp_result_, dxbc::Src::kXXXX),
                 dxbc::Src::LF(1.0f));
      }
      break;
    case AluVectorOpcode::kKillGt:
      result_swizzle = dxbc::Src::kXXXX;
      a_.OpLT(dxbc::Dest::R(system_temp_result_), operands[1], operands[0]);
      a_.OpOr(dxbc::Dest::R(system_temp_result_, 0b0011),
              dxbc::Src::R(system_temp_result_, 0b0100),
              dxbc::Src::R(system_temp_result_, 0b1110));
      a_.OpOr(dxbc::Dest::R(system_temp_result_, 0b0001),
              dxbc::Src::R(system_temp_result_, dxbc::Src::kXXXX),
              dxbc::Src::R(system_temp_result_, dxbc::Src::kYYYY));
      KillPixel(true, dxbc::Src::R(system_temp_result_, dxbc::Src::kXXXX));
      if (used_result_components) {
        a_.OpAnd(dxbc::Dest::R(system_temp_result_, 0b0001),
                 dxbc::Src::R(system_temp_result_, dxbc::Src::kXXXX),
                 dxbc::Src::LF(1.0f));
      }
      break;
    case AluVectorOpcode::kKillGe:
      result_swizzle = dxbc::Src::kXXXX;
      a_.OpGE(dxbc::Dest::R(system_temp_result_), operands[0], operands[1]);
      a_.OpOr(dxbc::Dest::R(system_temp_result_, 0b0011),
              dxbc::Src::R(system_temp_result_, 0b0100),
              dxbc::Src::R(system_temp_result_, 0b1110));
      a_.OpOr(dxbc::Dest::R(system_temp_result_, 0b0001),
              dxbc::Src::R(system_temp_result_, dxbc::Src::kXXXX),
              dxbc::Src::R(system_temp_result_, dxbc::Src::kYYYY));
      KillPixel(true, dxbc::Src::R(system_temp_result_, dxbc::Src::kXXXX));
      if (used_result_components) {
        a_.OpAnd(dxbc::Dest::R(system_temp_result_, 0b0001),
                 dxbc::Src::R(system_temp_result_, dxbc::Src::kXXXX),
                 dxbc::Src::LF(1.0f));
      }
      break;
    case AluVectorOpcode::kKillNe:
      result_swizzle = dxbc::Src::kXXXX;
      a_.OpNE(dxbc::Dest::R(system_temp_result_), operands[0], operands[1]);
      a_.OpOr(dxbc::Dest::R(system_temp_result_, 0b0011),
              dxbc::Src::R(system_temp_result_, 0b0100),
              dxbc::Src::R(system_temp_result_, 0b1110));
      a_.OpOr(dxbc::Dest::R(system_temp_result_, 0b0001),
              dxbc::Src::R(system_temp_result_, dxbc::Src::kXXXX),
              dxbc::Src::R(system_temp_result_, dxbc::Src::kYYYY));
      KillPixel(true, dxbc::Src::R(system_temp_result_, dxbc::Src::kXXXX));
      if (used_result_components) {
        a_.OpAnd(dxbc::Dest::R(system_temp_result_, 0b0001),
                 dxbc::Src::R(system_temp_result_, dxbc::Src::kXXXX),
                 dxbc::Src::LF(1.0f));
      }
      break;

    case AluVectorOpcode::kDst:
      if (used_result_components & 0b0001) {
        a_.OpMov(dxbc::Dest::R(system_temp_result_, 0b0001),
                 dxbc::Src::LF(1.0f));
      }
      if (used_result_components & 0b0010) {
        a_.OpMul(dxbc::Dest::R(system_temp_result_, 0b0010),
                 operands[0].SelectFromSwizzled(1),
                 operands[1].SelectFromSwizzled(1));
        if (!(instr.vector_operands[0].GetIdenticalComponents(
                  instr.vector_operands[1]) &
              0b0010)) {
          // Shader Model 3: +-0 or denormal * anything = +0.
          a_.OpMin(dxbc::Dest::R(system_temp_result_, 0b0100),
                   operands[0].SelectFromSwizzled(1).Abs(),
                   operands[1].SelectFromSwizzled(1).Abs());
          // min isn't required to flush denormals, eq is.
          a_.OpEq(dxbc::Dest::R(system_temp_result_, 0b0100),
                  dxbc::Src::R(system_temp_result_, dxbc::Src::kZZZZ),
                  dxbc::Src::LF(0.0f));
          a_.OpMovC(dxbc::Dest::R(system_temp_result_, 0b0010),
                    dxbc::Src::R(system_temp_result_, dxbc::Src::kZZZZ),
                    dxbc::Src::LF(0.0f),
                    dxbc::Src::R(system_temp_result_, dxbc::Src::kYYYY));
        }
      }
      if (used_result_components & 0b0100) {
        a_.OpMov(dxbc::Dest::R(system_temp_result_, 0b0100),
                 operands[0].SelectFromSwizzled(2));
      }
      if (used_result_components & 0b1000) {
        a_.OpMov(dxbc::Dest::R(system_temp_result_, 0b1000),
                 operands[1].SelectFromSwizzled(2));
      }
      break;

    case AluVectorOpcode::kMaxA:
      a_.OpAdd(dxbc::Dest::R(system_temp_ps_pc_p0_a0_, 0b1000),
               operands[0].SelectFromSwizzled(3), dxbc::Src::LF(0.5f));
      a_.OpRoundNI(dxbc::Dest::R(system_temp_ps_pc_p0_a0_, 0b1000),
                   dxbc::Src::R(system_temp_ps_pc_p0_a0_, dxbc::Src::kWWWW));
      a_.OpMax(dxbc::Dest::R(system_temp_ps_pc_p0_a0_, 0b1000),
               dxbc::Src::R(system_temp_ps_pc_p0_a0_, dxbc::Src::kWWWW),
               dxbc::Src::LF(-256.0f));
      a_.OpMin(dxbc::Dest::R(system_temp_ps_pc_p0_a0_, 0b1000),
               dxbc::Src::R(system_temp_ps_pc_p0_a0_, dxbc::Src::kWWWW),
               dxbc::Src::LF(255.0f));
      a_.OpFToI(dxbc::Dest::R(system_temp_ps_pc_p0_a0_, 0b1000),
                dxbc::Src::R(system_temp_ps_pc_p0_a0_, dxbc::Src::kWWWW));
      if (used_result_components) {
        uint32_t identical = instr.vector_operands[0].GetIdenticalComponents(
                                 instr.vector_operands[1]) &
                             used_result_components;
        uint32_t different = used_result_components & ~identical;
        if (different) {
          // Shader Model 3 NaN behavior (a >= b ? a : b, not fmax).
          a_.OpGE(dxbc::Dest::R(system_temp_result_, different), operands[0],
                  operands[1]);
          a_.OpMovC(dxbc::Dest::R(system_temp_result_, different),
                    dxbc::Src::R(system_temp_result_), operands[0],
                    operands[1]);
        }
        if (identical) {
          a_.OpMov(dxbc::Dest::R(system_temp_result_, identical), operands[0]);
        }
      }
      break;

    default:
      assert_unhandled_case(instr.vector_opcode);
      EmitTranslationError("Unknown ALU vector operation");
      a_.OpMov(dxbc::Dest::R(system_temp_result_), dxbc::Src::LF(0.0f));
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
  dxbc::Src operands_loaded[2]{dxbc::Src::LF(0.0f), dxbc::Src::LF(0.0f)};
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
  dxbc::Src operand_0_a(operands_loaded[0].SelectFromSwizzled(0));
  dxbc::Src operand_0_b(operands_loaded[0].SelectFromSwizzled(1));
  dxbc::Src operand_1(operands_loaded[1].SelectFromSwizzled(0));

  dxbc::Dest ps_dest(dxbc::Dest::R(system_temp_ps_pc_p0_a0_, 0b0001));
  dxbc::Src ps_src(dxbc::Src::R(system_temp_ps_pc_p0_a0_, dxbc::Src::kXXXX));
  switch (instr.scalar_opcode) {
    case AluScalarOpcode::kAdds:
      a_.OpAdd(ps_dest, operand_0_a, operand_0_b);
      break;
    case AluScalarOpcode::kAddsPrev:
      a_.OpAdd(ps_dest, operand_0_a, ps_src);
      break;
    case AluScalarOpcode::kMuls:
      a_.OpMul(ps_dest, operand_0_a, operand_0_b);
      if (instr.scalar_operands[0].components[0] !=
          instr.scalar_operands[0].components[1]) {
        // Shader Model 3: +-0 or denormal * anything = +0.
        uint32_t is_zero_temp = PushSystemTemp();
        a_.OpMin(dxbc::Dest::R(is_zero_temp, 0b0001), operand_0_a.Abs(),
                 operand_0_b.Abs());
        // min isn't required to flush denormals, eq is.
        a_.OpEq(dxbc::Dest::R(is_zero_temp, 0b0001),
                dxbc::Src::R(is_zero_temp, dxbc::Src::kXXXX),
                dxbc::Src::LF(0.0f));
        a_.OpMovC(ps_dest, dxbc::Src::R(is_zero_temp, dxbc::Src::kXXXX),
                  dxbc::Src::LF(0.0f), ps_src);
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
        a_.OpNE(dxbc::Dest::R(test_temp, 0b0001), ps_src,
                dxbc::Src::LF(-FLT_MAX));
        // isfinite(ps), or |ps| <= FLT_MAX, or -|ps| >= -FLT_MAX, since
        // -FLT_MAX is already loaded to an SGPR, this is also false if it's
        // NaN.
        a_.OpGE(dxbc::Dest::R(test_temp, 0b0010), -ps_src.Abs(),
                dxbc::Src::LF(-FLT_MAX));
        a_.OpAnd(dxbc::Dest::R(test_temp, 0b0001),
                 dxbc::Src::R(test_temp, dxbc::Src::kXXXX),
                 dxbc::Src::R(test_temp, dxbc::Src::kYYYY));
        // isfinite(src0.b).
        a_.OpGE(dxbc::Dest::R(test_temp, 0b0010), -operand_0_b.Abs(),
                dxbc::Src::LF(-FLT_MAX));
        a_.OpAnd(dxbc::Dest::R(test_temp, 0b0001),
                 dxbc::Src::R(test_temp, dxbc::Src::kXXXX),
                 dxbc::Src::R(test_temp, dxbc::Src::kYYYY));
        // src0.b > 0 (need !(src0.b <= 0), but src0.b has already been checked
        // for NaN).
        a_.OpLT(dxbc::Dest::R(test_temp, 0b0010), dxbc::Src::LF(0.0f),
                operand_0_b);
        a_.OpAnd(dxbc::Dest::R(test_temp, 0b0001),
                 dxbc::Src::R(test_temp, dxbc::Src::kXXXX),
                 dxbc::Src::R(test_temp, dxbc::Src::kYYYY));
        a_.OpIf(true, dxbc::Src::R(test_temp, dxbc::Src::kXXXX));
      }
      // Shader Model 3: +-0 or denormal * anything = +0.
      a_.OpMin(dxbc::Dest::R(test_temp, 0b0001), operand_0_a.Abs(),
               ps_src.Abs());
      // min isn't required to flush denormals, eq is.
      a_.OpEq(dxbc::Dest::R(test_temp, 0b0001),
              dxbc::Src::R(test_temp, dxbc::Src::kXXXX), dxbc::Src::LF(0.0f));
      a_.OpMul(ps_dest, operand_0_a, ps_src);
      a_.OpMovC(ps_dest, dxbc::Src::R(test_temp, dxbc::Src::kXXXX),
                dxbc::Src::LF(0.0f), ps_src);
      if (instr.scalar_opcode == AluScalarOpcode::kMulsPrev2) {
        a_.OpElse();
        a_.OpMov(ps_dest, dxbc::Src::LF(-FLT_MAX));
        a_.OpEndIf();
      }
      // Release test_temp.
      PopSystemTemp();
    } break;

    case AluScalarOpcode::kMaxs:
    case AluScalarOpcode::kMins:
      // max is commonly used as mov.
      if (instr.scalar_operands[0].components[0] ==
          instr.scalar_operands[0].components[1]) {
        a_.OpMov(ps_dest, operand_0_a);
      } else {
        // Shader Model 3 NaN behavior (a op b ? a : b, not fmax/fmin).
        if (instr.scalar_opcode == AluScalarOpcode::kMins) {
          a_.OpLT(ps_dest, operand_0_a, operand_0_b);
        } else {
          a_.OpGE(ps_dest, operand_0_a, operand_0_b);
        }
        a_.OpMovC(ps_dest, ps_src, operand_0_a, operand_0_b);
      }
      break;

    case AluScalarOpcode::kSeqs:
      a_.OpEq(ps_dest, operand_0_a, dxbc::Src::LF(0.0f));
      a_.OpAnd(ps_dest, ps_src, dxbc::Src::LF(1.0f));
      break;
    case AluScalarOpcode::kSgts:
      a_.OpLT(ps_dest, dxbc::Src::LF(0.0f), operand_0_a);
      a_.OpAnd(ps_dest, ps_src, dxbc::Src::LF(1.0f));
      break;
    case AluScalarOpcode::kSges:
      a_.OpGE(ps_dest, operand_0_a, dxbc::Src::LF(0.0f));
      a_.OpAnd(ps_dest, ps_src, dxbc::Src::LF(1.0f));
      break;
    case AluScalarOpcode::kSnes:
      a_.OpNE(ps_dest, operand_0_a, dxbc::Src::LF(0.0f));
      a_.OpAnd(ps_dest, ps_src, dxbc::Src::LF(1.0f));
      break;

    case AluScalarOpcode::kFrcs:
      a_.OpFrc(ps_dest, operand_0_a);
      break;
    case AluScalarOpcode::kTruncs:
      a_.OpRoundZ(ps_dest, operand_0_a);
      break;
    case AluScalarOpcode::kFloors:
      a_.OpRoundNI(ps_dest, operand_0_a);
      break;

    case AluScalarOpcode::kExp:
      a_.OpExp(ps_dest, operand_0_a);
      break;
    case AluScalarOpcode::kLogc: {
      a_.OpLog(ps_dest, operand_0_a);
      uint32_t is_neg_infinity_temp = PushSystemTemp();
      a_.OpEq(dxbc::Dest::R(is_neg_infinity_temp, 0b0001), ps_src,
              dxbc::Src::LF(-INFINITY));
      a_.OpMovC(ps_dest, dxbc::Src::R(is_neg_infinity_temp, dxbc::Src::kXXXX),
                dxbc::Src::LF(-FLT_MAX), ps_src);
      // Release is_neg_infinity_temp.
      PopSystemTemp();
    } break;
    case AluScalarOpcode::kLog:
      a_.OpLog(ps_dest, operand_0_a);
      break;
    case AluScalarOpcode::kRcpc:
    case AluScalarOpcode::kRsqc: {
      if (instr.scalar_opcode == AluScalarOpcode::kRsqc) {
        a_.OpRSq(ps_dest, operand_0_a);
      } else {
        a_.OpRcp(ps_dest, operand_0_a);
      }
      uint32_t is_infinity_temp = PushSystemTemp();
      a_.OpEq(dxbc::Dest::R(is_infinity_temp, 0b0001), ps_src.Abs(),
              dxbc::Src::LF(INFINITY));
      // If +-Infinity (0x7F800000 or 0xFF800000), add -1 (0xFFFFFFFF) to turn
      // into +-FLT_MAX (0x7F7FFFFF or 0xFF7FFFFF).
      a_.OpIAdd(ps_dest, ps_src,
                dxbc::Src::R(is_infinity_temp, dxbc::Src::kXXXX));
      // Release is_infinity_temp.
      PopSystemTemp();
    } break;
    case AluScalarOpcode::kRcpf:
    case AluScalarOpcode::kRsqf: {
      if (instr.scalar_opcode == AluScalarOpcode::kRsqf) {
        a_.OpRSq(ps_dest, operand_0_a);
      } else {
        a_.OpRcp(ps_dest, operand_0_a);
      }
      uint32_t is_not_infinity_temp = PushSystemTemp();
      a_.OpNE(dxbc::Dest::R(is_not_infinity_temp, 0b0001), ps_src.Abs(),
              dxbc::Src::LF(INFINITY));
      // Keep the sign bit if infinity.
      a_.OpOr(dxbc::Dest::R(is_not_infinity_temp, 0b0001),
              dxbc::Src::R(is_not_infinity_temp, dxbc::Src::kXXXX),
              dxbc::Src::LU(uint32_t(1) << 31));
      a_.OpAnd(ps_dest, ps_src,
               dxbc::Src::R(is_not_infinity_temp, dxbc::Src::kXXXX));
      // Release is_not_infinity_temp.
      PopSystemTemp();
    } break;
    case AluScalarOpcode::kRcp:
      a_.OpRcp(ps_dest, operand_0_a);
      break;
    case AluScalarOpcode::kRsq:
      a_.OpRSq(ps_dest, operand_0_a);
      break;

    case AluScalarOpcode::kMaxAs:
    case AluScalarOpcode::kMaxAsf:
      if (instr.scalar_opcode == AluScalarOpcode::kMaxAsf) {
        a_.OpRoundNI(dxbc::Dest::R(system_temp_ps_pc_p0_a0_, 0b1000),
                     operand_0_a);
      } else {
        a_.OpAdd(dxbc::Dest::R(system_temp_ps_pc_p0_a0_, 0b1000), operand_0_a,
                 dxbc::Src::LF(0.5f));
        a_.OpRoundNI(dxbc::Dest::R(system_temp_ps_pc_p0_a0_, 0b1000),
                     dxbc::Src::R(system_temp_ps_pc_p0_a0_, dxbc::Src::kWWWW));
      }
      a_.OpMax(dxbc::Dest::R(system_temp_ps_pc_p0_a0_, 0b1000),
               dxbc::Src::R(system_temp_ps_pc_p0_a0_, dxbc::Src::kWWWW),
               dxbc::Src::LF(-256.0f));
      a_.OpMin(dxbc::Dest::R(system_temp_ps_pc_p0_a0_, 0b1000),
               dxbc::Src::R(system_temp_ps_pc_p0_a0_, dxbc::Src::kWWWW),
               dxbc::Src::LF(255.0f));
      a_.OpFToI(dxbc::Dest::R(system_temp_ps_pc_p0_a0_, 0b1000),
                dxbc::Src::R(system_temp_ps_pc_p0_a0_, dxbc::Src::kWWWW));
      if (instr.scalar_operands[0].components[0] ==
          instr.scalar_operands[0].components[1]) {
        a_.OpMov(ps_dest, operand_0_a);
      } else {
        // Shader Model 3 NaN behavior (a >= b ? a : b, not fmax).
        a_.OpGE(ps_dest, operand_0_a, operand_0_b);
        a_.OpMovC(ps_dest, ps_src, operand_0_a, operand_0_b);
      }
      break;

    case AluScalarOpcode::kSubs:
      a_.OpAdd(ps_dest, operand_0_a, -operand_0_b);
      break;
    case AluScalarOpcode::kSubsPrev:
      a_.OpAdd(ps_dest, operand_0_a, -ps_src);
      break;

    case AluScalarOpcode::kSetpEq:
      predicate_written = true;
      a_.OpEq(dxbc::Dest::R(system_temp_ps_pc_p0_a0_, 0b0100), operand_0_a,
              dxbc::Src::LF(0.0f));
      a_.OpMovC(ps_dest,
                dxbc::Src::R(system_temp_ps_pc_p0_a0_, dxbc::Src::kZZZZ),
                dxbc::Src::LF(0.0f), dxbc::Src::LF(1.0f));
      break;
    case AluScalarOpcode::kSetpNe:
      predicate_written = true;
      a_.OpNE(dxbc::Dest::R(system_temp_ps_pc_p0_a0_, 0b0100), operand_0_a,
              dxbc::Src::LF(0.0f));
      a_.OpMovC(ps_dest,
                dxbc::Src::R(system_temp_ps_pc_p0_a0_, dxbc::Src::kZZZZ),
                dxbc::Src::LF(0.0f), dxbc::Src::LF(1.0f));
      break;
    case AluScalarOpcode::kSetpGt:
      predicate_written = true;
      a_.OpLT(dxbc::Dest::R(system_temp_ps_pc_p0_a0_, 0b0100),
              dxbc::Src::LF(0.0f), operand_0_a);
      a_.OpMovC(ps_dest,
                dxbc::Src::R(system_temp_ps_pc_p0_a0_, dxbc::Src::kZZZZ),
                dxbc::Src::LF(0.0f), dxbc::Src::LF(1.0f));
      break;
    case AluScalarOpcode::kSetpGe:
      predicate_written = true;
      a_.OpGE(dxbc::Dest::R(system_temp_ps_pc_p0_a0_, 0b0100), operand_0_a,
              dxbc::Src::LF(0.0f));
      a_.OpMovC(ps_dest,
                dxbc::Src::R(system_temp_ps_pc_p0_a0_, dxbc::Src::kZZZZ),
                dxbc::Src::LF(0.0f), dxbc::Src::LF(1.0f));
      break;
    case AluScalarOpcode::kSetpInv:
      predicate_written = true;
      // Calculate ps as if src0.a != 1.0 (the false predicate value case).
      a_.OpEq(ps_dest, operand_0_a, dxbc::Src::LF(0.0f));
      a_.OpMovC(ps_dest, ps_src, dxbc::Src::LF(1.0f), operand_0_a);
      // Set the predicate to src0.a == 1.0, and, if it's true, zero ps.
      a_.OpEq(dxbc::Dest::R(system_temp_ps_pc_p0_a0_, 0b0100), operand_0_a,
              dxbc::Src::LF(1.0f));
      a_.OpMovC(ps_dest,
                dxbc::Src::R(system_temp_ps_pc_p0_a0_, dxbc::Src::kZZZZ),
                dxbc::Src::LF(0.0f), ps_src);
      break;
    case AluScalarOpcode::kSetpPop:
      predicate_written = true;
      a_.OpAdd(ps_dest, operand_0_a, dxbc::Src::LF(-1.0f));
      a_.OpGE(dxbc::Dest::R(system_temp_ps_pc_p0_a0_, 0b0100),
              dxbc::Src::LF(0.0f), ps_src);
      a_.OpMovC(ps_dest,
                dxbc::Src::R(system_temp_ps_pc_p0_a0_, dxbc::Src::kZZZZ),
                dxbc::Src::LF(0.0f), ps_src);
      break;
    case AluScalarOpcode::kSetpClr:
      predicate_written = true;
      a_.OpMov(ps_dest, dxbc::Src::LF(FLT_MAX));
      a_.OpMov(dxbc::Dest::R(system_temp_ps_pc_p0_a0_, 0b0100),
               dxbc::Src::LU(0));
      break;
    case AluScalarOpcode::kSetpRstr:
      predicate_written = true;
      a_.OpEq(dxbc::Dest::R(system_temp_ps_pc_p0_a0_, 0b0100), operand_0_a,
              dxbc::Src::LF(0.0f));
      // Just copying src0.a to ps (since it's set to 0 if it's 0) could work,
      // but flush denormals and zero sign just for safety.
      a_.OpMovC(ps_dest,
                dxbc::Src::R(system_temp_ps_pc_p0_a0_, dxbc::Src::kZZZZ),
                dxbc::Src::LF(0.0f), operand_0_a);
      break;

    case AluScalarOpcode::kKillsEq:
      a_.OpEq(ps_dest, operand_0_a, dxbc::Src::LF(0.0f));
      KillPixel(true, ps_src);
      a_.OpAnd(ps_dest, ps_src, dxbc::Src::LF(1.0f));
      break;
    case AluScalarOpcode::kKillsGt:
      a_.OpLT(ps_dest, dxbc::Src::LF(0.0f), operand_0_a);
      KillPixel(true, ps_src);
      a_.OpAnd(ps_dest, ps_src, dxbc::Src::LF(1.0f));
      break;
    case AluScalarOpcode::kKillsGe:
      a_.OpGE(ps_dest, operand_0_a, dxbc::Src::LF(0.0f));
      KillPixel(true, ps_src);
      a_.OpAnd(ps_dest, ps_src, dxbc::Src::LF(1.0f));
      break;
    case AluScalarOpcode::kKillsNe:
      a_.OpNE(ps_dest, operand_0_a, dxbc::Src::LF(0.0f));
      KillPixel(true, ps_src);
      a_.OpAnd(ps_dest, ps_src, dxbc::Src::LF(1.0f));
      break;
    case AluScalarOpcode::kKillsOne:
      a_.OpEq(ps_dest, operand_0_a, dxbc::Src::LF(1.0f));
      KillPixel(true, ps_src);
      a_.OpAnd(ps_dest, ps_src, dxbc::Src::LF(1.0f));
      break;

    case AluScalarOpcode::kSqrt:
      a_.OpSqRt(ps_dest, operand_0_a);
      break;

    case AluScalarOpcode::kMulsc0:
    case AluScalarOpcode::kMulsc1:
      a_.OpMul(ps_dest, operand_0_a, operand_1);
      if (!(instr.scalar_operands[0].GetIdenticalComponents(
                instr.scalar_operands[1]) &
            0b0001)) {
        // Shader Model 3: +-0 or denormal * anything = +0.
        uint32_t is_zero_temp = PushSystemTemp();
        a_.OpMin(dxbc::Dest::R(is_zero_temp, 0b0001), operand_0_a.Abs(),
                 operand_1.Abs());
        // min isn't required to flush denormals, eq is.
        a_.OpEq(dxbc::Dest::R(is_zero_temp, 0b0001),
                dxbc::Src::R(is_zero_temp, dxbc::Src::kXXXX),
                dxbc::Src::LF(0.0f));
        a_.OpMovC(ps_dest, dxbc::Src::R(is_zero_temp, dxbc::Src::kXXXX),
                  dxbc::Src::LF(0.0f), ps_src);
        // Release is_zero_temp.
        PopSystemTemp();
      }
      break;
    case AluScalarOpcode::kAddsc0:
    case AluScalarOpcode::kAddsc1:
      a_.OpAdd(ps_dest, operand_0_a, operand_1);
      break;
    case AluScalarOpcode::kSubsc0:
    case AluScalarOpcode::kSubsc1:
      a_.OpAdd(ps_dest, operand_0_a, -operand_1);
      break;

    case AluScalarOpcode::kSin:
      a_.OpSinCos(ps_dest, dxbc::Dest::Null(), operand_0_a);
      break;
    case AluScalarOpcode::kCos:
      a_.OpSinCos(dxbc::Dest::Null(), ps_dest, operand_0_a);
      break;

    default:
      assert_unhandled_case(instr.scalar_opcode);
      EmitTranslationError("Unknown ALU scalar operation");
      a_.OpMov(ps_dest, dxbc::Src::LF(0.0f));
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
  uint32_t vector_result_swizzle = dxbc::Src::kXYZW;
  ProcessVectorAluOperation(instr, vector_result_swizzle,
                            predicate_written_vector);
  bool predicate_written_scalar = false;
  ProcessScalarAluOperation(instr, predicate_written_scalar);

  StoreResult(instr.vector_and_constant_result,
              dxbc::Src::R(system_temp_result_, vector_result_swizzle),
              instr.GetMemExportStreamConstant() != UINT32_MAX);
  StoreResult(instr.scalar_result,
              dxbc::Src::R(system_temp_ps_pc_p0_a0_, dxbc::Src::kXXXX));

  if (predicate_written_vector || predicate_written_scalar) {
    cf_exec_predicate_written_ = true;
    CloseInstructionPredication();
  }
}

}  // namespace gpu
}  // namespace xe
