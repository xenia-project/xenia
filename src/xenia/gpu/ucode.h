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

// Returns true if the given control flow opcode terminates the shader after
// executing.
constexpr bool DoesControlFlowOpcodeEndShader(ControlFlowOpcode opcode) {
  return opcode == ControlFlowOpcode::kExecEnd ||
         opcode == ControlFlowOpcode::kCondExecEnd ||
         opcode == ControlFlowOpcode::kCondExecPredEnd ||
         opcode == ControlFlowOpcode::kCondExecPredCleanEnd;
}

// Returns true if the given control flow opcode resets the predicate prior to
// execution.
constexpr bool DoesControlFlowOpcodeCleanPredicate(ControlFlowOpcode opcode) {
  return opcode == ControlFlowOpcode::kCondExecPredClean ||
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
  kNone = 0,
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
    return static_cast<ControlFlowOpcode>(opcode_);
  }
  AddressingMode addressing_mode() const {
    return static_cast<AddressingMode>(address_mode_);
  }
  // Address of the instructions to execute.
  uint32_t address() const { return address_; }
  // Number of instructions being executed.
  uint32_t count() const { return count_; }
  // Sequence bits, 2 per instruction, indicating whether ALU or fetch.
  uint32_t sequence() const { return serialize_; }
  // Whether to reset the current predicate.
  bool clean() const { return clean_ == 1; }
  // ?
  bool is_yield() const { return is_yeild_ == 1; }

 private:
  // Word 0: (32 bits)
  uint32_t address_ : 12;
  uint32_t count_ : 3;
  uint32_t is_yeild_ : 1;
  uint32_t serialize_ : 12;
  uint32_t vc_hi_ : 4;  // Vertex cache?

  // Word 1: (16 bits)
  uint32_t vc_lo_ : 2;
  uint32_t : 7;
  uint32_t clean_ : 1;
  uint32_t : 1;
  uint32_t address_mode_ : 1;
  uint32_t opcode_ : 4;
};
static_assert_size(ControlFlowExecInstruction, 8);

// Instruction data for ControlFlowOpcode::kCondExec and kCondExecEnd.
struct ControlFlowCondExecInstruction {
  ControlFlowOpcode opcode() const {
    return static_cast<ControlFlowOpcode>(opcode_);
  }
  AddressingMode addressing_mode() const {
    return static_cast<AddressingMode>(address_mode_);
  }
  // Address of the instructions to execute.
  uint32_t address() const { return address_; }
  // Number of instructions being executed.
  uint32_t count() const { return count_; }
  // Sequence bits, 2 per instruction, indicating whether ALU or fetch.
  uint32_t sequence() const { return serialize_; }
  // Constant index used as the conditional.
  uint32_t bool_address() const { return bool_address_; }
  // Required condition value of the comparision (true or false).
  bool condition() const { return condition_ == 1; }
  // ?
  bool is_yield() const { return is_yeild_ == 1; }

 private:
  // Word 0: (32 bits)
  uint32_t address_ : 12;
  uint32_t count_ : 3;
  uint32_t is_yeild_ : 1;
  uint32_t serialize_ : 12;
  uint32_t vc_hi_ : 4;  // Vertex cache?

  // Word 1: (16 bits)
  uint32_t vc_lo_ : 2;
  uint32_t bool_address_ : 8;
  uint32_t condition_ : 1;
  uint32_t address_mode_ : 1;
  uint32_t opcode_ : 4;
};
static_assert_size(ControlFlowCondExecInstruction, 8);

// Instruction data for ControlFlowOpcode::kCondExecPred, kCondExecPredEnd,
// kCondExecPredClean, kCondExecPredCleanEnd.
struct ControlFlowCondExecPredInstruction {
  ControlFlowOpcode opcode() const {
    return static_cast<ControlFlowOpcode>(opcode_);
  }
  AddressingMode addressing_mode() const {
    return static_cast<AddressingMode>(address_mode_);
  }
  // Address of the instructions to execute.
  uint32_t address() const { return address_; }
  // Number of instructions being executed.
  uint32_t count() const { return count_; }
  // Sequence bits, 2 per instruction, indicating whether ALU or fetch.
  uint32_t sequence() const { return serialize_; }
  // Whether to reset the current predicate.
  bool clean() const { return clean_ == 1; }
  // Required condition value of the comparision (true or false).
  bool condition() const { return condition_ == 1; }
  // ?
  bool is_yield() const { return is_yeild_ == 1; }

 private:
  // Word 0: (32 bits)
  uint32_t address_ : 12;
  uint32_t count_ : 3;
  uint32_t is_yeild_ : 1;
  uint32_t serialize_ : 12;
  uint32_t vc_hi_ : 4;  // Vertex cache?

