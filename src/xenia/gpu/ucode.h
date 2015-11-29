/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_UCODE_H_
#define XENIA_GPU_UCODE_H_

#include <cstdint>

#include "xenia/base/assert.h"
#include "xenia/base/platform.h"
#include "xenia/gpu/xenos.h"

// Closest AMD doc:
// http://developer.amd.com/wordpress/media/2012/10/R600_Instruction_Set_Architecture.pdf
// Microcode format differs, but most fields/enums are the same.

// This code comes from the freedreno project:
// https://github.com/freedreno/freedreno/blob/master/includes/instr-a2xx.h
/*
 * Copyright (c) 2012 Rob Clark <robdclark@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

namespace xe {
namespace gpu {
namespace ucode {

// Defines control flow opcodes used to schedule instructions.
enum class ControlFlowOpcode : uint32_t {
  // No-op - used to fill space.
  kNop = 0,
  // Executes fetch or ALU instructions.
  kExec = 1,
  // Executes fetch or ALU instructions then ends execution.
  kExecEnd = 2,
  // Conditionally executes based on a bool const.
  kCondExec = 3,
  // Conditionally executes based on a bool const then ends execution.
  kCondExecEnd = 4,
  // Conditionally executes based on the current predicate.
  kCondExecPred = 5,
  // Conditionally executes based on the current predicate then ends execution.
  kCondExecPredEnd = 6,
  // Starts a loop that must be terminated with kLoopEnd.
  kLoopStart = 7,
  // Continues or breaks out of a loop started with kLoopStart.
  kLoopEnd = 8,
  // Conditionally calls a function.
  // A return address is pushed to the stack to be used by a kReturn.
  kCondCall = 9,
  // Returns from the current function as called by kCondCall.
  // This is a no-op if not in a function.
  kReturn = 10,
  // Conditionally jumps to an arbitrary address based on a bool const.
  kCondJmp = 11,
  // Allocates output values.
  kAlloc = 12,
  // Conditionally executes based on the current predicate.
  // Optionally resets the predicate value.
  kCondExecPredClean = 13,
  // Conditionally executes based on the current predicate then ends execution.
  // Optionally resets the predicate value.
  kCondExecPredCleanEnd = 14,
  // Hints that no more vertex fetches will be performed.
  kMarkVsFetchDone = 15,
};

// Returns true if the given control flow opcode executes ALU or fetch
// instructions.
constexpr bool IsControlFlowOpcodeExec(ControlFlowOpcode opcode) {
  return opcode == ControlFlowOpcode::kExec ||
         opcode == ControlFlowOpcode::kExecEnd ||
         opcode == ControlFlowOpcode::kCondExec ||
         opcode == ControlFlowOpcode::kCondExecEnd ||
         opcode == ControlFlowOpcode::kCondExecPred ||
         opcode == ControlFlowOpcode::kCondExecPredEnd ||
         opcode == ControlFlowOpcode::kCondExecPredClean ||
         opcode == ControlFlowOpcode::kCondExecPredCleanEnd;
}

// Determines whether addressing is based on a0 or aL.
enum class AddressingMode : uint32_t {
  // Indexing into register sets is done based on aL.
  // This allows forms like c[aL + 5].
  kRelative = 0,
  // Indexing into register sets is done based on a0.
  // This allows forms like c[a0 + 5].
  kAbsolute = 1,
};

// Defines the type of a ControlFlowOpcode::kAlloc instruction.
// The allocation is just a size reservation and there may be multiple in a
// shader.
enum class AllocType : uint32_t {
  // ?
  kNoAlloc = 0,
  // Vertex shader exports a position.
  kVsPosition = 1,
  // Vertex shader exports interpolators.
  kVsInterpolators = 2,
  // Pixel shader exports colors.
  kPsColors = 2,
  // MEMEXPORT?
  kMemory = 3,
};

// Instruction data for ControlFlowOpcode::kExec and kExecEnd.
struct ControlFlowExecInstruction {
  ControlFlowOpcode opcode() const {
    return static_cast<ControlFlowOpcode>(data_.opcode);
  }
  AddressingMode addressing_mode() const {
    return static_cast<AddressingMode>(data_.address_mode);
  }
  // Address of the instructions to execute.
  uint32_t address() const { return data_.address; }
  // Number of instructions being executed.
  uint32_t count() const { return data_.count; }
  // Sequence bits, 2 per instruction, indicating whether ALU or fetch.
  uint32_t sequence() const { return data_.serialize; }
  // Whether to reset the current predicate.
  bool clean() const { return data_.clean == 1; }
  // ?
  bool is_yield() const { return data_.is_yeild == 1; }

 private:
  XEPACKEDSTRUCT(Data, {
    XEPACKEDSTRUCTANONYMOUS({
      uint32_t address : 12;
      uint32_t count : 3;
      uint32_t is_yeild : 1;
      uint32_t serialize : 12;
      uint32_t vc_hi : 4;  // Vertex cache?
    });
    XEPACKEDSTRUCTANONYMOUS({
      uint32_t vc_lo : 2;
      uint32_t unused_0 : 7;
      uint32_t clean : 1;
      uint32_t unused_1 : 1;
      uint32_t address_mode : 1;
      uint32_t opcode : 4;
    });
  });
  Data data_;
};
static_assert_size(ControlFlowExecInstruction, 8);

// Instruction data for ControlFlowOpcode::kCondExec and kCondExecEnd.
struct ControlFlowCondExecInstruction {
  ControlFlowOpcode opcode() const {
    return static_cast<ControlFlowOpcode>(data_.opcode);
  }
  AddressingMode addressing_mode() const {
    return static_cast<AddressingMode>(data_.address_mode);
  }
  // Address of the instructions to execute.
  uint32_t address() const { return data_.address; }
  // Number of instructions being executed.
  uint32_t count() const { return data_.count; }
  // Sequence bits, 2 per instruction, indicating whether ALU or fetch.
  uint32_t sequence() const { return data_.serialize; }
  // Constant index used as the conditional.
  uint32_t bool_address() const { return data_.bool_address; }
  // Required condition value of the comparision (true or false).
  bool condition() const { return data_.condition == 1; }
  // ?
  bool is_yield() const { return data_.is_yeild == 1; }

 private:
  XEPACKEDSTRUCT(Data, {
    XEPACKEDSTRUCTANONYMOUS({
      uint32_t address : 12;
      uint32_t count : 3;
      uint32_t is_yeild : 1;
      uint32_t serialize : 12;
      uint32_t vc_hi : 4;  // Vertex cache?
    });
    XEPACKEDSTRUCTANONYMOUS({
      uint32_t vc_lo : 2;
      uint32_t bool_address : 8;
      uint32_t condition : 1;
      uint32_t address_mode : 1;
      uint32_t opcode : 4;
    });
  });
  Data data_;
};
static_assert_size(ControlFlowCondExecInstruction, 8);

// Instruction data for ControlFlowOpcode::kCondExecPred, kCondExecPredEnd,
// kCondExecPredClean, kCondExecPredCleanEnd.
struct ControlFlowCondExecPredInstruction {
  ControlFlowOpcode opcode() const {
    return static_cast<ControlFlowOpcode>(data_.opcode);
  }
  AddressingMode addressing_mode() const {
    return static_cast<AddressingMode>(data_.address_mode);
  }
  // Address of the instructions to execute.
  uint32_t address() const { return data_.address; }
  // Number of instructions being executed.
  uint32_t count() const { return data_.count; }
  // Sequence bits, 2 per instruction, indicating whether ALU or fetch.
  uint32_t sequence() const { return data_.serialize; }
  // Whether to reset the current predicate.
  bool clean() const { return data_.clean == 1; }
  // Required condition value of the comparision (true or false).
  bool condition() const { return data_.condition == 1; }
  // ?
  bool is_yield() const { return data_.is_yeild == 1; }

 private:
  XEPACKEDSTRUCT(Data, {
    XEPACKEDSTRUCTANONYMOUS({
      uint32_t address : 12;
      uint32_t count : 3;
      uint32_t is_yeild : 1;
      uint32_t serialize : 12;
      uint32_t vc_hi : 4;  // Vertex cache?
    });
    XEPACKEDSTRUCTANONYMOUS({
      uint32_t vc_lo : 2;
      uint32_t unused_0 : 7;
      uint32_t clean : 1;
      uint32_t condition : 1;
      uint32_t address_mode : 1;
      uint32_t opcode : 4;
    });
  });
  Data data_;
};
static_assert_size(ControlFlowCondExecPredInstruction, 8);

// Instruction data for ControlFlowOpcode::kLoopStart.
struct ControlFlowLoopStartInstruction {
  ControlFlowOpcode opcode() const {
    return static_cast<ControlFlowOpcode>(data_.opcode);
  }
  AddressingMode addressing_mode() const {
    return static_cast<AddressingMode>(data_.address_mode);
  }
  // Target address to jump to when skipping the loop.
  uint32_t address() const { return data_.address; }
  // Whether to reuse the current aL instead of reset it to loop start.
  bool is_repeat() const { return data_.is_repeat; }
  // Integer constant register that holds the loop parameters.
  // Byte-wise: [loop count, start, step [-128, 127], ?]
  uint32_t loop_id() const { return data_.loop_id; }

 private:
  XEPACKEDSTRUCT(Data, {
    XEPACKEDSTRUCTANONYMOUS({
      uint32_t address : 13;
      uint32_t is_repeat : 1;
      uint32_t unused_0 : 2;
      uint32_t loop_id : 5;
      uint32_t unused_1 : 11;
    });
    XEPACKEDSTRUCTANONYMOUS({
      uint32_t unused_2 : 11;
      uint32_t address_mode : 1;
      uint32_t opcode : 4;
    });
  });
  Data data_;
};
static_assert_size(ControlFlowLoopStartInstruction, 8);

// Instruction data for ControlFlowOpcode::kLoopEnd.
struct ControlFlowLoopEndInstruction {
  ControlFlowOpcode opcode() const {
    return static_cast<ControlFlowOpcode>(data_.opcode);
  }
  AddressingMode addressing_mode() const {
    return static_cast<AddressingMode>(data_.address_mode);
  }
  // Target address of the start of the loop body.
  uint32_t address() const { return data_.address; }
  // Integer constant register that holds the loop parameters.
  // Byte-wise: [loop count, start, step [-128, 127], ?]
  uint32_t loop_id() const { return data_.loop_id; }
  // Break from the loop if the predicate matches the expected value.
  bool is_predicated_break() const { return data_.is_predicated_break; }
  // Required condition value of the comparision (true or false).
  bool condition() const { return data_.condition == 1; }

 private:
  XEPACKEDSTRUCT(Data, {
    XEPACKEDSTRUCTANONYMOUS({
      uint32_t address : 13;
      uint32_t unused_0 : 3;
      uint32_t loop_id : 5;
      uint32_t is_predicated_break : 1;
      uint32_t unused_1 : 10;
    });
    XEPACKEDSTRUCTANONYMOUS({
      uint32_t unused_2 : 10;
      uint32_t condition : 1;
      uint32_t address_mode : 1;
      uint32_t opcode : 4;
    });
  });
  Data data_;
};
static_assert_size(ControlFlowLoopEndInstruction, 8);

// Instruction data for ControlFlowOpcode::kCondCall.
struct ControlFlowCondCallInstruction {
  ControlFlowOpcode opcode() const {
    return static_cast<ControlFlowOpcode>(data_.opcode);
  }
  AddressingMode addressing_mode() const {
    return static_cast<AddressingMode>(data_.address_mode);
  }
  // Target address.
  uint32_t address() const { return data_.address; }
  // Unconditional call - ignores condition/predication.
  bool is_unconditional() const { return data_.is_unconditional; }
  // Whether the call is predicated (or conditional).
  bool is_predicated() const { return data_.is_predicated; }
  // Constant index used as the conditional.
  uint32_t bool_address() const { return data_.bool_address; }
  // Required condition value of the comparision (true or false).
  bool condition() const { return data_.condition == 1; }

 private:
  XEPACKEDSTRUCT(Data, {
    XEPACKEDSTRUCTANONYMOUS({
      uint32_t address : 13;
      uint32_t is_unconditional : 1;
      uint32_t is_predicated : 1;
      uint32_t unused_0 : 17;
    });
    XEPACKEDSTRUCTANONYMOUS({
      uint32_t unused_1 : 2;
      uint32_t bool_address : 8;
      uint32_t condition : 1;
      uint32_t address_mode : 1;
      uint32_t opcode : 4;
    });
  });
  Data data_;
};
static_assert_size(ControlFlowCondCallInstruction, 8);

// Instruction data for ControlFlowOpcode::kReturn.
struct ControlFlowReturnInstruction {
  ControlFlowOpcode opcode() const {
    return static_cast<ControlFlowOpcode>(data_.opcode);
  }
  AddressingMode addressing_mode() const {
    return static_cast<AddressingMode>(data_.address_mode);
  }

 private:
  XEPACKEDSTRUCT(Data, {
    uint32_t unused_0;
    XEPACKEDSTRUCTANONYMOUS({
      uint32_t unused_1 : 11;
      AddressingMode address_mode : 1;
      ControlFlowOpcode opcode : 4;
    });
  });
  Data data_;
};
static_assert_size(ControlFlowReturnInstruction, 8);

// Instruction data for ControlFlowOpcode::kCondJmp.
struct ControlFlowCondJmpInstruction {
  ControlFlowOpcode opcode() const {
    return static_cast<ControlFlowOpcode>(data_.opcode);
  }
  AddressingMode addressing_mode() const {
    return static_cast<AddressingMode>(data_.address_mode);
  }
  // Target address.
  uint32_t address() const { return data_.address; }
  // Unconditional jump - ignores condition/predication.
  bool is_unconditional() const { return data_.is_unconditional; }
  // Whether the jump is predicated (or conditional).
  bool is_predicated() const { return data_.is_predicated; }
  // Constant index used as the conditional.
  uint32_t bool_address() const { return data_.bool_address; }
  // Required condition value of the comparision (true or false).
  bool condition() const { return data_.condition == 1; }

 private:
  XEPACKEDSTRUCT(Data, {
    XEPACKEDSTRUCTANONYMOUS({
      uint32_t address : 13;
      uint32_t is_unconditional : 1;
      uint32_t is_predicated : 1;
      uint32_t unused_0 : 17;
    });
    XEPACKEDSTRUCTANONYMOUS({
      uint32_t unused_1 : 1;
      uint32_t direction : 1;
      uint32_t bool_address : 8;
      uint32_t condition : 1;
      uint32_t address_mode : 1;
      uint32_t opcode : 4;
    });
  });
  Data data_;
};
static_assert_size(ControlFlowCondJmpInstruction, 8);

// Instruction data for ControlFlowOpcode::kAlloc.
struct ControlFlowAllocInstruction {
  ControlFlowOpcode opcode() const {
    return static_cast<ControlFlowOpcode>(data_.opcode);
  }
  // The total number of the given type allocated by this instruction.
  uint32_t size() const { return data_.size; }
  // Unconditional jump - ignores condition/predication.
  AllocType alloc_type() const {
    return static_cast<AllocType>(data_.alloc_type);
  }

 private:
  XEPACKEDSTRUCT(Data, {
    XEPACKEDSTRUCTANONYMOUS({
      uint32_t size : 3;
      uint32_t unused_0 : 29;
    });
    XEPACKEDSTRUCTANONYMOUS({
      uint32_t unused_1 : 8;
      uint32_t is_unserialized : 1;
      uint32_t alloc_type : 2;
      uint32_t unused_2 : 1;
      uint32_t opcode : 4;
    });
  });
  Data data_;
};
static_assert_size(ControlFlowAllocInstruction, 8);

XEPACKEDUNION(ControlFlowInstruction, {
  ControlFlowOpcode opcode() const {
    return static_cast<ControlFlowOpcode>(opcode_value);
  }

  ControlFlowExecInstruction exec;                    // kExec*
  ControlFlowCondExecInstruction cond_exec;           // kCondExec*
  ControlFlowCondExecPredInstruction cond_exec_pred;  // kCondExecPred*
  ControlFlowLoopStartInstruction loop_start;         // kLoopStart
  ControlFlowLoopEndInstruction loop_end;             // kLoopEnd
  ControlFlowCondCallInstruction cond_call;           // kCondCall
  ControlFlowReturnInstruction ret;                   // kReturn
  ControlFlowCondJmpInstruction cond_jmp;             // kCondJmp
  ControlFlowAllocInstruction alloc;                  // kAlloc

  XEPACKEDSTRUCTANONYMOUS({
    uint32_t unused_0 : 32;
    uint32_t unused_1 : 12;
    uint32_t opcode_value : 4;
  });
  XEPACKEDSTRUCTANONYMOUS({
    uint32_t dword_0;
    uint32_t dword_1;
  });
});
static_assert_size(ControlFlowInstruction, 8);

inline void UnpackControlFlowInstructions(const uint32_t* dwords,
                                          ControlFlowInstruction* out_a,
                                          ControlFlowInstruction* out_b) {
  uint32_t dword_0 = dwords[0];
  uint32_t dword_1 = dwords[1];
  uint32_t dword_2 = dwords[2];
  out_a->dword_0 = dword_0;
  out_a->dword_1 = dword_1 & 0xFFFF;
  out_b->dword_0 = (dword_1 >> 16) | (dword_2 << 16);
  out_b->dword_1 = dword_2 >> 16;
}

enum class FetchOpcode {
  kVertexFetch = 0,
  kTextureFetch = 1,
  kGetTextureBorderColorFrac = 16,
  kGetTextureComputedLod = 17,
  kGetTextureGradients = 18,
  kGetTextureWeights = 19,
  kSetTextureLod = 24,
  kSetTextureGradientsHorz = 25,
  kSetTextureGradientsVert = 26,
  kUnknownTextureOp = 27,
};

struct VertexFetchInstruction {
  FetchOpcode opcode() const {
    return static_cast<FetchOpcode>(data_.opcode_value);
  }

  // Whether the jump is predicated (or conditional).
  bool is_predicated() const { return data_.is_predicated; }
  // Required condition value of the comparision (true or false).
  bool predicate_condition() const { return data_.pred_condition == 1; }
  // Vertex fetch constant index [0-95].
  uint32_t fetch_constant_index() const {
    return data_.const_index * 3 + data_.const_index_sel;
  }

  uint32_t dest() const { return data_.dst_reg; }
  uint32_t dest_swizzle() const { return data_.dst_swiz; }
  bool is_dest_relative() const { return data_.dst_reg_am; }
  uint32_t src() const { return data_.src_reg; }
  uint32_t src_swizzle() const { return data_.src_swiz; }
  bool is_src_relative() const { return data_.src_reg_am; }

  uint32_t prefetch_count() const { return data_.prefetch_count; }
  bool is_mini_fetch() const { return data_.is_mini_fetch == 1; }

  VertexFormat data_format() const {
    return static_cast<VertexFormat>(data_.format);
  }
  // [-32, 31]
  int exp_adjust() const {
    return ((static_cast<int>(data_.exp_adjust) << 26) >> 26);
  }
  bool is_signed() const { return data_.is_signed == 1; }
  bool is_integer() const { return data_.is_integer == 1; }
  bool is_index_rounded() const { return data_.is_index_rounded == 1; }
  // Dword stride, [0-255].
  uint32_t stride() const { return data_.stride; }
  // Dword offset, [
  uint32_t offset() const { return data_.offset; }

  void AssignFromFull(const VertexFetchInstruction& full) {
    data_.stride = full.data_.stride;
    data_.const_index = full.data_.const_index;
    data_.const_index_sel = full.data_.const_index_sel;
  }

 private:
  XEPACKEDSTRUCT(Data, {
    XEPACKEDSTRUCTANONYMOUS({
      uint32_t opcode_value : 5;
      uint32_t src_reg : 6;
      uint32_t src_reg_am : 1;
      uint32_t dst_reg : 6;
      uint32_t dst_reg_am : 1;
      uint32_t must_be_one : 1;
      uint32_t const_index : 5;
      uint32_t const_index_sel : 2;
      uint32_t prefetch_count : 3;
      uint32_t src_swiz : 2;
    });
    XEPACKEDSTRUCTANONYMOUS({
      uint32_t dst_swiz : 12;
      uint32_t is_signed : 1;
      uint32_t is_integer : 1;
      uint32_t signed_rf_mode_all : 1;
      uint32_t is_index_rounded : 1;
      uint32_t format : 6;
      uint32_t reserved2 : 2;
      uint32_t exp_adjust : 6;
      uint32_t is_mini_fetch : 1;
      uint32_t is_predicated : 1;
    });
    XEPACKEDSTRUCTANONYMOUS({
      uint32_t stride : 8;
      uint32_t offset : 23;
      uint32_t pred_condition : 1;
    });
  });
  Data data_;
};

struct TextureFetchInstruction {
  FetchOpcode opcode() const {
    return static_cast<FetchOpcode>(data_.opcode_value);
  }

  // Whether the jump is predicated (or conditional).
  bool is_predicated() const { return data_.is_predicated; }
  // Required condition value of the comparision (true or false).
  bool predicate_condition() const { return data_.pred_condition == 1; }
  // Texture fetch constant index [0-31].
  uint32_t fetch_constant_index() const { return data_.const_index; }

  uint32_t dest() const { return data_.dst_reg; }
  uint32_t dest_swizzle() const { return data_.dst_swiz; }
  bool is_dest_relative() const { return data_.dst_reg_am; }
  uint32_t src() const { return data_.src_reg; }
  uint32_t src_swizzle() const { return data_.src_swiz; }
  bool is_src_relative() const { return data_.src_reg_am; }

  TextureDimension dimension() const {
    return static_cast<TextureDimension>(data_.dimension);
  }
  bool fetch_valid_only() const { return data_.fetch_valid_only == 1; }
  bool unnormalized_coordinates() const { return data_.tx_coord_denorm == 1; }
  bool has_mag_filter() const { return data_.mag_filter != 0x3; }
  TextureFilter mag_filter() const {
    return static_cast<TextureFilter>(data_.mag_filter);
  }
  bool has_min_filter() const { return data_.min_filter != 0x3; }
  TextureFilter min_filter() const {
    return static_cast<TextureFilter>(data_.min_filter);
  }
  bool has_mip_filter() const { return data_.mip_filter != 0x3; }
  TextureFilter mip_filter() const {
    return static_cast<TextureFilter>(data_.mip_filter);
  }
  bool has_aniso_filter() const { return data_.aniso_filter != 0x7; }
  AnisoFilter aniso_filter() const {
    return static_cast<AnisoFilter>(data_.aniso_filter);
  }
  bool use_computed_lod() const { return data_.use_comp_lod == 1; }
  bool use_register_lod() const { return data_.use_reg_lod == 1; }
  bool use_register_gradients() const { return data_.use_reg_gradients == 1; }
  SampleLocation sample_location() const {
    return static_cast<SampleLocation>(data_.sample_location);
  }
  float offset_x() const {
    return ((static_cast<int>(data_.offset_x) << 27) >> 27) / 2.0f;
  }
  float offset_y() const {
    return ((static_cast<int>(data_.offset_y) << 27) >> 27) / 2.0f;
  }
  float offset_z() const {
    return ((static_cast<int>(data_.offset_z) << 27) >> 27) / 2.0f;
  }

 private:
  XEPACKEDSTRUCT(Data, {
    XEPACKEDSTRUCTANONYMOUS({
      uint32_t opcode_value : 5;
      uint32_t src_reg : 6;
      uint32_t src_reg_am : 1;
      uint32_t dst_reg : 6;
      uint32_t dst_reg_am : 1;
      uint32_t fetch_valid_only : 1;
      uint32_t const_index : 5;
      uint32_t tx_coord_denorm : 1;
      uint32_t src_swiz : 6;  // xyz
    });
    XEPACKEDSTRUCTANONYMOUS({
      uint32_t dst_swiz : 12;         // xyzw
      uint32_t mag_filter : 2;        // instr_tex_filter_t
      uint32_t min_filter : 2;        // instr_tex_filter_t
      uint32_t mip_filter : 2;        // instr_tex_filter_t
      uint32_t aniso_filter : 3;      // instr_aniso_filter_t
      uint32_t arbitrary_filter : 3;  // instr_arbitrary_filter_t
      uint32_t vol_mag_filter : 2;    // instr_tex_filter_t
      uint32_t vol_min_filter : 2;    // instr_tex_filter_t
      uint32_t use_comp_lod : 1;
      uint32_t use_reg_lod : 1;
      uint32_t unk : 1;
      uint32_t is_predicated : 1;
    });
    XEPACKEDSTRUCTANONYMOUS({
      uint32_t use_reg_gradients : 1;
      uint32_t sample_location : 1;
      uint32_t lod_bias : 7;
      uint32_t unused : 5;
      uint32_t dimension : 2;
      uint32_t offset_x : 5;
      uint32_t offset_y : 5;
      uint32_t offset_z : 5;
      uint32_t pred_condition : 1;
    });
  });
  Data data_;
};
static_assert_size(TextureFetchInstruction, 12);

enum class AluScalarOpcode {
  kADDs = 0,
  kADD_PREVs = 1,
  kMULs = 2,
  kMUL_PREVs = 3,
  kMUL_PREV2s = 4,
  kMAXs = 5,
  kMINs = 6,
  kSETEs = 7,
  kSETGTs = 8,
  kSETGTEs = 9,
  kSETNEs = 10,
  kFRACs = 11,
  kTRUNCs = 12,
  kFLOORs = 13,
  kEXP_IEEE = 14,
  kLOG_CLAMP = 15,
  kLOG_IEEE = 16,
  kRECIP_CLAMP = 17,
  kRECIP_FF = 18,
  kRECIP_IEEE = 19,
  kRECIPSQ_CLAMP = 20,
  kRECIPSQ_FF = 21,
  kRECIPSQ_IEEE = 22,
  kMOVAs = 23,
  kMOVA_FLOORs = 24,
  kSUBs = 25,
  kSUB_PREVs = 26,
  kPRED_SETEs = 27,
  kPRED_SETNEs = 28,
  kPRED_SETGTs = 29,
  kPRED_SETGTEs = 30,
  kPRED_SET_INVs = 31,
  kPRED_SET_POPs = 32,
  kPRED_SET_CLRs = 33,
  kPRED_SET_RESTOREs = 34,
  kKILLEs = 35,
  kKILLGTs = 36,
  kKILLGTEs = 37,
  kKILLNEs = 38,
  kKILLONEs = 39,
  kSQRT_IEEE = 40,
  kMUL_CONST_0 = 42,
  kMUL_CONST_1 = 43,
  kADD_CONST_0 = 44,
  kADD_CONST_1 = 45,
  kSUB_CONST_0 = 46,
  kSUB_CONST_1 = 47,
  kSIN = 48,
  kCOS = 49,
  kRETAIN_PREV = 50,
};

enum class AluVectorOpcode {
  kADDv = 0,
  kMULv = 1,
  kMAXv = 2,
  kMINv = 3,
  kSETEv = 4,
  kSETGTv = 5,
  kSETGTEv = 6,
  kSETNEv = 7,
  kFRACv = 8,
  kTRUNCv = 9,
  kFLOORv = 10,
  kMULADDv = 11,
  kCNDEv = 12,
  kCNDGTEv = 13,
  kCNDGTv = 14,
  kDOT4v = 15,
  kDOT3v = 16,
  kDOT2ADDv = 17,
  kCUBEv = 18,
  kMAX4v = 19,
  kPRED_SETE_PUSHv = 20,
  kPRED_SETNE_PUSHv = 21,
  kPRED_SETGT_PUSHv = 22,
  kPRED_SETGTE_PUSHv = 23,
  kKILLEv = 24,
  kKILLGTv = 25,
  kKILLGTEv = 26,
  kKILLNEv = 27,
  kDSTv = 28,
  kMAXAv = 29,
};

struct AluInstruction {
  // Whether data is being exported (or written to local registers).
  bool is_export() const { return data_.export_data == 1; }
  bool export_write_mask() const { return data_.scalar_dest_rel == 1; }

  // Whether the jump is predicated (or conditional).
  bool is_predicated() const { return data_.is_predicated; }
  // Required condition value of the comparision (true or false).
  bool predicate_condition() const { return data_.pred_condition == 1; }

  bool abs_constants() const { return data_.abs_constants == 1; }
  bool is_const_0_addressed() const { return data_.const_0_rel_abs == 1; }
  bool is_const_1_addressed() const { return data_.const_1_rel_abs == 1; }
  bool is_address_relative() const { return data_.address_absolute == 1; }

  bool has_vector_op() const {
    return vector_write_mask() || (is_export() && export_write_mask());
  }
  AluVectorOpcode vector_opcode() const {
    return static_cast<AluVectorOpcode>(data_.vector_opc);
  }
  uint32_t vector_write_mask() const { return data_.vector_write_mask; }
  uint32_t vector_dest() const { return data_.vector_dest; }
  bool is_vector_dest_relative() const { return data_.vector_dest_rel == 1; }
  bool vector_clamp() const { return data_.vector_clamp == 1; }

  bool has_scalar_op() const {
    return scalar_opcode() != AluScalarOpcode::kRETAIN_PREV ||
           scalar_write_mask() != 0;
  }
  AluScalarOpcode scalar_opcode() const {
    return static_cast<AluScalarOpcode>(data_.scalar_opc);
  }
  uint32_t scalar_write_mask() const { return data_.scalar_write_mask; }
  uint32_t scalar_dest() const { return data_.scalar_dest; }
  bool is_scalar_dest_relative() const { return data_.scalar_dest_rel == 1; }
  bool scalar_clamp() const { return data_.scalar_clamp == 1; }

  uint32_t src_reg(size_t i) const {
    switch (i) {
      case 1:
        return data_.src1_reg;
      case 2:
        return data_.src2_reg;
      case 3:
        return data_.src3_reg;
      default:
        assert_unhandled_case(i);
        return 0;
    }
  }
  bool src_is_temp(size_t i) const {
    switch (i) {
      case 1:
        return data_.src1_sel == 1;
      case 2:
        return data_.src2_sel == 1;
      case 3:
        return data_.src3_sel == 1;
      default:
        assert_unhandled_case(i);
        return 0;
    }
  }
  uint32_t src_swizzle(size_t i) const {
    switch (i) {
      case 1:
        return data_.src1_swiz;
      case 2:
        return data_.src2_swiz;
      case 3:
        return data_.src3_swiz;
      default:
        assert_unhandled_case(i);
        return 0;
    }
  }
  bool src_negate(size_t i) const {
    switch (i) {
      case 1:
        return data_.src1_reg_negate == 1;
      case 2:
        return data_.src2_reg_negate == 1;
      case 3:
        return data_.src3_reg_negate == 1;
      default:
        assert_unhandled_case(i);
        return 0;
    }
  }

 private:
  XEPACKEDSTRUCT(Data, {
    XEPACKEDSTRUCTANONYMOUS({
      uint32_t vector_dest : 6;
      uint32_t vector_dest_rel : 1;
      uint32_t abs_constants : 1;
      uint32_t scalar_dest : 6;
      uint32_t scalar_dest_rel : 1;
      uint32_t export_data : 1;
      uint32_t vector_write_mask : 4;
      uint32_t scalar_write_mask : 4;
      uint32_t vector_clamp : 1;
      uint32_t scalar_clamp : 1;
      uint32_t scalar_opc : 6;  // instr_scalar_opc_t
    });
    XEPACKEDSTRUCTANONYMOUS({
      uint32_t src3_swiz : 8;
      uint32_t src2_swiz : 8;
      uint32_t src1_swiz : 8;
      uint32_t src3_reg_negate : 1;
      uint32_t src2_reg_negate : 1;
      uint32_t src1_reg_negate : 1;
      uint32_t pred_condition : 1;
      uint32_t is_predicated : 1;
      uint32_t address_absolute : 1;
      uint32_t const_1_rel_abs : 1;
      uint32_t const_0_rel_abs : 1;
    });
    XEPACKEDSTRUCTANONYMOUS({
      uint32_t src3_reg : 8;
      uint32_t src2_reg : 8;
      uint32_t src1_reg : 8;
      uint32_t vector_opc : 5;  // instr_vector_opc_t
      uint32_t src3_sel : 1;
      uint32_t src2_sel : 1;
      uint32_t src1_sel : 1;
    });
  });
  Data data_;
};
static_assert_size(AluInstruction, 12);

}  // namespace ucode
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_UCODE_H_
