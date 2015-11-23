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

#ifndef XENIA_GPU_SPIRV_SPV_EMITTER_H_
#define XENIA_GPU_SPIRV_SPV_EMITTER_H_

#include <algorithm>
#include <map>
#include <stack>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/gpu/spirv/spirv_util.h"
#include "xenia/gpu/spirv/spv_ir.h"

namespace xe {
namespace gpu {
namespace spirv {

class SpvEmitter {
 public:
  SpvEmitter();
  ~SpvEmitter();

  void setSource(spv::SourceLanguage language, int version) {
    source_language_ = language;
    source_version_ = version;
  }

  void addSourceExtension(const char* ext) { extensions_.push_back(ext); }

  Id import(const char* name);

  void setMemoryModel(spv::AddressingModel addressing_model,
                      spv::MemoryModel memory_model) {
    addressing_model_ = addressing_model;
    memory_model_ = memory_model;
  }

  void addCapability(spv::Capability cap) { capabilities_.push_back(cap); }

  // To get a new <id> for anything needing a new one.
  Id getUniqueId() { return ++unique_id_; }

  // To get a set of new <id>s, e.g., for a set of function parameters
  Id getUniqueIds(int numIds) {
    Id id = unique_id_ + 1;
    unique_id_ += numIds;
    return id;
  }

  // For creating new types (will return old type if the requested one was
  // already made).
  Id makeVoidType();
  Id makeBoolType();
  Id makePointer(spv::StorageClass, Id type);
  Id makeIntegerType(int width, bool hasSign);  // generic
  Id makeIntType(int width) { return makeIntegerType(width, true); }
  Id makeUintType(int width) { return makeIntegerType(width, false); }
  Id makeFloatType(int width);
  Id makeStructType(std::vector<Id>& members, const char*);
  Id makeStructResultType(Id type0, Id type1);
  Id makeVectorType(Id component, int size);
  Id makeMatrixType(Id component, int cols, int rows);
  Id makeArrayType(Id element, unsigned size);
  Id makeRuntimeArray(Id element);
  Id makeFunctionType(Id return_type, std::vector<Id>& param_types);
  Id makeImageType(Id sampledType, spv::Dim, bool depth, bool arrayed, bool ms,
                   unsigned sampled, spv::ImageFormat format);
  Id makeSamplerType();
  Id makeSampledImageType(Id imageType);

  // For querying about types.
  Id getTypeId(Id result_id) const { return module_.getTypeId(result_id); }
  Id getDerefTypeId(Id result_id) const;
  Op getOpCode(Id id) const { return module_.getInstruction(id)->opcode(); }
  Op getTypeClass(Id type_id) const { return getOpCode(type_id); }
  Op getMostBasicTypeClass(Id type_id) const;
  int getNumComponents(Id result_id) const {
    return getNumTypeComponents(getTypeId(result_id));
  }
  int getNumTypeComponents(Id type_id) const;
  Id getScalarTypeId(Id type_id) const;
  Id getContainedTypeId(Id type_id) const;
  Id getContainedTypeId(Id type_id, int) const;
  spv::StorageClass getTypeStorageClass(Id type_id) const {
    return module_.getStorageClass(type_id);
  }

  bool isPointer(Id result_id) const {
    return isPointerType(getTypeId(result_id));
  }
  bool isScalar(Id result_id) const {
    return isScalarType(getTypeId(result_id));
  }
  bool isVector(Id result_id) const {
    return isVectorType(getTypeId(result_id));
  }
  bool isMatrix(Id result_id) const {
    return isMatrixType(getTypeId(result_id));
  }
  bool isAggregate(Id result_id) const {
    return isAggregateType(getTypeId(result_id));
  }
  bool isBoolType(Id type_id) const {
    return grouped_types_[static_cast<int>(spv::Op::OpTypeBool)].size() > 0 &&
           type_id ==
               grouped_types_[static_cast<int>(spv::Op::OpTypeBool)]
                   .back()
                   ->result_id();
  }