  // Word 1: (16 bits)
  uint32_t vc_lo_ : 2;
  uint32_t : 7;
  uint32_t clean_ : 1;
  uint32_t condition_ : 1;
  uint32_t address_mode_ : 1;
  uint32_t opcode_ : 4;
};
static_assert_size(ControlFlowCondExecPredInstruction, 8);

// Instruction data for ControlFlowOpcode::kLoopStart.
struct ControlFlowLoopStartInstruction {
  ControlFlowOpcode opcode() const {
    return static_cast<ControlFlowOpcode>(opcode_);
  }
  AddressingMode addressing_mode() const {
    return static_cast<AddressingMode>(address_mode_);
  }
  // Target address to jump to when skipping the loop.
  uint32_t address() const { return address_; }
  // Whether to reuse the current aL instead of reset it to loop start.
  bool is_repeat() const { return is_repeat_; }
  // Integer constant register that holds the loop parameters.
  // Byte-wise: [loop count, start, step [-128, 127], ?]
  uint32_t loop_id() const { return loop_id_; }

 private:
  // Word 0: (32 bits)
  uint32_t address_ : 13;
  uint32_t is_repeat_ : 1;
  uint32_t : 2;
  uint32_t loop_id_ : 5;
  uint32_t : 11;

  // Word 1: (16 bits)
  uint32_t : 11;
  uint32_t address_mode_ : 1;
  uint32_t opcode_ : 4;
};
static_assert_size(ControlFlowLoopStartInstruction, 8);

// Instruction data for ControlFlowOpcode::kLoopEnd.
struct ControlFlowLoopEndInstruction {
  ControlFlowOpcode opcode() const {
    return static_cast<ControlFlowOpcode>(opcode_);
  }
  AddressingMode addressing_mode() const {
    return static_cast<AddressingMode>(address_mode_);
  }
  // Target address of the start of the loop body.
  uint32_t address() const { return address_; }
  // Integer constant register that holds the loop parameters.
  // Byte-wise: [loop count, start, step [-128, 127], ?]
  uint32_t loop_id() const { return loop_id_; }
  // Break from the loop if the predicate matches the expected value.
  bool is_predicated_break() const { return is_predicated_break_; }
  // Required condition value of the comparision (true or false).
  bool condition() const { return condition_ == 1; }

 private:
  // Word 0: (32 bits)
  uint32_t address_ : 13;
  uint32_t : 3;
  uint32_t loop_id_ : 5;
  uint32_t is_predicated_break_ : 1;
  uint32_t : 10;

  // Word 1: (16 bits)
  uint32_t : 10;
  uint32_t condition_ : 1;
  uint32_t address_mode_ : 1;
  uint32_t opcode_ : 4;
};
static_assert_size(ControlFlowLoopEndInstruction, 8);

// Instruction data for ControlFlowOpcode::kCondCall.
struct ControlFlowCondCallInstruction {
  ControlFlowOpcode opcode() const {
    return static_cast<ControlFlowOpcode>(opcode_);
  }
  AddressingMode addressing_mode() const {
    return static_cast<AddressingMode>(address_mode_);
  }
  // Target address.
  uint32_t address() const { return address_; }
  // Unconditional call - ignores condition/predication.
  bool is_unconditional() const { return is_unconditional_; }
  // Whether the call is predicated (or conditional).
  bool is_predicated() const { return is_predicated_; }
  // Constant index used as the conditional.
  uint32_t bool_address() const { return bool_address_; }
  // Required condition value of the comparision (true or false).
  bool condition() const { return condition_ == 1; }

 private:
  // Word 0: (32 bits)
  uint32_t address_ : 13;
  uint32_t is_unconditional_ : 1;
  uint32_t is_predicated_ : 1;
  uint32_t : 17;

  // Word 1: (16 bits)
  uint32_t : 2;
  uint32_t bool_address_ : 8;
  uint32_t condition_ : 1;
  uint32_t address_mode_ : 1;
  uint32_t opcode_ : 4;
};
static_assert_size(ControlFlowCondCallInstruction, 8);

// Instruction data for ControlFlowOpcode::kReturn.
struct ControlFlowReturnInstruction {
  ControlFlowOpcode opcode() const {
    return static_cast<ControlFlowOpcode>(opcode_);
  }
  AddressingMode addressing_mode() const {
    return static_cast<AddressingMode>(address_mode_);
  }

 private:
  // Word 0: (32 bits)
  uint32_t : 32;

  // Word 1: (16 bits)
  uint32_t : 11;
  AddressingMode address_mode_ : 1;
  ControlFlowOpcode opcode_ : 4;
};
static_assert_size(ControlFlowReturnInstruction, 8);

