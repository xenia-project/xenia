/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// Contents originally forked from:
// https://github.com/KhronosGroup/glslang/
//
// Copyright (C) 2014 LunarG, Inc.
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
//    Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//
//    Redistributions in binary form must reproduce the above
//    copyright notice, this list of conditions and the following
//    disclaimer in the documentation and/or other materials provided
//    with the distribution.
//
//    Neither the name of 3Dlabs Inc. Ltd. nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

// SPIRV-IR
//
// Simple in-memory representation (IR) of SPIRV.  Just for holding
// Each function's CFG of blocks.  Has this hierarchy:
//  - Module, which is a list of
//    - Function, which is a list of
//      - Block, which is a list of
//        - Instruction
//

#ifndef XENIA_GPU_SPIRV_SPV_IR_H_
#define XENIA_GPU_SPIRV_SPV_IR_H_

#include <iostream>
#include <vector>

#include "xenia/gpu/spirv/spirv_util.h"

namespace xe {
namespace gpu {
namespace spirv {

using spv::Id;
using spv::Op;

class Function;
class Module;

const Id NoResult = 0;
const Id NoType = 0;

const uint32_t BadValue = 0xFFFFFFFF;
const spv::Decoration NoPrecision = static_cast<spv::Decoration>(BadValue);
const spv::MemorySemanticsMask MemorySemanticsAllMemory =
    static_cast<spv::MemorySemanticsMask>(0x3FF);

class Instruction {
 public:
  Instruction(Id result_id, Id type_id, Op opcode)
      : result_id_(result_id), type_id_(type_id), opcode_(opcode) {}
  explicit Instruction(Op opcode) : opcode_(opcode) {}
  ~Instruction() = default;

  void addIdOperand(Id id) { operands_.push_back(id); }

  void addIdOperands(const std::vector<Id>& ids) {
    for (auto id : ids) {
      operands_.push_back(id);
    }
  }

  void addImmediateOperand(uint32_t immediate) {
    operands_.push_back(immediate);
  }

  template <typename T>
  void addImmediateOperand(T immediate) {
    static_assert(sizeof(T) == sizeof(uint32_t), "Invalid operand size");
    operands_.push_back(static_cast<uint32_t>(immediate));
  }

  void addImmediateOperands(const std::vector<uint32_t>& immediates) {
    for (auto immediate : immediates) {
      operands_.push_back(immediate);
    }
  }

  void addStringOperand(const char* str) {
    original_string_ = str;
    uint32_t word;
    char* wordString = (char*)&word;
    char* wordPtr = wordString;
    int charCount = 0;
    char c;
    do {
      c = *(str++);
      *(wordPtr++) = c;
      ++charCount;
      if (charCount == 4) {
        addImmediateOperand(word);
        wordPtr = wordString;
        charCount = 0;
      }
    } while (c != 0);

    // deal with partial last word
    if (charCount > 0) {
      // pad with 0s
      for (; charCount < 4; ++charCount) {
        *(wordPtr++) = 0;
      }
      addImmediateOperand(word);
    }
  }

  Op opcode() const { return opcode_; }
  int operand_count() const { return static_cast<int>(operands_.size()); }
  Id result_id() const { return result_id_; }
  Id type_id() const { return type_id_; }
  Id id_operand(int op) const { return operands_[op]; }
  uint32_t immediate_operand(int op) const { return operands_[op]; }
  const char* string_operand() const { return original_string_.c_str(); }

  // Write out the binary form.
  void dump(std::vector<uint32_t>& out) const {
    uint32_t wordCount = 1;
    if (type_id_) {
      ++wordCount;
    }
    if (result_id_) {
      ++wordCount;
    }
    wordCount += static_cast<uint32_t>(operands_.size());

    out.push_back((wordCount << spv::WordCountShift) |
                  static_cast<uint32_t>(opcode_));
    if (type_id_) {
      out.push_back(type_id_);
    }
    if (result_id_) {
      out.push_back(result_id_);
    }
    for (auto operand : operands_) {
      out.push_back(operand);
    }
  }

 private:
  Instruction(const Instruction&) = delete;

  Id result_id_ = NoResult;
  Id type_id_ = NoType;
  Op opcode_;
  std::vector<Id> operands_;
  std::string original_string_;  // could be optimized away; convenience for
                                 // getting string operand
};

class Block {
 public:
  Block(Id id, Function& parent);
  ~Block() {
    // TODO: free instructions
  }

  Id id() { return instructions_.front()->result_id(); }

  Function& parent() const { return parent_; }

  void push_instruction(Instruction* inst);
  void push_local_variable(Instruction* inst) {
    local_variables_.push_back(inst);
  }

  void push_predecessor(Block* predecessor) {
    predecessors_.push_back(predecessor);
  }

  int predecessor_count() const {
    return static_cast<int>(predecessors_.size());
  }

  bool is_unreachable() const { return unreachable_; }
  void set_unreachable(bool value) { unreachable_ = value; }

  bool is_terminated() const {
    switch (instructions_.back()->opcode()) {
      case spv::Op::OpBranch:
      case spv::Op::OpBranchConditional:
      case spv::Op::OpSwitch:
      case spv::Op::OpKill:
      case spv::Op::OpReturn:
      case spv::Op::OpReturnValue:
        return true;
      default:
        return false;
    }
  }

