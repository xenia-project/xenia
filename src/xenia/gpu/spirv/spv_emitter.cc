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

#include "xenia/gpu/spirv/spv_emitter.h"

#include <unordered_set>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"

namespace xe {
namespace gpu {
namespace spirv {

SpvEmitter::SpvEmitter() { clearAccessChain(); }

SpvEmitter::~SpvEmitter() = default;

Id SpvEmitter::import(const char* name) {
  auto import = new Instruction(getUniqueId(), NoType, Op::OpExtInstImport);
  import->addStringOperand(name);

  imports_.push_back(import);
  return import->result_id();
}

// For creating new grouped_types_ (will return old type if the requested one
// was
// already made).
Id SpvEmitter::makeVoidType() {
  Instruction* type;
  auto& grouped_type = grouped_types_[static_cast<int>(Op::OpTypeVoid)];
  if (grouped_type.empty()) {
    type = new Instruction(getUniqueId(), NoType, Op::OpTypeVoid);
    grouped_type.push_back(type);
    constants_types_globals_.push_back(type);
    module_.mapInstruction(type);
  } else {
    type = grouped_type.back();
  }

  return type->result_id();
}

Id SpvEmitter::makeBoolType() {
  Instruction* type;
  auto& grouped_type = grouped_types_[static_cast<int>(Op::OpTypeBool)];
  if (grouped_type.empty()) {
    type = new Instruction(getUniqueId(), NoType, Op::OpTypeBool);
    grouped_type.push_back(type);
    constants_types_globals_.push_back(type);
    module_.mapInstruction(type);
  } else {
    type = grouped_type.back();
  }

  return type->result_id();
}

Id SpvEmitter::makeSamplerType() {
  Instruction* type;
  auto& grouped_type = grouped_types_[static_cast<int>(Op::OpTypeSampler)];
  if (grouped_type.empty()) {
    type = new Instruction(getUniqueId(), NoType, Op::OpTypeSampler);
    grouped_type.push_back(type);
    constants_types_globals_.push_back(type);
    module_.mapInstruction(type);
  } else {
    type = grouped_type.back();
  }

  return type->result_id();
}

Id SpvEmitter::makePointer(spv::StorageClass storage_class, Id pointee) {
  // try to find it
  auto& grouped_type = grouped_types_[static_cast<int>(Op::OpTypePointer)];
  for (auto& type : grouped_type) {
    if (type->immediate_operand(0) == (unsigned)storage_class &&
        type->id_operand(1) == pointee) {
      return type->result_id();
    }
  }

  // not found, make it
  auto type = new Instruction(getUniqueId(), NoType, Op::OpTypePointer);
  type->addImmediateOperand(storage_class);
  type->addIdOperand(pointee);
  grouped_type.push_back(type);
  constants_types_globals_.push_back(type);
  module_.mapInstruction(type);

  return type->result_id();
}

Id SpvEmitter::makeIntegerType(int width, bool hasSign) {
  // try to find it
  auto& grouped_type = grouped_types_[static_cast<int>(Op::OpTypeInt)];
  for (auto& type : grouped_type) {
    if (type->immediate_operand(0) == (unsigned)width &&
        type->immediate_operand(1) == (hasSign ? 1u : 0u)) {
      return type->result_id();
    }
  }

  // not found, make it
  auto type = new Instruction(getUniqueId(), NoType, Op::OpTypeInt);
  type->addImmediateOperand(width);
  type->addImmediateOperand(hasSign ? 1 : 0);
  grouped_type.push_back(type);
  constants_types_globals_.push_back(type);
  module_.mapInstruction(type);

  return type->result_id();
}

Id SpvEmitter::makeFloatType(int width) {
  // try to find it
  auto& grouped_type = grouped_types_[static_cast<int>(Op::OpTypeFloat)];
  for (auto& type : grouped_type) {
    if (type->immediate_operand(0) == (unsigned)width) {
      return type->result_id();
    }
  }

  // not found, make it
  auto type = new Instruction(getUniqueId(), NoType, Op::OpTypeFloat);
  type->addImmediateOperand(width);
  grouped_type.push_back(type);
  constants_types_globals_.push_back(type);
  module_.mapInstruction(type);

  return type->result_id();
}

// Make a struct without checking for duplication.
// See makeStructResultType() for non-decorated structs
// needed as the result of some instructions, which does
// check for duplicates.
Id SpvEmitter::makeStructType(std::vector<Id>& members, const char* name) {
  // Don't look for previous one, because in the general case,
  // structs can be duplicated except for decorations.

  // not found, make it
  Instruction* type = new Instruction(getUniqueId(), NoType, Op::OpTypeStruct);
  type->addIdOperands(members);
  auto& grouped_type = grouped_types_[static_cast<int>(Op::OpTypeStruct)];
  grouped_type.push_back(type);
  constants_types_globals_.push_back(type);
  module_.mapInstruction(type);
  addName(type->result_id(), name);

  return type->result_id();
}

// Make a struct for the simple results of several instructions,
// checking for duplication.
Id SpvEmitter::makeStructResultType(Id type0, Id type1) {
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
  std::vector<spv::Id> members;
  members.push_back(type0);
  members.push_back(type1);

  return makeStructType(members, "ResType");
}

Id SpvEmitter::makeVectorType(Id component, int size) {
  // try to find it
  auto& grouped_type = grouped_types_[static_cast<int>(Op::OpTypeVector)];
  for (auto& type : grouped_type) {
    if (type->id_operand(0) == component &&
        type->immediate_operand(1) == (unsigned)size) {
      return type->result_id();
    }
  }

  // not found, make it
  auto type = new Instruction(getUniqueId(), NoType, Op::OpTypeVector);
  type->addIdOperand(component);
  type->addImmediateOperand(size);
  grouped_type.push_back(type);
  constants_types_globals_.push_back(type);
  module_.mapInstruction(type);

  return type->result_id();
}

Id SpvEmitter::makeMatrixType(Id component, int cols, int rows) {
  assert(cols <= kMaxMatrixSize && rows <= kMaxMatrixSize);

  Id column = makeVectorType(component, rows);

  // try to find it
  auto& grouped_type = grouped_types_[static_cast<int>(Op::OpTypeMatrix)];
  for (auto& type : grouped_type) {
    if (type->id_operand(0) == column &&
        type->immediate_operand(1) == (unsigned)cols) {
      return type->result_id();
    }
  }

  // not found, make it
  auto type = new Instruction(getUniqueId(), NoType, Op::OpTypeMatrix);
  type->addIdOperand(column);
  type->addImmediateOperand(cols);
  grouped_type.push_back(type);
  constants_types_globals_.push_back(type);
  module_.mapInstruction(type);

  return type->result_id();
}

Id SpvEmitter::makeArrayType(Id element, unsigned size) {
  // First, we need a constant instruction for the size
  Id sizeId = makeUintConstant(size);

  // try to find existing type
  auto& grouped_type = grouped_types_[static_cast<int>(Op::OpTypeArray)];
  for (auto& type : grouped_type) {
    if (type->id_operand(0) == element && type->id_operand(1) == sizeId) {
      return type->result_id();
    }
  }

  // not found, make it
  auto type = new Instruction(getUniqueId(), NoType, Op::OpTypeArray);
  type->addIdOperand(element);
  type->addIdOperand(sizeId);
  grouped_type.push_back(type);
  constants_types_globals_.push_back(type);
  module_.mapInstruction(type);

  return type->result_id();
}

Id SpvEmitter::makeRuntimeArray(Id element) {
  Instruction* type =
      new Instruction(getUniqueId(), NoType, Op::OpTypeRuntimeArray);
  type->addIdOperand(element);
  constants_types_globals_.push_back(type);
  module_.mapInstruction(type);

  return type->result_id();
}

Id SpvEmitter::makeFunctionType(Id return_type, std::vector<Id>& param_types) {
  // try to find it
  auto& grouped_type = grouped_types_[static_cast<int>(Op::OpTypeFunction)];
  for (auto& type : grouped_type) {
    if (type->id_operand(0) != return_type ||
        (int)param_types.size() != type->operand_count() - 1) {
      continue;
    }
    bool mismatch = false;
    for (int p = 0; p < (int)param_types.size(); ++p) {
      if (param_types[p] != type->id_operand(p + 1)) {
        mismatch = true;
        break;
      }
    }
    if (!mismatch) {
      return type->result_id();
    }
  }

  // not found, make it
  auto type = new Instruction(getUniqueId(), NoType, Op::OpTypeFunction);
  type->addIdOperand(return_type);
  type->addIdOperands(param_types);
  grouped_type.push_back(type);
  constants_types_globals_.push_back(type);
  module_.mapInstruction(type);

  return type->result_id();
}

Id SpvEmitter::makeImageType(Id sampledType, spv::Dim dim, bool depth,
                             bool arrayed, bool ms, unsigned sampled,
                             spv::ImageFormat format) {
  // try to find it
  auto& grouped_type = grouped_types_[static_cast<int>(Op::OpTypeImage)];
  for (auto& type : grouped_type) {
    if (type->id_operand(0) == sampledType &&
        type->immediate_operand(1) == (unsigned int)dim &&
        type->immediate_operand(2) == (depth ? 1u : 0u) &&
        type->immediate_operand(3) == (arrayed ? 1u : 0u) &&
        type->immediate_operand(4) == (ms ? 1u : 0u) &&
        type->immediate_operand(5) == sampled &&
        type->immediate_operand(6) == (unsigned int)format) {
      return type->result_id();
    }
  }

  // not found, make it
  auto type = new Instruction(getUniqueId(), NoType, Op::OpTypeImage);
  type->addIdOperand(sampledType);
  type->addImmediateOperand(dim);
  type->addImmediateOperand(depth ? 1 : 0);
  type->addImmediateOperand(arrayed ? 1 : 0);
  type->addImmediateOperand(ms ? 1 : 0);
  type->addImmediateOperand(sampled);
  type->addImmediateOperand((unsigned int)format);

  grouped_type.push_back(type);
  constants_types_globals_.push_back(type);
  module_.mapInstruction(type);

  return type->result_id();
}

Id SpvEmitter::makeSampledImageType(Id imageType) {
  // try to find it
  auto& grouped_type = grouped_types_[static_cast<int>(Op::OpTypeSampledImage)];
  for (auto& type : grouped_type) {
    if (type->id_operand(0) == imageType) {
      return type->result_id();
    }
  }

  // not found, make it
  auto type = new Instruction(getUniqueId(), NoType, Op::OpTypeSampledImage);
  type->addIdOperand(imageType);

  grouped_type.push_back(type);
  constants_types_globals_.push_back(type);
  module_.mapInstruction(type);

  return type->result_id();
}

Id SpvEmitter::getDerefTypeId(Id result_id) const {
  Id type_id = getTypeId(result_id);
  assert(isPointerType(type_id));
  return module_.getInstruction(type_id)->immediate_operand(1);
}

Op SpvEmitter::getMostBasicTypeClass(Id type_id) const {
  auto instr = module_.getInstruction(type_id);

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
      return getMostBasicTypeClass(instr->id_operand(0));
    case Op::OpTypePointer:
      return getMostBasicTypeClass(instr->id_operand(1));
    default:
      assert(0);
      return Op::OpTypeFloat;
  }
}

int SpvEmitter::getNumTypeComponents(Id type_id) const {
  auto instr = module_.getInstruction(type_id);

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
Id SpvEmitter::getScalarTypeId(Id type_id) const {
  auto instr = module_.getInstruction(type_id);

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
      return getScalarTypeId(getContainedTypeId(type_id));
    default:
      assert(0);
      return NoResult;
  }
}

// Return the type of 'member' of a composite.
Id SpvEmitter::getContainedTypeId(Id type_id, int member) const {
  auto instr = module_.getInstruction(type_id);

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
Id SpvEmitter::getContainedTypeId(Id type_id) const {
  return getContainedTypeId(type_id, 0);
}

// See if a scalar constant of this type has already been created, so it
// can be reused rather than duplicated.  (Required by the specification).
Id SpvEmitter::findScalarConstant(Op type_class, Op opcode, Id type_id,
                                  unsigned value) const {
  auto& grouped_constant = grouped_constants_[static_cast<int>(type_class)];
  for (auto& constant : grouped_constant) {
    if (constant->opcode() == opcode && constant->type_id() == type_id &&
        constant->immediate_operand(0) == value) {
      return constant->result_id();
    }
  }
  return 0;
}

// Version of findScalarConstant (see above) for scalars that take two operands
// (e.g. a 'double').
Id SpvEmitter::findScalarConstant(Op type_class, Op opcode, Id type_id,
                                  unsigned v1, unsigned v2) const {
  auto& grouped_constant = grouped_constants_[static_cast<int>(type_class)];
  for (auto& constant : grouped_constant) {
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
bool SpvEmitter::isConstantOpCode(Op opcode) const {
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

Id SpvEmitter::makeBoolConstant(bool b, bool is_spec_constant) {
  Id type_id = makeBoolType();
  Op opcode = is_spec_constant
                  ? (b ? Op::OpSpecConstantTrue : Op::OpSpecConstantFalse)
                  : (b ? Op::OpConstantTrue : Op::OpConstantFalse);

  // See if we already made it
  Id existing = 0;
  auto& grouped_constant = grouped_constants_[static_cast<int>(Op::OpTypeBool)];
  for (auto& constant : grouped_constant) {
    if (constant->type_id() == type_id && constant->opcode() == opcode) {
      existing = constant->result_id();
    }
  }
  if (existing) {
    return existing;
  }

  // Make it
  auto c = new Instruction(getUniqueId(), type_id, opcode);
  constants_types_globals_.push_back(c);
  grouped_constants_[static_cast<int>(Op::OpTypeBool)].push_back(c);
  module_.mapInstruction(c);

  return c->result_id();
}

Id SpvEmitter::makeIntConstant(Id type_id, unsigned value,
                               bool is_spec_constant) {
  Op opcode = is_spec_constant ? Op::OpSpecConstant : Op::OpConstant;
  Id existing = findScalarConstant(Op::OpTypeInt, opcode, type_id, value);
  if (existing) {
    return existing;
  }

  auto c = new Instruction(getUniqueId(), type_id, opcode);
  c->addImmediateOperand(value);
  constants_types_globals_.push_back(c);
  grouped_constants_[static_cast<int>(Op::OpTypeInt)].push_back(c);
  module_.mapInstruction(c);

  return c->result_id();
}

Id SpvEmitter::makeFloatConstant(float f, bool is_spec_constant) {
  Op opcode = is_spec_constant ? Op::OpSpecConstant : Op::OpConstant;
  Id type_id = makeFloatType(32);
  unsigned value = *(unsigned int*)&f;
  Id existing = findScalarConstant(Op::OpTypeFloat, opcode, type_id, value);
  if (existing) {
    return existing;
  }

  auto c = new Instruction(getUniqueId(), type_id, opcode);
  c->addImmediateOperand(value);
  constants_types_globals_.push_back(c);
  grouped_constants_[static_cast<int>(Op::OpTypeFloat)].push_back(c);
  module_.mapInstruction(c);

  return c->result_id();
}

Id SpvEmitter::makeDoubleConstant(double d, bool is_spec_constant) {
  Op opcode = is_spec_constant ? Op::OpSpecConstant : Op::OpConstant;
  Id type_id = makeFloatType(64);
  unsigned long long value = *(unsigned long long*)&d;
  unsigned op1 = value & 0xFFFFFFFF;
  unsigned op2 = value >> 32;
  Id existing = findScalarConstant(Op::OpTypeFloat, opcode, type_id, op1, op2);
  if (existing) {
    return existing;
  }

  auto c = new Instruction(getUniqueId(), type_id, opcode);
  c->addImmediateOperand(op1);
  c->addImmediateOperand(op2);
  constants_types_globals_.push_back(c);
  grouped_constants_[static_cast<int>(Op::OpTypeFloat)].push_back(c);
  module_.mapInstruction(c);

  return c->result_id();
}

Id SpvEmitter::findCompositeConstant(Op type_class,
                                     std::vector<Id>& comps) const {
  Instruction* constant = nullptr;
  bool found = false;
  auto& grouped_constant = grouped_constants_[static_cast<int>(type_class)];
  for (auto& constant : grouped_constant) {
    // same shape?
    if (constant->operand_count() != (int)comps.size()) {
      continue;
    }

    // same contents?
    bool mismatch = false;
    for (int op = 0; op < constant->operand_count(); ++op) {
      if (constant->id_operand(op) != comps[op]) {
        mismatch = true;
        break;
      }
    }
    if (!mismatch) {
      found = true;
      break;
    }
  }

  return found ? constant->result_id() : NoResult;
}

Id SpvEmitter::makeCompositeConstant(Id type_id, std::vector<Id>& members) {
  assert(type_id);
  Op type_class = getTypeClass(type_id);

  switch (type_class) {
    case Op::OpTypeVector:
    case Op::OpTypeArray:
    case Op::OpTypeStruct:
    case Op::OpTypeMatrix:
      break;
    default:
      assert(0);
      return makeFloatConstant(0.0);
  }

  Id existing = findCompositeConstant(type_class, members);
  if (existing) {
    return existing;
  }

  auto c = new Instruction(getUniqueId(), type_id, Op::OpConstantComposite);
  c->addIdOperands(members);
  constants_types_globals_.push_back(c);
  grouped_constants_[static_cast<int>(type_class)].push_back(c);
  module_.mapInstruction(c);

  return c->result_id();
}

Instruction* SpvEmitter::addEntryPoint(spv::ExecutionModel model,
                                       Function* function, const char* name) {
  auto entry_point = new Instruction(Op::OpEntryPoint);
  entry_point->addImmediateOperand(model);
  entry_point->addIdOperand(function->id());
  entry_point->addStringOperand(name);

  entry_points_.push_back(entry_point);

  return entry_point;
}

// Currently relying on the fact that all 'value' of interest are small
// non-negative values.
void SpvEmitter::addExecutionMode(Function* entry_point,
                                  spv::ExecutionMode mode, int value1,
                                  int value2, int value3) {
  auto instr = new Instruction(Op::OpExecutionMode);
  instr->addIdOperand(entry_point->id());
  instr->addImmediateOperand(mode);
  if (value1 >= 0) {
    instr->addImmediateOperand(value1);
  }
  if (value2 >= 0) {
    instr->addImmediateOperand(value2);
  }
  if (value3 >= 0) {
    instr->addImmediateOperand(value3);
  }

  execution_modes_.push_back(instr);
}

void SpvEmitter::addName(Id id, const char* string) {
  auto name = new Instruction(Op::OpName);
  name->addIdOperand(id);
  name->addStringOperand(string);

  names_.push_back(name);
}

void SpvEmitter::addMemberName(Id id, int memberNumber, const char* string) {
  auto name = new Instruction(Op::OpMemberName);
  name->addIdOperand(id);
  name->addImmediateOperand(memberNumber);
  name->addStringOperand(string);

  names_.push_back(name);
}

void SpvEmitter::addLine(Id target, Id file_name, int lineNum, int column) {
  auto line = new Instruction(Op::OpLine);
  line->addIdOperand(target);
  line->addIdOperand(file_name);
  line->addImmediateOperand(lineNum);
  line->addImmediateOperand(column);

  lines_.push_back(line);
}

void SpvEmitter::addDecoration(Id id, spv::Decoration decoration, int num) {
  if (decoration == (spv::Decoration)BadValue) {
    return;
  }
  auto dec = new Instruction(Op::OpDecorate);
  dec->addIdOperand(id);
  dec->addImmediateOperand(decoration);
  if (num >= 0) {
    dec->addImmediateOperand(num);
  }

  decorations_.push_back(dec);
}

void SpvEmitter::addMemberDecoration(Id id, unsigned int member,
                                     spv::Decoration decoration, int num) {
  auto dec = new Instruction(Op::OpMemberDecorate);
  dec->addIdOperand(id);
  dec->addImmediateOperand(member);
  dec->addImmediateOperand(decoration);
  if (num >= 0) {
    dec->addImmediateOperand(num);
  }

  decorations_.push_back(dec);
}

Function* SpvEmitter::makeMain() {
  assert(!main_function_);

  Block* entry;
  std::vector<Id> params;

  main_function_ = makeFunctionEntry(makeVoidType(), "main", params, &entry);

  return main_function_;
}

Function* SpvEmitter::makeFunctionEntry(Id return_type, const char* name,
                                        std::vector<Id>& param_types,
                                        Block** entry) {
  Id type_id = makeFunctionType(return_type, param_types);
  Id firstParamId =
      param_types.empty() ? 0 : getUniqueIds((int)param_types.size());
  auto function =
      new Function(getUniqueId(), return_type, type_id, firstParamId, module_);

  if (entry) {
    *entry = new Block(getUniqueId(), *function);
    function->push_block(*entry);
    set_build_point(*entry);
  }

  if (name) {
    addName(function->id(), name);
  }

  return function;
}

void SpvEmitter::makeReturn(bool implicit, Id retVal) {
  if (retVal) {
    auto inst = new Instruction(NoResult, NoType, Op::OpReturnValue);
    inst->addIdOperand(retVal);
    build_point_->push_instruction(inst);
  } else {
    build_point_->push_instruction(
        new Instruction(NoResult, NoType, Op::OpReturn));
  }

  if (!implicit) {
    createAndSetNoPredecessorBlock("post-return");
  }
}

void SpvEmitter::leaveFunction() {
  Block* block = build_point_;
  Function& function = build_point_->parent();
  assert(block);

  // If our function did not contain a return, add a return void now.
  if (!block->is_terminated()) {
    // Whether we're in an unreachable (non-entry) block.
    bool unreachable =
        function.entry_block() != block && block->predecessor_count() == 0;

    if (unreachable) {
      // Given that this block is at the end of a function, it must be right
      // after an
      // explicit return, just remove it.
      function.pop_block(block);
    } else {
      // We'll add a return instruction at the end of the current block,
      // which for a non-void function is really error recovery (?), as the
      // source
      // being translated should have had an explicit return, which would have
      // been
      // followed by an unreachable block, which was handled above.
      if (function.return_type() == makeVoidType()) {
        makeReturn(true);
      } else {
        makeReturn(true, createUndefined(function.return_type()));
      }
    }
  }
}

void SpvEmitter::makeDiscard() {
  build_point_->push_instruction(new Instruction(Op::OpKill));
  createAndSetNoPredecessorBlock("post-discard");
}

Id SpvEmitter::createVariable(spv::StorageClass storage_class, Id type,
                              const char* name) {
  Id pointerType = makePointer(storage_class, type);
  auto inst = new Instruction(getUniqueId(), pointerType, Op::OpVariable);
  inst->addImmediateOperand(storage_class);

  switch (storage_class) {
    case spv::StorageClass::Function:
      // Validation rules require the declaration in the entry block
      build_point_->parent().push_local_variable(inst);
      break;

    default:
      constants_types_globals_.push_back(inst);
      module_.mapInstruction(inst);
      break;
  }

  if (name) {
    addName(inst->result_id(), name);
  }

  return inst->result_id();
}

Id SpvEmitter::createUndefined(Id type) {
  auto inst = new Instruction(getUniqueId(), type, Op::OpUndef);
  build_point_->push_instruction(inst);
  return inst->result_id();
}

void SpvEmitter::createStore(Id rvalue, Id lvalue) {
  auto store = new Instruction(Op::OpStore);
  store->addIdOperand(lvalue);
  store->addIdOperand(rvalue);
  build_point_->push_instruction(store);
}

Id SpvEmitter::createLoad(Id lvalue) {
  auto load =
      new Instruction(getUniqueId(), getDerefTypeId(lvalue), Op::OpLoad);
  load->addIdOperand(lvalue);
  build_point_->push_instruction(load);

  return load->result_id();
}

Id SpvEmitter::createAccessChain(spv::StorageClass storage_class, Id base,
                                 std::vector<Id>& offsets) {
  // Figure out the final resulting type.
  spv::Id type_id = getTypeId(base);
  assert(isPointerType(type_id) && !offsets.empty());
  type_id = getContainedTypeId(type_id);
  for (size_t i = 0; i < offsets.size(); ++i) {
    if (isStructType(type_id)) {
      assert(isConstantScalar(offsets[i]));
      type_id = getContainedTypeId(type_id, getConstantScalar(offsets[i]));
    } else {
      type_id = getContainedTypeId(type_id, offsets[i]);
    }
  }
  type_id = makePointer(storage_class, type_id);

  // Make the instruction
  auto chain = new Instruction(getUniqueId(), type_id, Op::OpAccessChain);
  chain->addIdOperand(base);
  chain->addIdOperands(offsets);
  build_point_->push_instruction(chain);

  return chain->result_id();
}

Id SpvEmitter::createArrayLength(Id base, unsigned int member) {
  auto length =
      new Instruction(getUniqueId(), makeIntType(32), Op::OpArrayLength);
  length->addIdOperand(base);
  length->addImmediateOperand(member);
  build_point_->push_instruction(length);

  return length->result_id();
}

Id SpvEmitter::createCompositeExtract(Id composite, Id type_id,
                                      unsigned index) {
  auto extract =
      new Instruction(getUniqueId(), type_id, Op::OpCompositeExtract);
  extract->addIdOperand(composite);
  extract->addImmediateOperand(index);
  build_point_->push_instruction(extract);

  return extract->result_id();
}

Id SpvEmitter::createCompositeExtract(Id composite, Id type_id,
                                      std::vector<unsigned>& indexes) {
  auto extract =
      new Instruction(getUniqueId(), type_id, Op::OpCompositeExtract);
  extract->addIdOperand(composite);
  extract->addImmediateOperands(indexes);
  build_point_->push_instruction(extract);

  return extract->result_id();
}

Id SpvEmitter::createCompositeInsert(Id object, Id composite, Id type_id,
                                     unsigned index) {
  auto insert = new Instruction(getUniqueId(), type_id, Op::OpCompositeInsert);
  insert->addIdOperand(object);
  insert->addIdOperand(composite);
  insert->addImmediateOperand(index);
  build_point_->push_instruction(insert);

  return insert->result_id();
}

Id SpvEmitter::createCompositeInsert(Id object, Id composite, Id type_id,
                                     std::vector<unsigned>& indexes) {
  auto insert = new Instruction(getUniqueId(), type_id, Op::OpCompositeInsert);
  insert->addIdOperand(object);
  insert->addIdOperand(composite);
  for (size_t i = 0; i < indexes.size(); ++i) {
    insert->addImmediateOperand(indexes[i]);
  }
  build_point_->push_instruction(insert);

  return insert->result_id();
}

Id SpvEmitter::createVectorExtractDynamic(Id vector, Id type_id,
                                          Id component_index) {
  auto extract =
      new Instruction(getUniqueId(), type_id, Op::OpVectorExtractDynamic);
  extract->addIdOperand(vector);
  extract->addIdOperand(component_index);
  build_point_->push_instruction(extract);

  return extract->result_id();
}

Id SpvEmitter::createVectorInsertDynamic(Id vector, Id type_id, Id component,
                                         Id component_index) {
  auto insert =
      new Instruction(getUniqueId(), type_id, Op::OpVectorInsertDynamic);
  insert->addIdOperand(vector);
  insert->addIdOperand(component);
  insert->addIdOperand(component_index);
  build_point_->push_instruction(insert);

  return insert->result_id();
}

// An opcode that has no operands, no result id, and no type
void SpvEmitter::createNoResultOp(Op opcode) {
  auto op = new Instruction(opcode);
  build_point_->push_instruction(op);
}

// An opcode that has one operand, no result id, and no type
void SpvEmitter::createNoResultOp(Op opcode, Id operand) {
  auto op = new Instruction(opcode);
  op->addIdOperand(operand);
  build_point_->push_instruction(op);
}

// An opcode that has one operand, no result id, and no type
void SpvEmitter::createNoResultOp(Op opcode, const std::vector<Id>& operands) {
  auto op = new Instruction(opcode);
  op->addIdOperands(operands);
  build_point_->push_instruction(op);
}

void SpvEmitter::createControlBarrier(spv::Scope execution, spv::Scope memory,
                                      spv::MemorySemanticsMask semantics) {
  auto op = new Instruction(Op::OpControlBarrier);
  op->addImmediateOperand(makeUintConstant(execution));
  op->addImmediateOperand(makeUintConstant(memory));
  op->addImmediateOperand(makeUintConstant(semantics));
  build_point_->push_instruction(op);
}

void SpvEmitter::createMemoryBarrier(unsigned execution_scope,
                                     unsigned memory_semantics) {
  auto op = new Instruction(Op::OpMemoryBarrier);
  op->addImmediateOperand(makeUintConstant(execution_scope));
  op->addImmediateOperand(makeUintConstant(memory_semantics));
  build_point_->push_instruction(op);
}

// An opcode that has one operands, a result id, and a type
Id SpvEmitter::createUnaryOp(Op opcode, Id type_id, Id operand) {
  auto op = new Instruction(getUniqueId(), type_id, opcode);
  op->addIdOperand(operand);
  build_point_->push_instruction(op);

  return op->result_id();
}

Id SpvEmitter::createBinOp(Op opcode, Id type_id, Id left, Id right) {
  auto op = new Instruction(getUniqueId(), type_id, opcode);
  op->addIdOperand(left);
  op->addIdOperand(right);
  build_point_->push_instruction(op);

  return op->result_id();
}

Id SpvEmitter::createTriOp(Op opcode, Id type_id, Id op1, Id op2, Id op3) {
  auto op = new Instruction(getUniqueId(), type_id, opcode);
  op->addIdOperand(op1);
  op->addIdOperand(op2);
  op->addIdOperand(op3);
  build_point_->push_instruction(op);

  return op->result_id();
}

Id SpvEmitter::createOp(Op opcode, Id type_id,
                        const std::vector<Id>& operands) {
  auto op = new Instruction(getUniqueId(), type_id, opcode);
  op->addIdOperands(operands);
  build_point_->push_instruction(op);

  return op->result_id();
}

Id SpvEmitter::createFunctionCall(Function* function,
                                  std::vector<spv::Id>& args) {
  auto op = new Instruction(getUniqueId(), function->return_type(),
                            Op::OpFunctionCall);
  op->addIdOperand(function->id());
  op->addIdOperands(args);
  build_point_->push_instruction(op);

  return op->result_id();
}

Id SpvEmitter::createRvalueSwizzle(Id type_id, Id source,
                                   std::vector<unsigned>& channels) {
  if (channels.size() == 1)
    return createCompositeExtract(source, type_id, channels.front());

  auto swizzle = new Instruction(getUniqueId(), type_id, Op::OpVectorShuffle);
  assert(isVector(source));
  swizzle->addIdOperand(source);
  swizzle->addIdOperand(source);
  swizzle->addImmediateOperands(channels);
  build_point_->push_instruction(swizzle);

  return swizzle->result_id();
}

Id SpvEmitter::createLvalueSwizzle(Id type_id, Id target, Id source,
                                   std::vector<unsigned>& channels) {
  assert(getNumComponents(source) == (int)channels.size());
  if (channels.size() == 1 && getNumComponents(source) == 1) {
    return createCompositeInsert(source, target, type_id, channels.front());
  }

  auto swizzle = new Instruction(getUniqueId(), type_id, Op::OpVectorShuffle);
  assert(isVector(source));
  assert(isVector(target));
  swizzle->addIdOperand(target);
  swizzle->addIdOperand(source);

  // Set up an identity shuffle from the base value to the result value
  unsigned int components[4];
  int numTargetComponents = getNumComponents(target);
  for (int i = 0; i < numTargetComponents; ++i) {
    components[i] = i;
  }

  // Punch in the l-value swizzle
  for (int i = 0; i < (int)channels.size(); ++i) {
    components[channels[i]] = numTargetComponents + i;
  }

  // finish the instruction with these components selectors
  for (int i = 0; i < numTargetComponents; ++i) {
    swizzle->addImmediateOperand(components[i]);
  }
  build_point_->push_instruction(swizzle);

  return swizzle->result_id();
}

void SpvEmitter::promoteScalar(spv::Decoration precision, Id& left, Id& right) {
  int direction = getNumComponents(right) - getNumComponents(left);
  if (direction > 0) {
    left = smearScalar(precision, left, getTypeId(right));
  } else if (direction < 0) {
    right = smearScalar(precision, right, getTypeId(left));
  }
}

Id SpvEmitter::smearScalar(spv::Decoration precision, Id scalar,
                           Id vectorType) {
  assert(getNumComponents(scalar) == 1);

  int numComponents = getNumTypeComponents(vectorType);
  if (numComponents == 1) {
    return scalar;
  }

  auto smear =
      new Instruction(getUniqueId(), vectorType, Op::OpCompositeConstruct);
  for (int c = 0; c < numComponents; ++c) {
    smear->addIdOperand(scalar);
  }
  build_point_->push_instruction(smear);

  return smear->result_id();
}

Id SpvEmitter::createBuiltinCall(spv::Decoration precision, Id result_type,
                                 Id builtins, int entry_point,
                                 std::initializer_list<Id> args) {
  auto inst = new Instruction(getUniqueId(), result_type, Op::OpExtInst);
  inst->addIdOperand(builtins);
  inst->addImmediateOperand(entry_point);
  inst->addIdOperands(args);

  build_point_->push_instruction(inst);
  return inst->result_id();
}

// Accept all parameters needed to create a texture instruction.
// Create the correct instruction based on the inputs, and make the call.
Id SpvEmitter::createTextureCall(spv::Decoration precision, Id result_type,
                                 bool fetch, bool proj, bool gather,
                                 const TextureParameters& parameters) {
  static const int maxTextureArgs = 10;
  Id texArgs[maxTextureArgs] = {};

  // Set up the fixed arguments
  int numArgs = 0;
  bool xplicit = false;
  texArgs[numArgs++] = parameters.sampler;
  texArgs[numArgs++] = parameters.coords;
  if (parameters.Dref) {
    texArgs[numArgs++] = parameters.Dref;
  }
  if (parameters.comp) {
    texArgs[numArgs++] = parameters.comp;
  }

  // Set up the optional arguments
  int optArgNum = numArgs;  // track which operand, if it exists, is the mask of
                            // optional arguments
  ++numArgs;                // speculatively make room for the mask operand
  auto mask = spv::ImageOperandsMask::MaskNone;  // the mask operand
  if (parameters.bias) {
    mask = mask | spv::ImageOperandsMask::Bias;
    texArgs[numArgs++] = parameters.bias;
  }
  if (parameters.lod) {
    mask = mask | spv::ImageOperandsMask::Lod;
    texArgs[numArgs++] = parameters.lod;
    xplicit = true;
  }
  if (parameters.gradX) {
    mask = mask | spv::ImageOperandsMask::Grad;
    texArgs[numArgs++] = parameters.gradX;
    texArgs[numArgs++] = parameters.gradY;
    xplicit = true;
  }
  if (parameters.offset) {
    if (isConstant(parameters.offset)) {
      mask = mask | spv::ImageOperandsMask::ConstOffset;
    } else {
      mask = mask | spv::ImageOperandsMask::Offset;
    }
    texArgs[numArgs++] = parameters.offset;
  }
  if (parameters.offsets) {
    mask = mask | spv::ImageOperandsMask::ConstOffsets;
    texArgs[numArgs++] = parameters.offsets;
  }
  if (parameters.sample) {
    mask = mask | spv::ImageOperandsMask::Sample;
    texArgs[numArgs++] = parameters.sample;
  }
  if (mask == spv::ImageOperandsMask::MaskNone) {
    --numArgs;  // undo speculative reservation for the mask argument
  } else {
    texArgs[optArgNum] = static_cast<Id>(mask);
  }

  // Set up the instruction
  Op opcode;
  opcode = Op::OpImageSampleImplicitLod;
  if (fetch) {
    opcode = Op::OpImageFetch;
  } else if (gather) {
    if (parameters.Dref)
      opcode = Op::OpImageDrefGather;
    else
      opcode = Op::OpImageGather;
  } else if (xplicit) {
    if (parameters.Dref) {
      if (proj)
        opcode = Op::OpImageSampleProjDrefExplicitLod;
      else
        opcode = Op::OpImageSampleDrefExplicitLod;
    } else {
      if (proj)
        opcode = Op::OpImageSampleProjExplicitLod;
      else
        opcode = Op::OpImageSampleExplicitLod;
    }
  } else {
    if (parameters.Dref) {
      if (proj)
        opcode = Op::OpImageSampleProjDrefImplicitLod;
      else
        opcode = Op::OpImageSampleDrefImplicitLod;
    } else {
      if (proj)
        opcode = Op::OpImageSampleProjImplicitLod;
      else
        opcode = Op::OpImageSampleImplicitLod;
    }
  }

  // See if the result type is expecting a smeared result.
  // This happens when a legacy shadow*() call is made, which
  // gets a vec4 back instead of a float.
  Id smearedType = result_type;
  if (!isScalarType(result_type)) {
    switch (opcode) {
      case Op::OpImageSampleDrefImplicitLod:
      case Op::OpImageSampleDrefExplicitLod:
      case Op::OpImageSampleProjDrefImplicitLod:
      case Op::OpImageSampleProjDrefExplicitLod:
        result_type = getScalarTypeId(result_type);
        break;
      default:
        break;
    }
  }

  // Build the SPIR-V instruction
  auto textureInst = new Instruction(getUniqueId(), result_type, opcode);
  for (int op = 0; op < optArgNum; ++op) {
    textureInst->addIdOperand(texArgs[op]);
  }
  if (optArgNum < numArgs) textureInst->addImmediateOperand(texArgs[optArgNum]);
  for (int op = optArgNum + 1; op < numArgs; ++op) {
    textureInst->addIdOperand(texArgs[op]);
  }
  setPrecision(textureInst->result_id(), precision);
  build_point_->push_instruction(textureInst);

  Id result_id = textureInst->result_id();

  // When a smear is needed, do it, as per what was computed
  // above when result_type was changed to a scalar type.
  if (result_type != smearedType) {
    result_id = smearScalar(precision, result_id, smearedType);
  }

  return result_id;
}

Id SpvEmitter::createTextureQueryCall(Op opcode,
                                      const TextureParameters& parameters) {
  // Figure out the result type
  Id result_type = 0;
  switch (opcode) {
    case Op::OpImageQuerySize:
    case Op::OpImageQuerySizeLod: {
      int numComponents;
      switch (getTypeDimensionality(getImageType(parameters.sampler))) {
        case spv::Dim::Dim1D:
        case spv::Dim::Buffer:
          numComponents = 1;
          break;
        case spv::Dim::Dim2D:
        case spv::Dim::Cube:
        case spv::Dim::Rect:
          numComponents = 2;
          break;
        case spv::Dim::Dim3D:
          numComponents = 3;
          break;
        case spv::Dim::SubpassData:
          CheckNotImplemented("input-attachment dim");
          break;

        default:
          assert(0);
          break;
      }
      if (isArrayedImageType(getImageType(parameters.sampler))) {
        ++numComponents;
      }
      if (numComponents == 1) {
        result_type = makeIntType(32);
      } else {
        result_type = makeVectorType(makeIntType(32), numComponents);
      }
      break;
    }
    case Op::OpImageQueryLod:
      result_type = makeVectorType(makeFloatType(32), 2);
      break;
    case Op::OpImageQueryLevels:
    case Op::OpImageQuerySamples:
      result_type = makeIntType(32);
      break;
    default:
      assert(0);
      break;
  }

  auto query = new Instruction(getUniqueId(), result_type, opcode);
  query->addIdOperand(parameters.sampler);
  if (parameters.coords) {
    query->addIdOperand(parameters.coords);
  }
  if (parameters.lod) {
    query->addIdOperand(parameters.lod);
  }
  build_point_->push_instruction(query);

  return query->result_id();
}

Id SpvEmitter::createCompare(spv::Decoration precision, Id value1, Id value2,
                             bool equal) {
  Id boolType = makeBoolType();
  Id valueType = getTypeId(value1);

  assert(valueType == getTypeId(value2));
  assert(!isScalar(value1));

  // Vectors

  if (isVectorType(valueType)) {
    Op op;
    if (getMostBasicTypeClass(valueType) == Op::OpTypeFloat) {
      op = equal ? Op::OpFOrdEqual : Op::OpFOrdNotEqual;
    } else {
      op = equal ? Op::OpIEqual : Op::OpINotEqual;
    }

    Id boolVectorType =
        makeVectorType(boolType, getNumTypeComponents(valueType));
    Id boolVector = createBinOp(op, boolVectorType, value1, value2);
    setPrecision(boolVector, precision);

    // Reduce vector compares with any() and all().

    op = equal ? Op::OpAll : Op::OpAny;

    return createUnaryOp(op, boolType, boolVector);
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
Id SpvEmitter::createCompositeConstruct(Id type_id,
                                        std::vector<Id>& constituents) {
  assert(isAggregateType(type_id) ||
         (getNumTypeComponents(type_id) > 1 &&
          getNumTypeComponents(type_id) == (int)constituents.size()));

  auto op = new Instruction(getUniqueId(), type_id, Op::OpCompositeConstruct);
  op->addIdOperands(constituents);
  build_point_->push_instruction(op);

  return op->result_id();
}

// Vector or scalar constructor
Id SpvEmitter::createConstructor(spv::Decoration precision,
                                 const std::vector<Id>& sources,
                                 Id result_type_id) {
  Id result = 0;
  unsigned int numTargetComponents = getNumTypeComponents(result_type_id);
  unsigned int targetComponent = 0;

  // Special case: when calling a vector constructor with a single scalar
  // argument, smear the scalar
  if (sources.size() == 1 && isScalar(sources[0]) && numTargetComponents > 1) {
    return smearScalar(precision, sources[0], result_type_id);
  }

  Id scalarTypeId = getScalarTypeId(result_type_id);
  std::vector<Id>
      constituents;  // accumulate the arguments for OpCompositeConstruct
  for (auto source : sources) {
    assert(!isAggregate(source));
    unsigned int sourceSize = getNumComponents(source);
    unsigned int sourcesToUse = sourceSize;
    if (sourcesToUse + targetComponent > numTargetComponents) {
      sourcesToUse = numTargetComponents - targetComponent;
    }

    for (unsigned int s = 0; s < sourcesToUse; ++s) {
      Id arg = source;
      if (sourceSize > 1) {
        std::vector<unsigned> swiz;
        swiz.push_back(s);
        arg = createRvalueSwizzle(scalarTypeId, arg, swiz);
      }

      if (numTargetComponents > 1) {
        constituents.push_back(arg);
      } else {
        result = arg;
      }
      ++targetComponent;
    }

    if (targetComponent >= numTargetComponents) {
      break;
    }
  }

  if (!constituents.empty()) {
    result = createCompositeConstruct(result_type_id, constituents);
  }

  setPrecision(result, precision);

  return result;
}

Id SpvEmitter::createMatrixConstructor(spv::Decoration precision,
                                       const std::vector<Id>& sources,
                                       Id result_type_id) {
  Id componentTypeId = getScalarTypeId(result_type_id);
  int numCols = getTypeNumColumns(result_type_id);
  int numRows = getTypeNumRows(result_type_id);

  // Will use a two step process
  // 1. make a compile-time 2D array of values
  // 2. construct a matrix from that array

  // Step 1.

  // initialize the array to the identity matrix
  Id ids[kMaxMatrixSize][kMaxMatrixSize];
  Id one = makeFloatConstant(1.0);
  Id zero = makeFloatConstant(0.0);
  for (int col = 0; col < 4; ++col) {
    for (int row = 0; row < 4; ++row) {
      if (col == row) {
        ids[col][row] = one;
      } else {
        ids[col][row] = zero;
      }
    }
  }

  // modify components as dictated by the arguments
  if (sources.size() == 1 && isScalar(sources[0])) {
    // a single scalar; resets the diagonals
    for (int col = 0; col < 4; ++col) {
      ids[col][col] = sources[0];
    }
  } else if (isMatrix(sources[0])) {
    // constructing from another matrix; copy over the parts that exist in both
    // the argument and constructee
    Id matrix = sources[0];
    int minCols = std::min(numCols, getNumColumns(matrix));
    int minRows = std::min(numRows, getNumRows(matrix));
    for (int col = 0; col < minCols; ++col) {
      std::vector<unsigned> indexes;
      indexes.push_back(col);
      for (int row = 0; row < minRows; ++row) {
        indexes.push_back(row);
        ids[col][row] =
            createCompositeExtract(matrix, componentTypeId, indexes);
        indexes.pop_back();
        setPrecision(ids[col][row], precision);
      }
    }
  } else {
    // fill in the matrix in column-major order with whatever argument
    // components are available
    int row = 0;
    int col = 0;

    for (auto source : sources) {
      Id argComp = source;
      for (int comp = 0; comp < getNumComponents(source); ++comp) {
        if (getNumComponents(source) > 1) {
          argComp = createCompositeExtract(source, componentTypeId, comp);
          setPrecision(argComp, precision);
        }
        ids[col][row++] = argComp;
        if (row == numRows) {
          row = 0;
          col++;
        }
      }
    }
  }

  // Step 2:  Construct a matrix from that array.
  // First make the column vectors, then make the matrix.

  // make the column vectors
  Id columnTypeId = getContainedTypeId(result_type_id);
  std::vector<Id> matrixColumns;
  for (int col = 0; col < numCols; ++col) {
    std::vector<Id> vectorComponents;
    for (int row = 0; row < numRows; ++row) {
      vectorComponents.push_back(ids[col][row]);
    }
    matrixColumns.push_back(
        createCompositeConstruct(columnTypeId, vectorComponents));
  }

  // make the matrix
  return createCompositeConstruct(result_type_id, matrixColumns);
}

SpvEmitter::If::If(SpvEmitter& emitter, Id condition)
    : emitter_(emitter), condition_(condition) {
  function_ = &emitter_.build_point()->parent();

  // make the blocks, but only put the then-block into the function,
  // the else-block and merge-block will be added later, in order, after
  // earlier code is emitted
  then_block_ = new Block(emitter_.getUniqueId(), *function_);
  merge_block_ = new Block(emitter_.getUniqueId(), *function_);

  // Save the current block, so that we can add in the flow control split when
  // makeEndIf is called.
  header_block_ = emitter_.build_point();

  function_->push_block(then_block_);
  emitter_.set_build_point(then_block_);
}

void SpvEmitter::If::makeBeginElse() {
  // Close out the "then" by having it jump to the merge_block
  emitter_.createBranch(merge_block_);

  // Make the first else block and add it to the function
  else_block_ = new Block(emitter_.getUniqueId(), *function_);
  function_->push_block(else_block_);

  // Start building the else block
  emitter_.set_build_point(else_block_);
}

void SpvEmitter::If::makeEndIf() {
  // jump to the merge block
  emitter_.createBranch(merge_block_);

  // Go back to the header_block and make the flow control split
  emitter_.set_build_point(header_block_);
  emitter_.createSelectionMerge(merge_block_,
                                spv::SelectionControlMask::MaskNone);
  if (else_block_) {
    emitter_.createConditionalBranch(condition_, then_block_, else_block_);
  } else {
    emitter_.createConditionalBranch(condition_, then_block_, merge_block_);
  }

  // add the merge block to the function
  function_->push_block(merge_block_);
  emitter_.set_build_point(merge_block_);
}

void SpvEmitter::makeSwitch(Id selector, int numSegments,
                            std::vector<int>& caseValues,
                            std::vector<int>& valueIndexToSegment,
                            int defaultSegment,
                            std::vector<Block*>& segmentBlocks) {
  Function& function = build_point_->parent();

  // make all the blocks
  for (int s = 0; s < numSegments; ++s) {
    segmentBlocks.push_back(new Block(getUniqueId(), function));
  }

  Block* merge_block = new Block(getUniqueId(), function);

  // make and insert the switch's selection-merge instruction
  createSelectionMerge(merge_block, spv::SelectionControlMask::MaskNone);

  // make the switch instruction
  auto switchInst = new Instruction(NoResult, NoType, Op::OpSwitch);
  switchInst->addIdOperand(selector);
  switchInst->addIdOperand(defaultSegment >= 0
                               ? segmentBlocks[defaultSegment]->id()
                               : merge_block->id());
  for (int i = 0; i < (int)caseValues.size(); ++i) {
    switchInst->addImmediateOperand(caseValues[i]);
    switchInst->addIdOperand(segmentBlocks[valueIndexToSegment[i]]->id());
  }
  build_point_->push_instruction(switchInst);

  // push the merge block
  switch_merges_.push(merge_block);
}

void SpvEmitter::addSwitchBreak() {
  // branch to the top of the merge block stack
  createBranch(switch_merges_.top());
  createAndSetNoPredecessorBlock("post-switch-break");
}

void SpvEmitter::nextSwitchSegment(std::vector<Block*>& segmentBlock,
                                   int nextSegment) {
  int lastSegment = nextSegment - 1;
  if (lastSegment >= 0) {
    // Close out previous segment by jumping, if necessary, to next segment
    if (!build_point_->is_terminated()) {
      createBranch(segmentBlock[nextSegment]);
    }
  }
  Block* block = segmentBlock[nextSegment];
  block->parent().push_block(block);
  set_build_point(block);
}

void SpvEmitter::endSwitch(std::vector<Block*>& /*segmentBlock*/) {
  // Close out previous segment by jumping, if necessary, to next segment
  if (!build_point_->is_terminated()) {
    addSwitchBreak();
  }

  switch_merges_.top()->parent().push_block(switch_merges_.top());
  set_build_point(switch_merges_.top());

  switch_merges_.pop();
}

void SpvEmitter::makeNewLoop(bool loopTestFirst) {
  loops_.push(Loop(*this, loopTestFirst));
  const Loop& loop = loops_.top();

  // The loop test is always emitted before the loop body.
  // But if the loop test executes at the bottom of the loop, then
  // execute the test only on the second and subsequent iterations.

  // Remember the block that branches to the loop header.  This
  // is required for the test-after-body case.
  Block* preheader = build_point();

  // Branch into the loop
  createBranch(loop.header);

  // Set ourselves inside the loop
  loop.function->push_block(loop.header);
  set_build_point(loop.header);

  if (!loopTestFirst) {
    // Generate code to defer the loop test until the second and
    // subsequent iterations.

    // It's always the first iteration when coming from the preheader.
    // All other branches to this loop header will need to indicate "false",
    // but we don't yet know where they will come from.
    loop.is_first_iteration->addIdOperand(makeBoolConstant(true));
    loop.is_first_iteration->addIdOperand(preheader->id());
    build_point()->push_instruction(loop.is_first_iteration);

    // Mark the end of the structured loop. This must exist in the loop header
    // block.
    createLoopMerge(loop.merge, loop.header, spv::LoopControlMask::MaskNone);

    // Generate code to see if this is the first iteration of the loop.
    // It needs to be in its own block, since the loop merge and
    // the selection merge instructions can't both be in the same
    // (header) block.
    Block* firstIterationCheck = new Block(getUniqueId(), *loop.function);
    createBranch(firstIterationCheck);
    loop.function->push_block(firstIterationCheck);
    set_build_point(firstIterationCheck);

    // Control flow after this "if" normally reconverges at the loop body.
    // However, the loop test has a "break branch" out of this selection
    // construct because it can transfer control to the loop merge block.
    createSelectionMerge(loop.body, spv::SelectionControlMask::MaskNone);

    Block* loopTest = new Block(getUniqueId(), *loop.function);
    createConditionalBranch(loop.is_first_iteration->result_id(), loop.body,
                            loopTest);

    loop.function->push_block(loopTest);
    set_build_point(loopTest);
  }
}

void SpvEmitter::createLoopTestBranch(Id condition) {
  const Loop& loop = loops_.top();

  // Generate the merge instruction. If the loop test executes before
  // the body, then this is a loop merge.  Otherwise the loop merge
  // has already been generated and this is a conditional merge.
  if (loop.test_first) {
    createLoopMerge(loop.merge, loop.header, spv::LoopControlMask::MaskNone);
    // Branching to the "body" block will keep control inside
    // the loop.
    createConditionalBranch(condition, loop.body, loop.merge);
    loop.function->push_block(loop.body);
    set_build_point(loop.body);
  } else {
    // The branch to the loop merge block is the allowed exception
    // to the structured control flow.  Otherwise, control flow will
    // continue to loop.body block.  Since that is already the target
    // of a merge instruction, and a block can't be the target of more
    // than one merge instruction, we need to make an intermediate block.
    Block* stayInLoopBlock = new Block(getUniqueId(), *loop.function);
    createSelectionMerge(stayInLoopBlock, spv::SelectionControlMask::MaskNone);

    // This is the loop test.
    createConditionalBranch(condition, stayInLoopBlock, loop.merge);

    // The dummy block just branches to the real loop body.
    loop.function->push_block(stayInLoopBlock);
    set_build_point(stayInLoopBlock);
    createBranchToBody();
  }
}

void SpvEmitter::createBranchToBody() {
  const Loop& loop = loops_.top();
  assert(loop.body);

  // This is a reconvergence of control flow, so no merge instruction
  // is required.
  createBranch(loop.body);
  loop.function->push_block(loop.body);
  set_build_point(loop.body);
}

void SpvEmitter::createLoopContinue() {
  createBranchToLoopHeaderFromInside(loops_.top());
  // Set up a block for dead code.
  createAndSetNoPredecessorBlock("post-loop-continue");
}

// Add an exit (e.g. "break") for the innermost loop that you're in
void SpvEmitter::createLoopExit() {
  createBranch(loops_.top().merge);
  // Set up a block for dead code.
  createAndSetNoPredecessorBlock("post-loop-break");
}

// Close the innermost loop
void SpvEmitter::closeLoop() {
  const Loop& loop = loops_.top();

  // Branch back to the top
  createBranchToLoopHeaderFromInside(loop);

  // Add the merge block and set the build point to it
  loop.function->push_block(loop.merge);
  set_build_point(loop.merge);

  loops_.pop();
}

// Create a branch to the header of the given loop, from inside
// the loop body.
// Adjusts the phi node for the first-iteration value if needeed.
void SpvEmitter::createBranchToLoopHeaderFromInside(const Loop& loop) {
  createBranch(loop.header);
  if (loop.is_first_iteration) {
    loop.is_first_iteration->addIdOperand(makeBoolConstant(false));
    loop.is_first_iteration->addIdOperand(build_point()->id());
  }
}

void SpvEmitter::clearAccessChain() {
  access_chain_.base = NoResult;
  access_chain_.index_chain.clear();
  access_chain_.instr = NoResult;
  access_chain_.swizzle.clear();
  access_chain_.component = NoResult;
  access_chain_.pre_swizzle_base_type = NoType;
  access_chain_.is_rvalue = false;
}

void SpvEmitter::accessChainPushSwizzle(std::vector<unsigned>& swizzle,
                                        Id pre_swizzle_base_type) {
  // swizzles can be stacked in GLSL, but simplified to a single
  // one here; the base type doesn't change
  if (access_chain_.pre_swizzle_base_type == NoType) {
    access_chain_.pre_swizzle_base_type = pre_swizzle_base_type;
  }

  // if needed, propagate the swizzle for the current access chain
  if (access_chain_.swizzle.size()) {
    std::vector<unsigned> oldSwizzle = access_chain_.swizzle;
    access_chain_.swizzle.resize(0);
    for (unsigned int i = 0; i < swizzle.size(); ++i) {
      access_chain_.swizzle.push_back(oldSwizzle[swizzle[i]]);
    }
  } else {
    access_chain_.swizzle = swizzle;
  }

  // determine if we need to track this swizzle anymore
  simplifyAccessChainSwizzle();
}

void SpvEmitter::accessChainStore(Id rvalue) {
  assert(access_chain_.is_rvalue == false);

  transferAccessChainSwizzle(true);
  Id base = collapseAccessChain();

  if (access_chain_.swizzle.size() && access_chain_.component != NoResult) {
    CheckNotImplemented(
        "simultaneous l-value swizzle and dynamic component selection");
  }

  // If swizzle still exists, it is out-of-order or not full, we must load the
  // target vector,
  // extract and insert elements to perform writeMask and/or swizzle.
  Id source = NoResult;
  if (access_chain_.swizzle.size()) {
    Id tempBaseId = createLoad(base);
    source = createLvalueSwizzle(getTypeId(tempBaseId), tempBaseId, rvalue,
                                 access_chain_.swizzle);
  }

  // dynamic component selection
  if (access_chain_.component != NoResult) {
    Id tempBaseId = (source == NoResult) ? createLoad(base) : source;
    source = createVectorInsertDynamic(tempBaseId, getTypeId(tempBaseId),
                                       rvalue, access_chain_.component);
  }

  if (source == NoResult) {
    source = rvalue;
  }

  createStore(source, base);
}

Id SpvEmitter::accessChainLoad(Id result_type) {
  Id id;

  if (access_chain_.is_rvalue) {
    // transfer access chain, but keep it static, so we can stay in registers
    transferAccessChainSwizzle(false);
    if (!access_chain_.index_chain.empty()) {
      Id swizzleBase = access_chain_.pre_swizzle_base_type != NoType
                           ? access_chain_.pre_swizzle_base_type
                           : result_type;

      // if all the accesses are constants, we can use OpCompositeExtract
      std::vector<unsigned> indexes;
      bool constant = true;
      for (auto index : access_chain_.index_chain) {
        if (isConstantScalar(index)) {
          indexes.push_back(getConstantScalar(index));
        } else {
          constant = false;
          break;
        }
      }

      if (constant) {
        id = createCompositeExtract(access_chain_.base, swizzleBase, indexes);
      } else {
        // make a new function variable for this r-value
        Id lvalue = createVariable(spv::StorageClass::Function,
                                   getTypeId(access_chain_.base), "indexable");

        // store into it
        createStore(access_chain_.base, lvalue);

        // move base to the new variable
        access_chain_.base = lvalue;
        access_chain_.is_rvalue = false;

        // load through the access chain
        id = createLoad(collapseAccessChain());
      }
    } else {
      id = access_chain_.base;
    }
  } else {
    transferAccessChainSwizzle(true);
    // load through the access chain
    id = createLoad(collapseAccessChain());
  }

  // Done, unless there are swizzles to do
  if (access_chain_.swizzle.empty() && access_chain_.component == NoResult) {
    return id;
  }

  // Do remaining swizzling
  // First, static swizzling
  if (access_chain_.swizzle.size()) {
    // static swizzle
    Id swizzledType = getScalarTypeId(getTypeId(id));
    if (access_chain_.swizzle.size() > 1) {
      swizzledType =
          makeVectorType(swizzledType, (int)access_chain_.swizzle.size());
    }
    id = createRvalueSwizzle(swizzledType, id, access_chain_.swizzle);
  }

  // dynamic single-component selection
  if (access_chain_.component != NoResult) {
    id = createVectorExtractDynamic(id, result_type, access_chain_.component);
  }

  return id;
}

Id SpvEmitter::accessChainGetLValue() {
  assert(access_chain_.is_rvalue == false);

  transferAccessChainSwizzle(true);
  Id lvalue = collapseAccessChain();

  // If swizzle exists, it is out-of-order or not full, we must load the target
  // vector,
  // extract and insert elements to perform writeMask and/or swizzle.  This does
  // not
  // go with getting a direct l-value pointer.
  assert(access_chain_.swizzle.empty());
  assert(access_chain_.component == NoResult);

  return lvalue;
}

void SpvEmitter::dump(std::vector<unsigned int>& out) const {
  // Header, before first instructions:
  out.push_back(spv::MagicNumber);
  out.push_back(spv::Version);
  out.push_back(builder_number_);
  out.push_back(unique_id_ + 1);
  out.push_back(0);

  for (auto capability : capabilities_) {
    Instruction capInst(0, 0, Op::OpCapability);
    capInst.addImmediateOperand(capability);
    capInst.dump(out);
  }

  // TBD: OpExtension ...

  dumpInstructions(out, imports_);
  Instruction memInst(0, 0, Op::OpMemoryModel);
  memInst.addImmediateOperand(addressing_model_);
  memInst.addImmediateOperand(memory_model_);
  memInst.dump(out);

  // Instructions saved up while building:
  dumpInstructions(out, entry_points_);
  dumpInstructions(out, execution_modes_);

  // Debug instructions
  if (source_language_ != spv::SourceLanguage::Unknown) {
    Instruction sourceInst(0, 0, Op::OpSource);
    sourceInst.addImmediateOperand(source_language_);
    sourceInst.addImmediateOperand(source_version_);
    sourceInst.dump(out);
  }
  for (auto extension : extensions_) {
    Instruction extInst(0, 0, Op::OpSourceExtension);
    extInst.addStringOperand(extension);
    extInst.dump(out);
  }
  dumpInstructions(out, names_);
  dumpInstructions(out, lines_);

  // Annotation instructions
  dumpInstructions(out, decorations_);

  dumpInstructions(out, constants_types_globals_);
  dumpInstructions(out, externals_);

  // The functions
  module_.dump(out);
}

// Turn the described access chain in 'accessChain' into an instruction
// computing its address.  This *cannot* include complex swizzles, which must
// be handled after this is called, but it does include swizzles that select
// an individual element, as a single address of a scalar type can be
// computed by an OpAccessChain instruction.
Id SpvEmitter::collapseAccessChain() {
  assert(access_chain_.is_rvalue == false);

  if (!access_chain_.index_chain.empty()) {
    if (!access_chain_.instr) {
      auto storage_class =
          module_.getStorageClass(getTypeId(access_chain_.base));
      access_chain_.instr = createAccessChain(storage_class, access_chain_.base,
                                              access_chain_.index_chain);
    }
    return access_chain_.instr;
  } else {
    return access_chain_.base;
  }

  // note that non-trivial swizzling is left pending...
}

// clear out swizzle if it is redundant, that is reselecting the same components
// that would be present without the swizzle.
void SpvEmitter::simplifyAccessChainSwizzle() {
  // If the swizzle has fewer components than the vector, it is subsetting, and
  // must stay
  // to preserve that fact.
  if (getNumTypeComponents(access_chain_.pre_swizzle_base_type) >
      (int)access_chain_.swizzle.size()) {
    return;
  }

  // if components are out of order, it is a swizzle
  for (unsigned int i = 0; i < access_chain_.swizzle.size(); ++i) {
    if (i != access_chain_.swizzle[i]) {
      return;
    }
  }

  // otherwise, there is no need to track this swizzle
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
void SpvEmitter::transferAccessChainSwizzle(bool dynamic) {
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
  if (isBoolType(getContainedTypeId(access_chain_.pre_swizzle_base_type))) {
    return;
  }

  if (access_chain_.swizzle.size() == 1) {
    // handle static component
    access_chain_.index_chain.push_back(
        makeUintConstant(access_chain_.swizzle.front()));
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

// Utility method for creating a new block and setting the insert point to
// be in it. This is useful for flow-control operations that need a "dummy"
// block proceeding them (e.g. instructions after a discard, etc).
void SpvEmitter::createAndSetNoPredecessorBlock(const char* name) {
  Block* block = new Block(getUniqueId(), build_point_->parent());
  block->set_unreachable(true);
  build_point_->parent().push_block(block);
  set_build_point(block);

  // if (name)
  //    addName(block->id(), name);
}

void SpvEmitter::createBranch(Block* block) {
  auto branch = new Instruction(Op::OpBranch);
  branch->addIdOperand(block->id());
  build_point_->push_instruction(branch);
  block->push_predecessor(build_point_);
}

void SpvEmitter::createSelectionMerge(Block* merge_block,
                                      spv::SelectionControlMask control) {
  auto merge = new Instruction(Op::OpSelectionMerge);
  merge->addIdOperand(merge_block->id());
  merge->addImmediateOperand(control);
  build_point_->push_instruction(merge);
}

void SpvEmitter::createLoopMerge(Block* merge_block, Block* continueBlock,
                                 spv::LoopControlMask control) {
  auto merge = new Instruction(Op::OpLoopMerge);
  merge->addIdOperand(merge_block->id());
  merge->addIdOperand(continueBlock->id());
  merge->addImmediateOperand(control);
  build_point_->push_instruction(merge);
}

void SpvEmitter::createConditionalBranch(Id condition, Block* then_block,
                                         Block* else_block) {
  auto branch = new Instruction(Op::OpBranchConditional);
  branch->addIdOperand(condition);
  branch->addIdOperand(then_block->id());
  branch->addIdOperand(else_block->id());
  build_point_->push_instruction(branch);
  then_block->push_predecessor(build_point_);
  else_block->push_predecessor(build_point_);
}

void SpvEmitter::dumpInstructions(
    std::vector<unsigned int>& out,
    const std::vector<Instruction*>& instructions) const {
  for (int i = 0; i < (int)instructions.size(); ++i) {
    instructions[i]->dump(out);
  }
}

void SpvEmitter::CheckNotImplemented(const char* message) {
  xe::FatalError("Missing functionality: %s", message);
}

SpvEmitter::Loop::Loop(SpvEmitter& emitter, bool testFirstArg)
    : function(&emitter.build_point()->parent()),
      header(new Block(emitter.getUniqueId(), *function)),
      merge(new Block(emitter.getUniqueId(), *function)),
      body(new Block(emitter.getUniqueId(), *function)),
      test_first(testFirstArg),
      is_first_iteration(nullptr) {
  if (!test_first) {
    // You may be tempted to rewrite this as
    // new Instruction(builder.getUniqueId(), builder.makeBoolType(), OpPhi);
    // This will cause subtle test failures because builder.getUniqueId(),
    // and builder.makeBoolType() can then get run in a compiler-specific
    // order making tests fail for certain configurations.
    Id instructionId = emitter.getUniqueId();
    is_first_iteration =
        new Instruction(instructionId, emitter.makeBoolType(), Op::OpPhi);
  }
}

}  // namespace spirv
}  // namespace gpu
}  // namespace xe