// Instruction data for ControlFlowOpcode::kCondJmp.
struct ControlFlowCondJmpInstruction {
  ControlFlowOpcode opcode() const {
    return static_cast<ControlFlowOpcode>(opcode_);
  }
  AddressingMode addressing_mode() const {
    return static_cast<AddressingMode>(address_mode_);
  }
  // Target address.
  uint32_t address() const { return address_; }
  // Unconditional jump - ignores condition/predication.
  bool is_unconditional() const { return is_unconditional_; }
  // Whether the jump is predicated (or conditional).
  bool is_predicated() const { return is_predicated_; }
  // Constant index used as the conditional.
  uint32_t bool_address() const { return bool_address_; }
  // Required condition value of the comparision (true or false).
  bool condition() const { return condition_ == 1; }

 private:
  // Word 0: (32 bits)
  uint32_t address_ : 13;
  uint32_t is_unconditional_ : 1;
  uint32_t is_predicated_ : 1;
  uint32_t : 17;

  // Word 1: (16 bits)
  uint32_t : 1;
  uint32_t direction_ : 1;
  uint32_t bool_address_ : 8;
  uint32_t condition_ : 1;
  uint32_t address_mode_ : 1;
  uint32_t opcode_ : 4;
};
static_assert_size(ControlFlowCondJmpInstruction, 8);

// Instruction data for ControlFlowOpcode::kAlloc.
struct ControlFlowAllocInstruction {
  ControlFlowOpcode opcode() const {
    return static_cast<ControlFlowOpcode>(opcode_);
  }
  // The total number of the given type allocated by this instruction.
  uint32_t size() const { return size_; }
  // Unconditional jump - ignores condition/predication.
  AllocType alloc_type() const { return static_cast<AllocType>(alloc_type_); }

 private:
  // Word 0: (32 bits)
  uint32_t size_ : 3;
  uint32_t : 29;

  // Word 1: (16 bits)
  uint32_t : 8;
  uint32_t is_unserialized_ : 1;
  uint32_t alloc_type_ : 2;
  uint32_t : 1;
  uint32_t opcode_ : 4;
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

  // Returns true if the fetch actually fetches data.
  // This may be false if it's used only to populate constants.
  bool fetches_any_data() const {
    uint32_t dst_swiz = data_.dst_swiz;
    bool fetches_any_data = false;
    for (int i = 0; i < 4; i++) {
      if ((dst_swiz & 0x7) == 4) {
        // 0.0
      } else if ((dst_swiz & 0x7) == 5) {
        // 1.0
      } else if ((dst_swiz & 0x7) == 6) {
        // ?
      } else if ((dst_swiz & 0x7) == 7) {
        // Previous register value.
      } else {
        fetches_any_data = true;
        break;
      }
      dst_swiz >>= 3;
    }
    return fetches_any_data;
  }

  uint32_t prefetch_count() const { return data_.prefetch_count; }
  bool is_mini_fetch() const { return data_.is_mini_fetch == 1; }

  VertexFormat data_format() const {
    return static_cast<VertexFormat>(data_.format);
  }
  // [-32, 31]
  int exp_adjust() const {
    return ((static_cast<int>(data_.exp_adjust) << 26) >> 26);
  }
  bool is_signed() const { return data_.fomat_comp_all == 1; }
  bool is_normalized() const { return data_.num_format_all == 0; }
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
      uint32_t fomat_comp_all : 1;
      uint32_t num_format_all : 1;
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

// What follows is largely a mash up of the microcode assembly naming and the
// R600 docs that have a near 1:1 with the instructions available in the xenos
// GPU. Some of the behavior has been experimentally verified. Some has been
// guessed.
// Docs: http://www.x.org/docs/AMD/old/r600isa.pdf
//
// Conventions:
// - All temporary registers are vec4s.
// - Scalar ops swizzle out a single component of their source registers denoted
//   by 'a' or 'b'. src0.a means 'the first component specified for src0' and
//   src0.ab means 'two components specified for src0, in order'.
// - Scalar ops write the result to the entire destination register.
// - pv and ps are the previous results of a vector or scalar ALU operation.
//   Both are valid only within the current ALU clause. They are not modified
//   when write masks are disabled or the instruction that would write them
//   fails its predication check.

enum class AluScalarOpcode {
  // Floating-Point Add
  // adds dest, src0.ab
  //     dest.xyzw = src0.a + src0.b;
  kAdds = 0,

  // Floating-Point Add (with Previous)
  // adds_prev dest, src0.a
  //     dest.xyzw = src0.a + ps;
  kAddsPrev = 1,