  void dump(std::vector<uint32_t>& out) const {
    // skip the degenerate unreachable blocks
    // TODO: code gen: skip all unreachable blocks (transitive closure)
    //                 (but, until that's done safer to keep non-degenerate
    //                 unreachable blocks, in case others depend on something)
    if (unreachable_ && instructions_.size() <= 2) {
      return;
    }

    instructions_[0]->dump(out);
    for (auto variable : local_variables_) {
      variable->dump(out);
    }
    for (int i = 1; i < instructions_.size(); ++i) {
      instructions_[i]->dump(out);
    }
  }

 private:
  Block(const Block&) = delete;
  Block& operator=(Block&) = delete;

  // To enforce keeping parent and ownership in sync:
  friend Function;

  std::vector<Instruction*> instructions_;
  std::vector<Block*> predecessors_;
  std::vector<Instruction*> local_variables_;
  Function& parent_;

  // track whether this block is known to be uncreachable (not necessarily
  // true for all unreachable blocks, but should be set at least
  // for the extraneous ones introduced by the builder).
  bool unreachable_;
};

class Function {
 public:
  Function(Id id, Id resultType, Id functionType, Id firstParam,
           Module& parent);
  ~Function() {
    for (size_t i = 0; i < parameter_instructions_.size(); ++i) {
      delete parameter_instructions_[i];
    }
    for (size_t i = 0; i < blocks_.size(); ++i) {
      delete blocks_[i];
    }
  }

  Id id() const { return function_instruction_.result_id(); }
  Id param_id(int p) { return parameter_instructions_[p]->result_id(); }

  void push_block(Block* block) { blocks_.push_back(block); }
  void pop_block(Block*) { blocks_.pop_back(); }

  Module& parent() const { return parent_; }
  Block* entry_block() const { return blocks_.front(); }
  Block* last_block() const { return blocks_.back(); }

  void push_local_variable(Instruction* inst);

  Id return_type() const { return function_instruction_.type_id(); }

  void dump(std::vector<uint32_t>& out) const {
    // OpFunction
    function_instruction_.dump(out);

    // OpFunctionParameter
    for (auto instruction : parameter_instructions_) {
      instruction->dump(out);
    }

    // Blocks
    for (auto block : blocks_) {
      block->dump(out);
    }

    Instruction end(0, 0, spv::Op::OpFunctionEnd);
    end.dump(out);
  }

 private:
  Function(const Function&) = delete;
  Function& operator=(Function&) = delete;

  Module& parent_;
  Instruction function_instruction_;
  std::vector<Instruction*> parameter_instructions_;
  std::vector<Block*> blocks_;
};

class Module {
 public:
  Module() = default;
  ~Module() {
    // TODO delete things
  }

  void push_function(Function* function) { functions_.push_back(function); }

  void mapInstruction(Instruction* instruction) {
    spv::Id result_id = instruction->result_id();
    // map the instruction's result id
    if (result_id >= id_to_instruction_.size())
      id_to_instruction_.resize(result_id + 16);
    id_to_instruction_[result_id] = instruction;
  }

  Instruction* getInstruction(Id id) const { return id_to_instruction_[id]; }

  spv::Id getTypeId(Id result_id) const {
    return id_to_instruction_[result_id]->type_id();
  }

  spv::StorageClass getStorageClass(Id type_id) const {
    return (spv::StorageClass)id_to_instruction_[type_id]->immediate_operand(0);
  }

  void dump(std::vector<uint32_t>& out) const {
    for (auto function : functions_) {
      function->dump(out);
    }
  }

 private:
  Module(const Module&) = delete;

  std::vector<Function*> functions_;

  // map from result id to instruction having that result id
  std::vector<Instruction*> id_to_instruction_;

  // map from a result id to its type id
};

inline Function::Function(Id id, Id resultType, Id functionType,
                          Id firstParamId, Module& parent)
    : parent_(parent),
      function_instruction_(id, resultType, spv::Op::OpFunction) {
  // OpFunction
  function_instruction_.addImmediateOperand(
      static_cast<uint32_t>(spv::FunctionControlMask::MaskNone));
  function_instruction_.addIdOperand(functionType);
  parent.mapInstruction(&function_instruction_);
  parent.push_function(this);

  // OpFunctionParameter
  Instruction* typeInst = parent.getInstruction(functionType);
  int numParams = typeInst->operand_count() - 1;
  for (int p = 0; p < numParams; ++p) {
    auto param = new Instruction(firstParamId + p, typeInst->id_operand(p + 1),
                                 spv::Op::OpFunctionParameter);
    parent.mapInstruction(param);
    parameter_instructions_.push_back(param);
  }
}

inline void Function::push_local_variable(Instruction* inst) {
  blocks_[0]->push_local_variable(inst);
  parent_.mapInstruction(inst);
}

inline Block::Block(Id id, Function& parent)
    : parent_(parent), unreachable_(false) {
  instructions_.push_back(new Instruction(id, NoType, spv::Op::OpLabel));
}

inline void Block::push_instruction(Instruction* inst) {
  instructions_.push_back(inst);
  if (inst->result_id()) {
    parent_.parent().mapInstruction(inst);
  }
}

}  // namespace spirv
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_SPIRV_SPV_IR_H_
