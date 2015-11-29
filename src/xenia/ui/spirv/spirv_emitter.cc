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

#include "xenia/ui/spirv/spirv_emitter.h"

#include <unordered_set>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"

namespace xe {
namespace ui {
namespace spirv {

SpirvEmitter::SpirvEmitter() { ClearAccessChain(); }

SpirvEmitter::~SpirvEmitter() = default;

Id SpirvEmitter::ImportExtendedInstructions(const char* name) {
  auto import =
      new Instruction(AllocateUniqueId(), NoType, Op::OpExtInstImport);
  import->AddStringOperand(name);

  imports_.push_back(import);
  return import->result_id();
}

// For creating new grouped_types_ (will return old type if the requested one
// was already made).
Id SpirvEmitter::MakeVoidType() {
  Instruction* type;
  auto& grouped_type = grouped_types_[static_cast<int>(Op::OpTypeVoid)];
  if (grouped_type.empty()) {
    type = new Instruction(AllocateUniqueId(), NoType, Op::OpTypeVoid);
    grouped_type.push_back(type);
    constants_types_globals_.push_back(type);
    module_.MapInstruction(type);
  } else {
    type = grouped_type.back();
  }

  return type->result_id();
}

Id SpirvEmitter::MakeBoolType() {
  Instruction* type;
  auto& grouped_type = grouped_types_[static_cast<int>(Op::OpTypeBool)];
  if (grouped_type.empty()) {
    type = new Instruction(AllocateUniqueId(), NoType, Op::OpTypeBool);
    grouped_type.push_back(type);
    constants_types_globals_.push_back(type);
    module_.MapInstruction(type);
  } else {
    type = grouped_type.back();
  }

  return type->result_id();
}

Id SpirvEmitter::MakeSamplerType() {
  Instruction* type;
  auto& grouped_type = grouped_types_[static_cast<int>(Op::OpTypeSampler)];
  if (grouped_type.empty()) {
    type = new Instruction(AllocateUniqueId(), NoType, Op::OpTypeSampler);
    grouped_type.push_back(type);
    constants_types_globals_.push_back(type);
    module_.MapInstruction(type);
  } else {
    type = grouped_type.back();
  }
  return type->result_id();
}

Id SpirvEmitter::MakePointer(spv::StorageClass storage_class, Id pointee) {
  // try to find it
  auto& grouped_type = grouped_types_[static_cast<int>(Op::OpTypePointer)];
  for (auto& type : grouped_type) {
    if (type->immediate_operand(0) == (unsigned)storage_class &&
        type->id_operand(1) == pointee) {
      return type->result_id();
    }
  }

  // not found, make it
  auto type = new Instruction(AllocateUniqueId(), NoType, Op::OpTypePointer);
  type->AddImmediateOperand(storage_class);
  type->AddIdOperand(pointee);
  grouped_type.push_back(type);
  constants_types_globals_.push_back(type);
  module_.MapInstruction(type);

  return type->result_id();
}

Id SpirvEmitter::MakeIntegerType(int bit_width, bool is_signed) {
  // try to find it
  auto& grouped_type = grouped_types_[static_cast<int>(Op::OpTypeInt)];
  for (auto& type : grouped_type) {
    if (type->immediate_operand(0) == (unsigned)bit_width &&
        type->immediate_operand(1) == (is_signed ? 1u : 0u)) {
      return type->result_id();
    }
  }

  // not found, make it
  auto type = new Instruction(AllocateUniqueId(), NoType, Op::OpTypeInt);
  type->AddImmediateOperand(bit_width);
  type->AddImmediateOperand(is_signed ? 1 : 0);
  grouped_type.push_back(type);
  constants_types_globals_.push_back(type);
  module_.MapInstruction(type);

  return type->result_id();
}

Id SpirvEmitter::MakeFloatType(int bit_width) {
  // try to find it
  auto& grouped_type = grouped_types_[static_cast<int>(Op::OpTypeFloat)];
  for (auto& type : grouped_type) {
    if (type->immediate_operand(0) == (unsigned)bit_width) {
      return type->result_id();
    }
  }

  // not found, make it
  auto type = new Instruction(AllocateUniqueId(), NoType, Op::OpTypeFloat);
  type->AddImmediateOperand(bit_width);
  grouped_type.push_back(type);
  constants_types_globals_.push_back(type);
  module_.MapInstruction(type);

  return type->result_id();
}

// Make a struct without checking for duplication.
// See makeStructResultType() for non-decorated structs
// needed as the result of some instructions, which does
// check for duplicates.
Id SpirvEmitter::MakeStructType(std::initializer_list<Id> members,
                                const char* name) {
  // Don't look for previous one, because in the general case,
  // structs can be duplicated except for decorations.

  // not found, make it
  Instruction* type =
      new Instruction(AllocateUniqueId(), NoType, Op::OpTypeStruct);
  type->AddIdOperands(members);
  auto& grouped_type = grouped_types_[static_cast<int>(Op::OpTypeStruct)];
  grouped_type.push_back(type);
  constants_types_globals_.push_back(type);
  module_.MapInstruction(type);
  AddName(type->result_id(), name);

  return type->result_id();
}

// Make a struct for the simple results of several instructions,
// checking for duplication.
Id SpirvEmitter::MakePairStructType(Id type0, Id type1) {
  // try to find it
  auto& grouped_type = grouped_types_[static_cast<int>(Op::OpTypeStruct)];
  for (auto& type : grouped_type) {
    if (type->operand_count() != 2) {
      continue;
    }
    if (type->id_operand(0) != type0 || type->id_operand(1) != type1) {
      continue;
    }
    return type->result_id();
  }

  // not found, make it
  return MakeStructType({type0, type1}, "ResType");
}

Id SpirvEmitter::MakeVectorType(Id component_type, int component_count) {
  // try to find it
  auto& grouped_type = grouped_types_[static_cast<int>(Op::OpTypeVector)];
  for (auto& type : grouped_type) {
    if (type->id_operand(0) == component_type &&
        type->immediate_operand(1) == (unsigned)component_count) {
      return type->result_id();
    }
  }

  // not found, make it
  auto type = new Instruction(AllocateUniqueId(), NoType, Op::OpTypeVector);
  type->AddIdOperand(component_type);
  type->AddImmediateOperand(component_count);
  grouped_type.push_back(type);
  constants_types_globals_.push_back(type);
  module_.MapInstruction(type);

  return type->result_id();
}

Id SpirvEmitter::MakeMatrix2DType(Id component_type, int cols, int rows) {
  assert(cols <= kMaxMatrixSize && rows <= kMaxMatrixSize);

  Id column = MakeVectorType(component_type, rows);

  // try to find it
  auto& grouped_type = grouped_types_[static_cast<int>(Op::OpTypeMatrix)];
  for (auto& type : grouped_type) {
    if (type->id_operand(0) == column &&
        type->immediate_operand(1) == (unsigned)cols) {
      return type->result_id();
    }
  }

  // not found, make it
  auto type = new Instruction(AllocateUniqueId(), NoType, Op::OpTypeMatrix);
  type->AddIdOperand(column);
  type->AddImmediateOperand(cols);
  grouped_type.push_back(type);
  constants_types_globals_.push_back(type);
  module_.MapInstruction(type);

  return type->result_id();
}

Id SpirvEmitter::MakeArrayType(Id element_type, int length) {
  // First, we need a constant instruction for the size
  Id length_id = MakeUintConstant(length);

  // try to find existing type
  auto& grouped_type = grouped_types_[static_cast<int>(Op::OpTypeArray)];
  for (auto& type : grouped_type) {
    if (type->id_operand(0) == element_type &&
        type->id_operand(1) == length_id) {
      return type->result_id();
    }
  }

  // not found, make it
  auto type = new Instruction(AllocateUniqueId(), NoType, Op::OpTypeArray);
  type->AddIdOperand(element_type);
  type->AddIdOperand(length_id);
  grouped_type.push_back(type);
  constants_types_globals_.push_back(type);
  module_.MapInstruction(type);

  return type->result_id();
}

Id SpirvEmitter::MakeRuntimeArray(Id element_type) {
  auto type =
      new Instruction(AllocateUniqueId(), NoType, Op::OpTypeRuntimeArray);
  type->AddIdOperand(element_type);
  constants_types_globals_.push_back(type);
  module_.MapInstruction(type);

  return type->result_id();
}

Id SpirvEmitter::MakeFunctionType(Id return_type,
                                  std::initializer_list<Id> param_types) {
  // try to find it
  auto& grouped_type = grouped_types_[static_cast<int>(Op::OpTypeFunction)];
  for (auto& type : grouped_type) {
    if (type->id_operand(0) == return_type &&
        param_types.size() == type->operand_count() - 1) {
      bool mismatch = false;
      for (int i = 0; i < param_types.size(); ++i) {
        if (type->id_operand(i + 1) != *(param_types.begin() + i)) {
          mismatch = true;
          break;
        }
      }
      if (!mismatch) {
        return type->result_id();
      }
    }
  }

  // not found, make it
  auto type = new Instruction(AllocateUniqueId(), NoType, Op::OpTypeFunction);
  type->AddIdOperand(return_type);
  type->AddIdOperands(param_types);
  grouped_type.push_back(type);
  constants_types_globals_.push_back(type);
  module_.MapInstruction(type);

  return type->result_id();
}

Id SpirvEmitter::MakeImageType(Id sampled_type, spv::Dim dim, bool has_depth,
                               bool is_arrayed, bool is_multisampled,
                               int sampled, spv::ImageFormat format) {
  // try to find it
  auto& grouped_type = grouped_types_[static_cast<int>(Op::OpTypeImage)];
  for (auto& type : grouped_type) {
    if (type->id_operand(0) == sampled_type &&
        type->immediate_operand(1) == (unsigned int)dim &&
        type->immediate_operand(2) == (has_depth ? 1u : 0u) &&
        type->immediate_operand(3) == (is_arrayed ? 1u : 0u) &&
        type->immediate_operand(4) == (is_multisampled ? 1u : 0u) &&
        type->immediate_operand(5) == sampled &&
        type->immediate_operand(6) == static_cast<uint32_t>(format)) {
      return type->result_id();
    }
  }

  // not found, make it
  auto type = new Instruction(AllocateUniqueId(), NoType, Op::OpTypeImage);
  type->AddIdOperand(sampled_type);
  type->AddImmediateOperand(dim);
  type->AddImmediateOperand(has_depth ? 1 : 0);
  type->AddImmediateOperand(is_arrayed ? 1 : 0);
  type->AddImmediateOperand(is_multisampled ? 1 : 0);
  type->AddImmediateOperand(sampled);
  type->AddImmediateOperand(format);

  grouped_type.push_back(type);
  constants_types_globals_.push_back(type);
  module_.MapInstruction(type);

  return type->result_id();
}

Id SpirvEmitter::MakeSampledImageType(Id image_type) {
  // try to find it
  auto& grouped_type = grouped_types_[static_cast<int>(Op::OpTypeSampledImage)];
  for (auto& type : grouped_type) {
    if (type->id_operand(0) == image_type) {
      return type->result_id();
    }
  }

  // not found, make it
  auto type =
      new Instruction(AllocateUniqueId(), NoType, Op::OpTypeSampledImage);
  type->AddIdOperand(image_type);

  grouped_type.push_back(type);
  constants_types_globals_.push_back(type);
  module_.MapInstruction(type);

  return type->result_id();
}

Id SpirvEmitter::GetDerefTypeId(Id result_id) const {
  Id type_id = GetTypeId(result_id);
  assert(IsPointerType(type_id));
  return module_.instruction(type_id)->immediate_operand(1);
}

Op SpirvEmitter::GetMostBasicTypeClass(Id type_id) const {
  auto instr = module_.instruction(type_id);

  Op type_class = instr->opcode();
  switch (type_class) {
    case Op::OpTypeVoid:
    case Op::OpTypeBool:
    case Op::OpTypeInt:
    case Op::OpTypeFloat:
    case Op::OpTypeStruct:
      return type_class;
    case Op::OpTypeVector:
    case Op::OpTypeMatrix:
    case Op::OpTypeArray:
    case Op::OpTypeRuntimeArray:
      return GetMostBasicTypeClass(instr->id_operand(0));
    case Op::OpTypePointer:
      return GetMostBasicTypeClass(instr->id_operand(1));
    default:
      assert(0);
      return Op::OpTypeFloat;
  }
}

int SpirvEmitter::GetTypeComponentCount(Id type_id) const {
  auto instr = module_.instruction(type_id);

  switch (instr->opcode()) {
    case Op::OpTypeBool:
    case Op::OpTypeInt:
    case Op::OpTypeFloat:
      return 1;
    case Op::OpTypeVector:
    case Op::OpTypeMatrix:
      return instr->immediate_operand(1);
    default:
      assert(0);
      return 1;
  }
}

// Return the lowest-level type of scalar that an homogeneous composite is made
// out of.
// Typically, this is just to find out if something is made out of ints or
// floats.
// However, it includes returning a structure, if say, it is an array of
// structure.
Id SpirvEmitter::GetScalarTypeId(Id type_id) const {
  auto instr = module_.instruction(type_id);

  Op type_class = instr->opcode();
  switch (type_class) {
    case Op::OpTypeVoid:
    case Op::OpTypeBool:
    case Op::OpTypeInt:
    case Op::OpTypeFloat:
    case Op::OpTypeStruct:
      return instr->result_id();
    case Op::OpTypeVector:
    case Op::OpTypeMatrix:
    case Op::OpTypeArray:
    case Op::OpTypeRuntimeArray:
    case Op::OpTypePointer:
      return GetScalarTypeId(GetContainedTypeId(type_id));
    default:
      assert(0);
      return NoResult;
  }
}

// Return the type of 'member' of a composite.
Id SpirvEmitter::GetContainedTypeId(Id type_id, int member) const {
  auto instr = module_.instruction(type_id);

  Op type_class = instr->opcode();
  switch (type_class) {
    case Op::OpTypeVector:
    case Op::OpTypeMatrix:
    case Op::OpTypeArray:
    case Op::OpTypeRuntimeArray:
      return instr->id_operand(0);
    case Op::OpTypePointer:
      return instr->id_operand(1);
    case Op::OpTypeStruct:
      return instr->id_operand(member);
    default:
      assert(0);
      return NoResult;
  }
}

// Return the immediately contained type of a given composite type.
Id SpirvEmitter::GetContainedTypeId(Id type_id) const {
  return GetContainedTypeId(type_id, 0);
}

// See if a scalar constant of this type has already been created, so it
// can be reused rather than duplicated.  (Required by the specification).
Id SpirvEmitter::FindScalarConstant(Op type_class, Op opcode, Id type_id,
                                    uint32_t value) const {
  auto& grouped_constant = grouped_constants_[static_cast<int>(type_class)];
  for (auto constant : grouped_constant) {
    if (constant->opcode() == opcode && constant->type_id() == type_id &&
        constant->immediate_operand(0) == value) {
      return constant->result_id();
    }
  }
  return 0;
}

// Version of findScalarConstant (see above) for scalars that take two operands
// (e.g. a 'double').
Id SpirvEmitter::FindScalarConstant(Op type_class, Op opcode, Id type_id,
                                    uint32_t v1, uint32_t v2) const {
  auto& grouped_constant = grouped_constants_[static_cast<int>(type_class)];
  for (auto constant : grouped_constant) {
    if (constant->opcode() == opcode && constant->type_id() == type_id &&
        constant->immediate_operand(0) == v1 &&
        constant->immediate_operand(1) == v2) {
      return constant->result_id();
    }
  }
  return 0;
}

// Return true if consuming 'opcode' means consuming a constant.
// "constant" here means after final transform to executable code,
// the value consumed will be a constant, so includes specialization.
bool SpirvEmitter::IsConstantOpCode(Op opcode) const {
  switch (opcode) {
    case Op::OpUndef:
    case Op::OpConstantTrue:
    case Op::OpConstantFalse:
    case Op::OpConstant:
    case Op::OpConstantComposite:
    case Op::OpConstantSampler:
    case Op::OpConstantNull:
    case Op::OpSpecConstantTrue:
    case Op::OpSpecConstantFalse:
    case Op::OpSpecConstant:
    case Op::OpSpecConstantComposite:
    case Op::OpSpecConstantOp:
      return true;
    default:
      return false;
  }
}

Id SpirvEmitter::MakeBoolConstant(bool value, bool is_spec_constant) {
  Id type_id = MakeBoolType();
  Op opcode = is_spec_constant
                  ? (value ? Op::OpSpecConstantTrue : Op::OpSpecConstantFalse)
                  : (value ? Op::OpConstantTrue : Op::OpConstantFalse);

  // See if we already made it
  Id existing = 0;
  auto& grouped_constant = grouped_constants_[static_cast<int>(Op::OpTypeBool)];
  for (auto& constant : grouped_constant) {
    if (constant->type_id() == type_id && constant->opcode() == opcode) {
      return constant->result_id();
    }
  }

  // Make it
  auto c = new Instruction(AllocateUniqueId(), type_id, opcode);
  constants_types_globals_.push_back(c);
  grouped_constants_[static_cast<int>(Op::OpTypeBool)].push_back(c);
  module_.MapInstruction(c);

  return c->result_id();
}

Id SpirvEmitter::MakeIntegerConstant(Id type_id, uint32_t value,
                                     bool is_spec_constant) {
  Op opcode = is_spec_constant ? Op::OpSpecConstant : Op::OpConstant;
  Id existing = FindScalarConstant(Op::OpTypeInt, opcode, type_id, value);
  if (existing) {
    return existing;
  }

  auto c = new Instruction(AllocateUniqueId(), type_id, opcode);
  c->AddImmediateOperand(value);
  constants_types_globals_.push_back(c);
  grouped_constants_[static_cast<int>(Op::OpTypeInt)].push_back(c);
  module_.MapInstruction(c);

  return c->result_id();
}

Id SpirvEmitter::MakeFloatConstant(float value, bool is_spec_constant) {
  Op opcode = is_spec_constant ? Op::OpSpecConstant : Op::OpConstant;
  Id type_id = MakeFloatType(32);
  uint32_t uint32_value = *reinterpret_cast<uint32_t*>(&value);
  Id existing =
      FindScalarConstant(Op::OpTypeFloat, opcode, type_id, uint32_value);
  if (existing) {
    return existing;
  }

  auto c = new Instruction(AllocateUniqueId(), type_id, opcode);
  c->AddImmediateOperand(uint32_value);
  constants_types_globals_.push_back(c);
  grouped_constants_[static_cast<int>(Op::OpTypeFloat)].push_back(c);
  module_.MapInstruction(c);

  return c->result_id();
}

Id SpirvEmitter::MakeDoubleConstant(double value, bool is_spec_constant) {
  Op opcode = is_spec_constant ? Op::OpSpecConstant : Op::OpConstant;
  Id type_id = MakeFloatType(64);
  uint64_t uint64_value = *reinterpret_cast<uint64_t*>(&value);
  uint32_t op1 = static_cast<uint32_t>(uint64_value & 0xFFFFFFFF);
  uint32_t op2 = static_cast<uint32_t>(uint64_value >> 32);
  Id existing = FindScalarConstant(Op::OpTypeFloat, opcode, type_id, op1, op2);
  if (existing) {
    return existing;
  }

  auto c = new Instruction(AllocateUniqueId(), type_id, opcode);
  c->AddImmediateOperand(op1);
  c->AddImmediateOperand(op2);
  constants_types_globals_.push_back(c);
  grouped_constants_[static_cast<int>(Op::OpTypeFloat)].push_back(c);
  module_.MapInstruction(c);

  return c->result_id();
}

Id SpirvEmitter::FindCompositeConstant(
    Op type_class, std::initializer_list<Id> components) const {
  auto& grouped_constant = grouped_constants_[static_cast<int>(type_class)];
  for (auto& constant : grouped_constant) {
    // same shape?
    if (constant->operand_count() != components.size()) {
      continue;
    }

    // same contents?
    bool mismatch = false;
    for (int op = 0; op < constant->operand_count(); ++op) {
      if (constant->id_operand(op) != *(components.begin() + op)) {
        mismatch = true;
        break;
      }
    }
    if (!mismatch) {
      return constant->result_id();
    }
  }

  return NoResult;
}

Id SpirvEmitter::MakeCompositeConstant(Id type_id,
                                       std::initializer_list<Id> components) {
  assert(type_id);
  Op type_class = GetTypeClass(type_id);

  switch (type_class) {
    case Op::OpTypeVector:
    case Op::OpTypeArray:
    case Op::OpTypeStruct:
    case Op::OpTypeMatrix:
      break;
    default:
      assert(0);
      return MakeFloatConstant(0.0);
  }

  Id existing = FindCompositeConstant(type_class, components);
  if (existing) {
    return existing;
  }

  auto c =
      new Instruction(AllocateUniqueId(), type_id, Op::OpConstantComposite);
  c->AddIdOperands(components);
  constants_types_globals_.push_back(c);
  grouped_constants_[static_cast<int>(type_class)].push_back(c);
  module_.MapInstruction(c);

  return c->result_id();
}

Instruction* SpirvEmitter::AddEntryPoint(spv::ExecutionModel execution_model,
                                         Function* entry_point,
                                         const char* name) {
  auto instr = new Instruction(Op::OpEntryPoint);
  instr->AddImmediateOperand(execution_model);
  instr->AddIdOperand(entry_point->id());
  instr->AddStringOperand(name);

  entry_points_.push_back(instr);

  return instr;
}

// Currently relying on the fact that all 'value' of interest are small
// non-negative values.
void SpirvEmitter::AddExecutionMode(Function* entry_point,
                                    spv::ExecutionMode execution_mode,
                                    int value1, int value2, int value3) {
  auto instr = new Instruction(Op::OpExecutionMode);
  instr->AddIdOperand(entry_point->id());
  instr->AddImmediateOperand(execution_mode);
  if (value1 >= 0) {
    instr->AddImmediateOperand(value1);
  }
  if (value2 >= 0) {
    instr->AddImmediateOperand(value2);
  }
  if (value3 >= 0) {
    instr->AddImmediateOperand(value3);
  }

  execution_modes_.push_back(instr);
}

void SpirvEmitter::AddName(Id target_id, const char* value) {
  if (!value) {
    return;
  }
  auto instr = new Instruction(Op::OpName);
  instr->AddIdOperand(target_id);
  instr->AddStringOperand(value);

  names_.push_back(instr);
}

void SpirvEmitter::AddMemberName(Id target_id, int member, const char* value) {
  if (!value) {
    return;
  }
  auto instr = new Instruction(Op::OpMemberName);
  instr->AddIdOperand(target_id);
  instr->AddImmediateOperand(member);
  instr->AddStringOperand(value);

  names_.push_back(instr);
}

void SpirvEmitter::AddLine(Id target_id, Id file_name, int line_number,
                           int column_number) {
  auto instr = new Instruction(Op::OpLine);
  instr->AddIdOperand(target_id);
  instr->AddIdOperand(file_name);
  instr->AddImmediateOperand(line_number);
  instr->AddImmediateOperand(column_number);

  lines_.push_back(instr);
}

void SpirvEmitter::AddDecoration(Id target_id, spv::Decoration decoration,
                                 int num) {
  if (decoration == static_cast<spv::Decoration>(BadValue)) {
    return;
  }
  auto instr = new Instruction(Op::OpDecorate);
  instr->AddIdOperand(target_id);
  instr->AddImmediateOperand(decoration);
  if (num >= 0) {
    instr->AddImmediateOperand(num);
  }

  decorations_.push_back(instr);
}

void SpirvEmitter::AddMemberDecoration(Id target_id, int member,
                                       spv::Decoration decoration, int num) {
  auto instr = new Instruction(Op::OpMemberDecorate);
  instr->AddIdOperand(target_id);
  instr->AddImmediateOperand(member);
  instr->AddImmediateOperand(decoration);
  if (num >= 0) {
    instr->AddImmediateOperand(num);
  }

  decorations_.push_back(instr);
}

Function* SpirvEmitter::MakeMainEntry() {
  assert(!main_function_);
  Block* entry = nullptr;
  main_function_ = MakeFunctionEntry(MakeVoidType(), "main", {}, &entry);
  return main_function_;
}

Function* SpirvEmitter::MakeFunctionEntry(Id return_type, const char* name,
                                          std::initializer_list<Id> param_types,
                                          Block** entry) {
  Id type_id = MakeFunctionType(return_type, param_types);
  Id first_param_id =
      param_types.size() ? AllocateUniqueIds((int)param_types.size()) : 0;
  auto function = new Function(AllocateUniqueId(), return_type, type_id,
                               first_param_id, module_);
  if (entry) {
    *entry = new Block(AllocateUniqueId(), *function);
    function->push_block(*entry);
    set_build_point(*entry);
  }
  AddName(function->id(), name);
  return function;
}

void SpirvEmitter::MakeReturn(bool implicit, Id return_value) {
  if (return_value) {
    auto inst = new Instruction(NoResult, NoType, Op::OpReturnValue);
    inst->AddIdOperand(return_value);
    build_point_->AddInstruction(inst);
  } else {
    build_point_->AddInstruction(
        new Instruction(NoResult, NoType, Op::OpReturn));
  }

  if (!implicit) {
    CreateAndSetNoPredecessorBlock("post-return");
  }
}

void SpirvEmitter::LeaveFunction() {
  Block* block = build_point_;
  Function& function = build_point_->parent();
  assert(block);

  // If our function did not contain a return, add a return void now.
  if (!block->is_terminated()) {
    // Whether we're in an unreachable (non-entry) block.
    bool unreachable =
        function.entry_block() != block && !block->predecessor_count();

    if (unreachable) {
      // Given that this block is at the end of a function, it must be right
      // after an explicit return, just remove it.
      function.pop_block(block);
    } else {
      // We'll add a return instruction at the end of the current block,
      // which for a non-void function is really error recovery (?), as the
      // source being translated should have had an explicit return, which would
      // have been followed by an unreachable block, which was handled above.
      if (function.return_type() == MakeVoidType()) {
        MakeReturn(true);
      } else {
        MakeReturn(true, CreateUndefined(function.return_type()));
      }
    }
  }
}

void SpirvEmitter::MakeDiscard() {
  build_point_->AddInstruction(new Instruction(Op::OpKill));
  CreateAndSetNoPredecessorBlock("post-discard");
}

Id SpirvEmitter::CreateVariable(spv::StorageClass storage_class, Id type,
                                const char* name) {
  Id pointer_type = MakePointer(storage_class, type);
  auto instr =
      new Instruction(AllocateUniqueId(), pointer_type, Op::OpVariable);
  instr->AddImmediateOperand(storage_class);

  switch (storage_class) {
    case spv::StorageClass::Function:
      // Validation rules require the declaration in the entry block.
      build_point_->parent().AddLocalVariable(instr);
      break;
    default:
      constants_types_globals_.push_back(instr);
      module_.MapInstruction(instr);
      break;
  }

  AddName(instr->result_id(), name);

  return instr->result_id();
}

Id SpirvEmitter::CreateUndefined(Id type) {
  auto instr = new Instruction(AllocateUniqueId(), type, Op::OpUndef);
  build_point_->AddInstruction(instr);
  return instr->result_id();
}

void SpirvEmitter::CreateStore(Id pointer_id, Id value_id) {
  auto instr = new Instruction(Op::OpStore);
  instr->AddIdOperand(pointer_id);
  instr->AddIdOperand(value_id);
  build_point_->AddInstruction(instr);
}

Id SpirvEmitter::CreateLoad(Id pointer_id) {
  auto instr = new Instruction(AllocateUniqueId(), GetDerefTypeId(pointer_id),
                               Op::OpLoad);
  instr->AddIdOperand(pointer_id);
  build_point_->AddInstruction(instr);

  return instr->result_id();
}

Id SpirvEmitter::CreateAccessChain(spv::StorageClass storage_class, Id base_id,
                                   std::vector<Id> index_ids) {
  // Figure out the final resulting type.
  auto base_type_id = GetTypeId(base_id);
  assert(IsPointerType(base_type_id) && index_ids.size());
  auto type_id = GetContainedTypeId(base_type_id);
  for (auto index_id : index_ids) {
    if (IsStructType(type_id)) {
      assert(IsConstantScalar(index_id));
      type_id = GetContainedTypeId(type_id, GetConstantScalar(index_id));
    } else {
      type_id = GetContainedTypeId(type_id, index_id);
    }
  }
  auto chain_type_id = MakePointer(storage_class, type_id);

  // Make the instruction
  auto instr =
      new Instruction(AllocateUniqueId(), chain_type_id, Op::OpAccessChain);
  instr->AddIdOperand(base_id);
  instr->AddIdOperands(index_ids);
  build_point_->AddInstruction(instr);

  return instr->result_id();
}

Id SpirvEmitter::CreateArrayLength(Id struct_id, int array_member) {
  auto instr =
      new Instruction(AllocateUniqueId(), MakeIntType(32), Op::OpArrayLength);
  instr->AddIdOperand(struct_id);
  instr->AddImmediateOperand(array_member);
  build_point_->AddInstruction(instr);

  return instr->result_id();
}

Id SpirvEmitter::CreateCompositeExtract(Id composite, Id type_id,
                                        uint32_t index) {
  auto instr =
      new Instruction(AllocateUniqueId(), type_id, Op::OpCompositeExtract);
  instr->AddIdOperand(composite);
  instr->AddImmediateOperand(index);
  build_point_->AddInstruction(instr);

  return instr->result_id();
}

Id SpirvEmitter::CreateCompositeExtract(Id composite, Id type_id,
                                        std::vector<uint32_t> indices) {
  auto instr =
      new Instruction(AllocateUniqueId(), type_id, Op::OpCompositeExtract);
  instr->AddIdOperand(composite);
  instr->AddImmediateOperands(indices);
  build_point_->AddInstruction(instr);

  return instr->result_id();
}

Id SpirvEmitter::CreateCompositeInsert(Id object, Id composite, Id type_id,
                                       uint32_t index) {
  auto instr =
      new Instruction(AllocateUniqueId(), type_id, Op::OpCompositeInsert);
  instr->AddIdOperand(object);
  instr->AddIdOperand(composite);
  instr->AddImmediateOperand(index);
  build_point_->AddInstruction(instr);

  return instr->result_id();
}

Id SpirvEmitter::CreateCompositeInsert(Id object, Id composite, Id type_id,
                                       std::vector<uint32_t> indices) {
  auto instr =
      new Instruction(AllocateUniqueId(), type_id, Op::OpCompositeInsert);
  instr->AddIdOperand(object);
  instr->AddIdOperand(composite);
  instr->AddImmediateOperands(indices);
  build_point_->AddInstruction(instr);

  return instr->result_id();
}

Id SpirvEmitter::CreateVectorExtractDynamic(Id vector, Id type_id,
                                            Id component_index) {
  auto instr =
      new Instruction(AllocateUniqueId(), type_id, Op::OpVectorExtractDynamic);
  instr->AddIdOperand(vector);
  instr->AddIdOperand(component_index);
  build_point_->AddInstruction(instr);

  return instr->result_id();
}

Id SpirvEmitter::CreateVectorInsertDynamic(Id vector, Id type_id, Id component,
                                           Id component_index) {
  auto instr =
      new Instruction(AllocateUniqueId(), type_id, Op::OpVectorInsertDynamic);
  instr->AddIdOperand(vector);
  instr->AddIdOperand(component);
  instr->AddIdOperand(component_index);
  build_point_->AddInstruction(instr);

  return instr->result_id();
}

void SpirvEmitter::CreateNop() {
  auto instr = new Instruction(spv::Op::OpNop);
  build_point_->AddInstruction(instr);
}

void SpirvEmitter::CreateControlBarrier(
    spv::Scope execution_scope, spv::Scope memory_scope,
    spv::MemorySemanticsMask memory_semantics) {
  auto instr = new Instruction(Op::OpControlBarrier);
  instr->AddImmediateOperand(MakeUintConstant(execution_scope));
  instr->AddImmediateOperand(MakeUintConstant(memory_scope));
  instr->AddImmediateOperand(MakeUintConstant(memory_semantics));
  build_point_->AddInstruction(instr);
}

void SpirvEmitter::CreateMemoryBarrier(
    spv::Scope execution_scope, spv::MemorySemanticsMask memory_semantics) {
  auto instr = new Instruction(Op::OpMemoryBarrier);
  instr->AddImmediateOperand(MakeUintConstant(execution_scope));
  instr->AddImmediateOperand(MakeUintConstant(memory_semantics));
  build_point_->AddInstruction(instr);
}

Id SpirvEmitter::CreateUnaryOp(Op opcode, Id type_id, Id operand) {
  auto instr = new Instruction(AllocateUniqueId(), type_id, opcode);
  instr->AddIdOperand(operand);
  build_point_->AddInstruction(instr);

  return instr->result_id();
}

Id SpirvEmitter::CreateBinOp(Op opcode, Id type_id, Id left, Id right) {
  auto instr = new Instruction(AllocateUniqueId(), type_id, opcode);
  instr->AddIdOperand(left);
  instr->AddIdOperand(right);
  build_point_->AddInstruction(instr);

  return instr->result_id();
}

Id SpirvEmitter::CreateTriOp(Op opcode, Id type_id, Id op1, Id op2, Id op3) {
  auto instr = new Instruction(AllocateUniqueId(), type_id, opcode);
  instr->AddIdOperand(op1);
  instr->AddIdOperand(op2);
  instr->AddIdOperand(op3);
  build_point_->AddInstruction(instr);

  return instr->result_id();
}

Id SpirvEmitter::CreateOp(Op opcode, Id type_id,
                          const std::vector<Id>& operands) {
  auto instr = new Instruction(AllocateUniqueId(), type_id, opcode);
  instr->AddIdOperands(operands);
  build_point_->AddInstruction(instr);

  return instr->result_id();
}

Id SpirvEmitter::CreateFunctionCall(Function* function,
                                    std::vector<spv::Id> args) {
  auto instr = new Instruction(AllocateUniqueId(), function->return_type(),
                               Op::OpFunctionCall);
  instr->AddIdOperand(function->id());
  instr->AddIdOperands(args);
  build_point_->AddInstruction(instr);

  return instr->result_id();
}

Id SpirvEmitter::CreateSwizzle(Id type_id, Id source,
                               std::vector<uint32_t> channels) {
  if (channels.size() == 1) {
    return CreateCompositeExtract(source, type_id, channels.front());
  }
  auto instr =
      new Instruction(AllocateUniqueId(), type_id, Op::OpVectorShuffle);
  assert(IsVector(source));
  instr->AddIdOperand(source);
  instr->AddIdOperand(source);
  instr->AddImmediateOperands(channels);
  build_point_->AddInstruction(instr);

  return instr->result_id();
}

Id SpirvEmitter::CreateLvalueSwizzle(Id type_id, Id target, Id source,
                                     std::vector<uint32_t> channels) {
  assert(GetComponentCount(source) == channels.size());
  if (channels.size() == 1 && GetComponentCount(source) == 1) {
    return CreateCompositeInsert(source, target, type_id, channels.front());
  }

  auto instr =
      new Instruction(AllocateUniqueId(), type_id, Op::OpVectorShuffle);
  assert(IsVector(source));
  assert(IsVector(target));
  instr->AddIdOperand(target);
  instr->AddIdOperand(source);

  // Set up an identity shuffle from the base value to the result value.
  uint32_t components[4] = {0, 1, 2, 3};

  // Punch in the l-value swizzle.
  int component_count = GetComponentCount(target);
  for (int i = 0; i < (int)channels.size(); ++i) {
    components[channels[i]] = component_count + i;
  }

  // finish the instruction with these components selectors.
  for (int i = 0; i < component_count; ++i) {
    instr->AddImmediateOperand(components[i]);
  }
  build_point_->AddInstruction(instr);

  return instr->result_id();
}

void SpirvEmitter::PromoteScalar(spv::Decoration precision, Id& left,
                                 Id& right) {
  int direction = GetComponentCount(right) - GetComponentCount(left);
  if (direction > 0) {
    left = SmearScalar(precision, left, GetTypeId(right));
  } else if (direction < 0) {
    right = SmearScalar(precision, right, GetTypeId(left));
  }
}

Id SpirvEmitter::SmearScalar(spv::Decoration precision, Id scalar_value,
                             Id vector_type_id) {
  assert(GetComponentCount(scalar_value) == 1);
  int component_count = GetTypeComponentCount(vector_type_id);
  if (component_count == 1) {
    return scalar_value;
  }

  auto instr = new Instruction(AllocateUniqueId(), vector_type_id,
                               Op::OpCompositeConstruct);
  for (int i = 0; i < component_count; ++i) {
    instr->AddIdOperand(scalar_value);
  }
  build_point_->AddInstruction(instr);

  return instr->result_id();
}

Id SpirvEmitter::CreateExtendedInstructionCall(spv::Decoration precision,
                                               Id result_type,
                                               Id instruction_set,
                                               int instruction_ordinal,
                                               std::initializer_list<Id> args) {
  auto instr = new Instruction(AllocateUniqueId(), result_type, Op::OpExtInst);
  instr->AddIdOperand(instruction_set);
  instr->AddImmediateOperand(instruction_ordinal);
  instr->AddIdOperands(args);

  build_point_->AddInstruction(instr);
  return instr->result_id();
}

Id SpirvEmitter::CreateGlslStd450InstructionCall(
    spv::Decoration precision, Id result_type,
    spv::GLSLstd450 instruction_ordinal, std::initializer_list<Id> args) {
  if (!glsl_std_450_instruction_set_) {
    glsl_std_450_instruction_set_ = ImportExtendedInstructions("GLSL.std.450");
  }
  return CreateExtendedInstructionCall(
      precision, result_type, glsl_std_450_instruction_set_,
      static_cast<int>(instruction_ordinal), args);
}

// Accept all parameters needed to create a texture instruction.
// Create the correct instruction based on the inputs, and make the call.
Id SpirvEmitter::CreateTextureCall(spv::Decoration precision, Id result_type,
                                   bool fetch, bool proj, bool gather,
                                   const TextureParameters& parameters) {
  static const int kMaxTextureArgs = 10;
  Id tex_args[kMaxTextureArgs] = {};

  // Set up the fixed arguments.
  int arg_count = 0;
  bool is_explicit = false;
  tex_args[arg_count++] = parameters.sampler;
  tex_args[arg_count++] = parameters.coords;
  if (parameters.depth_ref) {
    tex_args[arg_count++] = parameters.depth_ref;
  }
  if (parameters.comp) {
    tex_args[arg_count++] = parameters.comp;
  }

  // Set up the optional arguments.
  int opt_arg_index = arg_count;  // track which operand, if it exists, is the
                                  // mask of optional arguments speculatively
                                  // make room for the mask operand.
  ++arg_count;
  auto mask = spv::ImageOperandsMask::MaskNone;  // the mask operand
  if (parameters.bias) {
    mask = mask | spv::ImageOperandsMask::Bias;
    tex_args[arg_count++] = parameters.bias;
  }
  if (parameters.lod) {
    mask = mask | spv::ImageOperandsMask::Lod;
    tex_args[arg_count++] = parameters.lod;
    is_explicit = true;
  }
  if (parameters.grad_x) {
    mask = mask | spv::ImageOperandsMask::Grad;
    tex_args[arg_count++] = parameters.grad_x;
    tex_args[arg_count++] = parameters.grad_y;
    is_explicit = true;
  }
  if (parameters.offset) {
    if (IsConstant(parameters.offset)) {
      mask = mask | spv::ImageOperandsMask::ConstOffset;
    } else {
      mask = mask | spv::ImageOperandsMask::Offset;
    }
    tex_args[arg_count++] = parameters.offset;
  }
  if (parameters.offsets) {
    mask = mask | spv::ImageOperandsMask::ConstOffsets;
    tex_args[arg_count++] = parameters.offsets;
  }
  if (parameters.sample) {
    mask = mask | spv::ImageOperandsMask::Sample;
    tex_args[arg_count++] = parameters.sample;
  }
  if (mask == spv::ImageOperandsMask::MaskNone) {
    --arg_count;  // undo speculative reservation for the mask argument
  } else {
    tex_args[opt_arg_index] = static_cast<Id>(mask);
  }

  // Set up the instruction.
  Op opcode;
  opcode = Op::OpImageSampleImplicitLod;
  if (fetch) {
    opcode = Op::OpImageFetch;
  } else if (gather) {
    if (parameters.depth_ref) {
      opcode = Op::OpImageDrefGather;
    } else {
      opcode = Op::OpImageGather;
    }
  } else if (is_explicit) {
    if (parameters.depth_ref) {
      if (proj) {
        opcode = Op::OpImageSampleProjDrefExplicitLod;
      } else {
        opcode = Op::OpImageSampleDrefExplicitLod;
      }
    } else {
      if (proj) {
        opcode = Op::OpImageSampleProjExplicitLod;
      } else {
        opcode = Op::OpImageSampleExplicitLod;
      }
    }
  } else {
    if (parameters.depth_ref) {
      if (proj) {
        opcode = Op::OpImageSampleProjDrefImplicitLod;
      } else {
        opcode = Op::OpImageSampleDrefImplicitLod;
      }
    } else {
      if (proj) {
        opcode = Op::OpImageSampleProjImplicitLod;
      } else {
        opcode = Op::OpImageSampleImplicitLod;
      }
    }
  }

  // See if the result type is expecting a smeared result.
  // This happens when a legacy shadow*() call is made, which gets a vec4 back
  // instead of a float.
  Id smeared_type = result_type;
  if (!IsScalarType(result_type)) {
    switch (opcode) {
      case Op::OpImageSampleDrefImplicitLod:
      case Op::OpImageSampleDrefExplicitLod:
      case Op::OpImageSampleProjDrefImplicitLod:
      case Op::OpImageSampleProjDrefExplicitLod:
        result_type = GetScalarTypeId(result_type);
        break;
      default:
        break;
    }
  }

  // Build the SPIR-V instruction
  auto instr = new Instruction(AllocateUniqueId(), result_type, opcode);
  for (int op = 0; op < opt_arg_index; ++op) {
    instr->AddIdOperand(tex_args[op]);
  }
  if (opt_arg_index < arg_count) {
    instr->AddImmediateOperand(tex_args[opt_arg_index]);
  }
  for (int op = opt_arg_index + 1; op < arg_count; ++op) {
    instr->AddIdOperand(tex_args[op]);
  }
  SetPrecision(instr->result_id(), precision);
  build_point_->AddInstruction(instr);

  Id result_id = instr->result_id();

  // When a smear is needed, do it, as per what was computed above when
  // result_type was changed to a scalar type.
  if (result_type != smeared_type) {
    result_id = SmearScalar(precision, result_id, smeared_type);
  }

  return result_id;
}

Id SpirvEmitter::CreateTextureQueryCall(Op opcode,
                                        const TextureParameters& parameters) {
  // Figure out the result type.
  Id result_type = 0;
  switch (opcode) {
    case Op::OpImageQuerySize:
    case Op::OpImageQuerySizeLod: {
      int component_count;
      switch (GetTypeDimensionality(GetImageType(parameters.sampler))) {
        case spv::Dim::Dim1D:
        case spv::Dim::Buffer:
          component_count = 1;
          break;
        case spv::Dim::Dim2D:
        case spv::Dim::Cube:
        case spv::Dim::Rect:
          component_count = 2;
          break;
        case spv::Dim::Dim3D:
          component_count = 3;
          break;
        case spv::Dim::SubpassData:
          CheckNotImplemented("input-attachment dim");
          break;
        default:
          assert(0);
          break;
      }
      if (IsArrayedImageType(GetImageType(parameters.sampler))) {
        ++component_count;
      }
      if (component_count == 1) {
        result_type = MakeIntType(32);
      } else {
        result_type = MakeVectorType(MakeIntType(32), component_count);
      }
      break;
    }
    case Op::OpImageQueryLod:
      result_type = MakeVectorType(MakeFloatType(32), 2);
      break;
    case Op::OpImageQueryLevels:
    case Op::OpImageQuerySamples:
      result_type = MakeIntType(32);
      break;
    default:
      assert(0);
      break;
  }

  auto instr = new Instruction(AllocateUniqueId(), result_type, opcode);
  instr->AddIdOperand(parameters.sampler);
  if (parameters.coords) {
    instr->AddIdOperand(parameters.coords);
  }
  if (parameters.lod) {
    instr->AddIdOperand(parameters.lod);
  }
  build_point_->AddInstruction(instr);

  return instr->result_id();
}

Id SpirvEmitter::CreateCompare(spv::Decoration precision, Id value1, Id value2,
                               bool is_equal) {
  Id bool_type_id = MakeBoolType();
  Id value_type_id = GetTypeId(value1);

  assert(value_type_id == GetTypeId(value2));
  assert(!IsScalar(value1));

  // Vectors.
  if (IsVectorType(value_type_id)) {
    Op op;
    if (GetMostBasicTypeClass(value_type_id) == Op::OpTypeFloat) {
      op = is_equal ? Op::OpFOrdEqual : Op::OpFOrdNotEqual;
    } else {
      op = is_equal ? Op::OpIEqual : Op::OpINotEqual;
    }

    Id bool_vector_type_id =
        MakeVectorType(bool_type_id, GetTypeComponentCount(value_type_id));
    Id bool_vector = CreateBinOp(op, bool_vector_type_id, value1, value2);
    SetPrecision(bool_vector, precision);

    // Reduce vector compares with any() and all().
    op = is_equal ? Op::OpAll : Op::OpAny;

    return CreateUnaryOp(op, bool_type_id, bool_vector);
  }

  CheckNotImplemented("Composite comparison of non-vectors");
  return NoResult;

  // Recursively handle aggregates, which include matrices, arrays, and
  // structures
  // and accumulate the results.

  // Matrices

  // Arrays

  // int numElements;
  // const llvm::ArrayType* arrayType =
  // llvm::dyn_cast<llvm::ArrayType>(value1->getType());
  // if (arrayType)
  //    numElements = (int)arrayType->getNumElements();
  // else {
  //    // better be structure
  //    const llvm::StructType* structType =
  //    llvm::dyn_cast<llvm::StructType>(value1->getType());
  //    assert(structType);
  //    numElements = structType->getNumElements();
  //}

  // assert(numElements > 0);

  // for (int element = 0; element < numElements; ++element) {
  //    // Get intermediate comparison values
  //    llvm::Value* element1 = builder.CreateExtractValue(value1, element,
  //    "element1");
  //    setInstructionPrecision(element1, precision);
  //    llvm::Value* element2 = builder.CreateExtractValue(value2, element,
  //    "element2");
  //    setInstructionPrecision(element2, precision);

  //    llvm::Value* subResult = createCompare(precision, element1, element2,
  //    equal, "comp");

  //    // Accumulate intermediate comparison
  //    if (element == 0)
  //        result = subResult;
  //    else {
  //        if (equal)
  //            result = builder.CreateAnd(result, subResult);
  //        else
  //            result = builder.CreateOr(result, subResult);
  //        setInstructionPrecision(result, precision);
  //    }
  //}

  // return result;
}

// OpCompositeConstruct
Id SpirvEmitter::CreateCompositeConstruct(Id type_id,
                                          std::vector<Id> constituent_ids) {
  assert(IsAggregateType(type_id) ||
         (GetTypeComponentCount(type_id) > 1 &&
          GetTypeComponentCount(type_id) == constituent_ids.size()));

  auto instr =
      new Instruction(AllocateUniqueId(), type_id, Op::OpCompositeConstruct);
  instr->AddIdOperands(constituent_ids);
  build_point_->AddInstruction(instr);

  return instr->result_id();
}

Id SpirvEmitter::CreateConstructor(spv::Decoration precision,
                                   std::vector<Id> source_ids,
                                   Id result_type_id) {
  Id result = 0;
  int target_component_count = GetTypeComponentCount(result_type_id);
  int target_component = 0;

  // Special case: when calling a vector constructor with a single scalar
  // argument, smear the scalar
  if (source_ids.size() == 1 && IsScalar(source_ids[0]) &&
      target_component_count > 1) {
    return SmearScalar(precision, source_ids[0], result_type_id);
  }

  // Accumulate the arguments for OpCompositeConstruct.
  Id scalar_type_id = GetScalarTypeId(result_type_id);
  std::vector<Id> constituent_ids;
  for (auto source_id : source_ids) {
    assert(!IsAggregate(source_id));
    int source_component_count = GetComponentCount(source_id);
    int sources_to_use = source_component_count;
    if (sources_to_use + target_component > target_component_count) {
      sources_to_use = target_component_count - target_component;
    }
    for (int s = 0; s < sources_to_use; ++s) {
      Id arg = source_id;
      if (source_component_count > 1) {
        arg = CreateSwizzle(scalar_type_id, arg, {static_cast<uint32_t>(s)});
      }
      if (target_component_count > 1) {
        constituent_ids.push_back(arg);
      } else {
        result = arg;
      }
      ++target_component;
    }
    if (target_component >= target_component_count) {
      break;
    }
  }

  if (!constituent_ids.empty()) {
    result = CreateCompositeConstruct(result_type_id, constituent_ids);
  }

  SetPrecision(result, precision);

  return result;
}

Id SpirvEmitter::CreateMatrixConstructor(spv::Decoration precision,
                                         std::vector<Id> sources,
                                         Id result_type_id) {
  Id component_type_id = GetScalarTypeId(result_type_id);
  int column_count = GetTypeColumnCount(result_type_id);
  int row_count = GetTypeRowCount(result_type_id);

  // Will use a two step process:
  // 1. make a compile-time 2D array of values
  // 2. construct a matrix from that array

  // Step 1.

  // Initialize the array to the identity matrix.
  Id ids[kMaxMatrixSize][kMaxMatrixSize];
  Id one = MakeFloatConstant(1.0);
  Id zero = MakeFloatConstant(0.0);
  for (int col = 0; col < kMaxMatrixSize; ++col) {
    for (int row = 0; row < kMaxMatrixSize; ++row) {
      if (col == row) {
        ids[col][row] = one;
      } else {
        ids[col][row] = zero;
      }
    }
  }

  // Modify components as dictated by the arguments.
  if (sources.size() == 1 && IsScalar(sources[0])) {
    // A single scalar; resets the diagonals.
    for (int col = 0; col < kMaxMatrixSize; ++col) {
      ids[col][col] = sources[0];
    }
  } else if (IsMatrix(sources[0])) {
    // Constructing from another matrix; copy over the parts that exist in both
    // the argument and constructee.
    Id matrix = sources[0];
    int min_column_count = std::min(column_count, GetColumnCount(matrix));
    int min_row_count = std::min(row_count, GetRowCount(matrix));
    for (int col = 0; col < min_column_count; ++col) {
      std::vector<uint32_t> indexes;
      indexes.push_back(col);
      for (int row = 0; row < min_row_count; ++row) {
        indexes.push_back(row);
        ids[col][row] =
            CreateCompositeExtract(matrix, component_type_id, indexes);
        indexes.pop_back();
        SetPrecision(ids[col][row], precision);
      }
    }
  } else {
    // Fill in the matrix in column-major order with whatever argument
    // components are available.
    int row = 0;
    int col = 0;
    for (auto source : sources) {
      Id arg_component = source;
      for (int comp = 0; comp < GetComponentCount(source); ++comp) {
        if (GetComponentCount(source) > 1) {
          arg_component =
              CreateCompositeExtract(source, component_type_id, comp);
          SetPrecision(arg_component, precision);
        }
        ids[col][row++] = arg_component;
        if (row == row_count) {
          row = 0;
          ++col;
        }
      }
    }
  }

  // Step 2: construct a matrix from that array.

  // Make the column vectors.
  Id column_type_id = GetContainedTypeId(result_type_id);
  std::vector<Id> matrix_columns;
  for (int col = 0; col < column_count; ++col) {
    std::vector<Id> vector_components;
    for (int row = 0; row < row_count; ++row) {
      vector_components.push_back(ids[col][row]);
    }
    matrix_columns.push_back(
        CreateCompositeConstruct(column_type_id, vector_components));
  }

  // Make the matrix.
  return CreateCompositeConstruct(result_type_id, matrix_columns);
}

SpirvEmitter::If::If(SpirvEmitter& emitter, Id condition)
    : emitter_(emitter), condition_(condition) {
  function_ = &emitter_.build_point()->parent();

  // make the blocks, but only put the then-block into the function,
  // the else-block and merge-block will be added later, in order, after
  // earlier code is emitted
  then_block_ = new Block(emitter_.AllocateUniqueId(), *function_);
  merge_block_ = new Block(emitter_.AllocateUniqueId(), *function_);

  // Save the current block, so that we can add in the flow control split when
  // makeEndIf is called.
  header_block_ = emitter_.build_point();

  function_->push_block(then_block_);
  emitter_.set_build_point(then_block_);
}

void SpirvEmitter::If::MakeBeginElse() {
  // Close out the "then" by having it jump to the merge_block
  emitter_.CreateBranch(merge_block_);

  // Make the first else block and add it to the function
  else_block_ = new Block(emitter_.AllocateUniqueId(), *function_);
  function_->push_block(else_block_);

  // Start building the else block
  emitter_.set_build_point(else_block_);
}

void SpirvEmitter::If::MakeEndIf() {
  // jump to the merge block
  emitter_.CreateBranch(merge_block_);

  // Go back to the header_block and make the flow control split
  emitter_.set_build_point(header_block_);
  emitter_.CreateSelectionMerge(merge_block_,
                                spv::SelectionControlMask::MaskNone);
  if (else_block_) {
    emitter_.CreateConditionalBranch(condition_, then_block_, else_block_);
  } else {
    emitter_.CreateConditionalBranch(condition_, then_block_, merge_block_);
  }

  // add the merge block to the function
  function_->push_block(merge_block_);
  emitter_.set_build_point(merge_block_);
}

void SpirvEmitter::MakeSwitch(Id selector, int segment_count,
                              std::vector<int> case_values,
                              std::vector<int> value_index_to_segment,
                              int default_segment,
                              std::vector<Block*>& segment_blocks) {
  Function& function = build_point_->parent();

  // Make all the blocks.
  for (int s = 0; s < segment_count; ++s) {
    segment_blocks.push_back(new Block(AllocateUniqueId(), function));
  }

  Block* merge_block = new Block(AllocateUniqueId(), function);

  // Make and insert the switch's selection-merge instruction.
  CreateSelectionMerge(merge_block, spv::SelectionControlMask::MaskNone);

  // Make the switch instruction.
  auto switchInst = new Instruction(NoResult, NoType, Op::OpSwitch);
  switchInst->AddIdOperand(selector);
  switchInst->AddIdOperand(default_segment >= 0
                               ? segment_blocks[default_segment]->id()
                               : merge_block->id());
  for (size_t i = 0; i < case_values.size(); ++i) {
    switchInst->AddImmediateOperand(case_values[i]);
    switchInst->AddIdOperand(segment_blocks[value_index_to_segment[i]]->id());
  }
  build_point_->AddInstruction(switchInst);

  // Push the merge block.
  switch_merges_.push(merge_block);
}

void SpirvEmitter::AddSwitchBreak() {
  // Branch to the top of the merge block stack.
  CreateBranch(switch_merges_.top());
  CreateAndSetNoPredecessorBlock("post-switch-break");
}

void SpirvEmitter::NextSwitchSegment(std::vector<Block*>& segment_block,
                                     int next_segment) {
  int last_segment = next_segment - 1;
  if (last_segment >= 0) {
    // Close out previous segment by jumping, if necessary, to next segment.
    if (!build_point_->is_terminated()) {
      CreateBranch(segment_block[next_segment]);
    }
  }
  Block* block = segment_block[next_segment];
  block->parent().push_block(block);
  set_build_point(block);
}

void SpirvEmitter::EndSwitch(std::vector<Block*>& segment_block) {
  // Close out previous segment by jumping, if necessary, to next segment.
  if (!build_point_->is_terminated()) {
    AddSwitchBreak();
  }

  switch_merges_.top()->parent().push_block(switch_merges_.top());
  set_build_point(switch_merges_.top());

  switch_merges_.pop();
}

void SpirvEmitter::MakeNewLoop(bool test_first) {
  loops_.push(Loop(*this, test_first));
  const Loop& loop = loops_.top();

  // The loop test is always emitted before the loop body.
  // But if the loop test executes at the bottom of the loop, then
  // execute the test only on the second and subsequent iterations.

  // Remember the block that branches to the loop header.  This
  // is required for the test-after-body case.
  Block* preheader = build_point();

  // Branch into the loop
  CreateBranch(loop.header);

  // Set ourselves inside the loop
  loop.function->push_block(loop.header);
  set_build_point(loop.header);

  if (!test_first) {
    // Generate code to defer the loop test until the second and
    // subsequent iterations.

    // It's always the first iteration when coming from the preheader.
    // All other branches to this loop header will need to indicate "false",
    // but we don't yet know where they will come from.
    loop.is_first_iteration->AddIdOperand(MakeBoolConstant(true));
    loop.is_first_iteration->AddIdOperand(preheader->id());
    build_point()->AddInstruction(loop.is_first_iteration);

    // Mark the end of the structured loop. This must exist in the loop header
    // block.
    CreateLoopMerge(loop.merge, loop.header, spv::LoopControlMask::MaskNone);

    // Generate code to see if this is the first iteration of the loop.
    // It needs to be in its own block, since the loop merge and
    // the selection merge instructions can't both be in the same
    // (header) block.
    Block* firstIterationCheck = new Block(AllocateUniqueId(), *loop.function);
    CreateBranch(firstIterationCheck);
    loop.function->push_block(firstIterationCheck);
    set_build_point(firstIterationCheck);

    // Control flow after this "if" normally reconverges at the loop body.
    // However, the loop test has a "break branch" out of this selection
    // construct because it can transfer control to the loop merge block.
    CreateSelectionMerge(loop.body, spv::SelectionControlMask::MaskNone);

    Block* loopTest = new Block(AllocateUniqueId(), *loop.function);
    CreateConditionalBranch(loop.is_first_iteration->result_id(), loop.body,
                            loopTest);

    loop.function->push_block(loopTest);
    set_build_point(loopTest);
  }
}

void SpirvEmitter::CreateLoopTestBranch(Id condition) {
  const Loop& loop = loops_.top();

  // Generate the merge instruction. If the loop test executes before
  // the body, then this is a loop merge.  Otherwise the loop merge
  // has already been generated and this is a conditional merge.
  if (loop.test_first) {
    CreateLoopMerge(loop.merge, loop.header, spv::LoopControlMask::MaskNone);
    // Branching to the "body" block will keep control inside
    // the loop.
    CreateConditionalBranch(condition, loop.body, loop.merge);
    loop.function->push_block(loop.body);
    set_build_point(loop.body);
  } else {
    // The branch to the loop merge block is the allowed exception
    // to the structured control flow.  Otherwise, control flow will
    // continue to loop.body block.  Since that is already the target
    // of a merge instruction, and a block can't be the target of more
    // than one merge instruction, we need to make an intermediate block.
    Block* stayInLoopBlock = new Block(AllocateUniqueId(), *loop.function);
    CreateSelectionMerge(stayInLoopBlock, spv::SelectionControlMask::MaskNone);

    // This is the loop test.
    CreateConditionalBranch(condition, stayInLoopBlock, loop.merge);

    // The dummy block just branches to the real loop body.
    loop.function->push_block(stayInLoopBlock);
    set_build_point(stayInLoopBlock);
    CreateBranchToBody();
  }
}

void SpirvEmitter::CreateBranchToBody() {
  const Loop& loop = loops_.top();
  assert(loop.body);

  // This is a reconvergence of control flow, so no merge instruction
  // is required.
  CreateBranch(loop.body);
  loop.function->push_block(loop.body);
  set_build_point(loop.body);
}

void SpirvEmitter::CreateLoopContinue() {
  CreateBranchToLoopHeaderFromInside(loops_.top());
  // Set up a block for dead code.
  CreateAndSetNoPredecessorBlock("post-loop-continue");
}

void SpirvEmitter::CreateLoopExit() {
  CreateBranch(loops_.top().merge);
  // Set up a block for dead code.
  CreateAndSetNoPredecessorBlock("post-loop-break");
}

void SpirvEmitter::CloseLoop() {
  const Loop& loop = loops_.top();

  // Branch back to the top.
  CreateBranchToLoopHeaderFromInside(loop);

  // Add the merge block and set the build point to it.
  loop.function->push_block(loop.merge);
  set_build_point(loop.merge);

  loops_.pop();
}

void SpirvEmitter::ClearAccessChain() {
  access_chain_.base = NoResult;
  access_chain_.index_chain.clear();
  access_chain_.instr = NoResult;
  access_chain_.swizzle.clear();
  access_chain_.component = NoResult;
  access_chain_.pre_swizzle_base_type = NoType;
  access_chain_.is_rvalue = false;
}

// Turn the described access chain in 'accessChain' into an instruction
// computing its address.  This *cannot* include complex swizzles, which must
// be handled after this is called, but it does include swizzles that select
// an individual element, as a single address of a scalar type can be
// computed by an OpAccessChain instruction.
Id SpirvEmitter::CollapseAccessChain() {
  assert(access_chain_.is_rvalue == false);

  if (!access_chain_.index_chain.empty()) {
    if (!access_chain_.instr) {
      auto storage_class = module_.storage_class(GetTypeId(access_chain_.base));
      access_chain_.instr = CreateAccessChain(storage_class, access_chain_.base,
                                              access_chain_.index_chain);
    }
    return access_chain_.instr;
  } else {
    return access_chain_.base;
  }

  // Note that non-trivial swizzling is left pending...
}

// Clear out swizzle if it is redundant, that is reselecting the same components
// that would be present without the swizzle.
void SpirvEmitter::SimplifyAccessChainSwizzle() {
  // If the swizzle has fewer components than the vector, it is subsetting, and
  // must stay to preserve that fact.
  if (GetTypeComponentCount(access_chain_.pre_swizzle_base_type) >
      access_chain_.swizzle.size()) {
    return;
  }

  // If components are out of order, it is a swizzle.
  for (size_t i = 0; i < access_chain_.swizzle.size(); ++i) {
    if (i != access_chain_.swizzle[i]) {
      return;
    }
  }

  // Otherwise, there is no need to track this swizzle.
  access_chain_.swizzle.clear();
  if (access_chain_.component == NoResult) {
    access_chain_.pre_swizzle_base_type = NoType;
  }
}

// To the extent any swizzling can become part of the chain
// of accesses instead of a post operation, make it so.
// If 'dynamic' is true, include transfering a non-static component index,
// otherwise, only transfer static indexes.
//
// Also, Boolean vectors are likely to be special.  While
// for external storage, they should only be integer types,
// function-local bool vectors could use sub-word indexing,
// so keep that as a separate Insert/Extract on a loaded vector.
void SpirvEmitter::TransferAccessChainSwizzle(bool dynamic) {
  // too complex?
  if (access_chain_.swizzle.size() > 1) {
    return;
  }

  // non existent?
  if (access_chain_.swizzle.empty() && access_chain_.component == NoResult) {
    return;
  }

  // single component...

  // skip doing it for Boolean vectors
  if (IsBoolType(GetContainedTypeId(access_chain_.pre_swizzle_base_type))) {
    return;
  }

  if (access_chain_.swizzle.size() == 1) {
    // handle static component
    access_chain_.index_chain.push_back(
        MakeUintConstant(access_chain_.swizzle.front()));
    access_chain_.swizzle.clear();
    // note, the only valid remaining dynamic access would be to this one
    // component, so don't bother even looking at access_chain_.component
    access_chain_.pre_swizzle_base_type = NoType;
    access_chain_.component = NoResult;
  } else if (dynamic && access_chain_.component != NoResult) {
    // handle dynamic component
    access_chain_.index_chain.push_back(access_chain_.component);
    access_chain_.pre_swizzle_base_type = NoType;
    access_chain_.component = NoResult;
  }
}

void SpirvEmitter::PushAccessChainSwizzle(std::vector<uint32_t> swizzle,
                                          Id pre_swizzle_base_type) {
  // Swizzles can be stacked in GLSL, but simplified to a single
  // one here; the base type doesn't change.
  if (access_chain_.pre_swizzle_base_type == NoType) {
    access_chain_.pre_swizzle_base_type = pre_swizzle_base_type;
  }

  // If needed, propagate the swizzle for the current access chain.
  if (access_chain_.swizzle.size()) {
    std::vector<unsigned> oldSwizzle = access_chain_.swizzle;
    access_chain_.swizzle.resize(0);
    for (unsigned int i = 0; i < swizzle.size(); ++i) {
      access_chain_.swizzle.push_back(oldSwizzle[swizzle[i]]);
    }
  } else {
    access_chain_.swizzle = swizzle;
  }

  // Determine if we need to track this swizzle anymore.
  SimplifyAccessChainSwizzle();
}

void SpirvEmitter::CreateAccessChainStore(Id rvalue) {
  assert(access_chain_.is_rvalue == false);

  TransferAccessChainSwizzle(true);
  Id base = CollapseAccessChain();

  if (access_chain_.swizzle.size() && access_chain_.component != NoResult) {
    CheckNotImplemented(
        "simultaneous l-value swizzle and dynamic component selection");
    return;
  }

  // If swizzle still exists, it is out-of-order or not full, we must load the
  // target vector, extract and insert elements to perform writeMask and/or
  // swizzle.
  Id source = NoResult;
  if (access_chain_.swizzle.size()) {
    Id temp_base_id = CreateLoad(base);
    source = CreateLvalueSwizzle(GetTypeId(temp_base_id), temp_base_id, rvalue,
                                 access_chain_.swizzle);
  }

  // Dynamic component selection.
  if (access_chain_.component != NoResult) {
    Id temp_base_id = (source == NoResult) ? CreateLoad(base) : source;
    source = CreateVectorInsertDynamic(temp_base_id, GetTypeId(temp_base_id),
                                       rvalue, access_chain_.component);
  }

  if (source == NoResult) {
    source = rvalue;
  }

  CreateStore(source, base);
}

Id SpirvEmitter::CreateAccessChainLoad(Id result_type_id) {
  Id id;

  if (access_chain_.is_rvalue) {
    // Transfer access chain, but keep it static, so we can stay in registers.
    TransferAccessChainSwizzle(false);
    if (!access_chain_.index_chain.empty()) {
      Id swizzle_base_type_id = access_chain_.pre_swizzle_base_type != NoType
                                    ? access_chain_.pre_swizzle_base_type
                                    : result_type_id;

      // If all the accesses are constants we can use OpCompositeExtract.
      std::vector<uint32_t> indexes;
      bool constant = true;
      for (auto index : access_chain_.index_chain) {
        if (IsConstantScalar(index)) {
          indexes.push_back(GetConstantScalar(index));
        } else {
          constant = false;
          break;
        }
      }

      if (constant) {
        id = CreateCompositeExtract(access_chain_.base, swizzle_base_type_id,
                                    indexes);
      } else {
        // Make a new function variable for this r-value.
        Id lvalue = CreateVariable(spv::StorageClass::Function,
                                   GetTypeId(access_chain_.base), "indexable");

        // Store into it.
        CreateStore(access_chain_.base, lvalue);

        // Move base to the new variable.
        access_chain_.base = lvalue;
        access_chain_.is_rvalue = false;

        // Load through the access chain.
        id = CreateLoad(CollapseAccessChain());
      }
    } else {
      id = access_chain_.base;
    }
  } else {
    TransferAccessChainSwizzle(true);
    // Load through the access chain.
    id = CreateLoad(CollapseAccessChain());
  }

  // Done, unless there are swizzles to do.
  if (access_chain_.swizzle.empty() && access_chain_.component == NoResult) {
    return id;
  }

  // Do remaining swizzling.
  // First, static swizzling.
  if (access_chain_.swizzle.size()) {
    // Static swizzle.
    Id swizzledType = GetScalarTypeId(GetTypeId(id));
    if (access_chain_.swizzle.size() > 1) {
      swizzledType =
          MakeVectorType(swizzledType, (int)access_chain_.swizzle.size());
    }
    id = CreateSwizzle(swizzledType, id, access_chain_.swizzle);
  }

  // Dynamic single-component selection.
  if (access_chain_.component != NoResult) {
    id =
        CreateVectorExtractDynamic(id, result_type_id, access_chain_.component);
  }

  return id;
}

Id SpirvEmitter::CreateAccessChainLValue() {
  assert(access_chain_.is_rvalue == false);

  TransferAccessChainSwizzle(true);
  Id lvalue = CollapseAccessChain();

  // If swizzle exists, it is out-of-order or not full, we must load the target
  // vector, extract and insert elements to perform writeMask and/or swizzle.
  // This does not go with getting a direct l-value pointer.
  assert(access_chain_.swizzle.empty());
  assert(access_chain_.component == NoResult);

  return lvalue;
}

void SpirvEmitter::Serialize(std::vector<uint32_t>& out) const {
  // Header, before first instructions:
  out.push_back(spv::MagicNumber);
  out.push_back(spv::Version);
  out.push_back(builder_number_);
  out.push_back(unique_id_ + 1);
  out.push_back(0);

  for (auto capability : capabilities_) {
    Instruction capInst(0, 0, Op::OpCapability);
    capInst.AddImmediateOperand(capability);
    capInst.Serialize(out);
  }

  // TBD: OpExtension ...

  SerializeInstructions(out, imports_);
  Instruction memInst(0, 0, Op::OpMemoryModel);
  memInst.AddImmediateOperand(addressing_model_);
  memInst.AddImmediateOperand(memory_model_);
  memInst.Serialize(out);

  // Instructions saved up while building:
  SerializeInstructions(out, entry_points_);
  SerializeInstructions(out, execution_modes_);

  // Debug instructions:
  if (source_language_ != spv::SourceLanguage::Unknown) {
    Instruction sourceInst(0, 0, Op::OpSource);
    sourceInst.AddImmediateOperand(source_language_);
    sourceInst.AddImmediateOperand(source_version_);
    sourceInst.Serialize(out);
  }
  for (auto extension : source_extensions_) {
    Instruction extInst(0, 0, Op::OpSourceExtension);
    extInst.AddStringOperand(extension);
    extInst.Serialize(out);
  }
  SerializeInstructions(out, names_);
  SerializeInstructions(out, lines_);

  // Annotation instructions:
  SerializeInstructions(out, decorations_);

  SerializeInstructions(out, constants_types_globals_);
  SerializeInstructions(out, externals_);

  // The functions:
  module_.Serialize(out);
}

void SpirvEmitter::SerializeInstructions(
    std::vector<unsigned int>& out,
    const std::vector<Instruction*>& instructions) const {
  for (auto instruction : instructions) {
    instruction->Serialize(out);
  }
}

// Utility method for creating a new block and setting the insert point to
// be in it. This is useful for flow-control operations that need a "dummy"
// block proceeding them (e.g. instructions after a discard, etc).
void SpirvEmitter::CreateAndSetNoPredecessorBlock(const char* name) {
  Block* block = new Block(AllocateUniqueId(), build_point_->parent());
  block->set_unreachable(true);
  build_point_->parent().push_block(block);
  set_build_point(block);

  AddName(block->id(), name);
}

void SpirvEmitter::CreateBranch(Block* block) {
  auto instr = new Instruction(Op::OpBranch);
  instr->AddIdOperand(block->id());
  build_point_->AddInstruction(instr);
  block->AddPredecessor(build_point_);
}

void SpirvEmitter::CreateSelectionMerge(Block* merge_block,
                                        spv::SelectionControlMask control) {
  auto instr = new Instruction(Op::OpSelectionMerge);
  instr->AddIdOperand(merge_block->id());
  instr->AddImmediateOperand(control);
  build_point_->AddInstruction(instr);
}

void SpirvEmitter::CreateLoopMerge(Block* merge_block, Block* continueBlock,
                                   spv::LoopControlMask control) {
  auto instr = new Instruction(Op::OpLoopMerge);
  instr->AddIdOperand(merge_block->id());
  instr->AddIdOperand(continueBlock->id());
  instr->AddImmediateOperand(control);
  build_point_->AddInstruction(instr);
}

void SpirvEmitter::CreateConditionalBranch(Id condition, Block* then_block,
                                           Block* else_block) {
  auto instr = new Instruction(Op::OpBranchConditional);
  instr->AddIdOperand(condition);
  instr->AddIdOperand(then_block->id());
  instr->AddIdOperand(else_block->id());
  build_point_->AddInstruction(instr);
  then_block->AddPredecessor(build_point_);
  else_block->AddPredecessor(build_point_);
}

SpirvEmitter::Loop::Loop(SpirvEmitter& emitter, bool testFirstArg)
    : function(&emitter.build_point()->parent()),
      header(new Block(emitter.AllocateUniqueId(), *function)),
      merge(new Block(emitter.AllocateUniqueId(), *function)),
      body(new Block(emitter.AllocateUniqueId(), *function)),
      test_first(testFirstArg),
      is_first_iteration(nullptr) {
  if (!test_first) {
    // You may be tempted to rewrite this as
    // new Instruction(builder.getUniqueId(), builder.makeBoolType(), OpPhi);
    // This will cause subtle test failures because builder.getUniqueId(),
    // and builder.makeBoolType() can then get run in a compiler-specific
    // order making tests fail for certain configurations.
    Id instructionId = emitter.AllocateUniqueId();
    is_first_iteration =
        new Instruction(instructionId, emitter.MakeBoolType(), Op::OpPhi);
  }
}

// Create a branch to the header of the given loop, from inside
// the loop body.
// Adjusts the phi node for the first-iteration value if needeed.
void SpirvEmitter::CreateBranchToLoopHeaderFromInside(const Loop& loop) {
  CreateBranch(loop.header);
  if (loop.is_first_iteration) {
    loop.is_first_iteration->AddIdOperand(MakeBoolConstant(false));
    loop.is_first_iteration->AddIdOperand(build_point()->id());
  }
}

void SpirvEmitter::CheckNotImplemented(const char* message) {
  xe::FatalError("Missing functionality: %s", message);
}

}  // namespace spirv
}  // namespace ui
}  // namespace xe