  bool isPointerType(Id type_id) const {
    return getTypeClass(type_id) == spv::Op::OpTypePointer;
  }
  bool isScalarType(Id type_id) const {
    return getTypeClass(type_id) == spv::Op::OpTypeFloat ||
           getTypeClass(type_id) == spv::Op::OpTypeInt ||
           getTypeClass(type_id) == spv::Op::OpTypeBool;
  }
  bool isVectorType(Id type_id) const {
    return getTypeClass(type_id) == spv::Op::OpTypeVector;
  }
  bool isMatrixType(Id type_id) const {
    return getTypeClass(type_id) == spv::Op::OpTypeMatrix;
  }
  bool isStructType(Id type_id) const {
    return getTypeClass(type_id) == spv::Op::OpTypeStruct;
  }
  bool isArrayType(Id type_id) const {
    return getTypeClass(type_id) == spv::Op::OpTypeArray;
  }
  bool isAggregateType(Id type_id) const {
    return isArrayType(type_id) || isStructType(type_id);
  }
  bool isImageType(Id type_id) const {
    return getTypeClass(type_id) == spv::Op::OpTypeImage;
  }
  bool isSamplerType(Id type_id) const {
    return getTypeClass(type_id) == spv::Op::OpTypeSampler;
  }
  bool isSampledImageType(Id type_id) const {
    return getTypeClass(type_id) == spv::Op::OpTypeSampledImage;
  }

  bool isConstantOpCode(Op opcode) const;
  bool isConstant(Id result_id) const {
    return isConstantOpCode(getOpCode(result_id));
  }
  bool isConstantScalar(Id result_id) const {
    return getOpCode(result_id) == spv::Op::OpConstant;
  }
  unsigned int getConstantScalar(Id result_id) const {
    return module_.getInstruction(result_id)->immediate_operand(0);
  }
  spv::StorageClass getStorageClass(Id result_id) const {
    return getTypeStorageClass(getTypeId(result_id));
  }

  int getTypeNumColumns(Id type_id) const {
    assert(isMatrixType(type_id));
    return getNumTypeComponents(type_id);
  }
  int getNumColumns(Id result_id) const {
    return getTypeNumColumns(getTypeId(result_id));
  }
  int getTypeNumRows(Id type_id) const {
    assert(isMatrixType(type_id));
    return getNumTypeComponents(getContainedTypeId(type_id));
  }
  int getNumRows(Id result_id) const {
    return getTypeNumRows(getTypeId(result_id));
  }

  spv::Dim getTypeDimensionality(Id type_id) const {
    assert(isImageType(type_id));
    return static_cast<spv::Dim>(
        module_.getInstruction(type_id)->immediate_operand(1));
  }
  Id getImageType(Id result_id) const {
    Id type_id = getTypeId(result_id);
    assert(isImageType(type_id) || isSampledImageType(type_id));
    return isSampledImageType(type_id)
               ? module_.getInstruction(type_id)->id_operand(0)
               : type_id;
  }
  bool isArrayedImageType(Id type_id) const {
    assert(isImageType(type_id));
    return module_.getInstruction(type_id)->immediate_operand(3) != 0;
  }

  // For making new constants (will return old constant if the requested one was
  // already made).
  Id makeBoolConstant(bool b, bool is_spec_constant = false);
  Id makeIntConstant(int i, bool is_spec_constant = false) {
    return makeIntConstant(makeIntType(32), (unsigned)i, is_spec_constant);
  }
  Id makeUintConstant(uint32_t u, bool is_spec_constant = false) {
    return makeIntConstant(makeUintType(32), u, is_spec_constant);
  }
  template <typename T>
  Id makeUintConstant(T u, bool is_spec_constant = false) {
    static_assert(sizeof(T) == sizeof(uint32_t), "Invalid type");
    return makeIntConstant(makeUintType(32), static_cast<uint32_t>(u),
                           is_spec_constant);
  }
  Id makeFloatConstant(float f, bool is_spec_constant = false);
  Id makeDoubleConstant(double d, bool is_spec_constant = false);

  // Turn the array of constants into a proper spv constant of the requested
  // type.
  Id makeCompositeConstant(Id type, std::vector<Id>& comps);

