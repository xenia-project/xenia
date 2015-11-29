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

#ifndef XENIA_UI_SPIRV_SPIRV_EMITTER_H_
#define XENIA_UI_SPIRV_SPIRV_EMITTER_H_

#include <algorithm>
#include <map>
#include <stack>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/ui/spirv/spirv_ir.h"
#include "xenia/ui/spirv/spirv_util.h"

namespace xe {
namespace ui {
namespace spirv {

class SpirvEmitter {
 public:
  SpirvEmitter();
  ~SpirvEmitter();

  // Document what source language and text this module was translated from.
  void SetSourceLanguage(spv::SourceLanguage language, int version) {
    source_language_ = language;
    source_version_ = version;
  }

  // Document an extension to the source language. Informational only.
  void AddSourceExtension(const char* ext) {
    source_extensions_.push_back(ext);
  }

  // Set addressing model and memory model for the entire module.
  void SetMemoryModel(spv::AddressingModel addressing_model,
                      spv::MemoryModel memory_model) {
    addressing_model_ = addressing_model;
    memory_model_ = memory_model;
  }

  // Declare a capability used by this module.
  void DeclareCapability(spv::Capability cap) { capabilities_.push_back(cap); }

  // Import an extended set of instructions that can be later referenced by the
  // returned id.
  Id ImportExtendedInstructions(const char* name);

  // For creating new types (will return old type if the requested one was
  // already made).
  Id MakeVoidType();
  Id MakeBoolType();
  Id MakePointer(spv::StorageClass storage_class, Id pointee);
  Id MakeIntegerType(int bit_width, bool is_signed);
  Id MakeIntType(int bit_width) { return MakeIntegerType(bit_width, true); }
  Id MakeUintType(int bit_width) { return MakeIntegerType(bit_width, false); }
  Id MakeFloatType(int bit_width);
  Id MakeStructType(std::initializer_list<Id> members, const char* name);
  Id MakePairStructType(Id type0, Id type1);
  Id MakeVectorType(Id component_type, int component_count);
  Id MakeMatrix2DType(Id component_type, int cols, int rows);
  Id MakeArrayType(Id element_type, int length);
  Id MakeRuntimeArray(Id element_type);
  Id MakeFunctionType(Id return_type, std::initializer_list<Id> param_types);
  Id MakeImageType(Id sampled_type, spv::Dim dim, bool has_depth,
                   bool is_arrayed, bool is_multisampled, int sampled,
                   spv::ImageFormat format);
  Id MakeSamplerType();
  Id MakeSampledImageType(Id image_type);

  // For querying about types.
  Id GetTypeId(Id result_id) const { return module_.type_id(result_id); }
  Id GetDerefTypeId(Id result_id) const;
  Op GetOpcode(Id id) const { return module_.instruction(id)->opcode(); }
  Op GetTypeClass(Id type_id) const { return GetOpcode(type_id); }
  Op GetMostBasicTypeClass(Id type_id) const;
  int GetComponentCount(Id result_id) const {
    return GetTypeComponentCount(GetTypeId(result_id));
  }
  int GetTypeComponentCount(Id type_id) const;
  Id GetScalarTypeId(Id type_id) const;
  Id GetContainedTypeId(Id type_id) const;
  Id GetContainedTypeId(Id type_id, int member) const;
  spv::StorageClass GetTypeStorageClass(Id type_id) const {
    return module_.storage_class(type_id);
  }

  bool IsPointer(Id result_id) const {
    return IsPointerType(GetTypeId(result_id));
  }
  bool IsScalar(Id result_id) const {
    return IsScalarType(GetTypeId(result_id));
  }
  bool IsVector(Id result_id) const {
    return IsVectorType(GetTypeId(result_id));
  }
  bool IsMatrix(Id result_id) const {
    return IsMatrixType(GetTypeId(result_id));
  }
  bool IsAggregate(Id result_id) const {
    return IsAggregateType(GetTypeId(result_id));
  }
  bool IsBoolType(Id type_id) const {
    return grouped_types_[static_cast<int>(spv::Op::OpTypeBool)].size() > 0 &&
           type_id ==
               grouped_types_[static_cast<int>(spv::Op::OpTypeBool)]
                   .back()
                   ->result_id();
  }