  // Floating-Point Multiply
  // muls dest, src0.ab
  //     dest.xyzw = src0.a * src0.b;
  kMuls = 2,

  // Floating-Point Multiply (with Previous)
  // muls_prev dest, src0.a
  //     dest.xyzw = src0.a * ps;
  kMulsPrev = 3,

  // Scalar Multiply Emulating LIT Operation
  // muls_prev2 dest, src0.ab
  //    dest.xyzw =
  //        ps == -FLT_MAX || !isfinite(ps) || !isfinite(src0.b) || src0.b <= 0
  //        ? -FLT_MAX : src0.a * ps;
  kMulsPrev2 = 4,

  // Floating-Point Maximum
  // maxs dest, src0.ab
  //     dest.xyzw = src0.a >= src0.b ? src0.a : src0.b;
  kMaxs = 5,

  // Floating-Point Minimum
  // mins dest, src0.ab
  //     dest.xyzw = src0.a < src0.b ? src0.a : src0.b;
  kMins = 6,

  // Floating-Point Set If Equal
  // seqs dest, src0.a
  //     dest.xyzw = src0.a == 0.0 ? 1.0 : 0.0;
  kSeqs = 7,

  // Floating-Point Set If Greater Than
  // sgts dest, src0.a
  //     dest.xyzw = src0.a > 0.0 ? 1.0 : 0.0;
  kSgts = 8,

  // Floating-Point Set If Greater Than Or Equal
  // sges dest, src0.a
  //     dest.xyzw = src0.a >= 0.0 ? 1.0 : 0.0;
  kSges = 9,

  // Floating-Point Set If Not Equal
  // snes dest, src0.a
  //     dest.xyzw = src0.a != 0.0 ? 1.0 : 0.0;
  kSnes = 10,

  // Floating-Point Fractional
  // frcs dest, src0.a
  //     dest.xyzw = src0.a - floor(src0.a);
  kFrcs = 11,

  // Floating-Point Truncate
  // truncs dest, src0.a
  //     dest.xyzw = src0.a >= 0 ? floor(src0.a) : -floor(-src0.a);
  kTruncs = 12,

  // Floating-Point Floor
  // floors dest, src0.a
  //     dest.xyzw = floor(src0.a);
  kFloors = 13,

  // Scalar Base-2 Exponent, IEEE
  // exp dest, src0.a
  //     dest.xyzw = src0.a == 0.0 ? 1.0 : pow(2, src0.a);
  kExp = 14,

  // Scalar Base-2 Log
  // logc dest, src0.a
  //     float t = src0.a == 1.0 ? 0.0 : log(src0.a) / log(2.0);
  //     dest.xyzw = t == -INF ? -FLT_MAX : t;
  kLogc = 15,

  // Scalar Base-2 IEEE Log
  // log dest, src0.a
  //     dest.xyzw = src0.a == 1.0 ? 0.0 : log(src0.a) / log(2.0);
  kLog = 16,

  // Scalar Reciprocal, Clamp to Maximum
  // rcpc dest, src0.a
  //     float t = src0.a == 1.0 ? 1.0 : 1.0 / src0.a;
  //     if (t == -INF) t = -FLT_MAX;
  //     else if (t == INF) t = FLT_MAX;
  //     dest.xyzw = t;
  kRcpc = 17,

  // Scalar Reciprocal, Clamp to Zero
  // rcpf dest, src0.a
  //     float t = src0.a == 1.0 ? 1.0 : 1.0 / src0.a;
  //     if (t == -INF) t = -0.0;
  //     else if (t == INF) t = 0.0;
  //     dest.xyzw = t;
  kRcpf = 18,

  // Scalar Reciprocal, IEEE Approximation
  // rcp dest, src0.a
  //     dest.xyzw = src0.a == 1.0 ? 1.0 : 1.0 / src0.a;
  kRcp = 19,

  // Scalar Reciprocal Square Root, Clamp to Maximum
  // rsqc dest, src0.a
  //     float t = src0.a == 1.0 ? 1.0 : 1.0 / sqrt(src0.a);
  //     if (t == -INF) t = -FLT_MAX;
  //     else if (t == INF) t = FLT_MAX;
  //     dest.xyzw = t;
  kRsqc = 20,

  // Scalar Reciprocal Square Root, Clamp to Zero
  // rsqc dest, src0.a
  //     float t = src0.a == 1.0 ? 1.0 : 1.0 / sqrt(src0.a);
  //     if (t == -INF) t = -0.0;
  //     else if (t == INF) t = 0.0;
  //     dest.xyzw = t;
  kRsqf = 21,

  // Scalar Reciprocal Square Root, IEEE Approximation
  // rsq dest, src0.a
  //     dest.xyzw = src0.a == 1.0 ? 1.0 : 1.0 / sqrt(src0.a);
  kRsq = 22,