  // Methods for adding information outside the CFG.
  Instruction* addEntryPoint(spv::ExecutionModel, Function*, const char* name);
  void addExecutionMode(Function*, spv::ExecutionMode mode, int value1 = -1,
                        int value2 = -1, int value3 = -1);
  void addName(Id, const char* name);
  void addMemberName(Id, int member, const char* name);
  void addLine(Id target, Id file_name, int line, int column);
  void addDecoration(Id, spv::Decoration, int num = -1);
  void addMemberDecoration(Id, unsigned int member, spv::Decoration,
                           int num = -1);

  // At the end of what block do the next create*() instructions go?
  Block* build_point() const { return build_point_; }
  void set_build_point(Block* build_point) { build_point_ = build_point; }

  // Make the main function.
  Function* makeMain();

  // Make a shader-style function, and create its entry block if entry is
  // non-zero.
  // Return the function, pass back the entry.
  Function* makeFunctionEntry(Id return_type, const char* name,
                              std::vector<Id>& param_types, Block** entry = 0);

  // Create a return. An 'implicit' return is one not appearing in the source
  // code.  In the case of an implicit return, no post-return block is inserted.
  void makeReturn(bool implicit, Id retVal = 0);

  // Generate all the code needed to finish up a function.
  void leaveFunction();

  // Create a discard.
  void makeDiscard();

  // Create a global or function local or IO variable.
  Id createVariable(spv::StorageClass storage_class, Id type,
                    const char* name = 0);

  // Create an imtermediate with an undefined value.
  Id createUndefined(Id type);

  // Store into an Id and return the l-value
  void createStore(Id rvalue, Id lvalue);

  // Load from an Id and return it
  Id createLoad(Id lvalue);

  // Create an OpAccessChain instruction
  Id createAccessChain(spv::StorageClass storage_class, Id base,
                       std::vector<Id>& offsets);

  // Create an OpArrayLength instruction
  Id createArrayLength(Id base, unsigned int member);

  // Create an OpCompositeExtract instruction
  Id createCompositeExtract(Id composite, Id type_id, unsigned index);
  Id createCompositeExtract(Id composite, Id type_id,
                            std::vector<unsigned>& indexes);
  Id createCompositeInsert(Id object, Id composite, Id type_id, unsigned index);
  Id createCompositeInsert(Id object, Id composite, Id type_id,
                           std::vector<unsigned>& indexes);

  Id createVectorExtractDynamic(Id vector, Id type_id, Id component_index);
  Id createVectorInsertDynamic(Id vector, Id type_id, Id component,
                               Id component_index);

  void createNoResultOp(Op);
  void createNoResultOp(Op, Id operand);
  void createNoResultOp(Op, const std::vector<Id>& operands);
  void createControlBarrier(spv::Scope execution, spv::Scope memory,
                            spv::MemorySemanticsMask);
  void createMemoryBarrier(unsigned execution_scope, unsigned memory_semantics);
  Id createUnaryOp(Op, Id type_id, Id operand);
  Id createBinOp(Op, Id type_id, Id operand1, Id operand2);
  Id createTriOp(Op, Id type_id, Id operand1, Id operand2, Id operand3);
  Id createOp(Op, Id type_id, const std::vector<Id>& operands);
  Id createFunctionCall(Function*, std::vector<spv::Id>&);

  // Take an rvalue (source) and a set of channels to extract from it to
  // make a new rvalue, which is returned.
  Id createRvalueSwizzle(Id type_id, Id source,
                         std::vector<unsigned>& channels);

  // Take a copy of an lvalue (target) and a source of components, and set the
  // source components into the lvalue where the 'channels' say to put them.
  // An updated version of the target is returned.
  // (No true lvalue or stores are used.)
  Id createLvalueSwizzle(Id type_id, Id target, Id source,
                         std::vector<unsigned>& channels);

  // If the value passed in is an instruction and the precision is not EMpNone,
  // it gets tagged with the requested precision.
  void setPrecision(Id value, spv::Decoration precision) {
    CheckNotImplemented("setPrecision");
  }