  bool IsPointerType(Id type_id) const {
    return GetTypeClass(type_id) == spv::Op::OpTypePointer;
  }
  bool IsScalarType(Id type_id) const {
    return GetTypeClass(type_id) == spv::Op::OpTypeFloat ||
           GetTypeClass(type_id) == spv::Op::OpTypeInt ||
           GetTypeClass(type_id) == spv::Op::OpTypeBool;
  }
  bool IsVectorType(Id type_id) const {
    return GetTypeClass(type_id) == spv::Op::OpTypeVector;
  }
  bool IsMatrixType(Id type_id) const {
    return GetTypeClass(type_id) == spv::Op::OpTypeMatrix;
  }
  bool IsStructType(Id type_id) const {
    return GetTypeClass(type_id) == spv::Op::OpTypeStruct;
  }
  bool IsArrayType(Id type_id) const {
    return GetTypeClass(type_id) == spv::Op::OpTypeArray;
  }
  bool IsAggregateType(Id type_id) const {
    return IsArrayType(type_id) || IsStructType(type_id);
  }
  bool IsImageType(Id type_id) const {
    return GetTypeClass(type_id) == spv::Op::OpTypeImage;
  }
  bool IsSamplerType(Id type_id) const {
    return GetTypeClass(type_id) == spv::Op::OpTypeSampler;
  }
  bool IsSampledImageType(Id type_id) const {
    return GetTypeClass(type_id) == spv::Op::OpTypeSampledImage;
  }

  bool IsConstantOpCode(Op opcode) const;
  bool IsConstant(Id result_id) const {
    return IsConstantOpCode(GetOpcode(result_id));
  }
  bool IsConstantScalar(Id result_id) const {
    return GetOpcode(result_id) == spv::Op::OpConstant;
  }
  uint32_t GetConstantScalar(Id result_id) const {
    return module_.instruction(result_id)->immediate_operand(0);
  }
  spv::StorageClass GetStorageClass(Id result_id) const {
    return GetTypeStorageClass(GetTypeId(result_id));
  }

  int GetTypeColumnCount(Id type_id) const {
    assert(IsMatrixType(type_id));
    return GetTypeComponentCount(type_id);
  }
  int GetColumnCount(Id result_id) const {
    return GetTypeColumnCount(GetTypeId(result_id));
  }
  int GetTypeRowCount(Id type_id) const {
    assert(IsMatrixType(type_id));
    return GetTypeComponentCount(GetContainedTypeId(type_id));
  }
  int GetRowCount(Id result_id) const {
    return GetTypeRowCount(GetTypeId(result_id));
  }

  spv::Dim GetTypeDimensionality(Id type_id) const {
    assert(IsImageType(type_id));
    return static_cast<spv::Dim>(
        module_.instruction(type_id)->immediate_operand(1));
  }
  Id GetImageType(Id result_id) const {
    Id type_id = GetTypeId(result_id);
    assert(IsImageType(type_id) || IsSampledImageType(type_id));
    return IsSampledImageType(type_id)
               ? module_.instruction(type_id)->id_operand(0)
               : type_id;
  }
  bool IsArrayedImageType(Id type_id) const {
    assert(IsImageType(type_id));
    return module_.instruction(type_id)->immediate_operand(3) != 0;
  }

  // For making new constants (will return old constant if the requested one was
  // already made).
  Id MakeBoolConstant(bool value, bool is_spec_constant = false);
  Id MakeIntConstant(int value, bool is_spec_constant = false) {
    return MakeIntegerConstant(MakeIntType(32), static_cast<uint32_t>(value),
                               is_spec_constant);
  }
  Id MakeUintConstant(uint32_t value, bool is_spec_constant = false) {
    return MakeIntegerConstant(MakeUintType(32), value, is_spec_constant);
  }
  template <typename T>
  Id MakeUintConstant(T value, bool is_spec_constant = false) {
    static_assert(sizeof(T) == sizeof(uint32_t), "Invalid type");
    return MakeIntegerConstant(MakeUintType(32), static_cast<uint32_t>(value),
                               is_spec_constant);
  }
  Id MakeFloatConstant(float value, bool is_spec_constant = false);
  Id MakeDoubleConstant(double value, bool is_spec_constant = false);

  // Turns the array of constants into a proper constant of the requested type.
  Id MakeCompositeConstant(Id type, std::initializer_list<Id> components);

  // Declares an entry point and its execution model.
  Instruction* AddEntryPoint(spv::ExecutionModel execution_model,
                             Function* entry_point, const char* name);
  void AddExecutionMode(Function* entry_point,
                        spv::ExecutionMode execution_mode, int value1 = -1,
                        int value2 = -1, int value3 = -1);
  void AddName(Id target_id, const char* name);
  void AddMemberName(Id target_id, int member, const char* name);
  void AddLine(Id target_id, Id file_name, int line_number, int column_number);
  void AddDecoration(Id target_id, spv::Decoration decoration, int num = -1);
  void AddMemberDecoration(Id target_id, int member, spv::Decoration,
                           int num = -1);