  // Floating-Point Maximum with Copy To Integer in AR
  // maxas dest, src0.ab
  // movas dest, src0.aa
  //     int result = (int)floor(src0.a + 0.5);
  //     a0 = clamp(result, -256, 255);
  //     dest.xyzw = src0.a >= src0.b ? src0.a : src0.b;
  kMaxAs = 23,

  // Floating-Point Maximum with Copy Truncated To Integer in AR
  // maxasf dest, src0.ab
  // movasf dest, src0.aa
  //     int result = (int)floor(src0.a);
  //     a0 = clamp(result, -256, 255);
  //     dest.xyzw = src0.a >= src0.b ? src0.a : src0.b;
  kMaxAsf = 24,

  // Floating-Point Subtract
  // subs dest, src0.ab
  //     dest.xyzw = src0.a - src0.b;
  kSubs = 25,

  // Floating-Point Subtract (with Previous)
  // subs_prev dest, src0.a
  //     dest.xyzw = src0.a - ps;
  kSubsPrev = 26,

  // Floating-Point Predicate Set If Equal
  // setp_eq dest, src0.a
  //     if (src0.a == 0.0) {
  //       dest.xyzw = 0.0;
  //       p0 = 1;
  //     } else {
  //       dest.xyzw = 1.0;
  //       p0 = 0;
  //     }
  kSetpEq = 27,

  // Floating-Point Predicate Set If Not Equal
  // setp_ne dest, src0.a
  //     if (src0.a != 0.0) {
  //       dest.xyzw = 0.0;
  //       p0 = 1;
  //     } else {
  //       dest.xyzw = 1.0;
  //       p0 = 0;
  //     }
  kSetpNe = 28,

  // Floating-Point Predicate Set If Greater Than
  // setp_gt dest, src0.a
  //     if (src0.a > 0.0) {
  //       dest.xyzw = 0.0;
  //       p0 = 1;
  //     } else {
  //       dest.xyzw = 1.0;
  //       p0 = 0;
  //     }
  kSetpGt = 29,

  // Floating-Point Predicate Set If Greater Than Or Equal
  // setp_ge dest, src0.a
  //     if (src0.a >= 0.0) {
  //       dest.xyzw = 0.0;
  //       p0 = 1;
  //     } else {
  //       dest.xyzw = 1.0;
  //       p0 = 0;
  //     }
  kSetpGe = 30,

  // Predicate Counter Invert
  // setp_inv dest, src0.a
  //     if (src0.a == 1.0) {
  //       dest.xyzw = 0.0;
  //       p0 = 1;
  //     } else {
  //       if (src0.a == 0.0) {
  //         dest.xyzw = 1.0;
  //       } else {
  //         dest.xyzw = src0.a;
  //       }
  //       p0 = 0;
  //     }
  kSetpInv = 31,

  // Predicate Counter Pop
  // setp_pop dest, src0.a
  //     if (src0.a - 1.0 <= 0.0) {
  //       dest.xyzw = 0.0;
  //       p0 = 1;
  //     } else {
  //       dest.xyzw = src0.a - 1.0;
  //       p0 = 0;
  //     }
  kSetpPop = 32,

  // Predicate Counter Clear
  // setp_clr dest
  //     dest.xyzw = FLT_MAX;
  //     p0 = 0;
  kSetpClr = 33,

  // Predicate Counter Restore
  // setp_rstr dest, src0.a
  //     if (src0.a == 0.0) {
  //       dest.xyzw = 0.0;
  //       p0 = 1;
  //     } else {
  //       dest.xyzw = src0.a;
  //       p0 = 0;
  //     }
  kSetpRstr = 34,

  // Floating-Point Pixel Kill If Equal
  // kills_eq dest, src0.a
  //     if (src0.a == 0.0) {
  //       dest.xyzw = 1.0;
  //       discard;
  //     } else {
  //       dest.xyzw = 0.0;
  //     }
  kKillsEq = 35,

  // Floating-Point Pixel Kill If Greater Than
  // kills_gt dest, src0.a
  //     if (src0.a > 0.0) {
  //       dest.xyzw = 1.0;
  //       discard;
  //     } else {
  //       dest.xyzw = 0.0;
  //     }
  kKillsGt = 36,

  // Floating-Point Pixel Kill If Greater Than Or Equal
  // kills_ge dest, src0.a
  //     if (src0.a >= 0.0) {
  //       dest.xyzw = 1.0;
  //       discard;
  //     } else {
  //       dest.xyzw = 0.0;
  //     }
  kKillsGe = 37,