  // Can smear a scalar to a vector for the following forms:
  //   - promoteScalar(scalar, vector)  // smear scalar to width of vector
  //   - promoteScalar(vector, scalar)  // smear scalar to width of vector
  //   - promoteScalar(pointer, scalar) // smear scalar to width of what pointer
  //   points to
  //   - promoteScalar(scalar, scalar)  // do nothing
  // Other forms are not allowed.
  //
  // Note: One of the arguments will change, with the result coming back that
  // way rather than
  // through the return value.
  void promoteScalar(spv::Decoration precision, Id& left, Id& right);

  // make a value by smearing the scalar to fill the type
  Id smearScalar(spv::Decoration precision, Id scalarVal, Id);

  // Create a call to a built-in function.
  Id createBuiltinCall(spv::Decoration precision, Id result_type, Id builtins,
                       int entry_point, std::initializer_list<Id> args);

  // List of parameters used to create a texture operation
  struct TextureParameters {
    Id sampler;
    Id coords;
    Id bias;
    Id lod;
    Id Dref;
    Id offset;
    Id offsets;
    Id gradX;
    Id gradY;
    Id sample;
    Id comp;
  };

  // Select the correct texture operation based on all inputs, and emit the
  // correct instruction
  Id createTextureCall(spv::Decoration precision, Id result_type, bool fetch,
                       bool proj, bool gather, const TextureParameters&);

  // Emit the OpTextureQuery* instruction that was passed in.
  // Figure out the right return value and type, and return it.
  Id createTextureQueryCall(Op, const TextureParameters&);

  Id createSamplePositionCall(spv::Decoration precision, Id, Id);

  Id createBitFieldExtractCall(spv::Decoration precision, Id, Id, Id,
                               bool isSigned);
  Id createBitFieldInsertCall(spv::Decoration precision, Id, Id, Id, Id);

  // Reduction comparision for composites:  For equal and not-equal resulting in
  // a scalar.
  Id createCompare(spv::Decoration precision, Id, Id,
                   bool /* true if for equal, fales if for not-equal */);

  // OpCompositeConstruct
  Id createCompositeConstruct(Id type_id, std::vector<Id>& constituents);

  // vector or scalar constructor
  Id createConstructor(spv::Decoration precision,
                       const std::vector<Id>& sources, Id result_type_id);

  // matrix constructor
  Id createMatrixConstructor(spv::Decoration precision,
                             const std::vector<Id>& sources, Id constructee);

  // Helper to use for building nested control flow with if-then-else.
  class If {
   public:
    If(SpvEmitter& emitter, Id condition);
    ~If() = default;

    void makeBeginElse();
    void makeEndIf();

   private:
    If(const If&) = delete;
    If& operator=(If&) = delete;

    SpvEmitter& emitter_;
    Id condition_;
    Function* function_ = nullptr;
    Block* header_block_ = nullptr;
    Block* then_block_ = nullptr;
    Block* else_block_ = nullptr;
    Block* merge_block_ = nullptr;
  };

  // Make a switch statement.
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
  void makeSwitch(Id condition, int numSegments, std::vector<int>& caseValues,
                  std::vector<int>& valueToSegment, int defaultSegment,
                  std::vector<Block*>& segmentBB);  // return argument

  // Add a branch to the innermost switch's merge block.
  void addSwitchBreak();

  // Move to the next code segment, passing in the return argument in
  // makeSwitch()
  void nextSwitchSegment(std::vector<Block*>& segmentBB, int segment);

  // Finish off the innermost switch.
  void endSwitch(std::vector<Block*>& segmentBB);

  // Start the beginning of a new loop, and prepare the builder to
  // generate code for the loop test.
  // The loopTestFirst parameter is true when the loop test executes before
  // the body.  (It is false for do-while loops.)
  void makeNewLoop(bool loopTestFirst);

  // Add the branch for the loop test, based on the given condition.
  // The true branch goes to the first block in the loop body, and
  // the false branch goes to the loop's merge block.  The builder insertion
  // point will be placed at the start of the body.
  void createLoopTestBranch(Id condition);

  // Generate an unconditional branch to the loop body.  The builder insertion
  // point will be placed at the start of the body.  Use this when there is
  // no loop test.
  void createBranchToBody();