  // At the end of what block do the next create*() instructions go?
  Block* build_point() const { return build_point_; }
  void set_build_point(Block* build_point) { build_point_ = build_point; }

  // Makes the main function.
  Function* MakeMainEntry();

  // Makes a shader-style function, and create its entry block if entry is
  // non-zero.
  // Return the function, pass back the entry.
  Function* MakeFunctionEntry(Id return_type, const char* name,
                              std::initializer_list<Id> param_types,
                              Block** entry = 0);

  // Creates a return statement.
  // An 'implicit' return is one not appearing in the source code. In the case
  // of an implicit return, no post-return block is inserted.
  void MakeReturn(bool implicit, Id return_value = 0);

  // Generates all the code needed to finish up a function.
  void LeaveFunction();

  // Creates a fragment-shader discard (kill).
  void MakeDiscard();

  // Creates a global or function local or IO variable.
  Id CreateVariable(spv::StorageClass storage_class, Id type,
                    const char* name = 0);

  // Creates an intermediate object whose value is undefined.
  Id CreateUndefined(Id type);

  // Stores the given value into the specified pointer.
  void CreateStore(Id pointer_id, Id value_id);

  // Loads the value from the given pointer.
  Id CreateLoad(Id pointer_id);

  // Creates a pointer into a composite object that can be used with OpLoad and
  // OpStore.
  Id CreateAccessChain(spv::StorageClass storage_class, Id base_id,
                       std::vector<Id> index_ids);

  // Queries the length of a run-time array.
  Id CreateArrayLength(Id struct_id, int array_member);

  Id CreateCompositeExtract(Id composite, Id type_id, uint32_t index);
  Id CreateCompositeExtract(Id composite, Id type_id,
                            std::vector<uint32_t> indexes);
  Id CreateCompositeInsert(Id object, Id composite, Id type_id, uint32_t index);
  Id CreateCompositeInsert(Id object, Id composite, Id type_id,
                           std::vector<uint32_t> indexes);

  Id CreateVectorExtractDynamic(Id vector, Id type_id, Id component_index);
  Id CreateVectorInsertDynamic(Id vector, Id type_id, Id component,
                               Id component_index);

  // Does nothing.
  void CreateNop();

  // Waits for other invocations of this module to reach the current point of
  // execution.
  void CreateControlBarrier(spv::Scope execution_scope, spv::Scope memory_scope,
                            spv::MemorySemanticsMask memory_semantics);
  // Controls the order that memory accesses are observed.
  void CreateMemoryBarrier(spv::Scope execution_scope,
                           spv::MemorySemanticsMask memory_semantics);

  Id CreateUnaryOp(Op opcode, Id type_id, Id operand);
  Id CreateBinOp(Op opcode, Id type_id, Id operand1, Id operand2);
  Id CreateTriOp(Op opcode, Id type_id, Id operand1, Id operand2, Id operand3);
  Id CreateOp(Op opcode, Id type_id, const std::vector<Id>& operands);
  Id CreateFunctionCall(Function* function, std::vector<spv::Id> args);

  // Takes an rvalue (source) and a set of channels to extract from it to
  // make a new rvalue.
  Id CreateSwizzle(Id type_id, Id source, std::vector<uint32_t> channels);

  // Takes a copy of an lvalue (target) and a source of components, and sets the
  // source components into the lvalue where the 'channels' say to put them.
  Id CreateLvalueSwizzle(Id type_id, Id target, Id source,
                         std::vector<uint32_t> channels);

  // If the value passed in is an instruction and the precision is not EMpNone,
  // it gets tagged with the requested precision.
  void SetPrecision(Id value, spv::Decoration precision) {
    CheckNotImplemented("setPrecision");
  }

  // Smears a scalar to a vector for the following forms:
  //   - PromoteScalar(scalar, vector)  // smear scalar to width of vector
  //   - PromoteScalar(vector, scalar)  // smear scalar to width of vector
  //   - PromoteScalar(pointer, scalar) // smear scalar to width of what pointer
  //     points to
  //   - PromoteScalar(scalar, scalar)  // do nothing
  // Other forms are not allowed.
  //
  // Note: One of the arguments will change, with the result coming back that
  // way rather than through the return value.
  void PromoteScalar(spv::Decoration precision, Id& left, Id& right);