  // Floating-Point Pixel Kill If Not Equal
  // kills_ne dest, src0.a
  //     if (src0.a != 0.0) {
  //       dest.xyzw = 1.0;
  //       discard;
  //     } else {
  //       dest.xyzw = 0.0;
  //     }
  kKillsNe = 38,

  // Floating-Point Pixel Kill If One
  // kills_one dest, src0.a
  //     if (src0.a == 1.0) {
  //       dest.xyzw = 1.0;
  //       discard;
  //     } else {
  //       dest.xyzw = 0.0;
  //     }
  kKillsOne = 39,

  // Scalar Square Root, IEEE Aproximation
  // sqrt dest, src0.a
  //     dest.xyzw = sqrt(src0.a);
  kSqrt = 40,

  // mulsc dest, src0.a, src0.b
  kMulsc0 = 42,
  // mulsc dest, src0.a, src0.b
  kMulsc1 = 43,
  // addsc dest, src0.a, src0.b
  kAddsc0 = 44,
  // addsc dest, src0.a, src0.b
  kAddsc1 = 45,
  // subsc dest, src0.a, src0.b
  kSubsc0 = 46,
  // subsc dest, src0.a, src0.b
  kSubsc1 = 47,

  // Scalar Sin
  // sin dest, src0.a
  //     dest.xyzw = sin(src0.a);
  kSin = 48,

  // Scalar Cos
  // cos dest, src0.a
  //     dest.xyzw = cos(src0.a);
  kCos = 49,

  // retain_prev dest
  //     dest.xyzw = ps;
  kRetainPrev = 50,
};

enum class AluVectorOpcode {
  // Per-Component Floating-Point Add
  // add dest, src0, src1
  //     dest.x = src0.x + src1.x;
  //     dest.y = src0.y + src1.y;
  //     dest.z = src0.z + src1.z;
  //     dest.w = src0.w + src1.w;
  kAdd = 0,

  // Per-Component Floating-Point Multiply
  // mul dest, src0, src1
  //     dest.x = src0.x * src1.x;
  //     dest.y = src0.y * src1.y;
  //     dest.z = src0.z * src1.z;
  //     dest.w = src0.w * src1.w;
  kMul = 1,

  // Per-Component Floating-Point Maximum
  // max dest, src0, src1
  //     dest.x = src0.x >= src1.x ? src0.x : src1.x;
  //     dest.y = src0.x >= src1.y ? src0.y : src1.y;
  //     dest.z = src0.x >= src1.z ? src0.z : src1.z;
  //     dest.w = src0.x >= src1.w ? src0.w : src1.w;
  kMax = 2,

  // Per-Component Floating-Point Minimum
  // min dest, src0, src1
  //     dest.x = src0.x < src1.x ? src0.x : src1.x;
  //     dest.y = src0.x < src1.y ? src0.y : src1.y;
  //     dest.z = src0.x < src1.z ? src0.z : src1.z;
  //     dest.w = src0.x < src1.w ? src0.w : src1.w;
  kMin = 3,

  // Per-Component Floating-Point Set If Equal
  // seq dest, src0, src1
  //     dest.x = src0.x == src1.x ? 1.0 : 0.0;
  //     dest.y = src0.y == src1.y ? 1.0 : 0.0;
  //     dest.z = src0.z == src1.z ? 1.0 : 0.0;
  //     dest.w = src0.w == src1.w ? 1.0 : 0.0;
  kSeq = 4,

  // Per-Component Floating-Point Set If Greater Than
  // sgt dest, src0, src1
  //     dest.x = src0.x > src1.x ? 1.0 : 0.0;
  //     dest.y = src0.y > src1.y ? 1.0 : 0.0;
  //     dest.z = src0.z > src1.z ? 1.0 : 0.0;
  //     dest.w = src0.w > src1.w ? 1.0 : 0.0;
  kSgt = 5,

  // Per-Component Floating-Point Set If Greater Than Or Equal
  // sge dest, src0, src1
  //     dest.x = src0.x >= src1.x ? 1.0 : 0.0;
  //     dest.y = src0.y >= src1.y ? 1.0 : 0.0;
  //     dest.z = src0.z >= src1.z ? 1.0 : 0.0;
  //     dest.w = src0.w >= src1.w ? 1.0 : 0.0;
  kSge = 6,

  // Per-Component Floating-Point Set If Not Equal
  // sne dest, src0, src1
  //     dest.x = src0.x != src1.x ? 1.0 : 0.0;
  //     dest.y = src0.y != src1.y ? 1.0 : 0.0;
  //     dest.z = src0.z != src1.z ? 1.0 : 0.0;
  //     dest.w = src0.w != src1.w ? 1.0 : 0.0;
  kSne = 7,