  // Add a branch to the test of the current (innermost) loop.
  // The way we generate code, that's also the loop header.
  void createLoopContinue();

  // Add an exit (e.g. "break") for the innermost loop that you're in
  void createLoopExit();

  // Close the innermost loop that you're in
  void closeLoop();

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
    std::vector<unsigned> swizzle;  // each std::vector element selects the next
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

  // clear accessChain
  void clearAccessChain();

  // set new base as an l-value base
  void setAccessChainLValue(Id lvalue) {
    assert(isPointer(lvalue));
    access_chain_.base = lvalue;
  }

  // set new base value as an r-value
  void setAccessChainRValue(Id rvalue) {
    access_chain_.is_rvalue = true;
    access_chain_.base = rvalue;
  }

  // push offset onto the end of the chain
  void accessChainPush(Id offset) {
    access_chain_.index_chain.push_back(offset);
  }

  // push new swizzle onto the end of any existing swizzle, merging into a
  // single swizzle
  void accessChainPushSwizzle(std::vector<unsigned>& swizzle,
                              Id pre_swizzle_base_type);

  // push a variable component selection onto the access chain; supporting only
  // one, so unsided
  void accessChainPushComponent(Id component, Id pre_swizzle_base_type) {
    access_chain_.component = component;
    if (access_chain_.pre_swizzle_base_type == NoType)
      access_chain_.pre_swizzle_base_type = pre_swizzle_base_type;
  }

  // use accessChain and swizzle to store value
  void accessChainStore(Id rvalue);

  // use accessChain and swizzle to load an r-value
  Id accessChainLoad(Id result_type);

  // get the direct pointer for an l-value
  Id accessChainGetLValue();

  void dump(std::vector<unsigned int>&) const;

 private:
  // Maximum dimension for column/row in a matrix.
  static const int kMaxMatrixSize = 4;

  // Asserts on unimplemnted functionality.
  void CheckNotImplemented(const char* message);

  Id makeIntConstant(Id type_id, unsigned value, bool is_spec_constant);
  Id findScalarConstant(Op type_class, Op opcode, Id type_id,
                        unsigned value) const;
  Id findScalarConstant(Op type_class, Op opcode, Id type_id, unsigned v1,
                        unsigned v2) const;
  Id findCompositeConstant(Op type_class, std::vector<Id>& comps) const;
  Id collapseAccessChain();
  void transferAccessChainSwizzle(bool dynamic);
  void simplifyAccessChainSwizzle();
  void createAndSetNoPredecessorBlock(const char*);
  void createBranch(Block* block);
  void createSelectionMerge(Block* merge_block,
                            spv::SelectionControlMask control);
  void createLoopMerge(Block* merge_block, Block* continueBlock,
                       spv::LoopControlMask control);
  void createConditionalBranch(Id condition, Block* then_block,
                               Block* else_block);
  void dumpInstructions(std::vector<unsigned int>&,
                        const std::vector<Instruction*>&) const;

  struct Loop;  // Defined below.
  void createBranchToLoopHeaderFromInside(const Loop& loop);

  spv::SourceLanguage source_language_ = spv::SourceLanguage::Unknown;
  int source_version_ = 0;
  std::vector<const char*> extensions_;
  spv::AddressingModel addressing_model_ = spv::AddressingModel::Logical;
  spv::MemoryModel memory_model_ = spv::MemoryModel::GLSL450;
  std::vector<spv::Capability> capabilities_;
  int builder_number_ = 0;
  Module module_;
  Block* build_point_ = nullptr;
  Id unique_id_ = 0;
  Function* main_function_ = nullptr;
  AccessChain access_chain_;

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
  std::vector<Instruction*> grouped_constants_[static_cast<int>(
      spv::Op::OpConstant)];  // all types appear before OpConstant
  std::vector<Instruction*>
      grouped_types_[static_cast<int>(spv::Op::OpConstant)];

  // stack of switches
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
    Loop(SpvEmitter& emitter, bool test_first);

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
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_SPIRV_SPV_EMITTER_H_