  // Makes a value by smearing the scalar to fill the type.
  Id SmearScalar(spv::Decoration precision, Id scalar_value, Id vector_type_id);

  // Executes an instruction in an imported set of extended instructions.
  Id CreateExtendedInstructionCall(spv::Decoration precision, Id result_type,
                                   Id instruction_set, int instruction_ordinal,
                                   std::initializer_list<Id> args);
  // Executes an instruction from the extended GLSL set.
  Id CreateGlslStd450InstructionCall(spv::Decoration precision, Id result_type,
                                     spv::GLSLstd450 instruction_ordinal,
                                     std::initializer_list<Id> args);

  // List of parameters used to create a texture operation
  struct TextureParameters {
    Id sampler;
    Id coords;
    Id bias;
    Id lod;
    Id depth_ref;
    Id offset;
    Id offsets;
    Id grad_x;
    Id grad_y;
    Id sample;
    Id comp;
  };

  // Selects the correct texture operation based on all inputs, and emit the
  // correct instruction.
  Id CreateTextureCall(spv::Decoration precision, Id result_type, bool fetch,
                       bool proj, bool gather,
                       const TextureParameters& parameters);

  // Emits the OpTextureQuery* instruction that was passed in and figures out
  // the right return value and type.
  Id CreateTextureQueryCall(Op opcode, const TextureParameters& parameters);

  Id CreateSamplePositionCall(spv::Decoration precision, Id, Id);
  Id CreateBitFieldExtractCall(spv::Decoration precision, Id, Id, Id,
                               bool isSigned);
  Id CreateBitFieldInsertCall(spv::Decoration precision, Id, Id, Id, Id);

  // Reduction comparision for composites:  For equal and not-equal resulting in
  // a scalar.
  Id CreateCompare(spv::Decoration precision, Id value1, Id value2,
                   bool is_equal);

  // OpCompositeConstruct
  Id CreateCompositeConstruct(Id type_id, std::vector<Id> constituent_ids);

  // vector or scalar constructor
  Id CreateConstructor(spv::Decoration precision, std::vector<Id> source_ids,
                       Id result_type_id);

  // matrix constructor
  Id CreateMatrixConstructor(spv::Decoration precision, std::vector<Id> sources,
                             Id constructee);

  // Helper to use for building nested control flow with if-then-else.
  class If {
   public:
    If(SpirvEmitter& emitter, Id condition);
    ~If() = default;

    void MakeBeginElse();
    void MakeEndIf();

   private:
    If(const If&) = delete;
    If& operator=(If&) = delete;

    SpirvEmitter& emitter_;
    Id condition_;
    Function* function_ = nullptr;
    Block* header_block_ = nullptr;
    Block* then_block_ = nullptr;
    Block* else_block_ = nullptr;
    Block* merge_block_ = nullptr;
  };

  // Makes a switch statement.
  // A switch has 'numSegments' of pieces of code, not containing any
  // case/default labels, all separated by one or more case/default labels.
  // Each possible case value v is a jump to the caseValues[v] segment. The
  // defaultSegment is also in this number space. How to compute the value is
  // given by 'condition', as in switch(condition).
  //
  // The SPIR-V Builder will maintain the stack of post-switch merge blocks for
  // nested switches.
  //
  // Use a defaultSegment < 0 if there is no default segment (to branch to post
  // switch).
  //
  // Returns the right set of basic blocks to start each code segment with, so
  // that the caller's recursion stack can hold the memory for it.
  void MakeSwitch(Id condition, int segment_count, std::vector<int> case_values,
                  std::vector<int> value_index_to_segment, int default_segment,
                  std::vector<Block*>& segment_blocks);

  // Adds a branch to the innermost switch's merge block.
  void AddSwitchBreak();

  // Move sto the next code segment, passing in the return argument in
  // MakeSwitch().
  void NextSwitchSegment(std::vector<Block*>& segment_block, int next_segment);

  // Finishes off the innermost switch.
  void EndSwitch(std::vector<Block*>& segment_block);

  // Starts the beginning of a new loop, and prepare the builder to
  // generate code for the loop test.
  // The test_first parameter is true when the loop test executes before
  // the body (it is false for do-while loops).
  void MakeNewLoop(bool test_first);

  // Adds the branch for the loop test, based on the given condition.
  // The true branch goes to the first block in the loop body, and
  // the false branch goes to the loop's merge block.  The builder insertion
  // point will be placed at the start of the body.
  void CreateLoopTestBranch(Id condition);