  // Per-Component Floating-Point Fractional
  // frc dest, src0
  //     dest.x = src0.x - floor(src0.x);
  //     dest.y = src0.y - floor(src0.y);
  //     dest.z = src0.z - floor(src0.z);
  //     dest.w = src0.w - floor(src0.w);
  kFrc = 8,

  // Per-Component Floating-Point Truncate
  // trunc dest, src0
  //     dest.x = src0.x >= 0 ? floor(src0.x) : -floor(-src0.x);
  //     dest.y = src0.y >= 0 ? floor(src0.y) : -floor(-src0.y);
  //     dest.z = src0.z >= 0 ? floor(src0.z) : -floor(-src0.z);
  //     dest.w = src0.w >= 0 ? floor(src0.w) : -floor(-src0.w);
  kTrunc = 9,

  // Per-Component Floating-Point Floor
  // floor dest, src0
  //     dest.x = floor(src0.x);
  //     dest.y = floor(src0.y);
  //     dest.z = floor(src0.z);
  //     dest.w = floor(src0.w);
  kFloor = 10,

  // Per-Component Floating-Point Multiply-Add
  // mad dest, src0, src1, src2
  //     dest.x = src0.x * src1.x + src2.x;
  //     dest.y = src0.y * src1.y + src2.y;
  //     dest.z = src0.z * src1.z + src2.z;
  //     dest.w = src0.w * src1.w + src2.w;
  kMad = 11,

  // Per-Component Floating-Point Conditional Move If Equal
  // cndeq dest, src0, src1, src2
  //     dest.x = src0.x == 0.0 ? src1.x : src2.x;
  //     dest.y = src0.y == 0.0 ? src1.y : src2.y;
  //     dest.z = src0.z == 0.0 ? src1.z : src2.z;
  //     dest.w = src0.w == 0.0 ? src1.w : src2.w;
  kCndEq = 12,

  // Per-Component Floating-Point Conditional Move If Greater Than Or Equal
  // cndge dest, src0, src1, src2
  //     dest.x = src0.x >= 0.0 ? src1.x : src2.x;
  //     dest.y = src0.y >= 0.0 ? src1.y : src2.y;
  //     dest.z = src0.z >= 0.0 ? src1.z : src2.z;
  //     dest.w = src0.w >= 0.0 ? src1.w : src2.w;
  kCndGe = 13,

  // Per-Component Floating-Point Conditional Move If Greater Than
  // cndgt dest, src0, src1, src2
  //     dest.x = src0.x > 0.0 ? src1.x : src2.x;
  //     dest.y = src0.y > 0.0 ? src1.y : src2.y;
  //     dest.z = src0.z > 0.0 ? src1.z : src2.z;
  //     dest.w = src0.w > 0.0 ? src1.w : src2.w;
  kCndGt = 14,

  // Four-Element Dot Product
  // dp4 dest, src0, src1
  //     dest.xyzw = src0.x * src1.x + src0.y * src1.y + src0.z * src1.z +
  //                 src0.w * src1.w;
  // Note: only pv.x contains the value.
  kDp4 = 15,

  // Three-Element Dot Product
  // dp3 dest, src0, src1
  //     dest.xyzw = src0.x * src1.x + src0.y * src1.y + src0.z * src1.z;
  // Note: only pv.x contains the value.
  kDp3 = 16,

  // Two-Element Dot Product and Add
  // dp2add dest, src0, src1, src2
  //     dest.xyzw = src0.x * src1.x + src0.y * src1.y + src2.x;
  // Note: only pv.x contains the value.
  kDp2Add = 17,

  // Cube Map
  // cube dest, src0, src1
  //     dest.x = T cube coordinate;
  //     dest.y = S cube coordinate;
  //     dest.z = 2.0 * MajorAxis;
  //     dest.w = FaceID;
  // Expects src0.zzxy and src1.yxzz swizzles.
  // FaceID is D3DCUBEMAP_FACES:
  // https://msdn.microsoft.com/en-us/library/windows/desktop/bb172528(v=vs.85).aspx
  kCube = 18,

  // Four-Element Maximum
  // max4 dest, src0
  //     dest.xyzw = max(src0.x, src0.y, src0.z, src0.w);
  // Note: only pv.x contains the value.
  kMax4 = 19,

  // Floating-Point Predicate Counter Increment If Equal
  // setp_eq_push dest, src0, src1
  //     if (src0.w == 0.0 && src1.w == 0.0) {
  //       p0 = 1;
  //     } else {
  //       p0 = 0;
  //     }
  //     if (src0.x == 0.0 && src1.x == 0.0) {
  //       dest.xyzw = 0.0;
  //     } else {
  //       dest.xyzw = src0.x + 1.0;
  //     }
  kSetpEqPush = 20,

