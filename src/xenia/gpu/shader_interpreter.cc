/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/shader_interpreter.h"

#include <cfloat>
#include <cmath>
#include <cstring>

namespace xe {
namespace gpu {

void ShaderInterpreter::Execute() {
  // For more consistency between invocations in case of a malformed shader.
  state_.Reset();

  const uint32_t* bool_constants =
      &register_file_[XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031].u32;
  const xenos::LoopConstant* loop_constants =
      reinterpret_cast<const xenos::LoopConstant*>(
          &register_file_[XE_GPU_REG_SHADER_CONSTANT_LOOP_00].u32);

  bool exec_ended = false;
  uint32_t cf_index_next = 1;
  for (uint32_t cf_index = 0; !exec_ended; cf_index = cf_index_next) {
    cf_index_next = cf_index + 1;

    const uint32_t* cf_pair = &ucode_[3 * (cf_index >> 1)];
    ucode::ControlFlowInstruction cf_instr;
    if (cf_index & 1) {
      cf_instr.dword_0 = (cf_pair[1] >> 16) | (cf_pair[2] << 16);
      cf_instr.dword_1 = cf_pair[2] >> 16;
    } else {
      cf_instr.dword_0 = cf_pair[0];
      cf_instr.dword_1 = cf_pair[1] & 0xFFFF;
    }

    ucode::ControlFlowOpcode cf_opcode = cf_instr.opcode();
    switch (cf_opcode) {
      case ucode::ControlFlowOpcode::kNop: {
      } break;

      case ucode::ControlFlowOpcode::kExec:
      case ucode::ControlFlowOpcode::kExecEnd:
      case ucode::ControlFlowOpcode::kCondExec:
      case ucode::ControlFlowOpcode::kCondExecEnd:
      case ucode::ControlFlowOpcode::kCondExecPred:
      case ucode::ControlFlowOpcode::kCondExecPredEnd:
      case ucode::ControlFlowOpcode::kCondExecPredClean:
      case ucode::ControlFlowOpcode::kCondExecPredCleanEnd: {
        ucode::ControlFlowExecInstruction cf_exec =
            *reinterpret_cast<const ucode::ControlFlowExecInstruction*>(
                &cf_instr);

        switch (cf_opcode) {
          case ucode::ControlFlowOpcode::kCondExec:
          case ucode::ControlFlowOpcode::kCondExecEnd:
          case ucode::ControlFlowOpcode::kCondExecPredClean:
          case ucode::ControlFlowOpcode::kCondExecPredCleanEnd: {
            const ucode::ControlFlowCondExecInstruction cf_cond_exec =
                *reinterpret_cast<const ucode::ControlFlowCondExecInstruction*>(
                    &cf_exec);
            uint32_t bool_address = cf_cond_exec.bool_address();
            if (cf_cond_exec.condition() !=
                ((bool_constants[bool_address >> 5] &
                  (UINT32_C(1) << (bool_address & 31))) != 0)) {
              continue;
            }
          } break;
          case ucode::ControlFlowOpcode::kCondExecPred:
          case ucode::ControlFlowOpcode::kCondExecPredEnd: {
            const ucode::ControlFlowCondExecPredInstruction cf_cond_exec_pred =
                *reinterpret_cast<
                    const ucode::ControlFlowCondExecPredInstruction*>(&cf_exec);
            if (cf_cond_exec_pred.condition() != state_.predicate) {
              continue;
            }
          } break;
          default:
            break;
        }

        for (uint32_t exec_index = 0; exec_index < cf_exec.count();
             ++exec_index) {
          const uint32_t* exec_instruction =
              &ucode_[3 * (cf_exec.address() + exec_index)];
          if ((cf_exec.sequence() >> (exec_index << 1)) & 0b01) {
            const ucode::FetchInstruction& fetch_instr =
                *reinterpret_cast<const ucode::FetchInstruction*>(
                    exec_instruction);
            if (fetch_instr.is_predicated() &&
                fetch_instr.predicate_condition() != state_.predicate) {
              continue;
            }
            if (fetch_instr.opcode() == ucode::FetchOpcode::kVertexFetch) {
              ExecuteVertexFetchInstruction(fetch_instr.vertex_fetch());
            } else {
              // Not supporting texture fetching (very complex).
              float zero_result[4] = {};
              StoreFetchResult(fetch_instr.dest(),
                               fetch_instr.is_dest_relative(),
                               fetch_instr.dest_swizzle(), zero_result);
            }
          } else {
            const ucode::AluInstruction& alu_instr =
                *reinterpret_cast<const ucode::AluInstruction*>(
                    exec_instruction);
            if (alu_instr.is_predicated() &&
                alu_instr.predicate_condition() != state_.predicate) {
              continue;
            }
            ExecuteAluInstruction(alu_instr);
          }
        }

        if (ucode::DoesControlFlowOpcodeEndShader(cf_opcode)) {
          exec_ended = true;
        }
      } break;

      case ucode::ControlFlowOpcode::kLoopStart: {
        ucode::ControlFlowLoopStartInstruction cf_loop_start =
            *reinterpret_cast<const ucode::ControlFlowLoopStartInstruction*>(
                &cf_instr);
        assert_true(state_.loop_stack_depth < 4);
        if (++state_.loop_stack_depth > 4) {
          cf_index_next = cf_loop_start.address();
          continue;
        }
        xenos::LoopConstant loop_constant =
            loop_constants[cf_loop_start.loop_id()];
        state_.loop_constants[state_.loop_stack_depth] = loop_constant;
        uint32_t& loop_iterator_ref =
            state_.loop_iterators[state_.loop_stack_depth];
        if (!cf_loop_start.is_repeat()) {
          loop_iterator_ref = 0;
        }
        if (loop_iterator_ref >= loop_constant.count) {
          cf_index_next = cf_loop_start.address();
          continue;
        }
        ++state_.loop_stack_depth;
      } break;

      case ucode::ControlFlowOpcode::kLoopEnd: {
        assert_not_zero(state_.loop_stack_depth);
        if (!state_.loop_stack_depth) {
          continue;
        }
        assert_true(state_.loop_stack_depth <= 4);
        if (state_.loop_stack_depth > 4) {
          --state_.loop_stack_depth;
          continue;
        }
        ucode::ControlFlowLoopEndInstruction cf_loop_end =
            *reinterpret_cast<const ucode::ControlFlowLoopEndInstruction*>(
                &cf_instr);
        xenos::LoopConstant loop_constant =
            state_.loop_constants[state_.loop_stack_depth - 1];
        assert_true(loop_constant.value ==
                    loop_constants[cf_loop_end.loop_id()].value);
        uint32_t loop_iterator =
            ++state_.loop_iterators[state_.loop_stack_depth - 1];
        if (loop_iterator < loop_constant.count &&
            (!cf_loop_end.is_predicated_break() ||
             cf_loop_end.condition() != state_.predicate)) {
          cf_index_next = cf_loop_end.address();
          continue;
        }
        --state_.loop_stack_depth;
      } break;

      case ucode::ControlFlowOpcode::kCondCall: {
        assert_true(state_.call_stack_depth < 4);
        if (state_.call_stack_depth >= 4) {
          continue;
        }
        const ucode::ControlFlowCondCallInstruction cf_cond_call =
            *reinterpret_cast<const ucode::ControlFlowCondCallInstruction*>(
                &cf_instr);
        if (!cf_cond_call.is_unconditional()) {
          if (cf_cond_call.is_predicated()) {
            if (cf_cond_call.condition() != state_.predicate) {
              continue;
            }
          } else {
            uint32_t bool_address = cf_cond_call.bool_address();
            if (cf_cond_call.condition() !=
                ((bool_constants[bool_address >> 5] &
                  (UINT32_C(1) << (bool_address & 31))) != 0)) {
              continue;
            }
          }
        }
        state_.call_return_addresses[state_.call_stack_depth++] = cf_index + 1;
        cf_index_next = cf_cond_call.address();
      } break;

      case ucode::ControlFlowOpcode::kReturn: {
        // No stack depth assertion - skipping the return is a well-defined
        // behavior for `return` outside a function call.
        if (!state_.call_stack_depth) {
          continue;
        }
        cf_index_next = state_.call_return_addresses[--state_.call_stack_depth];
      } break;

      case ucode::ControlFlowOpcode::kCondJmp: {
        const ucode::ControlFlowCondJmpInstruction cf_cond_jmp =
            *reinterpret_cast<const ucode::ControlFlowCondJmpInstruction*>(
                &cf_instr);
        if (!cf_cond_jmp.is_unconditional()) {
          if (cf_cond_jmp.is_predicated()) {
            if (cf_cond_jmp.condition() != state_.predicate) {
              continue;
            }
          } else {
            uint32_t bool_address = cf_cond_jmp.bool_address();
            if (cf_cond_jmp.condition() !=
                ((bool_constants[bool_address >> 5] &
                  (UINT32_C(1) << (bool_address & 31))) != 0)) {
              continue;
            }
          }
        }
        cf_index_next = cf_cond_jmp.address();
      } break;

      case ucode::ControlFlowOpcode::kAlloc: {
        if (export_sink_) {
          const ucode::ControlFlowAllocInstruction& cf_alloc =
              *reinterpret_cast<const ucode::ControlFlowAllocInstruction*>(
                  &cf_instr);
          export_sink_->AllocExport(cf_alloc.alloc_type(), cf_alloc.size());
        }
      } break;

      case ucode::ControlFlowOpcode::kMarkVsFetchDone: {
      } break;

      default:
        assert_unhandled_case(cf_opcode);
    }
  }
}

const float* ShaderInterpreter::GetFloatConstant(
    uint32_t address, bool is_relative, bool relative_address_is_a0) const {
  static const float zero[4] = {};
  int32_t index = int32_t(address);
  if (is_relative) {
    index += relative_address_is_a0 ? state_.address_register
                                    : state_.GetLoopAddress();
  }
  if (index < 0) {
    return zero;
  }
  auto base_and_size_minus_1 = register_file_.Get<reg::SQ_VS_CONST>(
      shader_type_ == xenos::ShaderType::kVertex ? XE_GPU_REG_SQ_VS_CONST
                                                 : XE_GPU_REG_SQ_PS_CONST);
  if (uint32_t(index) > base_and_size_minus_1.size) {
    return zero;
  }
  index += base_and_size_minus_1.base;
  if (index >= 512) {
    return zero;
  }
  return &register_file_[XE_GPU_REG_SHADER_CONSTANT_000_X + 4 * index].f32;
}

void ShaderInterpreter::ExecuteAluInstruction(ucode::AluInstruction instr) {
  // Vector operation.
  float vector_result[4] = {};
  ucode::AluVectorOpcode vector_opcode = instr.vector_opcode();
  const ucode::AluVectorOpcodeInfo& vector_opcode_info =
      ucode::GetAluVectorOpcodeInfo(vector_opcode);
  uint32_t vector_result_write_mask = instr.GetVectorOpResultWriteMask();
  if (vector_result_write_mask || vector_opcode_info.changed_state) {
    float vector_operands[3][4];
    for (uint32_t i = 0; i < 3; ++i) {
      if (!vector_opcode_info.operand_components_used[i]) {
        continue;
      }
      const float* vector_src_ptr;
      uint32_t vector_src_register = instr.src_reg(1 + i);
      bool vector_src_absolute = false;
      if (instr.src_is_temp(1 + i)) {
        vector_src_ptr = GetTempRegister(
            ucode::AluInstruction::src_temp_reg(vector_src_register),
            ucode::AluInstruction::is_src_temp_relative(vector_src_register));
        vector_src_absolute = ucode::AluInstruction::is_src_temp_value_absolute(
            vector_src_register);
      } else {
        vector_src_ptr = GetFloatConstant(
            vector_src_register, instr.src_const_is_addressed(1 + i),
            instr.is_const_address_register_relative());
      }
      uint32_t vector_src_absolute_mask =
          ~(uint32_t(vector_src_absolute) << 31);
      uint32_t vector_src_negate_bit = uint32_t(instr.src_negate(1 + i)) << 31;
      uint32_t vector_src_swizzle = instr.src_swizzle(1 + i);
      for (uint32_t j = 0; j < 4; ++j) {
        float vector_src_component = FlushDenormal(
            vector_src_ptr[ucode::AluInstruction::GetSwizzledComponentIndex(
                vector_src_swizzle, j)]);
        *reinterpret_cast<uint32_t*>(&vector_src_component) =
            (*reinterpret_cast<const uint32_t*>(&vector_src_component) &
             vector_src_absolute_mask) ^
            vector_src_negate_bit;
        vector_operands[i][j] = vector_src_component;
      }
    }

    bool replicate_vector_result_x = false;
    switch (vector_opcode) {
      case ucode::AluVectorOpcode::kAdd: {
        for (uint32_t i = 0; i < 4; ++i) {
          vector_result[i] = vector_operands[0][i] + vector_operands[1][i];
        }
      } break;
      case ucode::AluVectorOpcode::kMul: {
        for (uint32_t i = 0; i < 4; ++i) {
          // Direct3D 9 behavior (0 or denormal * anything = +0).
          vector_result[i] = (vector_operands[0][i] && vector_operands[1][i])
                                 ? vector_operands[0][i] * vector_operands[1][i]
                                 : 0.0f;
        }
      } break;
      case ucode::AluVectorOpcode::kMax: {
        for (uint32_t i = 0; i < 4; ++i) {
          vector_result[i] = vector_operands[0][i] >= vector_operands[1][i]
                                 ? vector_operands[0][i]
                                 : vector_operands[1][i];
        }
      } break;
      case ucode::AluVectorOpcode::kMin: {
        for (uint32_t i = 0; i < 4; ++i) {
          vector_result[i] = vector_operands[0][i] < vector_operands[1][i]
                                 ? vector_operands[0][i]
                                 : vector_operands[1][i];
        }
      } break;
      case ucode::AluVectorOpcode::kSeq: {
        for (uint32_t i = 0; i < 4; ++i) {
          vector_result[i] =
              float(vector_operands[0][i] == vector_operands[1][i]);
        }
      } break;
      case ucode::AluVectorOpcode::kSgt: {
        for (uint32_t i = 0; i < 4; ++i) {
          vector_result[i] =
              float(vector_operands[0][i] > vector_operands[1][i]);
        }
      } break;
      case ucode::AluVectorOpcode::kSge: {
        for (uint32_t i = 0; i < 4; ++i) {
          vector_result[i] =
              float(vector_operands[0][i] >= vector_operands[1][i]);
        }
      } break;
      case ucode::AluVectorOpcode::kSne: {
        for (uint32_t i = 0; i < 4; ++i) {
          vector_result[i] =
              float(vector_operands[0][i] != vector_operands[1][i]);
        }
      } break;
      case ucode::AluVectorOpcode::kFrc: {
        for (uint32_t i = 0; i < 4; ++i) {
          vector_result[i] =
              vector_operands[0][i] - std::floor(vector_operands[0][i]);
        }
      } break;
      case ucode::AluVectorOpcode::kTrunc: {
        for (uint32_t i = 0; i < 4; ++i) {
          vector_result[i] = std::trunc(vector_operands[0][i]);
        }
      } break;
      case ucode::AluVectorOpcode::kFloor: {
        for (uint32_t i = 0; i < 4; ++i) {
          vector_result[i] = std::floor(vector_operands[0][i]);
        }
      } break;
      case ucode::AluVectorOpcode::kMad: {
        for (uint32_t i = 0; i < 4; ++i) {
          // Direct3D 9 behavior (0 or denormal * anything = +0).
          // Doing the addition rather than conditional assignment even for zero
          // operands because +0 + -0 must be +0.
          vector_result[i] =
              ((vector_operands[0][i] && vector_operands[1][i])
                   ? vector_operands[0][i] * vector_operands[1][i]
                   : 0.0f) +
              vector_operands[2][i];
        }
      } break;
      case ucode::AluVectorOpcode::kCndEq: {
        for (uint32_t i = 0; i < 4; ++i) {
          vector_result[i] = vector_operands[0][i] == 0.0f
                                 ? vector_operands[1][i]
                                 : vector_operands[2][i];
        }
      } break;
      case ucode::AluVectorOpcode::kCndGe: {
        for (uint32_t i = 0; i < 4; ++i) {
          vector_result[i] = vector_operands[0][i] >= 0.0f
                                 ? vector_operands[1][i]
                                 : vector_operands[2][i];
        }
      } break;
      case ucode::AluVectorOpcode::kCndGt: {
        for (uint32_t i = 0; i < 4; ++i) {
          vector_result[i] = vector_operands[0][i] > 0.0f
                                 ? vector_operands[1][i]
                                 : vector_operands[2][i];
        }
      } break;
      case ucode::AluVectorOpcode::kDp4: {
        vector_result[0] = 0.0f;
        for (uint32_t i = 0; i < 4; ++i) {
          // Direct3D 9 behavior (0 or denormal * anything = +0).
          // Doing the addition even for zero operands because +0 + -0 must be
          // +0.
          vector_result[0] +=
              (vector_operands[0][i] && vector_operands[1][i])
                  ? vector_operands[0][i] * vector_operands[1][i]
                  : 0.0f;
        }
        replicate_vector_result_x = true;
      } break;
      case ucode::AluVectorOpcode::kDp3: {
        vector_result[0] = 0.0f;
        for (uint32_t i = 0; i < 3; ++i) {
          // Direct3D 9 behavior (0 or denormal * anything = +0).
          // Doing the addition even for zero operands because +0 + -0 must be
          // +0.
          vector_result[0] +=
              (vector_operands[0][i] && vector_operands[1][i])
                  ? vector_operands[0][i] * vector_operands[1][i]
                  : 0.0f;
        }
        replicate_vector_result_x = true;
      } break;
      case ucode::AluVectorOpcode::kDp2Add: {
        // Doing the addition even for zero operands because +0 + -0 must be +0.
        vector_result[0] = 0.0f;
        for (uint32_t i = 0; i < 2; ++i) {
          // Direct3D 9 behavior (0 or denormal * anything = +0).
          vector_result[0] +=
              (vector_operands[0][i] && vector_operands[1][i])
                  ? vector_operands[0][i] * vector_operands[1][i]
                  : 0.0f;
        }
        vector_result[0] += vector_operands[2][0];
        replicate_vector_result_x = true;
      } break;
      case ucode::AluVectorOpcode::kCube: {
        // Operand [0] is .z_xy.
        float x = vector_operands[0][2];
        float y = vector_operands[0][3];
        float z = vector_operands[0][0];
        float x_abs = std::abs(x), y_abs = std::abs(y), z_abs = std::abs(z);
        // Result is T coordinate, S coordinate, 2 * major axis, face ID.
        if (z_abs >= x_abs && z_abs >= y_abs) {
          vector_result[0] = -y;
          vector_result[1] = z < 0.0f ? -x : x;
          vector_result[2] = z;
          vector_result[3] = z < 0.0f ? 5.0f : 4.0f;
        } else if (y_abs >= x_abs) {
          vector_result[0] = y < 0.0f ? -z : z;
          vector_result[1] = x;
          vector_result[2] = y;
          vector_result[3] = y < 0.0f ? 3.0f : 2.0f;
        } else {
          vector_result[0] = -y;
          vector_result[1] = x < 0.0f ? z : -z;
          vector_result[2] = x;
          vector_result[3] = x < 0.0f ? 1.0f : 0.0f;
        }
        vector_result[2] *= 2.0f;
      } break;
      case ucode::AluVectorOpcode::kMax4: {
        if (vector_operands[0][0] >= vector_operands[0][1] &&
            vector_operands[0][0] >= vector_operands[0][2] &&
            vector_operands[0][0] >= vector_operands[0][3]) {
          vector_result[0] = vector_operands[0][0];
        } else if (vector_operands[0][1] >= vector_operands[0][2] &&
                   vector_operands[0][1] >= vector_operands[0][3]) {
          vector_result[0] = vector_operands[0][1];
        } else if (vector_operands[0][2] >= vector_operands[0][3]) {
          vector_result[0] = vector_operands[0][2];
        } else {
          vector_result[0] = vector_operands[0][3];
        }
        replicate_vector_result_x = true;
      } break;
      case ucode::AluVectorOpcode::kSetpEqPush: {
        state_.predicate =
            vector_operands[0][3] == 0.0f && vector_operands[1][3] == 0.0f;
        vector_result[0] =
            (vector_operands[0][0] == 0.0f && vector_operands[1][0] == 0.0f)
                ? 0.0f
                : vector_operands[0][0] + 1.0f;
        replicate_vector_result_x = true;
      } break;
      case ucode::AluVectorOpcode::kSetpNePush: {
        state_.predicate =
            vector_operands[0][3] == 0.0f && vector_operands[1][3] != 0.0f;
        vector_result[0] =
            (vector_operands[0][0] == 0.0f && vector_operands[1][0] != 0.0f)
                ? 0.0f
                : vector_operands[0][0] + 1.0f;
        replicate_vector_result_x = true;
      } break;
      case ucode::AluVectorOpcode::kSetpGtPush: {
        state_.predicate =
            vector_operands[0][3] == 0.0f && vector_operands[1][3] > 0.0f;
        vector_result[0] =
            (vector_operands[0][0] == 0.0f && vector_operands[1][0] > 0.0f)
                ? 0.0f
                : vector_operands[0][0] + 1.0f;
        replicate_vector_result_x = true;
      } break;
      case ucode::AluVectorOpcode::kSetpGePush: {
        state_.predicate =
            vector_operands[0][3] == 0.0f && vector_operands[1][3] >= 0.0f;
        vector_result[0] =
            (vector_operands[0][0] == 0.0f && vector_operands[1][0] >= 0.0f)
                ? 0.0f
                : vector_operands[0][0] + 1.0f;
        replicate_vector_result_x = true;
      } break;
      // Not implementing pixel kill currently, the interpreter is currently
      // used only for vertex shaders.
      case ucode::AluVectorOpcode::kKillEq: {
        vector_result[0] =
            float(vector_operands[0][0] == vector_operands[1][0] ||
                  vector_operands[0][1] == vector_operands[1][1] ||
                  vector_operands[0][2] == vector_operands[1][2] ||
                  vector_operands[0][3] == vector_operands[1][3]);
        replicate_vector_result_x = true;
      } break;
      case ucode::AluVectorOpcode::kKillGt: {
        vector_result[0] =
            float(vector_operands[0][0] > vector_operands[1][0] ||
                  vector_operands[0][1] > vector_operands[1][1] ||
                  vector_operands[0][2] > vector_operands[1][2] ||
                  vector_operands[0][3] > vector_operands[1][3]);
        replicate_vector_result_x = true;
      } break;
      case ucode::AluVectorOpcode::kKillGe: {
        vector_result[0] =
            float(vector_operands[0][0] >= vector_operands[1][0] ||
                  vector_operands[0][1] >= vector_operands[1][1] ||
                  vector_operands[0][2] >= vector_operands[1][2] ||
                  vector_operands[0][3] >= vector_operands[1][3]);
        replicate_vector_result_x = true;
      } break;
      case ucode::AluVectorOpcode::kKillNe: {
        vector_result[0] =
            float(vector_operands[0][0] != vector_operands[1][0] ||
                  vector_operands[0][1] != vector_operands[1][1] ||
                  vector_operands[0][2] != vector_operands[1][2] ||
                  vector_operands[0][3] != vector_operands[1][3]);
        replicate_vector_result_x = true;
      } break;
      case ucode::AluVectorOpcode::kDst: {
        vector_result[0] = 1.0f;
        // Direct3D 9 behavior (0 or denormal * anything = +0).
        vector_result[1] = (vector_operands[0][1] && vector_operands[1][1])
                               ? vector_operands[0][1] * vector_operands[1][1]
                               : 0.0f;
        vector_result[2] = vector_operands[0][2];
        vector_result[3] = vector_operands[1][3];
      } break;
      case ucode::AluVectorOpcode::kMaxA: {
        // std::max is `a < b ? b : a`, thus in case of NaN, the first argument
        // (-256.0f) is always the result.
        state_.address_register = int32_t(std::floor(
            std::min(255.0f, std::max(-256.0f, vector_operands[0][3])) + 0.5f));
        for (uint32_t i = 0; i < 4; ++i) {
          vector_result[i] = vector_operands[0][i] >= vector_operands[1][i]
                                 ? vector_operands[0][i]
                                 : vector_operands[1][i];
        }
      } break;
      default: {
        assert_unhandled_case(vector_opcode);
      }
    }
    if (replicate_vector_result_x) {
      for (uint32_t i = 1; i < 4; ++i) {
        vector_result[i] = vector_result[0];
      }
    }
  }

  // Scalar operation.
  ucode::AluScalarOpcode scalar_opcode = instr.scalar_opcode();
  const ucode::AluScalarOpcodeInfo& scalar_opcode_info =
      ucode::GetAluScalarOpcodeInfo(scalar_opcode);
  float scalar_operands[2];
  uint32_t scalar_operand_component_count = 0;
  bool scalar_src_absolute = false;
  switch (scalar_opcode_info.operand_count) {
    case 1: {
      // r#/c#.w or r#/c#.wx.
      const float* scalar_src_ptr;
      uint32_t scalar_src_register = instr.src_reg(3);
      if (instr.src_is_temp(3)) {
        scalar_src_ptr = GetTempRegister(
            ucode::AluInstruction::src_temp_reg(scalar_src_register),
            ucode::AluInstruction::is_src_temp_relative(scalar_src_register));
        scalar_src_absolute = ucode::AluInstruction::is_src_temp_value_absolute(
            scalar_src_register);
      } else {
        scalar_src_ptr = GetFloatConstant(
            scalar_src_register, instr.src_const_is_addressed(3),
            instr.is_const_address_register_relative());
      }
      uint32_t scalar_src_swizzle = instr.src_swizzle(3);
      scalar_operand_component_count =
          scalar_opcode_info.single_operand_is_two_component ? 2 : 1;
      for (uint32_t i = 0; i < scalar_operand_component_count; ++i) {
        scalar_operands[i] =
            scalar_src_ptr[ucode::AluInstruction::GetSwizzledComponentIndex(
                scalar_src_swizzle, (3 + i) & 3)];
      }
    } break;
    case 2: {
      scalar_operand_component_count = 2;
      uint32_t scalar_src_absolute_mask =
          ~(uint32_t(instr.abs_constants()) << 31);
      uint32_t scalar_src_negate_bit = uint32_t(instr.src_negate(3)) << 31;
      uint32_t scalar_src_swizzle = instr.src_swizzle(3);
      // c#.w.
      scalar_operands[0] =
          GetFloatConstant(instr.src_reg(3), instr.src_const_is_addressed(3),
                           instr.is_const_address_register_relative())
              [ucode::AluInstruction::GetSwizzledComponentIndex(
                  scalar_src_swizzle, 3)];
      // r#.x.
      scalar_operands[1] = GetTempRegister(
          instr.scalar_const_reg_op_src_temp_reg(),
          false)[ucode::AluInstruction::GetSwizzledComponentIndex(
          scalar_src_swizzle, 0)];
    } break;
  }
  if (scalar_operand_component_count) {
    uint32_t scalar_src_absolute_mask = ~(uint32_t(scalar_src_absolute) << 31);
    uint32_t scalar_src_negate_bit = uint32_t(instr.src_negate(3)) << 31;
    for (uint32_t i = 0; i < scalar_operand_component_count; ++i) {
      float scalar_operand = FlushDenormal(scalar_operands[i]);
      *reinterpret_cast<uint32_t*>(&scalar_operand) =
          (*reinterpret_cast<const uint32_t*>(&scalar_operand) &
           scalar_src_absolute_mask) ^
          scalar_src_negate_bit;
      scalar_operands[i] = scalar_operand;
    }
  }
  switch (scalar_opcode) {
    case ucode::AluScalarOpcode::kAdds:
    case ucode::AluScalarOpcode::kAddsc0:
    case ucode::AluScalarOpcode::kAddsc1: {
      state_.previous_scalar = scalar_operands[0] + scalar_operands[1];
    } break;
    case ucode::AluScalarOpcode::kAddsPrev: {
      state_.previous_scalar = scalar_operands[0] + state_.previous_scalar;
    } break;
    case ucode::AluScalarOpcode::kMuls:
    case ucode::AluScalarOpcode::kMulsc0:
    case ucode::AluScalarOpcode::kMulsc1: {
      // Direct3D 9 behavior (0 or denormal * anything = +0).
      state_.previous_scalar = (scalar_operands[0] && scalar_operands[1])
                                   ? scalar_operands[0] * scalar_operands[1]
                                   : 0.0f;
    } break;
    case ucode::AluScalarOpcode::kMulsPrev: {
      // Direct3D 9 behavior (0 or denormal * anything = +0).
      state_.previous_scalar = (scalar_operands[0] && state_.previous_scalar)
                                   ? scalar_operands[0] * state_.previous_scalar
                                   : 0.0f;
    } break;
    case ucode::AluScalarOpcode::kMulsPrev2: {
      if (state_.previous_scalar == -FLT_MAX ||
          !std::isfinite(state_.previous_scalar) ||
          !std::isfinite(scalar_operands[1]) || scalar_operands[1] <= 0.0f) {
        state_.previous_scalar = -FLT_MAX;
      } else {
        // Direct3D 9 behavior (0 or denormal * anything = +0).
        state_.previous_scalar =
            (scalar_operands[0] && state_.previous_scalar)
                ? scalar_operands[0] * state_.previous_scalar
                : 0.0f;
      }
    } break;
    case ucode::AluScalarOpcode::kMaxs: {
      state_.previous_scalar = scalar_operands[0] >= scalar_operands[1]
                                   ? scalar_operands[0]
                                   : scalar_operands[1];
    } break;
    case ucode::AluScalarOpcode::kMins: {
      state_.previous_scalar = scalar_operands[0] >= scalar_operands[1]
                                   ? scalar_operands[0]
                                   : scalar_operands[1];
    } break;
    case ucode::AluScalarOpcode::kSeqs: {
      state_.previous_scalar = float(scalar_operands[0] == 0.0f);
    } break;
    case ucode::AluScalarOpcode::kSgts: {
      state_.previous_scalar = float(scalar_operands[0] > 0.0f);
    } break;
    case ucode::AluScalarOpcode::kSges: {
      state_.previous_scalar = float(scalar_operands[0] >= 0.0f);
    } break;
    case ucode::AluScalarOpcode::kSnes: {
      state_.previous_scalar = float(scalar_operands[0] != 0.0f);
    } break;
    case ucode::AluScalarOpcode::kFrcs: {
      state_.previous_scalar =
          scalar_operands[0] - std::floor(scalar_operands[0]);
    } break;
    case ucode::AluScalarOpcode::kTruncs: {
      state_.previous_scalar = std::trunc(scalar_operands[0]);
    } break;
    case ucode::AluScalarOpcode::kFloors: {
      state_.previous_scalar = std::floor(scalar_operands[0]);
    } break;
    case ucode::AluScalarOpcode::kExp: {
      state_.previous_scalar = std::exp2(scalar_operands[0]);
    } break;
    case ucode::AluScalarOpcode::kLogc: {
      state_.previous_scalar = std::log2(scalar_operands[0]);
      if (state_.previous_scalar == -INFINITY) {
        state_.previous_scalar = -FLT_MAX;
      }
    } break;
    case ucode::AluScalarOpcode::kLog: {
      state_.previous_scalar = std::log2(scalar_operands[0]);
    } break;
    case ucode::AluScalarOpcode::kRcpc: {
      state_.previous_scalar = 1.0f / scalar_operands[0];
      if (state_.previous_scalar == -INFINITY) {
        state_.previous_scalar = -FLT_MAX;
      } else if (state_.previous_scalar == INFINITY) {
        state_.previous_scalar = FLT_MAX;
      }
    } break;
    case ucode::AluScalarOpcode::kRcpf: {
      state_.previous_scalar = 1.0f / scalar_operands[0];
      if (state_.previous_scalar == -INFINITY) {
        state_.previous_scalar = -0.0f;
      } else if (state_.previous_scalar == INFINITY) {
        state_.previous_scalar = 0.0f;
      }
    } break;
    case ucode::AluScalarOpcode::kRcp: {
      state_.previous_scalar = 1.0f / scalar_operands[0];
    } break;
    case ucode::AluScalarOpcode::kRsqc: {
      state_.previous_scalar = 1.0f / std::sqrt(scalar_operands[0]);
      if (state_.previous_scalar == -INFINITY) {
        state_.previous_scalar = -FLT_MAX;
      } else if (state_.previous_scalar == INFINITY) {
        state_.previous_scalar = FLT_MAX;
      }
    } break;
    case ucode::AluScalarOpcode::kRsqf: {
      state_.previous_scalar = 1.0f / std::sqrt(scalar_operands[0]);
      if (state_.previous_scalar == -INFINITY) {
        state_.previous_scalar = -0.0f;
      } else if (state_.previous_scalar == INFINITY) {
        state_.previous_scalar = 0.0f;
      }
    } break;
    case ucode::AluScalarOpcode::kRsq: {
      state_.previous_scalar = 1.0f / std::sqrt(scalar_operands[0]);
    } break;
    case ucode::AluScalarOpcode::kMaxAs: {
      // std::max is `a < b ? b : a`, thus in case of NaN, the first argument
      // (-256.0f) is always the result.
      state_.address_register = int32_t(std::floor(
          std::min(255.0f, std::max(-256.0f, scalar_operands[0])) + 0.5f));
      state_.previous_scalar = scalar_operands[0] >= scalar_operands[1]
                                   ? scalar_operands[0]
                                   : scalar_operands[1];
    } break;
    case ucode::AluScalarOpcode::kMaxAsf: {
      // std::max is `a < b ? b : a`, thus in case of NaN, the first argument
      // (-256.0f) is always the result.
      state_.address_register = int32_t(
          std::floor(std::min(255.0f, std::max(-256.0f, scalar_operands[0]))));
      state_.previous_scalar = scalar_operands[0] >= scalar_operands[1]
                                   ? scalar_operands[0]
                                   : scalar_operands[1];
    } break;
    case ucode::AluScalarOpcode::kSubs:
    case ucode::AluScalarOpcode::kSubsc0:
    case ucode::AluScalarOpcode::kSubsc1: {
      state_.previous_scalar = scalar_operands[0] - scalar_operands[1];
    } break;
    case ucode::AluScalarOpcode::kSubsPrev: {
      state_.previous_scalar = scalar_operands[0] - state_.previous_scalar;
    } break;
    case ucode::AluScalarOpcode::kSetpEq: {
      state_.predicate = scalar_operands[0] == 0.0f;
      state_.previous_scalar = float(!state_.predicate);
    } break;
    case ucode::AluScalarOpcode::kSetpNe: {
      state_.predicate = scalar_operands[0] != 0.0f;
      state_.previous_scalar = float(!state_.predicate);
    } break;
    case ucode::AluScalarOpcode::kSetpGt: {
      state_.predicate = scalar_operands[0] > 0.0f;
      state_.previous_scalar = float(!state_.predicate);
    } break;
    case ucode::AluScalarOpcode::kSetpGe: {
      state_.predicate = scalar_operands[0] >= 0.0f;
      state_.previous_scalar = float(!state_.predicate);
    } break;
    case ucode::AluScalarOpcode::kSetpInv: {
      state_.predicate = scalar_operands[0] == 1.0f;
      state_.previous_scalar =
          state_.predicate
              ? 0.0f
              : (scalar_operands[0] == 0.0f ? 1.0f : scalar_operands[0]);
    } break;
    case ucode::AluScalarOpcode::kSetpPop: {
      float new_counter = scalar_operands[0] - 1.0f;
      state_.predicate = new_counter <= 0.0f;
      state_.previous_scalar = state_.predicate ? 0.0f : new_counter;
    } break;
    case ucode::AluScalarOpcode::kSetpClr: {
      state_.predicate = false;
      state_.previous_scalar = FLT_MAX;
    } break;
    case ucode::AluScalarOpcode::kSetpRstr: {
      state_.predicate = scalar_operands[0] == 0.0f;
      state_.previous_scalar = state_.predicate ? 0.0f : scalar_operands[0];
    } break;
    // Not implementing pixel kill currently, the interpreter is currently used
    // only for vertex shaders.
    case ucode::AluScalarOpcode::kKillsEq: {
      state_.previous_scalar = float(scalar_operands[0] == 0.0f);
    } break;
    case ucode::AluScalarOpcode::kKillsGt: {
      state_.previous_scalar = float(scalar_operands[0] > 0.0f);
    } break;
    case ucode::AluScalarOpcode::kKillsGe: {
      state_.previous_scalar = float(scalar_operands[0] >= 0.0f);
    } break;
    case ucode::AluScalarOpcode::kKillsNe: {
      state_.previous_scalar = float(scalar_operands[0] != 0.0f);
    } break;
    case ucode::AluScalarOpcode::kKillsOne: {
      state_.previous_scalar = float(scalar_operands[0] == 1.0f);
    } break;
    case ucode::AluScalarOpcode::kSqrt: {
      state_.previous_scalar = std::sqrt(scalar_operands[0]);
    } break;
    case ucode::AluScalarOpcode::kSin: {
      state_.previous_scalar = std::sin(scalar_operands[0]);
    } break;
    case ucode::AluScalarOpcode::kCos: {
      state_.previous_scalar = std::cos(scalar_operands[0]);
    } break;
    case ucode::AluScalarOpcode::kRetainPrev: {
    } break;
    default: {
      assert_unhandled_case(scalar_opcode);
    }
  }

  if (instr.vector_clamp()) {
    for (uint32_t i = 0; i < 4; ++i) {
      vector_result[i] = xe::saturate_unsigned(vector_result[i]);
    }
  }
  float scalar_result = instr.scalar_clamp()
                            ? xe::saturate_unsigned(state_.previous_scalar)
                            : state_.previous_scalar;

  uint32_t scalar_result_write_mask = instr.GetScalarOpResultWriteMask();
  if (instr.is_export()) {
    if (export_sink_) {
      float export_value[4];
      uint32_t export_constant_1_mask = instr.GetConstant1WriteMask();
      uint32_t export_mask =
          vector_result_write_mask | scalar_result_write_mask |
          instr.GetConstant0WriteMask() | export_constant_1_mask;
      for (uint32_t i = 0; i < 4; ++i) {
        uint32_t export_component_bit = UINT32_C(1) << i;
        float export_component = 0.0f;
        if (vector_result_write_mask & export_component_bit) {
          export_component = vector_result[i];
        } else if (scalar_result_write_mask & export_component_bit) {
          export_component = scalar_result;
        } else if (export_constant_1_mask & export_component_bit) {
          export_component = 1.0f;
        } else {
          export_component = 0.0f;
        }
        export_value[i] = export_component;
      }
      export_sink_->Export(
          ucode::ExportRegister(instr.vector_dest()), export_value,
          vector_result_write_mask | scalar_result_write_mask |
              instr.GetConstant0WriteMask() | export_constant_1_mask);
    }
  } else {
    if (vector_result_write_mask) {
      float* vector_dest =
          GetTempRegister(instr.vector_dest(), instr.is_vector_dest_relative());
      for (uint32_t i = 0; i < 4; ++i) {
        if (vector_result_write_mask & (UINT32_C(1) << i)) {
          vector_dest[i] = vector_result[i];
        }
      }
    }
    if (scalar_result_write_mask) {
      float* scalar_dest =
          GetTempRegister(instr.scalar_dest(), instr.is_scalar_dest_relative());
      for (uint32_t i = 0; i < 4; ++i) {
        if (scalar_result_write_mask & (UINT32_C(1) << i)) {
          scalar_dest[i] = scalar_result;
        }
      }
    }
  }
}

void ShaderInterpreter::StoreFetchResult(uint32_t dest, bool is_dest_relative,
                                         uint32_t swizzle, const float* value) {
  float* dest_data = GetTempRegister(dest, is_dest_relative);
  for (uint32_t i = 0; i < 4; ++i) {
    ucode::FetchDestinationSwizzle component_swizzle =
        ucode::GetFetchDestinationComponentSwizzle(swizzle, i);
    switch (component_swizzle) {
      case ucode::FetchDestinationSwizzle::kX:
        dest_data[i] = value[0];
        break;
      case ucode::FetchDestinationSwizzle::kY:
        dest_data[i] = value[1];
        break;
      case ucode::FetchDestinationSwizzle::kZ:
        dest_data[i] = value[2];
        break;
      case ucode::FetchDestinationSwizzle::kW:
        dest_data[i] = value[3];
        break;
      case ucode::FetchDestinationSwizzle::k1:
        dest_data[i] = 1.0f;
        break;
      case ucode::FetchDestinationSwizzle::kKeep:
        break;
      default:
        // ucode::FetchDestinationSwizzle::k0 or the invalid swizzle 6.
        // TODO(Triang3l): Find the correct handling of the invalid swizzle 6.
        assert_true(component_swizzle == ucode::FetchDestinationSwizzle::k0);
        dest_data[i] = 0.0f;
        break;
    }
  }
}

void ShaderInterpreter::ExecuteVertexFetchInstruction(
    ucode::VertexFetchInstruction instr) {
  // FIXME(Triang3l): Bit scan loops over components cause a link-time
  // optimization internal error in Visual Studio 2019, mainly in the format
  // unpacking. Using loops with up to 4 iterations here instead.

  if (!instr.is_mini_fetch()) {
    state_.vfetch_full_last = instr;
  }

  xenos::xe_gpu_vertex_fetch_t fetch_constant =
      *reinterpret_cast<const xenos::xe_gpu_vertex_fetch_t*>(
          &register_file_[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 +
                          state_.vfetch_full_last.fetch_constant_index()]);

  if (!instr.is_mini_fetch()) {
    // Get the part of the address that depends on vfetch_full data.
    uint32_t vertex_index = uint32_t(std::floor(
        GetTempRegister(instr.src(),
                        instr.is_src_relative())[instr.src_swizzle()] +
        (instr.is_index_rounded() ? 0.5f : 0.0f)));
    state_.vfetch_address_dwords =
        instr.stride() * vertex_index + fetch_constant.address;
  }

  // TODO(Triang3l): Find the default values for unused components.
  float result[4] = {};
  uint32_t dest_swizzle = instr.dest_swizzle();
  uint32_t used_result_components = 0b0000;
  for (uint32_t i = 0; i < 4; ++i) {
    uint32_t dest_component_swizzle = (dest_swizzle >> (3 * i)) & 0b111;
    if (dest_component_swizzle <= 3) {
      used_result_components |= UINT32_C(1) << dest_component_swizzle;
    }
  }
  uint32_t needed_dwords = xenos::GetVertexFormatNeededWords(
      instr.data_format(), used_result_components);
  if (needed_dwords) {
    uint32_t data[4] = {};
    const uint32_t* memory_dwords =
        reinterpret_cast<const uint32_t*>(memory_.physical_membase());
    uint32_t buffer_end_dwords = fetch_constant.address + fetch_constant.size;
    uint32_t dword_0_address_dwords =
        uint32_t(int32_t(state_.vfetch_address_dwords) + instr.offset());
    for (uint32_t i = 0; i < 4; ++i) {
      if (!(needed_dwords & (UINT32_C(1) << i))) {
        continue;
      }
      uint32_t dword_value = 0;
      uint32_t dword_address_dwords = dword_0_address_dwords + i;
      if (dword_address_dwords >= fetch_constant.address &&
          dword_address_dwords < buffer_end_dwords) {
        if (trace_writer_) {
          trace_writer_->WriteMemoryRead(
              sizeof(uint32_t) * dword_address_dwords, sizeof(uint32_t));
        }
        dword_value = xenos::GpuSwap(memory_dwords[dword_address_dwords],
                                     fetch_constant.endian);
      }
      data[i] = dword_value;
    }

    uint32_t packed_components = 0b0000;
    uint32_t packed_widths[4], packed_offsets[4];
    uint32_t packed_dwords[] = {data[0], data[0]};
    switch (instr.data_format()) {
      case xenos::VertexFormat::k_8_8_8_8: {
        packed_components = 0b1111;
        packed_widths[0] = packed_widths[1] = packed_widths[2] =
            packed_widths[3] = 8;
        packed_offsets[1] = 8;
        packed_offsets[2] = 16;
        packed_offsets[3] = 24;
      } break;
      case xenos::VertexFormat::k_2_10_10_10: {
        packed_components = 0b1111;
        packed_widths[0] = packed_widths[1] = packed_widths[2] = 10;
        packed_widths[3] = 2;
        packed_offsets[1] = 10;
        packed_offsets[2] = 20;
        packed_offsets[3] = 30;
      } break;
      case xenos::VertexFormat::k_10_11_11: {
        packed_components = 0b0111;
        packed_widths[0] = packed_widths[1] = 11;
        packed_widths[2] = 10;
        packed_offsets[1] = 11;
        packed_offsets[2] = 22;
      } break;
      case xenos::VertexFormat::k_11_11_10: {
        packed_components = 0b0111;
        packed_widths[0] = 10;
        packed_widths[1] = packed_widths[2] = 11;
        packed_offsets[1] = 10;
        packed_offsets[2] = 21;
      } break;
      case xenos::VertexFormat::k_16_16: {
        packed_components = 0b0011;
        packed_widths[0] = packed_widths[1] = 16;
        packed_offsets[1] = 16;
      } break;
      case xenos::VertexFormat::k_16_16_16_16: {
        packed_components = 0b1111;
        packed_widths[0] = packed_widths[1] = packed_widths[2] =
            packed_widths[3] = 16;
        packed_offsets[1] = packed_offsets[3] = 16;
        packed_dwords[1] = data[1];
      } break;
      case xenos::VertexFormat::k_16_16_16_16_FLOAT: {
        if (used_result_components & 0b1000) {
          result[3] = xe::xenos_half_to_float(uint16_t(data[1] >> 16));
        }
        if (used_result_components & 0b0100) {
          result[2] = xe::xenos_half_to_float(uint16_t(data[1]));
        }
      }
        [[fallthrough]];
      case xenos::VertexFormat::k_16_16_FLOAT: {
        if (used_result_components & 0b0010) {
          result[1] = xe::xenos_half_to_float(uint16_t(data[0] >> 16));
        }
        if (used_result_components & 0b0001) {
          result[0] = xe::xenos_half_to_float(uint16_t(data[0]));
        }
      } break;
      case xenos::VertexFormat::k_32:
      case xenos::VertexFormat::k_32_32:
      case xenos::VertexFormat::k_32_32_32_32: {
        if (instr.is_signed()) {
          for (uint32_t i = 0; i < 4; ++i) {
            result[i] = float(int32_t(data[i]));
          }
          if (instr.is_normalized()) {
            if (instr.signed_rf_mode() ==
                xenos::SignedRepeatingFractionMode::kNoZero) {
              for (uint32_t i = 0; i < 4; ++i) {
                result[i] = (result[i] + 0.5f) / 2147483647.5f;
              }
            } else {
              for (uint32_t i = 0; i < 4; ++i) {
                result[i] /= 2147483647.0f;
                // No need to clamp to -1 if signed - the smallest value will be
                // -2^23 / 2^23 due to rounding.
              }
            }
          }
        } else {
          for (uint32_t i = 0; i < 4; ++i) {
            result[i] = float(data[i]);
          }
          if (instr.is_normalized()) {
            for (uint32_t i = 0; i < 4; ++i) {
              result[i] /= 4294967295.0f;
            }
          }
        }
      } break;
      case xenos::VertexFormat::k_32_FLOAT:
      case xenos::VertexFormat::k_32_32_FLOAT:
      case xenos::VertexFormat::k_32_32_32_32_FLOAT:
      case xenos::VertexFormat::k_32_32_32_FLOAT: {
        for (uint32_t i = 0; i < 4; ++i) {
          result[i] = *reinterpret_cast<const float*>(&data[i]);
        }
      } break;
      default:
        assert_unhandled_case(instr.data_format());
        break;
    }

    packed_components &= used_result_components;
    if (packed_components) {
      if (instr.is_signed()) {
        for (uint32_t i = 0; i < 4; ++i) {
          if (!(packed_components & (UINT32_C(1) << i))) {
            continue;
          }
          uint32_t packed_width = packed_widths[i];
          result[i] = float(int32_t(packed_dwords[i >> 1])
                                << (32 - (packed_width + packed_offsets[i])) >>
                            (32 - packed_width));
        }
        if (instr.is_normalized()) {
          if (instr.signed_rf_mode() ==
              xenos::SignedRepeatingFractionMode::kNoZero) {
            for (uint32_t i = 0; i < 4; ++i) {
              if (!(packed_components & (UINT32_C(1) << i))) {
                continue;
              }
              result[i] = (result[i] + 0.5f) * 2.0f /
                          float((UINT32_C(1) << packed_widths[i]) - 1);
            }
          } else {
            for (uint32_t i = 0; i < 4; ++i) {
              if (!(packed_components & (UINT32_C(1) << i))) {
                continue;
              }
              result[i] = std::max(
                  -1.0f,
                  result[i] /
                      float((UINT32_C(1) << (packed_widths[i] - 1)) - 1));
            }
          }
        }
      } else {
        for (uint32_t i = 0; i < 4; ++i) {
          if (!(packed_components & (UINT32_C(1) << i))) {
            continue;
          }
          uint32_t packed_width = packed_widths[i];
          result[i] = float(packed_dwords[i >> 1] &
                            ((UINT32_C(1) << packed_widths[i]) - 1));
        }
        if (instr.is_normalized()) {
          for (uint32_t i = 0; i < 4; ++i) {
            if (!(packed_components & (UINT32_C(1) << i))) {
              continue;
            }
            result[i] /= float((UINT32_C(1) << packed_widths[i]) - 1);
          }
        }
      }
    }
  }

  int32_t exp_adjust = instr.exp_adjust();
  if (exp_adjust) {
    float exp_adjust_factor = std::ldexp(1.0f, exp_adjust);
    for (uint32_t i = 0; i < 4; ++i) {
      result[i] *= exp_adjust_factor;
    }
  }

  StoreFetchResult(instr.dest(), instr.is_dest_relative(), instr.dest_swizzle(),
                   result);
}

}  // namespace gpu
}  // namespace xe