  // Generates an unconditional branch to the loop body.
  // The builder insertion point will be placed at the start of the body.
  // Use this when there is no loop test.
  void CreateBranchToBody();

  // Adds a branch to the test of the current (innermost) loop.
  // The way we generate code, that's also the loop header.
  void CreateLoopContinue();

  // Adds an exit (e.g. "break") for the innermost loop that you're in.
  void CreateLoopExit();

  // Close the innermost loop that you're in.
  void CloseLoop();

  // Access chain design for an R-Value vs. L-Value:
  //
  // There is a single access chain the builder is building at
  // any particular time.  Such a chain can be used to either to a load or
  // a store, when desired.
  //
  // Expressions can be r-values, l-values, or both, or only r-values:
  //    a[b.c].d = ....  // l-value
  //    ... = a[b.c].d;  // r-value, that also looks like an l-value
  //    ++a[b.c].d;      // r-value and l-value
  //    (x + y)[2];      // r-value only, can't possibly be l-value
  //
  // Computing an r-value means generating code.  Hence,
  // r-values should only be computed when they are needed, not speculatively.
  //
  // Computing an l-value means saving away information for later use in the
  // compiler,
  // no code is generated until the l-value is later dereferenced.  It is okay
  // to speculatively generate an l-value, just not okay to speculatively
  // dereference it.
  //
  // The base of the access chain (the left-most variable or expression
  // from which everything is based) can be set either as an l-value
  // or as an r-value.  Most efficient would be to set an l-value if one
  // is available.  If an expression was evaluated, the resulting r-value
  // can be set as the chain base.
  //
  // The users of this single access chain can save and restore if they
  // want to nest or manage multiple chains.
  //
  struct AccessChain {
    Id base;  // for l-values, pointer to the base object, for r-values, the
              // base object
    std::vector<Id> index_chain;
    Id instr;  // cache the instruction that generates this access chain
    std::vector<uint32_t> swizzle;  // each std::vector element selects the next
                                    // GLSL component number
    Id component;  // a dynamic component index, can coexist with a swizzle,
                   // done after the swizzle, NoResult if not present
    Id pre_swizzle_base_type;  // dereferenced type, before swizzle or component
                               // is
    // applied; NoType unless a swizzle or component is
    // present
    bool is_rvalue;  // true if 'base' is an r-value, otherwise, base is an
                     // l-value
  };

  //
  // the SPIR-V builder maintains a single active chain that
  // the following methods operated on
  //

  // for external save and restore
  AccessChain access_chain() { return access_chain_; }
  void set_access_chain(AccessChain new_chain) { access_chain_ = new_chain; }

  void ClearAccessChain();

  // set new base as an l-value base
  void set_access_chain_lvalue(Id lvalue) {
    assert(IsPointer(lvalue));
    access_chain_.base = lvalue;
  }

  // set new base value as an r-value
  void set_access_chain_rvalue(Id rvalue) {
    access_chain_.is_rvalue = true;
    access_chain_.base = rvalue;
  }

  // push offset onto the end of the chain
  void PushAccessChainOffset(Id offset) {
    access_chain_.index_chain.push_back(offset);
  }

  // push new swizzle onto the end of any existing swizzle, merging into a
  // single swizzle
  void PushAccessChainSwizzle(std::vector<uint32_t> swizzle,
                              Id pre_swizzle_base_type);

  // push a variable component selection onto the access chain; supporting only
  // one, so unsided
  void PushAccessChainComponent(Id component, Id pre_swizzle_base_type) {
    access_chain_.component = component;
    if (access_chain_.pre_swizzle_base_type == NoType) {
      access_chain_.pre_swizzle_base_type = pre_swizzle_base_type;
    }
  }

  // use accessChain and swizzle to store value
  void CreateAccessChainStore(Id rvalue);

  // use accessChain and swizzle to load an r-value
  Id CreateAccessChainLoad(Id result_type_id);

  // get the direct pointer for an l-value
  Id CreateAccessChainLValue();

  void Serialize(std::vector<uint32_t>& out) const;

 private:
  // Maximum dimension for column/row in a matrix.
  static const int kMaxMatrixSize = 4;

  // Allocates a new <id>.
  Id AllocateUniqueId() { return ++unique_id_; }

  // Allocates a contiguous sequence of <id>s.
  Id AllocateUniqueIds(int count) {
    Id id = unique_id_ + 1;
    unique_id_ += count;
    return id;
  }