  // Floating-Point Predicate Counter Increment If Not Equal
  // setp_ne_push dest, src0, src1
  //     if (src0.w == 0.0 && src1.w != 0.0) {
  //       p0 = 1;
  //     } else {
  //       p0 = 0;
  //     }
  //     if (src0.x == 0.0 && src1.x != 0.0) {
  //       dest.xyzw = 0.0;
  //     } else {
  //       dest.xyzw = src0.x + 1.0;
  //     }
  kSetpNePush = 21,

  // Floating-Point Predicate Counter Increment If Greater Than
  // setp_gt_push dest, src0, src1
  //     if (src0.w == 0.0 && src1.w > 0.0) {
  //       p0 = 1;
  //     } else {
  //       p0 = 0;
  //     }
  //     if (src0.x == 0.0 && src1.x > 0.0) {
  //       dest.xyzw = 0.0;
  //     } else {
  //       dest.xyzw = src0.x + 1.0;
  //     }
  kSetpGtPush = 22,

  // Floating-Point Predicate Counter Increment If Greater Than Or Equal
  // setp_ge_push dest, src0, src1
  //     if (src0.w == 0.0 && src1.w >= 0.0) {
  //       p0 = 1;
  //     } else {
  //       p0 = 0;
  //     }
  //     if (src0.x == 0.0 && src1.x >= 0.0) {
  //       dest.xyzw = 0.0;
  //     } else {
  //       dest.xyzw = src0.x + 1.0;
  //     }
  kSetpGePush = 23,

  // Floating-Point Pixel Kill If Equal
  // kill_eq dest, src0, src1
  //     if (src0.x == src1.x ||
  //         src0.y == src1.y ||
  //         src0.z == src1.z ||
  //         src0.w == src1.w) {
  //       dest.xyzw = 1.0;
  //       discard;
  //     } else {
  //       dest.xyzw = 0.0;
  //     }
  kKillEq = 24,

  // Floating-Point Pixel Kill If Greater Than
  // kill_gt dest, src0, src1
  //     if (src0.x > src1.x ||
  //         src0.y > src1.y ||
  //         src0.z > src1.z ||
  //         src0.w > src1.w) {
  //       dest.xyzw = 1.0;
  //       discard;
  //     } else {
  //       dest.xyzw = 0.0;
  //     }
  kKillGt = 25,

  // Floating-Point Pixel Kill If Equal
  // kill_ge dest, src0, src1
  //     if (src0.x >= src1.x ||
  //         src0.y >= src1.y ||
  //         src0.z >= src1.z ||
  //         src0.w >= src1.w) {
  //       dest.xyzw = 1.0;
  //       discard;
  //     } else {
  //       dest.xyzw = 0.0;
  //     }
  kKillGe = 26,

  // Floating-Point Pixel Kill If Equal
  // kill_ne dest, src0, src1
  //     if (src0.x != src1.x ||
  //         src0.y != src1.y ||
  //         src0.z != src1.z ||
  //         src0.w != src1.w) {
  //       dest.xyzw = 1.0;
  //       discard;
  //     } else {
  //       dest.xyzw = 0.0;
  //     }
  kKillNe = 27,

  // dst dest, src0, src1
  //     dest.x = 1.0;
  //     dest.y = src0.y * src1.y;
  //     dest.z = src0.z;
  //     dest.w = src1.w;
  kDst = 28,

  // Per-Component Floating-Point Maximum with Copy To Integer in AR
  // maxa dest, src0, src1
  // This is a combined max + mova.
  //     int result = (int)floor(src0.w + 0.5);
  //     a0 = clamp(result, -256, 255);
  //     dest.x = src0.x >= src1.x ? src0.x : src1.x;
  //     dest.y = src0.x >= src1.y ? src0.y : src1.y;
  //     dest.z = src0.x >= src1.z ? src0.z : src1.z;
  //     dest.w = src0.x >= src1.w ? src0.w : src1.w;
  kMaxA = 29,
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

  bool has_vector_op() const { return vector_write_mask() || is_export(); }
  AluVectorOpcode vector_opcode() const {
    return static_cast<AluVectorOpcode>(data_.vector_opc);
  }
  uint32_t vector_write_mask() const { return data_.vector_write_mask; }
  uint32_t vector_dest() const { return data_.vector_dest; }
  bool is_vector_dest_relative() const { return data_.vector_dest_rel == 1; }
  bool vector_clamp() const { return data_.vector_clamp == 1; }

  bool has_scalar_op() const {
    return scalar_opcode() != AluScalarOpcode::kRetainPrev ||
           (!is_export() && scalar_write_mask() != 0);
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