  Id MakeIntegerConstant(Id type_id, uint32_t value, bool is_spec_constant);
  Id FindScalarConstant(Op type_class, Op opcode, Id type_id,
                        uint32_t value) const;
  Id FindScalarConstant(Op type_class, Op opcode, Id type_id, uint32_t v1,
                        uint32_t v2) const;
  Id FindCompositeConstant(Op type_class,
                           std::initializer_list<Id> components) const;

  Id CollapseAccessChain();
  void SimplifyAccessChainSwizzle();
  void TransferAccessChainSwizzle(bool dynamic);

  void SerializeInstructions(
      std::vector<uint32_t>& out,
      const std::vector<Instruction*>& instructions) const;

  void CreateAndSetNoPredecessorBlock(const char* name);
  void CreateBranch(Block* block);
  void CreateSelectionMerge(Block* merge_block,
                            spv::SelectionControlMask control);
  void CreateLoopMerge(Block* merge_block, Block* continueBlock,
                       spv::LoopControlMask control);
  void CreateConditionalBranch(Id condition, Block* then_block,
                               Block* else_block);

  struct Loop;  // Defined below.
  void CreateBranchToLoopHeaderFromInside(const Loop& loop);

  // Asserts on unimplemented functionality.
  void CheckNotImplemented(const char* message);

  spv::SourceLanguage source_language_ = spv::SourceLanguage::Unknown;
  int source_version_ = 0;
  std::vector<const char*> source_extensions_;
  spv::AddressingModel addressing_model_ = spv::AddressingModel::Logical;
  spv::MemoryModel memory_model_ = spv::MemoryModel::GLSL450;
  std::vector<spv::Capability> capabilities_;
  int builder_number_ = 0;
  Module module_;
  Block* build_point_ = nullptr;
  Id unique_id_ = 0;
  Function* main_function_ = nullptr;
  AccessChain access_chain_;
  Id glsl_std_450_instruction_set_ = 0;

  // special blocks of instructions for output
  std::vector<Instruction*> imports_;
  std::vector<Instruction*> entry_points_;
  std::vector<Instruction*> execution_modes_;
  std::vector<Instruction*> names_;
  std::vector<Instruction*> lines_;
  std::vector<Instruction*> decorations_;
  std::vector<Instruction*> constants_types_globals_;
  std::vector<Instruction*> externals_;

  // not output, internally used for quick & dirty canonical (unique) creation
  // All types appear before OpConstant.
  std::vector<Instruction*>
      grouped_constants_[static_cast<int>(spv::Op::OpConstant)];
  std::vector<Instruction*>
      grouped_types_[static_cast<int>(spv::Op::OpConstant)];

  // Stack of switches.
  std::stack<Block*> switch_merges_;

  // Data that needs to be kept in order to properly handle loops.
  struct Loop {
    // Constructs a default Loop structure containing new header, merge, and
    // body blocks for the current function.
    // The test_first argument indicates whether the loop test executes at
    // the top of the loop rather than at the bottom.  In the latter case,
    // also create a phi instruction whose value indicates whether we're on
    // the first iteration of the loop.  The phi instruction is initialized
    // with no values or predecessor operands.
    Loop(SpirvEmitter& emitter, bool test_first);

    // The function containing the loop.
    Function* const function;
    // The header is the first block generated for the loop.
    // It dominates all the blocks in the loop, i.e. it is always
    // executed before any others.
    // If the loop test is executed before the body (as in "while" and
    // "for" loops), then the header begins with the test code.
    // Otherwise, the loop is a "do-while" loop and the header contains the
    // start of the body of the loop (if the body exists).
    Block* const header;
    // The merge block marks the end of the loop.  Control is transferred
    // to the merge block when either the loop test fails, or when a
    // nested "break" is encountered.
    Block* const merge;
    // The body block is the first basic block in the body of the loop, i.e.
    // the code that is to be repeatedly executed, aside from loop control.
    // This member is null until we generate code that references the loop
    // body block.
    Block* const body;
    // True when the loop test executes before the body.
    const bool test_first;
    // When the test executes after the body, this is defined as the phi
    // instruction that tells us whether we are on the first iteration of
    // the loop.  Otherwise this is null. This is non-const because
    // it has to be initialized outside of the initializer-list.
    Instruction* is_first_iteration;
  };

  // Our loop stack.
  std::stack<Loop> loops_;
};

}  // namespace spirv
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_SPIRV_SPIRV_EMITTER_H_
