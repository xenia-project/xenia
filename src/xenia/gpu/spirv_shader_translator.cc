/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2017 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/spirv_shader_translator.h"

#include <gflags/gflags.h>

#include <algorithm>
#include <cfloat>
#include <cstddef>
#include <cstring>
#include <vector>

#include "xenia/base/logging.h"
#include "xenia/base/math.h"

DEFINE_bool(spv_validate, false, "Validate SPIR-V shaders after generation");
DEFINE_bool(spv_disasm, false, "Disassemble SPIR-V shaders after generation");

namespace xe {
namespace gpu {
using namespace ucode;

constexpr uint32_t kMaxInterpolators = 16;
constexpr uint32_t kMaxTemporaryRegisters = 64;

using spv::GLSLstd450;
using spv::Id;
using spv::Op;

SpirvShaderTranslator::SpirvShaderTranslator() {}
SpirvShaderTranslator::~SpirvShaderTranslator() = default;

void SpirvShaderTranslator::StartTranslation() {
  // Create a new builder.
  builder_ = std::make_unique<spv::Builder>(0x10000, 0xFFFFFFFF, nullptr);
  auto& b = *builder_;

  // Import required modules.
  glsl_std_450_instruction_set_ = b.import("GLSL.std.450");

  // Configure environment.
  b.setSource(spv::SourceLanguage::SourceLanguageUnknown, 0);
  b.setMemoryModel(spv::AddressingModel::AddressingModelLogical,
                   spv::MemoryModel::MemoryModelGLSL450);
  b.addCapability(spv::Capability::CapabilityShader);
  b.addCapability(spv::Capability::CapabilityImageQuery);

  if (is_vertex_shader()) {
    b.addCapability(spv::Capability::CapabilityClipDistance);
    b.addCapability(spv::Capability::CapabilityCullDistance);
  }
  if (is_pixel_shader()) {
    b.addCapability(spv::Capability::CapabilityDerivativeControl);
  }

  bool_type_ = b.makeBoolType();
  float_type_ = b.makeFloatType(32);
  int_type_ = b.makeIntType(32);
  uint_type_ = b.makeUintType(32);
  vec2_int_type_ = b.makeVectorType(int_type_, 2);
  vec2_uint_type_ = b.makeVectorType(uint_type_, 2);
  vec2_float_type_ = b.makeVectorType(float_type_, 2);
  vec3_int_type_ = b.makeVectorType(int_type_, 3);
  vec3_float_type_ = b.makeVectorType(float_type_, 3);
  vec4_float_type_ = b.makeVectorType(float_type_, 4);
  vec4_int_type_ = b.makeVectorType(int_type_, 4);
  vec4_uint_type_ = b.makeVectorType(uint_type_, 4);
  vec2_bool_type_ = b.makeVectorType(bool_type_, 2);
  vec3_bool_type_ = b.makeVectorType(bool_type_, 3);
  vec4_bool_type_ = b.makeVectorType(bool_type_, 4);

  vec4_float_one_ = b.makeCompositeConstant(
      vec4_float_type_,
      std::vector<Id>({b.makeFloatConstant(1.f), b.makeFloatConstant(1.f),
                       b.makeFloatConstant(1.f), b.makeFloatConstant(1.f)}));
  vec4_float_zero_ = b.makeCompositeConstant(
      vec4_float_type_,
      std::vector<Id>({b.makeFloatConstant(0.f), b.makeFloatConstant(0.f),
                       b.makeFloatConstant(0.f), b.makeFloatConstant(0.f)}));

  cube_function_ = CreateCubeFunction();

  spv::Block* function_block = nullptr;
  translated_main_ =
      b.makeFunctionEntry(spv::NoPrecision, b.makeVoidType(), "translated_main",
                          {}, {}, &function_block);

  registers_type_ = b.makeArrayType(vec4_float_type_,
                                    b.makeUintConstant(register_count()), 0);
  registers_ptr_ = b.createVariable(spv::StorageClass::StorageClassFunction,
                                    registers_type_, "r");

  aL_ = b.createVariable(spv::StorageClass::StorageClassFunction,
                         vec4_uint_type_, "aL");

  loop_count_ = b.createVariable(spv::StorageClass::StorageClassFunction,
                                 vec4_uint_type_, "loop_count");
  p0_ = b.createVariable(spv::StorageClass::StorageClassFunction, bool_type_,
                         "p0");
  ps_ = b.createVariable(spv::StorageClass::StorageClassFunction, float_type_,
                         "ps");
  pv_ = b.createVariable(spv::StorageClass::StorageClassFunction,
                         vec4_float_type_, "pv");
  pc_ = b.createVariable(spv::StorageClass::StorageClassFunction, int_type_,
                         "pc");
  a0_ = b.createVariable(spv::StorageClass::StorageClassFunction, int_type_,
                         "a0");
  lod_ = b.createVariable(spv::StorageClass::StorageClassFunction, float_type_,
                          "lod");

  // Uniform constants.
  Id float_consts_type =
      b.makeArrayType(vec4_float_type_, b.makeUintConstant(512), 1);
  Id loop_consts_type =
      b.makeArrayType(vec4_uint_type_, b.makeUintConstant(8), 1);
  Id bool_consts_type =
      b.makeArrayType(vec4_uint_type_, b.makeUintConstant(2), 1);

  // Strides
  b.addDecoration(float_consts_type, spv::Decoration::DecorationArrayStride,
                  4 * sizeof(float));
  b.addDecoration(loop_consts_type, spv::Decoration::DecorationArrayStride,
                  4 * sizeof(uint32_t));
  b.addDecoration(bool_consts_type, spv::Decoration::DecorationArrayStride,
                  4 * sizeof(uint32_t));

  Id consts_struct_type = b.makeStructType(
      {float_consts_type, loop_consts_type, bool_consts_type}, "consts_type");
  b.addDecoration(consts_struct_type, spv::Decoration::DecorationBlock);

  // Constants member decorations.
  b.addMemberDecoration(consts_struct_type, 0,
                        spv::Decoration::DecorationOffset, 0);
  b.addMemberName(consts_struct_type, 0, "float_consts");

  b.addMemberDecoration(consts_struct_type, 1,
                        spv::Decoration::DecorationOffset,
                        512 * 4 * sizeof(float));
  b.addMemberName(consts_struct_type, 1, "loop_consts");

  b.addMemberDecoration(consts_struct_type, 2,
                        spv::Decoration::DecorationOffset,
                        512 * 4 * sizeof(float) + 32 * sizeof(uint32_t));
  b.addMemberName(consts_struct_type, 2, "bool_consts");

  consts_ = b.createVariable(spv::StorageClass::StorageClassUniform,
                             consts_struct_type, "consts");

  b.addDecoration(consts_, spv::Decoration::DecorationDescriptorSet, 0);
  if (is_vertex_shader()) {
    b.addDecoration(consts_, spv::Decoration::DecorationBinding, 0);
  } else if (is_pixel_shader()) {
    b.addDecoration(consts_, spv::Decoration::DecorationBinding, 1);
  }

  // Push constants, represented by SpirvPushConstants.
  Id push_constants_type =
      b.makeStructType({vec4_float_type_, vec4_float_type_, vec4_float_type_,
                        vec4_float_type_, vec4_float_type_, uint_type_},
                       "push_consts_type");
  b.addDecoration(push_constants_type, spv::Decoration::DecorationBlock);

  // float4 window_scale;
  b.addMemberDecoration(
      push_constants_type, 0, spv::Decoration::DecorationOffset,
      static_cast<int>(offsetof(SpirvPushConstants, window_scale)));
  b.addMemberName(push_constants_type, 0, "window_scale");
  // float4 vtx_fmt;
  b.addMemberDecoration(
      push_constants_type, 1, spv::Decoration::DecorationOffset,
      static_cast<int>(offsetof(SpirvPushConstants, vtx_fmt)));
  b.addMemberName(push_constants_type, 1, "vtx_fmt");
  // float4 vtx_fmt;
  b.addMemberDecoration(
      push_constants_type, 2, spv::Decoration::DecorationOffset,
      static_cast<int>(offsetof(SpirvPushConstants, point_size)));
  b.addMemberName(push_constants_type, 2, "point_size");
  // float4 alpha_test;
  b.addMemberDecoration(
      push_constants_type, 3, spv::Decoration::DecorationOffset,
      static_cast<int>(offsetof(SpirvPushConstants, alpha_test)));
  b.addMemberName(push_constants_type, 3, "alpha_test");
  // float4 color_exp_bias;
  b.addMemberDecoration(
      push_constants_type, 4, spv::Decoration::DecorationOffset,
      static_cast<int>(offsetof(SpirvPushConstants, color_exp_bias)));
  b.addMemberName(push_constants_type, 4, "color_exp_bias");
  // uint ps_param_gen;
  b.addMemberDecoration(
      push_constants_type, 5, spv::Decoration::DecorationOffset,
      static_cast<int>(offsetof(SpirvPushConstants, ps_param_gen)));
  b.addMemberName(push_constants_type, 5, "ps_param_gen");
  push_consts_ = b.createVariable(spv::StorageClass::StorageClassPushConstant,
                                  push_constants_type, "push_consts");

  if (!texture_bindings().empty()) {
    image_2d_type_ =
        b.makeImageType(float_type_, spv::Dim::Dim2D, false, false, false, 1,
                        spv::ImageFormat::ImageFormatUnknown);
    image_3d_type_ =
        b.makeImageType(float_type_, spv::Dim::Dim3D, false, false, false, 1,
                        spv::ImageFormat::ImageFormatUnknown);
    image_cube_type_ =
        b.makeImageType(float_type_, spv::Dim::DimCube, false, false, false, 1,
                        spv::ImageFormat::ImageFormatUnknown);

    // Texture bindings
    Id tex_t[] = {b.makeSampledImageType(image_2d_type_),
                  b.makeSampledImageType(image_3d_type_),
                  b.makeSampledImageType(image_cube_type_)};

    uint32_t num_tex_bindings = 0;
    for (const auto& binding : texture_bindings()) {
      // Calculate the highest binding index.
      num_tex_bindings =
          std::max(num_tex_bindings, uint32_t(binding.binding_index + 1));
    }

    Id tex_a_t[] = {
        b.makeArrayType(tex_t[0], b.makeUintConstant(num_tex_bindings), 0),
        b.makeArrayType(tex_t[1], b.makeUintConstant(num_tex_bindings), 0),
        b.makeArrayType(tex_t[2], b.makeUintConstant(num_tex_bindings), 0)};

    // Create 3 texture types, all aliased on the same binding
    for (int i = 0; i < 3; i++) {
      tex_[i] = b.createVariable(
          spv::StorageClass::StorageClassUniformConstant, tex_a_t[i],
          xe::format_string("textures%dD", i + 2).c_str());
      b.addDecoration(tex_[i], spv::Decoration::DecorationDescriptorSet, 1);
      b.addDecoration(tex_[i], spv::Decoration::DecorationBinding, 0);
    }

    // Set up the map from binding -> ssbo index
    for (const auto& binding : texture_bindings()) {
      tex_binding_map_[binding.fetch_constant] =
          uint32_t(binding.binding_index);
    }
  }

  // Interpolators.
  Id interpolators_type = b.makeArrayType(
      vec4_float_type_, b.makeUintConstant(kMaxInterpolators), 0);
  if (is_vertex_shader()) {
    // Vertex inputs/outputs
    // Inputs: 32 SSBOs on DS 2 binding 0

    if (!vertex_bindings().empty()) {
      // Runtime array for vertex data
      Id vtx_t = b.makeRuntimeArray(uint_type_);
      b.addDecoration(vtx_t, spv::Decoration::DecorationArrayStride,
                      sizeof(uint32_t));

      Id vtx_s = b.makeStructType({vtx_t}, "vertex_type");
      b.addDecoration(vtx_s, spv::Decoration::DecorationBufferBlock);

      // Describe the actual data
      b.addMemberName(vtx_s, 0, "data");
      b.addMemberDecoration(vtx_s, 0, spv::Decoration::DecorationOffset, 0);

      // Create the vertex bindings variable.
      Id vtx_a_t = b.makeArrayType(
          vtx_s, b.makeUintConstant(uint32_t(vertex_bindings().size())), 0);
      vtx_ = b.createVariable(spv::StorageClass::StorageClassUniform, vtx_a_t,
                              "vertex_bindings");

      // DS 2 binding 0
      b.addDecoration(vtx_, spv::Decoration::DecorationDescriptorSet, 2);
      b.addDecoration(vtx_, spv::Decoration::DecorationBinding, 0);
      b.addDecoration(vtx_, spv::Decoration::DecorationNonWritable);

      // Set up the map from binding -> ssbo index
      for (const auto& binding : vertex_bindings()) {
        vtx_binding_map_[binding.fetch_constant] = binding.binding_index;
      }
    }

    // Outputs
    interpolators_ = b.createVariable(spv::StorageClass::StorageClassOutput,
                                      interpolators_type, "interpolators");
    b.addDecoration(interpolators_, spv::Decoration::DecorationLocation, 0);
    for (uint32_t i = 0; i < std::min(register_count(), kMaxInterpolators);
         i++) {
      // Zero interpolators.
      auto ptr = b.createAccessChain(spv::StorageClass::StorageClassOutput,
                                     interpolators_,
                                     std::vector<Id>({b.makeUintConstant(i)}));
      b.createStore(vec4_float_zero_, ptr);
    }

    point_size_ = b.createVariable(spv::StorageClass::StorageClassOutput,
                                   float_type_, "point_size");
    b.addDecoration(point_size_, spv::Decoration::DecorationLocation, 17);
    // Set default point-size value (-1.0f, indicating to the geometry shader
    // that the register value should be used instead of the per-vertex value)
    b.createStore(b.makeFloatConstant(-1.0f), point_size_);

    point_coord_ = b.createVariable(spv::StorageClass::StorageClassOutput,
                                    vec2_float_type_, "point_coord");
    b.addDecoration(point_coord_, spv::Decoration::DecorationLocation, 16);
    // point_coord is only ever populated in a geometry shader. Just write
    // zero to it in the vertex shader.
    b.createStore(
        b.makeCompositeConstant(vec2_float_type_,
                                std::vector<Id>({b.makeFloatConstant(0.0f),
                                                 b.makeFloatConstant(0.0f)})),
        point_coord_);

    pos_ = b.createVariable(spv::StorageClass::StorageClassOutput,
                            vec4_float_type_, "gl_Position");
    b.addDecoration(pos_, spv::Decoration::DecorationBuiltIn,
                    spv::BuiltIn::BuiltInPosition);

    vertex_idx_ = b.createVariable(spv::StorageClass::StorageClassInput,
                                   int_type_, "gl_VertexIndex");
    b.addDecoration(vertex_idx_, spv::Decoration::DecorationBuiltIn,
                    spv::BuiltIn::BuiltInVertexIndex);

    interface_ids_.push_back(interpolators_);
    interface_ids_.push_back(point_coord_);
    interface_ids_.push_back(point_size_);
    interface_ids_.push_back(pos_);
    interface_ids_.push_back(vertex_idx_);

    auto vertex_idx = b.createLoad(vertex_idx_);
    vertex_idx =
        b.createUnaryOp(spv::Op::OpConvertSToF, float_type_, vertex_idx);
    auto r0_ptr = b.createAccessChain(spv::StorageClass::StorageClassFunction,
                                      registers_ptr_,
                                      std::vector<Id>({b.makeUintConstant(0)}));
    auto r0 = b.createLoad(r0_ptr);
    r0 = b.createCompositeInsert(vertex_idx, r0, vec4_float_type_, 0);
    b.createStore(r0, r0_ptr);
  } else {
    // Pixel inputs from vertex shader.
    interpolators_ = b.createVariable(spv::StorageClass::StorageClassInput,
                                      interpolators_type, "interpolators");
    b.addDecoration(interpolators_, spv::Decoration::DecorationLocation, 0);

    point_coord_ = b.createVariable(spv::StorageClass::StorageClassInput,
                                    vec2_float_type_, "point_coord");
    b.addDecoration(point_coord_, spv::Decoration::DecorationLocation, 16);

    // Pixel fragment outputs (one per render target).
    Id frag_outputs_type =
        b.makeArrayType(vec4_float_type_, b.makeUintConstant(4), 0);
    frag_outputs_ = b.createVariable(spv::StorageClass::StorageClassOutput,
                                     frag_outputs_type, "oC");
    b.addDecoration(frag_outputs_, spv::Decoration::DecorationLocation, 0);

    frag_depth_ = b.createVariable(spv::StorageClass::StorageClassOutput,
                                   float_type_, "gl_FragDepth");
    b.addDecoration(frag_depth_, spv::Decoration::DecorationBuiltIn,
                    spv::BuiltIn::BuiltInFragDepth);

    interface_ids_.push_back(interpolators_);
    interface_ids_.push_back(point_coord_);
    interface_ids_.push_back(frag_outputs_);
    interface_ids_.push_back(frag_depth_);
    // TODO(benvanik): frag depth, etc.

    // TODO(DrChat): Verify this naive, stupid approach to uninitialized values.
    for (uint32_t i = 0; i < 4; i++) {
      auto idx = b.makeUintConstant(i);
      auto oC = b.createAccessChain(spv::StorageClass::StorageClassOutput,
                                    frag_outputs_, std::vector<Id>({idx}));
      b.createStore(vec4_float_zero_, oC);
    }

    // Copy interpolators to r[0..16].
    // TODO: Need physical addressing in order to do this.
    // b.createNoResultOp(spv::Op::OpCopyMemorySized,
    //                   {registers_ptr_, interpolators_,
    //                    b.makeUintConstant(16 * 4 * sizeof(float))});
    for (uint32_t i = 0; i < std::min(register_count(), kMaxInterpolators);
         i++) {
      // For now, copy interpolators register-by-register :/
      auto idx = b.makeUintConstant(i);
      auto i_a = b.createAccessChain(spv::StorageClass::StorageClassInput,
                                     interpolators_, std::vector<Id>({idx}));
      auto r_a = b.createAccessChain(spv::StorageClass::StorageClassFunction,
                                     registers_ptr_, std::vector<Id>({idx}));
      b.createNoResultOp(spv::Op::OpCopyMemory, std::vector<Id>({r_a, i_a}));
    }

    // Setup ps_param_gen
    auto ps_param_gen_idx_ptr = b.createAccessChain(
        spv::StorageClass::StorageClassPushConstant, push_consts_,
        std::vector<Id>({b.makeUintConstant(5)}));
    auto ps_param_gen_idx = b.createLoad(ps_param_gen_idx_ptr);

    auto frag_coord = b.createVariable(spv::StorageClass::StorageClassInput,
                                       vec4_float_type_, "gl_FragCoord");
    b.addDecoration(frag_coord, spv::Decoration::DecorationBuiltIn,
                    spv::BuiltIn::BuiltInFragCoord);

    interface_ids_.push_back(frag_coord);

    auto param = b.createOp(
        spv::Op::OpVectorShuffle, vec4_float_type_,
        {b.createLoad(frag_coord), b.createLoad(point_coord_), 0, 1, 4, 5});
    /*
    // TODO: gl_FrontFacing
    auto param_x = b.createCompositeExtract(param, float_type_, 0);
    auto param_x_inv = b.createBinOp(spv::Op::OpFMul, float_type_, param_x,
                                     b.makeFloatConstant(-1.f));
    param_x = b.createCompositeInsert(param_x_inv, param, vec4_float_type_, 0);
    */

    auto cond = b.createBinOp(spv::Op::OpINotEqual, bool_type_,
                              ps_param_gen_idx, b.makeUintConstant(-1));
    spv::Builder::If ifb(cond, 0, b);

    // FYI: We do this instead of r[ps_param_gen_idx] because that causes
    // nvidia to move all registers into local memory (slow!)
    for (uint32_t i = 0; i < std::min(register_count(), kMaxInterpolators);
         i++) {
      auto reg_ptr = b.createAccessChain(
          spv::StorageClass::StorageClassFunction, registers_ptr_,
          std::vector<Id>({b.makeUintConstant(i)}));

      auto cond = b.createBinOp(spv::Op::OpIEqual, bool_type_, ps_param_gen_idx,
                                b.makeUintConstant(i));
      cond = b.smearScalar(spv::NoPrecision, cond, vec4_bool_type_);
      auto reg = b.createTriOp(spv::Op::OpSelect, vec4_float_type_, cond, param,
                               b.createLoad(reg_ptr));
      b.createStore(reg, reg_ptr);
    }

    ifb.makeEndIf();
  }

  b.createStore(b.makeIntConstant(0x0), pc_);

  loop_head_block_ = &b.makeNewBlock();
  auto block = &b.makeNewBlock();
  loop_body_block_ = &b.makeNewBlock();
  loop_cont_block_ = &b.makeNewBlock();
  loop_exit_block_ = &b.makeNewBlock();
  b.createBranch(loop_head_block_);

  // Setup continue block
  b.setBuildPoint(loop_cont_block_);
  b.createBranch(loop_head_block_);

  // While loop header block
  b.setBuildPoint(loop_head_block_);
  b.createLoopMerge(loop_exit_block_, loop_cont_block_,
                    spv::LoopControlMask::LoopControlDontUnrollMask, 0);
  b.createBranch(block);

  // Condition block
  b.setBuildPoint(block);

  // while (pc != 0xFFFF)
  auto c = b.createBinOp(spv::Op::OpINotEqual, bool_type_, b.createLoad(pc_),
                         b.makeIntConstant(0xFFFF));
  b.createConditionalBranch(c, loop_body_block_, loop_exit_block_);
  b.setBuildPoint(loop_body_block_);
}

std::vector<uint8_t> SpirvShaderTranslator::CompleteTranslation() {
  auto& b = *builder_;

  assert_false(open_predicated_block_);
  b.setBuildPoint(loop_exit_block_);
  b.makeReturn(false);
  exec_cond_ = false;
  exec_skip_block_ = nullptr;

  // main() entry point.
  spv::Block* entry_block;
  auto mainFn = b.makeFunctionEntry(spv::NoPrecision, b.makeVoidType(), "main",
                                    {}, {}, &entry_block);
  if (is_vertex_shader()) {
    auto entry = b.addEntryPoint(spv::ExecutionModel::ExecutionModelVertex,
                                 mainFn, "main");

    for (auto id : interface_ids_) {
      entry->addIdOperand(id);
    }
  } else {
    auto entry = b.addEntryPoint(spv::ExecutionModel::ExecutionModelFragment,
                                 mainFn, "main");
    b.addExecutionMode(mainFn, spv::ExecutionModeOriginUpperLeft);

    // If we write a new depth value, we must declare this mode!
    if (writes_depth()) {
      b.addExecutionMode(mainFn, spv::ExecutionModeDepthReplacing);
    }

    for (auto id : interface_ids_) {
      entry->addIdOperand(id);
    }
  }

  // TODO(benvanik): transform feedback.
  if (false) {
    b.addCapability(spv::Capability::CapabilityTransformFeedback);
    b.addExecutionMode(mainFn, spv::ExecutionMode::ExecutionModeXfb);
  }

  b.createFunctionCall(translated_main_, std::vector<Id>({}));
  if (is_vertex_shader()) {
    // gl_Position transform
    auto vtx_fmt_ptr = b.createAccessChain(
        spv::StorageClass::StorageClassPushConstant, push_consts_,
        std::vector<Id>({b.makeUintConstant(1)}));
    auto window_scale_ptr = b.createAccessChain(
        spv::StorageClass::StorageClassPushConstant, push_consts_,
        std::vector<Id>({b.makeUintConstant(0)}));
    auto vtx_fmt = b.createLoad(vtx_fmt_ptr);
    auto window_scale = b.createLoad(window_scale_ptr);

    auto p = b.createLoad(pos_);
    auto c = b.createBinOp(spv::Op::OpFOrdNotEqual, vec4_bool_type_, vtx_fmt,
                           vec4_float_zero_);

    // pos.w = vtx_fmt.w == 0.0 ? 1.0 / pos.w : pos.w
    auto c_w = b.createCompositeExtract(c, bool_type_, 3);
    auto p_w = b.createCompositeExtract(p, float_type_, 3);
    auto p_w_inv = b.createBinOp(spv::Op::OpFDiv, float_type_,
                                 b.makeFloatConstant(1.f), p_w);
    p_w = b.createTriOp(spv::Op::OpSelect, float_type_, c_w, p_w, p_w_inv);

    // pos.xyz = vtx_fmt.xyz != 0.0 ? pos.xyz / pos.w : pos.xyz
    auto p_all_w = b.smearScalar(spv::NoPrecision, p_w, vec4_float_type_);
    auto p_inv = b.createBinOp(spv::Op::OpFDiv, vec4_float_type_, p, p_all_w);
    p = b.createTriOp(spv::Op::OpSelect, vec4_float_type_, c, p_inv, p);

    // Reinsert w
    p = b.createCompositeInsert(p_w, p, vec4_float_type_, 3);

    // Apply window offset
    // pos.xy += window_scale.zw
    auto window_offset = b.createOp(spv::Op::OpVectorShuffle, vec4_float_type_,
                                    {window_scale, window_scale, 2, 3, 0, 1});
    auto p_offset =
        b.createBinOp(spv::Op::OpFAdd, vec4_float_type_, p, window_offset);

    // Apply window scaling
    // pos.xy *= window_scale.xy
    auto p_scaled = b.createBinOp(spv::Op::OpFMul, vec4_float_type_, p_offset,
                                  window_scale);

    p = b.createOp(spv::Op::OpVectorShuffle, vec4_float_type_,
                   {p, p_scaled, 4, 5, 2, 3});

    b.createStore(p, pos_);
  } else {
    // Color exponent bias
    {
      auto bias_ptr = b.createAccessChain(
          spv::StorageClass::StorageClassPushConstant, push_consts_,
          std::vector<Id>({b.makeUintConstant(4)}));
      auto bias = b.createLoad(bias_ptr);
      for (uint32_t i = 0; i < 4; i++) {
        auto bias_value = b.createOp(spv::Op::OpVectorShuffle, vec4_float_type_,
                                     {bias, bias, i, i, i, i});
        auto oC_ptr = b.createAccessChain(
            spv::StorageClass::StorageClassOutput, frag_outputs_,
            std::vector<Id>({b.makeUintConstant(i)}));
        auto oC_biased = b.createBinOp(spv::Op::OpFMul, vec4_float_type_,
                                       b.createLoad(oC_ptr), bias_value);
        b.createStore(oC_biased, oC_ptr);
      }
    }

    // Alpha test
    {
      auto alpha_test_ptr = b.createAccessChain(
          spv::StorageClass::StorageClassPushConstant, push_consts_,
          std::vector<Id>({b.makeUintConstant(3)}));
      auto alpha_test = b.createLoad(alpha_test_ptr);

      auto alpha_test_enabled =
          b.createCompositeExtract(alpha_test, float_type_, 0);
      auto alpha_test_func =
          b.createCompositeExtract(alpha_test, float_type_, 1);
      auto alpha_test_ref =
          b.createCompositeExtract(alpha_test, float_type_, 2);

      alpha_test_func =
          b.createUnaryOp(spv::Op::OpConvertFToU, uint_type_, alpha_test_func);

      auto oC0_ptr = b.createAccessChain(
          spv::StorageClass::StorageClassOutput, frag_outputs_,
          std::vector<Id>({b.makeUintConstant(0)}));
      auto oC0_alpha =
          b.createCompositeExtract(b.createLoad(oC0_ptr), float_type_, 3);

      auto cond = b.createBinOp(spv::Op::OpFOrdEqual, bool_type_,
                                alpha_test_enabled, b.makeFloatConstant(1.f));
      spv::Builder::If alpha_if(cond, 0, b);

      std::vector<spv::Block*> switch_segments;
      b.makeSwitch(
          alpha_test_func, 0, 8, std::vector<int>({0, 1, 2, 3, 4, 5, 6, 7}),
          std::vector<int>({0, 1, 2, 3, 4, 5, 6, 7}), 7, switch_segments);

      const static spv::Op alpha_op_map[] = {
          spv::Op::OpNop,
          spv::Op::OpFOrdGreaterThanEqual,
          spv::Op::OpFOrdNotEqual,
          spv::Op::OpFOrdGreaterThan,
          spv::Op::OpFOrdLessThanEqual,
          spv::Op::OpFOrdEqual,
          spv::Op::OpFOrdLessThan,
          spv::Op::OpNop,
      };

      // if (alpha_func == 0) passes = false;
      b.nextSwitchSegment(switch_segments, 0);
      b.makeDiscard();
      b.addSwitchBreak();

      for (int i = 1; i < 7; i++) {
        b.nextSwitchSegment(switch_segments, i);
        auto cond = b.createBinOp(alpha_op_map[i], bool_type_, oC0_alpha,
                                  alpha_test_ref);
        spv::Builder::If discard_if(cond, 0, b);
        b.makeDiscard();
        discard_if.makeEndIf();
        b.addSwitchBreak();
      }

      // if (alpha_func == 7) passes = true;
      b.nextSwitchSegment(switch_segments, 7);
      b.endSwitch(switch_segments);

      alpha_if.makeEndIf();
    }
  }

  b.makeReturn(false);

  // Compile the spv IR
  // compiler_.Compile(b.getModule());

  std::vector<uint32_t> spirv_words;
  b.dump(spirv_words);

  // Cleanup builder.
  cf_blocks_.clear();
  loop_head_block_ = nullptr;
  loop_body_block_ = nullptr;
  loop_cont_block_ = nullptr;
  loop_exit_block_ = nullptr;
  builder_.reset();

  interface_ids_.clear();

  // Copy bytes out.
  // TODO(benvanik): avoid copy?
  std::vector<uint8_t> spirv_bytes;
  spirv_bytes.resize(spirv_words.size() * 4);
  std::memcpy(spirv_bytes.data(), spirv_words.data(), spirv_bytes.size());
  return spirv_bytes;
}

void SpirvShaderTranslator::PostTranslation(Shader* shader) {
  // Validation.
  if (FLAGS_spv_validate) {
    auto validation = validator_.Validate(
        reinterpret_cast<const uint32_t*>(shader->translated_binary().data()),
        shader->translated_binary().size() / sizeof(uint32_t));
    if (validation->has_error()) {
      XELOGE("SPIR-V Shader Validation failed! Error: %s",
             validation->error_string());
    }
  }

  if (FLAGS_spv_disasm) {
    // TODO(benvanik): only if needed? could be slowish.
    auto disasm = disassembler_.Disassemble(
        reinterpret_cast<const uint32_t*>(shader->translated_binary().data()),
        shader->translated_binary().size() / 4);
    if (disasm->has_error()) {
      XELOGE("Failed to disassemble SPIRV - invalid?");
    } else {
      set_host_disassembly(shader, disasm->to_string());
    }
  }
}

void SpirvShaderTranslator::PreProcessControlFlowInstructions(
    std::vector<ucode::ControlFlowInstruction> instrs) {
  auto& b = *builder_;

  auto default_block = &b.makeNewBlock();
  switch_break_block_ = &b.makeNewBlock();

  b.setBuildPoint(default_block);
  b.createStore(b.makeIntConstant(0xFFFF), pc_);
  b.createBranch(switch_break_block_);

  b.setBuildPoint(switch_break_block_);
  b.createBranch(loop_cont_block_);

  // Now setup the switch.
  default_block->addPredecessor(loop_body_block_);
  b.setBuildPoint(loop_body_block_);

  cf_blocks_.resize(instrs.size());
  for (size_t i = 0; i < cf_blocks_.size(); i++) {
    cf_blocks_[i].block = &b.makeNewBlock();
    cf_blocks_[i].labelled = false;
  }

  std::vector<uint32_t> operands;
  operands.push_back(b.createLoad(pc_));       // Selector
  operands.push_back(default_block->getId());  // Default

  // Always have a case for block 0.
  operands.push_back(0);
  operands.push_back(cf_blocks_[0].block->getId());
  cf_blocks_[0].block->addPredecessor(loop_body_block_);
  cf_blocks_[0].labelled = true;

  for (size_t i = 0; i < instrs.size(); i++) {
    auto& instr = instrs[i];
    if (instr.opcode() == ucode::ControlFlowOpcode::kCondJmp) {
      uint32_t address = instr.cond_jmp.address();

      if (!cf_blocks_[address].labelled) {
        cf_blocks_[address].labelled = true;
        operands.push_back(address);
        operands.push_back(cf_blocks_[address].block->getId());
        cf_blocks_[address].block->addPredecessor(loop_body_block_);
      }

      if (!cf_blocks_[i + 1].labelled) {
        cf_blocks_[i + 1].labelled = true;
        operands.push_back(uint32_t(i + 1));
        operands.push_back(cf_blocks_[i + 1].block->getId());
        cf_blocks_[i + 1].block->addPredecessor(loop_body_block_);
      }
    } else if (instr.opcode() == ucode::ControlFlowOpcode::kLoopStart) {
      uint32_t address = instr.loop_start.address();

      // Label the body
      if (!cf_blocks_[i + 1].labelled) {
        cf_blocks_[i + 1].labelled = true;
        operands.push_back(uint32_t(i + 1));
        operands.push_back(cf_blocks_[i + 1].block->getId());
        cf_blocks_[i + 1].block->addPredecessor(loop_body_block_);
      }

      // Label the loop skip address.
      if (!cf_blocks_[address].labelled) {
        cf_blocks_[address].labelled = true;
        operands.push_back(address);
        operands.push_back(cf_blocks_[address].block->getId());
        cf_blocks_[address].block->addPredecessor(loop_body_block_);
      }
    } else if (instr.opcode() == ucode::ControlFlowOpcode::kLoopEnd) {
      uint32_t address = instr.loop_end.address();

      if (!cf_blocks_[address].labelled) {
        cf_blocks_[address].labelled = true;
        operands.push_back(address);
        operands.push_back(cf_blocks_[address].block->getId());
        cf_blocks_[address].block->addPredecessor(loop_body_block_);
      }
    }
  }

  b.createSelectionMerge(switch_break_block_, 0);
  b.createNoResultOp(spv::Op::OpSwitch, operands);
}

void SpirvShaderTranslator::ProcessLabel(uint32_t cf_index) {
  auto& b = *builder_;
}

void SpirvShaderTranslator::ProcessControlFlowInstructionBegin(
    uint32_t cf_index) {
  auto& b = *builder_;
}

void SpirvShaderTranslator::ProcessControlFlowInstructionEnd(
    uint32_t cf_index) {
  auto& b = *builder_;
}

void SpirvShaderTranslator::ProcessControlFlowNopInstruction(
    uint32_t cf_index) {
  auto& b = *builder_;

  auto head = cf_blocks_[cf_index].block;
  b.setBuildPoint(head);
  b.createNoResultOp(spv::Op::OpNop);
  if (cf_blocks_.size() > cf_index + 1) {
    b.createBranch(cf_blocks_[cf_index + 1].block);
  } else {
    b.makeReturn(false);
  }
}

void SpirvShaderTranslator::ProcessExecInstructionBegin(
    const ParsedExecInstruction& instr) {
  auto& b = *builder_;

  assert_false(open_predicated_block_);
  open_predicated_block_ = false;
  predicated_block_cond_ = false;
  predicated_block_end_ = nullptr;

  // Head has the logic to check if the body should execute.
  auto head = cf_blocks_[instr.dword_index].block;
  b.setBuildPoint(head);
  auto body = head;
  switch (instr.type) {
    case ParsedExecInstruction::Type::kUnconditional: {
      // No need to do anything.
      exec_cond_ = false;
    } break;
    case ParsedExecInstruction::Type::kConditional: {
      // Based off of bool_consts
      std::vector<Id> offsets;
      offsets.push_back(b.makeUintConstant(2));  // bool_consts
      uint32_t bitfield_index = instr.bool_constant_index / 32;
      offsets.push_back(b.makeUintConstant(bitfield_index / 4));
      auto v = b.createAccessChain(spv::StorageClass::StorageClassUniform,
                                   consts_, offsets);
      v = b.createLoad(v);
      v = b.createCompositeExtract(v, uint_type_, bitfield_index % 4);

      // Bitfield extract the bool constant.
      // FIXME: NVidia's compiler seems to be broken on this instruction?
      /*
      v = b.createTriOp(spv::Op::OpBitFieldUExtract, uint_type_, v,
                        b.makeUintConstant(instr.bool_constant_index % 32),
                        b.makeUintConstant(1));

      // Conditional branch
      auto cond = b.createBinOp(spv::Op::OpIEqual, bool_type_, v,
                                b.makeUintConstant(instr.condition ? 1 : 0));
      */
      v = b.createBinOp(
          spv::Op::OpBitwiseAnd, uint_type_, v,
          b.makeUintConstant(1 << (instr.bool_constant_index % 32)));
      auto cond = b.createBinOp(
          instr.condition ? spv::Op::OpINotEqual : spv::Op::OpIEqual,
          bool_type_, v, b.makeUintConstant(0));

      // Conditional branch
      body = &b.makeNewBlock();
      exec_cond_ = true;
      exec_skip_block_ = &b.makeNewBlock();

      b.createSelectionMerge(
          exec_skip_block_,
          spv::SelectionControlMask::SelectionControlMaskNone);
      b.createConditionalBranch(cond, body, exec_skip_block_);

      b.setBuildPoint(exec_skip_block_);
      if (!instr.is_end || cf_blocks_.size() > instr.dword_index + 1) {
        assert_true(cf_blocks_.size() > instr.dword_index + 1);
        b.createBranch(cf_blocks_[instr.dword_index + 1].block);
      } else {
        b.makeReturn(false);
      }
    } break;
    case ParsedExecInstruction::Type::kPredicated: {
      // Branch based on p0.
      body = &b.makeNewBlock();
      exec_cond_ = true;
      exec_skip_block_ = &b.makeNewBlock();

      auto cond =
          b.createBinOp(spv::Op::OpLogicalEqual, bool_type_, b.createLoad(p0_),
                        b.makeBoolConstant(instr.condition));
      b.createSelectionMerge(
          exec_skip_block_,
          spv::SelectionControlMask::SelectionControlMaskNone);
      b.createConditionalBranch(cond, body, exec_skip_block_);

      b.setBuildPoint(exec_skip_block_);
      if (!instr.is_end || cf_blocks_.size() > instr.dword_index + 1) {
        assert_true(cf_blocks_.size() > instr.dword_index + 1);
        b.createBranch(cf_blocks_[instr.dword_index + 1].block);
      } else {
        b.makeReturn(false);
      }
    } break;
  }
  b.setBuildPoint(body);
}

void SpirvShaderTranslator::ProcessExecInstructionEnd(
    const ParsedExecInstruction& instr) {
  auto& b = *builder_;

  if (open_predicated_block_) {
    b.createBranch(predicated_block_end_);
    b.setBuildPoint(predicated_block_end_);
    open_predicated_block_ = false;
    predicated_block_cond_ = false;
    predicated_block_end_ = nullptr;
  }

  if (instr.is_end) {
    b.makeReturn(false);
  } else if (exec_cond_) {
    b.createBranch(exec_skip_block_);
  } else {
    assert_true(cf_blocks_.size() > instr.dword_index + 1);
    b.createBranch(cf_blocks_[instr.dword_index + 1].block);
  }
}

void SpirvShaderTranslator::ProcessLoopStartInstruction(
    const ParsedLoopStartInstruction& instr) {
  auto& b = *builder_;

  auto head = cf_blocks_[instr.dword_index].block;
  b.setBuildPoint(head);

  // loop il<idx>, L<idx> - loop with loop data il<idx>, end @ L<idx>

  std::vector<Id> offsets;
  offsets.push_back(b.makeUintConstant(1));  // loop_consts
  offsets.push_back(b.makeUintConstant(instr.loop_constant_index / 4));
  auto loop_const = b.createAccessChain(spv::StorageClass::StorageClassUniform,
                                        consts_, offsets);
  loop_const = b.createLoad(loop_const);
  loop_const = b.createCompositeExtract(loop_const, uint_type_,
                                        instr.loop_constant_index % 4);

  // uint loop_count_value = loop_const & 0xFF;
  auto loop_count_value = b.createBinOp(spv::Op::OpBitwiseAnd, uint_type_,
                                        loop_const, b.makeUintConstant(0xFF));

  // uint loop_aL_value = (loop_const >> 8) & 0xFF;
  auto loop_aL_value = b.createBinOp(spv::Op::OpShiftRightLogical, uint_type_,
                                     loop_const, b.makeUintConstant(8));
  loop_aL_value = b.createBinOp(spv::Op::OpBitwiseAnd, uint_type_,
                                loop_aL_value, b.makeUintConstant(0xFF));

  // loop_count_ = uvec4(loop_count_value, loop_count_.xyz);
  auto loop_count = b.createLoad(loop_count_);
  loop_count =
      b.createRvalueSwizzle(spv::NoPrecision, vec4_uint_type_, loop_count,
                            std::vector<uint32_t>({0, 0, 1, 2}));
  loop_count =
      b.createCompositeInsert(loop_count_value, loop_count, vec4_uint_type_, 0);
  b.createStore(loop_count, loop_count_);

  // aL = aL.xxyz;
  auto aL = b.createLoad(aL_);
  aL = b.createRvalueSwizzle(spv::NoPrecision, vec4_uint_type_, aL,
                             std::vector<uint32_t>({0, 0, 1, 2}));
  if (!instr.is_repeat) {
    // aL.x = loop_aL_value;
    aL = b.createCompositeInsert(loop_aL_value, aL, vec4_uint_type_, 0);
  }
  b.createStore(aL, aL_);

  // Short-circuit if loop counter is 0
  auto cond = b.createBinOp(spv::Op::OpIEqual, bool_type_, loop_count_value,
                            b.makeUintConstant(0));
  auto next_pc = b.createTriOp(spv::Op::OpSelect, int_type_, cond,
                               b.makeIntConstant(instr.loop_skip_address),
                               b.makeIntConstant(instr.dword_index + 1));
  b.createStore(next_pc, pc_);
  b.createBranch(switch_break_block_);
}

void SpirvShaderTranslator::ProcessLoopEndInstruction(
    const ParsedLoopEndInstruction& instr) {
  auto& b = *builder_;

  auto head = cf_blocks_[instr.dword_index].block;
  b.setBuildPoint(head);

  // endloop il<idx>, L<idx> - end loop w/ data il<idx>, head @ L<idx>
  auto loop_count = b.createLoad(loop_count_);
  auto count = b.createCompositeExtract(loop_count, uint_type_, 0);
  count =
      b.createBinOp(spv::Op::OpISub, uint_type_, count, b.makeUintConstant(1));
  loop_count = b.createCompositeInsert(count, loop_count, vec4_uint_type_, 0);
  b.createStore(loop_count, loop_count_);

  // if (--loop_count_.x == 0 || [!]p0)
  auto c1 = b.createBinOp(spv::Op::OpIEqual, bool_type_, count,
                          b.makeUintConstant(0));
  auto c2 =
      b.createBinOp(spv::Op::OpLogicalEqual, bool_type_, b.createLoad(p0_),
                    b.makeBoolConstant(instr.predicate_condition));
  auto cond = b.createBinOp(spv::Op::OpLogicalOr, bool_type_, c1, c2);

  auto loop = &b.makeNewBlock();
  auto end = &b.makeNewBlock();
  auto tail = &b.makeNewBlock();
  b.createSelectionMerge(tail, spv::SelectionControlMaskNone);
  b.createConditionalBranch(cond, end, loop);

  // ================================================
  // Loop completed - pop the current loop off the stack and exit
  b.setBuildPoint(end);
  loop_count = b.createLoad(loop_count_);
  auto aL = b.createLoad(aL_);

  // loop_count = loop_count.yzw0
  loop_count =
      b.createRvalueSwizzle(spv::NoPrecision, vec4_uint_type_, loop_count,
                            std::vector<uint32_t>({1, 2, 3, 3}));
  loop_count = b.createCompositeInsert(b.makeUintConstant(0), loop_count,
                                       vec4_uint_type_, 3);
  b.createStore(loop_count, loop_count_);

  // aL = aL.yzw0
  aL = b.createRvalueSwizzle(spv::NoPrecision, vec4_uint_type_, aL,
                             std::vector<uint32_t>({1, 2, 3, 3}));
  aL = b.createCompositeInsert(b.makeUintConstant(0), aL, vec4_uint_type_, 3);
  b.createStore(aL, aL_);

  // Update pc with the next block
  // pc_ = instr.dword_index + 1
  b.createStore(b.makeIntConstant(instr.dword_index + 1), pc_);
  b.createBranch(tail);

  // ================================================
  // Still looping - increment aL and loop
  b.setBuildPoint(loop);
  aL = b.createLoad(aL_);
  auto aL_x = b.createCompositeExtract(aL, uint_type_, 0);

  std::vector<Id> offsets;
  offsets.push_back(b.makeUintConstant(1));  // loop_consts
  offsets.push_back(b.makeUintConstant(instr.loop_constant_index / 4));
  auto loop_const = b.createAccessChain(spv::StorageClass::StorageClassUniform,
                                        consts_, offsets);
  loop_const = b.createLoad(loop_const);
  loop_const = b.createCompositeExtract(loop_const, uint_type_,
                                        instr.loop_constant_index % 4);

  // uint loop_aL_value = (loop_const >> 16) & 0xFF;
  auto loop_aL_value = b.createBinOp(spv::Op::OpShiftRightLogical, uint_type_,
                                     loop_const, b.makeUintConstant(16));
  loop_aL_value = b.createBinOp(spv::Op::OpBitwiseAnd, uint_type_,
                                loop_aL_value, b.makeUintConstant(0xFF));

  aL_x = b.createBinOp(spv::Op::OpIAdd, uint_type_, aL_x, loop_aL_value);
  aL = b.createCompositeInsert(aL_x, aL, vec4_uint_type_, 0);
  b.createStore(aL, aL_);

  // pc_ = instr.loop_body_address;
  b.createStore(b.makeIntConstant(instr.loop_body_address), pc_);
  b.createBranch(tail);

  // ================================================
  b.setBuildPoint(tail);
  b.createBranch(switch_break_block_);
}

void SpirvShaderTranslator::ProcessCallInstruction(
    const ParsedCallInstruction& instr) {
  auto& b = *builder_;

  auto head = cf_blocks_[instr.dword_index].block;
  b.setBuildPoint(head);

  // Unused instruction(?)
  assert_always();
  EmitUnimplementedTranslationError();

  assert_true(cf_blocks_.size() > instr.dword_index + 1);
  b.createBranch(cf_blocks_[instr.dword_index + 1].block);
}

void SpirvShaderTranslator::ProcessReturnInstruction(
    const ParsedReturnInstruction& instr) {
  auto& b = *builder_;

  auto head = cf_blocks_[instr.dword_index].block;
  b.setBuildPoint(head);

  // Unused instruction(?)
  assert_always();
  EmitUnimplementedTranslationError();

  assert_true(cf_blocks_.size() > instr.dword_index + 1);
  b.createBranch(cf_blocks_[instr.dword_index + 1].block);
}

// CF jump
void SpirvShaderTranslator::ProcessJumpInstruction(
    const ParsedJumpInstruction& instr) {
  auto& b = *builder_;

  auto head = cf_blocks_[instr.dword_index].block;
  b.setBuildPoint(head);
  switch (instr.type) {
    case ParsedJumpInstruction::Type::kUnconditional: {
      b.createStore(b.makeIntConstant(instr.target_address), pc_);
      b.createBranch(switch_break_block_);
    } break;
    case ParsedJumpInstruction::Type::kConditional: {
      assert_true(cf_blocks_.size() > instr.dword_index + 1);

      // Based off of bool_consts
      std::vector<Id> offsets;
      offsets.push_back(b.makeUintConstant(2));  // bool_consts
      uint32_t bitfield_index = instr.bool_constant_index / 32;
      offsets.push_back(b.makeUintConstant(bitfield_index / 4));
      auto v = b.createAccessChain(spv::StorageClass::StorageClassUniform,
                                   consts_, offsets);
      v = b.createLoad(v);
      v = b.createCompositeExtract(v, uint_type_, bitfield_index % 4);

      // Bitfield extract the bool constant.
      // FIXME: NVidia's compiler seems to be broken on this instruction?
      /*
      v = b.createTriOp(spv::Op::OpBitFieldUExtract, uint_type_, v,
                        b.makeUintConstant(instr.bool_constant_index % 32),
                        b.makeUintConstant(1));

      // Conditional branch
      auto cond = b.createBinOp(spv::Op::OpIEqual, bool_type_, v,
                                b.makeUintConstant(instr.condition ? 1 : 0));
      */
      v = b.createBinOp(
          spv::Op::OpBitwiseAnd, uint_type_, v,
          b.makeUintConstant(1 << (instr.bool_constant_index % 32)));
      auto cond = b.createBinOp(
          instr.condition ? spv::Op::OpINotEqual : spv::Op::OpIEqual,
          bool_type_, v, b.makeUintConstant(0));

      auto next_pc = b.createTriOp(spv::Op::OpSelect, int_type_, cond,
                                   b.makeIntConstant(instr.target_address),
                                   b.makeIntConstant(instr.dword_index + 1));
      b.createStore(next_pc, pc_);
      b.createBranch(switch_break_block_);
    } break;
    case ParsedJumpInstruction::Type::kPredicated: {
      assert_true(cf_blocks_.size() > instr.dword_index + 1);

      auto cond =
          b.createBinOp(spv::Op::OpLogicalEqual, bool_type_, b.createLoad(p0_),
                        b.makeBoolConstant(instr.condition));

      auto next_pc = b.createTriOp(spv::Op::OpSelect, int_type_, cond,
                                   b.makeIntConstant(instr.target_address),
                                   b.makeIntConstant(instr.dword_index + 1));
      b.createStore(next_pc, pc_);
      b.createBranch(switch_break_block_);
    } break;
  }
}

void SpirvShaderTranslator::ProcessAllocInstruction(
    const ParsedAllocInstruction& instr) {
  auto& b = *builder_;

  auto head = cf_blocks_[instr.dword_index].block;
  b.setBuildPoint(head);

  switch (instr.type) {
    case AllocType::kNone: {
      // ?
    } break;
    case AllocType::kVsPosition: {
      assert_true(is_vertex_shader());
    } break;
    // Also PS Colors
    case AllocType::kVsInterpolators: {
      // Already included, nothing to do here.
    } break;
    case AllocType::kMemory: {
      // Nothing to do for this.
    } break;
    default:
      break;
  }

  assert_true(cf_blocks_.size() > instr.dword_index + 1);
  b.createBranch(cf_blocks_[instr.dword_index + 1].block);
}

spv::Id SpirvShaderTranslator::BitfieldExtract(spv::Id result_type,
                                               spv::Id base, bool is_signed,
                                               uint32_t offset,
                                               uint32_t count) {
  auto& b = *builder_;

  spv::Id base_type = b.getTypeId(base);

  // <-- 32 - (offset + count) ------ [bits] -?-
  if (32 - (offset + count) > 0) {
    base = b.createBinOp(spv::Op::OpShiftLeftLogical, base_type, base,
                         b.makeUintConstant(32 - (offset + count)));
  }
  // [bits] -?-?-?---------------------------
  auto op = is_signed ? spv::Op::OpShiftRightArithmetic
                      : spv::Op::OpShiftRightLogical;
  base = b.createBinOp(op, base_type, base, b.makeUintConstant(32 - count));

  return base;
}

spv::Id SpirvShaderTranslator::ConvertNormVar(spv::Id var, spv::Id result_type,
                                              uint32_t bits, bool is_signed) {
  auto& b = *builder_;
  if (is_signed) {
    auto c = b.createBinOp(spv::Op::OpFOrdEqual, bool_type_, var,
                           b.makeFloatConstant(-float(1 << (bits - 1))));
    auto v = b.createBinOp(spv::Op::OpFDiv, result_type, var,
                           b.makeFloatConstant(float((1 << (bits - 1)) - 1)));
    var = b.createTriOp(spv::Op::OpSelect, result_type, c,
                        b.makeFloatConstant(-1.f), v);
  } else {
    var = b.createBinOp(spv::Op::OpFDiv, result_type, var,
                        b.makeFloatConstant(float((1 << bits) - 1)));
  }

  return var;
}

void SpirvShaderTranslator::ProcessVertexFetchInstruction(
    const ParsedVertexFetchInstruction& instr) {
  auto& b = *builder_;
  assert_true(is_vertex_shader());
  assert_not_zero(vertex_idx_);

  // Close the open predicated block if this instr isn't predicated or the
  // conditions do not match.
  if (open_predicated_block_ &&
      (!instr.is_predicated ||
       instr.predicate_condition != predicated_block_cond_)) {
    b.createBranch(predicated_block_end_);
    b.setBuildPoint(predicated_block_end_);
    open_predicated_block_ = false;
    predicated_block_cond_ = false;
    predicated_block_end_ = nullptr;
  }

  if (!open_predicated_block_ && instr.is_predicated) {
    Id pred_cond =
        b.createBinOp(spv::Op::OpLogicalEqual, bool_type_, b.createLoad(p0_),
                      b.makeBoolConstant(instr.predicate_condition));
    auto block = &b.makeNewBlock();
    open_predicated_block_ = true;
    predicated_block_cond_ = instr.predicate_condition;
    predicated_block_end_ = &b.makeNewBlock();

    b.createSelectionMerge(predicated_block_end_,
                           spv::SelectionControlMaskNone);
    b.createConditionalBranch(pred_cond, block, predicated_block_end_);
    b.setBuildPoint(block);
  }

  // Operand 0 is the index
  // Operand 1 is the binding
  // TODO: Indexed fetch
  auto vertex_idx = LoadFromOperand(instr.operands[0]);
  vertex_idx = b.createUnaryOp(spv::Op::OpConvertFToS, int_type_, vertex_idx);

  // vertex_idx * stride + offset
  vertex_idx = b.createBinOp(spv::Op::OpIMul, int_type_, vertex_idx,
                             b.makeUintConstant(instr.attributes.stride));
  vertex_idx = b.createBinOp(spv::Op::OpIAdd, int_type_, vertex_idx,
                             b.makeUintConstant(instr.attributes.offset));

  auto data_ptr = b.createAccessChain(
      spv::StorageClass::StorageClassUniform, vtx_,
      {b.makeUintConstant(vtx_binding_map_[instr.operands[1].storage_index]),
       b.makeUintConstant(0)});

  spv::Id vertex = 0;
  switch (instr.attributes.data_format) {
    case VertexFormat::k_8_8_8_8: {
      auto vertex_ptr = b.createAccessChain(
          spv::StorageClass::StorageClassUniform, data_ptr, {vertex_idx});
      auto vertex_data = b.createLoad(vertex_ptr);

      if (instr.attributes.is_integer) {
        spv::Id components[4] = {};

        auto op = instr.attributes.is_signed ? spv::Op::OpConvertSToF
                                             : spv::Op::OpConvertUToF;
        auto comp_type = instr.attributes.is_signed ? int_type_ : uint_type_;

        for (int i = 0; i < 4; i++) {
          components[i] = BitfieldExtract(comp_type, vertex_data,
                                          instr.attributes.is_signed, 8 * i, 8);
          components[i] = b.createUnaryOp(op, float_type_, components[i]);
        }

        vertex = b.createCompositeConstruct(
            vec4_float_type_,
            {components[0], components[1], components[2], components[3]});
      } else {
        spv::GLSLstd450 op;
        if (instr.attributes.is_signed) {
          op = spv::GLSLstd450::kUnpackSnorm4x8;
        } else {
          op = spv::GLSLstd450::kUnpackUnorm4x8;
        }
        vertex = CreateGlslStd450InstructionCall(
            spv::NoPrecision, vec4_float_type_, op, {vertex_data});
      }
    } break;

    case VertexFormat::k_16_16: {
      spv::Id components[1] = {};
      for (uint32_t i = 0; i < 1; i++) {
        auto index = b.createBinOp(spv::Op::OpIAdd, int_type_, vertex_idx,
                                   b.makeUintConstant(i));
        auto vertex_ptr = b.createAccessChain(
            spv::StorageClass::StorageClassUniform, data_ptr, {index});
        auto vertex_data = b.createLoad(vertex_ptr);

        if (instr.attributes.is_integer) {
          spv::Id comp[2] = {};

          bool is_signed = instr.attributes.is_signed;
          bool is_integer = instr.attributes.is_integer;
          auto comp_type = is_signed ? int_type_ : uint_type_;

          if (is_signed) {
            vertex_data =
                b.createUnaryOp(spv::Op::OpBitcast, int_type_, vertex_data);
          }

          comp[0] = BitfieldExtract(comp_type, vertex_data, is_signed, 0, 16);
          comp[1] = BitfieldExtract(comp_type, vertex_data, is_signed, 16, 16);

          auto op = is_signed ? spv::Op::OpConvertSToF : spv::Op::OpConvertUToF;
          for (int i = 0; i < xe::countof(comp); i++) {
            comp[i] = b.createUnaryOp(op, float_type_, comp[i]);
          }

          components[i] =
              b.createCompositeConstruct(vec2_float_type_, {comp[0], comp[1]});
        } else {
          spv::GLSLstd450 op;
          if (instr.attributes.is_signed) {
            op = spv::GLSLstd450::kUnpackSnorm2x16;
          } else {
            op = spv::GLSLstd450::kUnpackUnorm2x16;
          }

          components[i] = CreateGlslStd450InstructionCall(
              spv::NoPrecision, vec2_float_type_, op, {vertex_data});
        }
      }

      vertex = components[0];
    } break;

    case VertexFormat::k_16_16_16_16: {
      spv::Id components[2] = {};
      for (uint32_t i = 0; i < 2; i++) {
        auto index = b.createBinOp(spv::Op::OpIAdd, int_type_, vertex_idx,
                                   b.makeUintConstant(i));
        auto vertex_ptr = b.createAccessChain(
            spv::StorageClass::StorageClassUniform, data_ptr, {index});
        auto vertex_data = b.createLoad(vertex_ptr);

        if (instr.attributes.is_integer) {
          spv::Id comp[2] = {};

          bool is_signed = instr.attributes.is_signed;
          bool is_integer = instr.attributes.is_integer;
          auto comp_type = is_signed ? int_type_ : uint_type_;

          if (is_signed) {
            vertex_data =
                b.createUnaryOp(spv::Op::OpBitcast, int_type_, vertex_data);
          }

          comp[0] = BitfieldExtract(comp_type, vertex_data, is_signed, 0, 16);
          comp[1] = BitfieldExtract(comp_type, vertex_data, is_signed, 16, 16);

          auto op = is_signed ? spv::Op::OpConvertSToF : spv::Op::OpConvertUToF;
          for (int i = 0; i < xe::countof(comp); i++) {
            comp[i] = b.createUnaryOp(op, float_type_, comp[i]);
          }

          components[i] =
              b.createCompositeConstruct(vec2_float_type_, {comp[0], comp[1]});
        } else {
          spv::GLSLstd450 op;
          if (instr.attributes.is_signed) {
            op = spv::GLSLstd450::kUnpackSnorm2x16;
          } else {
            op = spv::GLSLstd450::kUnpackUnorm2x16;
          }

          components[i] = CreateGlslStd450InstructionCall(
              spv::NoPrecision, vec2_float_type_, op, {vertex_data});
        }
      }

      vertex = b.createConstructor(
          spv::NoPrecision, {components[0], components[1]}, vec4_float_type_);
    } break;

    case VertexFormat::k_16_16_FLOAT: {
      spv::Id components[1] = {};
      for (uint32_t i = 0; i < 1; i++) {
        auto index = b.createBinOp(spv::Op::OpIAdd, int_type_, vertex_idx,
                                   b.makeUintConstant(i));
        auto vertex_ptr = b.createAccessChain(
            spv::StorageClass::StorageClassUniform, data_ptr, {index});
        auto vertex_data = b.createLoad(vertex_ptr);

        assert_true(instr.attributes.is_integer);
        assert_true(instr.attributes.is_signed);
        components[i] = CreateGlslStd450InstructionCall(
            spv::NoPrecision, vec2_float_type_,
            spv::GLSLstd450::kUnpackHalf2x16, {vertex_data});
      }

      vertex = components[0];
    } break;

    case VertexFormat::k_16_16_16_16_FLOAT: {
      spv::Id components[2] = {};
      for (uint32_t i = 0; i < 2; i++) {
        auto index = b.createBinOp(spv::Op::OpIAdd, int_type_, vertex_idx,
                                   b.makeUintConstant(i));
        auto vertex_ptr = b.createAccessChain(
            spv::StorageClass::StorageClassUniform, data_ptr, {index});
        auto vertex_data = b.createLoad(vertex_ptr);

        assert_true(instr.attributes.is_integer);
        assert_true(instr.attributes.is_signed);
        components[i] = CreateGlslStd450InstructionCall(
            spv::NoPrecision, vec2_float_type_,
            spv::GLSLstd450::kUnpackHalf2x16, {vertex_data});
      }

      vertex = b.createConstructor(
          spv::NoPrecision, {components[0], components[1]}, vec4_float_type_);
    } break;

    case VertexFormat::k_32: {
      spv::Id components[1] = {};
      for (uint32_t i = 0; i < 1; i++) {
        auto index = b.createBinOp(spv::Op::OpIAdd, int_type_, vertex_idx,
                                   b.makeUintConstant(i));
        auto vertex_ptr = b.createAccessChain(
            spv::StorageClass::StorageClassUniform, data_ptr, {index});
        auto vertex_data = b.createLoad(vertex_ptr);

        if (instr.attributes.is_integer) {
          if (instr.attributes.is_signed) {
            components[i] =
                b.createUnaryOp(spv::Op::OpBitcast, int_type_, vertex_data);
            components[i] = b.createUnaryOp(spv::Op::OpConvertSToF, float_type_,
                                            vertex_data);
          } else {
            components[i] = b.createUnaryOp(spv::Op::OpConvertUToF, float_type_,
                                            vertex_data);
          }
        } else {
          if (instr.attributes.is_signed) {
            // TODO(DrChat): This is gonna be harder to convert. There's not
            // enough precision in a float to shove INT_MAX into it.
            assert_always();
            components[i] = b.makeFloatConstant(0.f);
          } else {
            components[i] = ConvertNormVar(vertex_data, uint_type_, 32, false);
          }
        }
      }

      // vertex = b.createCompositeConstruct(float_type_, {components[0]});
      vertex = components[0];
    } break;

    case VertexFormat::k_32_32: {
      spv::Id components[2] = {};
      for (uint32_t i = 0; i < 2; i++) {
        auto index = b.createBinOp(spv::Op::OpIAdd, int_type_, vertex_idx,
                                   b.makeUintConstant(i));
        auto vertex_ptr = b.createAccessChain(
            spv::StorageClass::StorageClassUniform, data_ptr, {index});
        auto vertex_data = b.createLoad(vertex_ptr);

        if (instr.attributes.is_integer) {
          if (instr.attributes.is_signed) {
            components[i] =
                b.createUnaryOp(spv::Op::OpBitcast, int_type_, vertex_data);
            components[i] = b.createUnaryOp(spv::Op::OpConvertSToF, float_type_,
                                            vertex_data);
          } else {
            components[i] = b.createUnaryOp(spv::Op::OpConvertUToF, float_type_,
                                            vertex_data);
          }
        } else {
          if (instr.attributes.is_signed) {
            // TODO(DrChat): This is gonna be harder to convert. There's not
            // enough precision in a float to shove INT_MAX into it.
            assert_always();
            components[i] = b.makeFloatConstant(0.f);
          } else {
            components[i] = ConvertNormVar(vertex_data, uint_type_, 32, false);
          }
        }
      }

      vertex = b.createCompositeConstruct(vec2_float_type_,
                                          {components[0], components[1]});
    } break;

    case VertexFormat::k_32_32_32_32: {
      spv::Id components[4] = {};
      for (uint32_t i = 0; i < 4; i++) {
        auto index = b.createBinOp(spv::Op::OpIAdd, int_type_, vertex_idx,
                                   b.makeUintConstant(i));
        auto vertex_ptr = b.createAccessChain(
            spv::StorageClass::StorageClassUniform, data_ptr, {index});
        auto vertex_data = b.createLoad(vertex_ptr);

        if (instr.attributes.is_integer) {
          if (instr.attributes.is_signed) {
            components[i] =
                b.createUnaryOp(spv::Op::OpBitcast, int_type_, vertex_data);
            components[i] = b.createUnaryOp(spv::Op::OpConvertSToF, float_type_,
                                            vertex_data);
          } else {
            components[i] = b.createUnaryOp(spv::Op::OpConvertUToF, float_type_,
                                            vertex_data);
          }
        } else {
          if (instr.attributes.is_signed) {
            // TODO(DrChat): This is gonna be harder to convert. There's not
            // enough precision in a float to shove INT_MAX into it.
            assert_always();
            components[i] = b.makeFloatConstant(0.f);
          } else {
            components[i] = ConvertNormVar(vertex_data, uint_type_, 32, false);
          }
        }
      }

      vertex = b.createCompositeConstruct(
          vec2_float_type_,
          {components[0], components[1], components[2], components[3]});
    } break;

    case VertexFormat::k_32_FLOAT: {
      auto vertex_ptr = b.createAccessChain(
          spv::StorageClass::StorageClassUniform, data_ptr, {vertex_idx});
      auto vertex_data = b.createLoad(vertex_ptr);

      vertex = b.createUnaryOp(spv::Op::OpBitcast, float_type_, vertex_data);
    } break;

    case VertexFormat::k_32_32_FLOAT: {
      spv::Id components[2] = {};
      for (uint32_t i = 0; i < 2; i++) {
        auto index = b.createBinOp(spv::Op::OpIAdd, int_type_, vertex_idx,
                                   b.makeUintConstant(i));
        auto vertex_ptr = b.createAccessChain(
            spv::StorageClass::StorageClassUniform, data_ptr, {index});
        auto vertex_data = b.createLoad(vertex_ptr);

        components[i] =
            b.createUnaryOp(spv::Op::OpBitcast, float_type_, vertex_data);
      }

      vertex = b.createCompositeConstruct(vec2_float_type_,
                                          {components[0], components[1]});
    } break;

    case VertexFormat::k_32_32_32_FLOAT: {
      spv::Id components[3] = {};
      for (uint32_t i = 0; i < 3; i++) {
        auto index = b.createBinOp(spv::Op::OpIAdd, int_type_, vertex_idx,
                                   b.makeUintConstant(i));
        auto vertex_ptr = b.createAccessChain(
            spv::StorageClass::StorageClassUniform, data_ptr, {index});
        auto vertex_data = b.createLoad(vertex_ptr);

        components[i] =
            b.createUnaryOp(spv::Op::OpBitcast, float_type_, vertex_data);
      }

      vertex = b.createCompositeConstruct(
          vec3_float_type_, {components[0], components[1], components[2]});
    } break;

    case VertexFormat::k_32_32_32_32_FLOAT: {
      spv::Id components[4] = {};
      for (uint32_t i = 0; i < 4; i++) {
        auto index = b.createBinOp(spv::Op::OpIAdd, int_type_, vertex_idx,
                                   b.makeUintConstant(i));
        auto vertex_ptr = b.createAccessChain(
            spv::StorageClass::StorageClassUniform, data_ptr, {index});
        auto vertex_data = b.createLoad(vertex_ptr);

        components[i] =
            b.createUnaryOp(spv::Op::OpBitcast, float_type_, vertex_data);
      }

      vertex = b.createCompositeConstruct(
          vec4_float_type_,
          {components[0], components[1], components[2], components[3]});
    } break;

    case VertexFormat::k_2_10_10_10: {
      auto vertex_ptr = b.createAccessChain(
          spv::StorageClass::StorageClassUniform, data_ptr, {vertex_idx});
      auto vertex_data = b.createLoad(vertex_ptr);
      assert(b.getTypeId(vertex_data) == uint_type_);

      // This needs to be converted.
      bool is_signed = instr.attributes.is_signed;
      bool is_integer = instr.attributes.is_integer;
      auto comp_type = is_signed ? int_type_ : uint_type_;

      if (is_signed) {
        vertex_data =
            b.createUnaryOp(spv::Op::OpBitcast, int_type_, vertex_data);
      }

      spv::Id components[4] = {0};
      components[0] =
          BitfieldExtract(comp_type, vertex_data, is_signed, 00, 10);
      components[1] =
          BitfieldExtract(comp_type, vertex_data, is_signed, 10, 10);
      components[2] =
          BitfieldExtract(comp_type, vertex_data, is_signed, 20, 10);
      components[3] =
          BitfieldExtract(comp_type, vertex_data, is_signed, 30, 02);

      auto op = is_signed ? spv::Op::OpConvertSToF : spv::Op::OpConvertUToF;
      for (int i = 0; i < xe::countof(components); i++) {
        components[i] = b.createUnaryOp(op, float_type_, components[i]);
      }

      if (!is_integer) {
        components[0] =
            ConvertNormVar(components[0], float_type_, 10, is_signed);
        components[1] =
            ConvertNormVar(components[1], float_type_, 10, is_signed);
        components[2] =
            ConvertNormVar(components[2], float_type_, 10, is_signed);
        components[3] =
            ConvertNormVar(components[3], float_type_, 02, is_signed);
      }

      vertex = b.createCompositeConstruct(
          vec4_float_type_, std::vector<Id>({components[0], components[1],
                                             components[2], components[3]}));
    } break;

    case VertexFormat::k_10_11_11: {
      auto vertex_ptr = b.createAccessChain(
          spv::StorageClass::StorageClassUniform, data_ptr, {vertex_idx});
      auto vertex_data = b.createLoad(vertex_ptr);
      assert(b.getTypeId(vertex_data) == uint_type_);

      // This needs to be converted.
      bool is_signed = instr.attributes.is_signed;
      bool is_integer = instr.attributes.is_integer;
      auto op =
          is_signed ? spv::Op::OpBitFieldSExtract : spv::Op::OpBitFieldUExtract;
      auto comp_type = is_signed ? int_type_ : uint_type_;

      if (is_signed) {
        vertex_data =
            b.createUnaryOp(spv::Op::OpBitcast, int_type_, vertex_data);
      }

      assert_true(comp_type == b.getTypeId(vertex_data));

      spv::Id components[3] = {0};
      /*
      components[2] = b.createTriOp(
          op, comp_type, vertex, b.makeUintConstant(0), b.makeUintConstant(10));
      components[1] =
          b.createTriOp(op, comp_type, vertex, b.makeUintConstant(10),
                        b.makeUintConstant(11));
      components[0] =
          b.createTriOp(op, comp_type, vertex, b.makeUintConstant(21),
                        b.makeUintConstant(11));
      */
      // Workaround until NVIDIA fixes their compiler :|
      components[0] =
          BitfieldExtract(comp_type, vertex_data, is_signed, 00, 11);
      components[1] =
          BitfieldExtract(comp_type, vertex_data, is_signed, 11, 11);
      components[2] =
          BitfieldExtract(comp_type, vertex_data, is_signed, 22, 10);

      op = is_signed ? spv::Op::OpConvertSToF : spv::Op::OpConvertUToF;
      for (int i = 0; i < 3; i++) {
        components[i] = b.createUnaryOp(op, float_type_, components[i]);
      }

      if (!is_integer) {
        components[0] =
            ConvertNormVar(components[0], float_type_, 11, is_signed);
        components[1] =
            ConvertNormVar(components[1], float_type_, 11, is_signed);
        components[2] =
            ConvertNormVar(components[2], float_type_, 10, is_signed);
      }

      vertex = b.createCompositeConstruct(
          vec3_float_type_,
          std::vector<Id>({components[0], components[1], components[2]}));
    } break;

    case VertexFormat::k_11_11_10: {
      auto vertex_ptr = b.createAccessChain(
          spv::StorageClass::StorageClassUniform, data_ptr, {vertex_idx});
      auto vertex_data = b.createLoad(vertex_ptr);
      assert(b.getTypeId(vertex_data) == uint_type_);

      // This needs to be converted.
      bool is_signed = instr.attributes.is_signed;
      bool is_integer = instr.attributes.is_integer;
      auto op =
          is_signed ? spv::Op::OpBitFieldSExtract : spv::Op::OpBitFieldUExtract;
      auto comp_type = is_signed ? int_type_ : uint_type_;

      spv::Id components[3] = {0};
      /*
      components[2] = b.createTriOp(
          op, comp_type, vertex, b.makeUintConstant(0), b.makeUintConstant(11));
      components[1] =
          b.createTriOp(op, comp_type, vertex, b.makeUintConstant(11),
                        b.makeUintConstant(11));
      components[0] =
          b.createTriOp(op, comp_type, vertex, b.makeUintConstant(22),
                        b.makeUintConstant(10));
      */
      // Workaround until NVIDIA fixes their compiler :|
      components[0] =
          BitfieldExtract(comp_type, vertex_data, is_signed, 00, 10);
      components[1] =
          BitfieldExtract(comp_type, vertex_data, is_signed, 10, 11);
      components[2] =
          BitfieldExtract(comp_type, vertex_data, is_signed, 21, 11);

      op = is_signed ? spv::Op::OpConvertSToF : spv::Op::OpConvertUToF;
      for (int i = 0; i < 3; i++) {
        components[i] = b.createUnaryOp(op, float_type_, components[i]);
      }

      if (!is_integer) {
        components[0] =
            ConvertNormVar(components[0], float_type_, 11, is_signed);
        components[1] =
            ConvertNormVar(components[1], float_type_, 11, is_signed);
        components[2] =
            ConvertNormVar(components[2], float_type_, 10, is_signed);
      }

      vertex = b.createCompositeConstruct(
          vec3_float_type_,
          std::vector<Id>({components[0], components[1], components[2]}));
    } break;

    case VertexFormat::kUndefined:
      break;
  }

  assert_not_zero(vertex);
  StoreToResult(vertex, instr.result);
}

void SpirvShaderTranslator::ProcessTextureFetchInstruction(
    const ParsedTextureFetchInstruction& instr) {
  auto& b = *builder_;

  // Close the open predicated block if this instr isn't predicated or the
  // conditions do not match.
  if (open_predicated_block_ &&
      (!instr.is_predicated ||
       instr.predicate_condition != predicated_block_cond_)) {
    b.createBranch(predicated_block_end_);
    b.setBuildPoint(predicated_block_end_);
    open_predicated_block_ = false;
    predicated_block_cond_ = false;
    predicated_block_end_ = nullptr;
  }

  if (!open_predicated_block_ && instr.is_predicated) {
    Id pred_cond =
        b.createBinOp(spv::Op::OpLogicalEqual, bool_type_, b.createLoad(p0_),
                      b.makeBoolConstant(instr.predicate_condition));
    auto block = &b.makeNewBlock();
    open_predicated_block_ = true;
    predicated_block_cond_ = instr.predicate_condition;
    predicated_block_end_ = &b.makeNewBlock();

    b.createSelectionMerge(predicated_block_end_,
                           spv::SelectionControlMaskNone);
    b.createConditionalBranch(pred_cond, block, predicated_block_end_);
    b.setBuildPoint(block);
  }

  // Operand 0 is the offset
  // Operand 1 is the sampler index
  Id dest = vec4_float_zero_;
  Id src = LoadFromOperand(instr.operands[0]);
  assert_not_zero(src);

  uint32_t dim_idx = 0;
  switch (instr.dimension) {
    case TextureDimension::k1D:
    case TextureDimension::k2D: {
      dim_idx = 0;
    } break;
    case TextureDimension::k3D: {
      dim_idx = 1;
    } break;
    case TextureDimension::kCube: {
      dim_idx = 2;
    } break;
    default:
      assert_unhandled_case(instr.dimension);
  }

  switch (instr.opcode) {
    case FetchOpcode::kTextureFetch: {
      auto texture_index =
          b.makeUintConstant(tex_binding_map_[instr.operands[1].storage_index]);
      auto texture_ptr =
          b.createAccessChain(spv::StorageClass::StorageClassUniformConstant,
                              tex_[dim_idx], std::vector<Id>({texture_index}));
      auto texture = b.createLoad(texture_ptr);

      if (instr.dimension == TextureDimension::k1D) {
        // Upgrade 1D src coordinate into 2D
        src = b.createCompositeConstruct(vec2_float_type_,
                                         {src, b.makeFloatConstant(0.f)});
      }

      spv::Builder::TextureParameters params = {0};
      params.coords = src;
      params.sampler = texture;
      if (instr.attributes.use_register_lod) {
        params.lod = b.createLoad(lod_);
      }
      if (instr.attributes.offset_x || instr.attributes.offset_y ||
          instr.attributes.offset_z) {
        float offset_x = instr.attributes.offset_x;
        float offset_y = instr.attributes.offset_y;
        float offset_z = instr.attributes.offset_z;

        // Round numbers away from zero. No effect if offset is 0.
        offset_x += instr.attributes.offset_x < 0 ? -0.5f : 0.5f;
        offset_y += instr.attributes.offset_y < 0 ? -0.5f : 0.5f;
        offset_z += instr.attributes.offset_z < 0 ? -0.5f : 0.5f;

        Id offset = 0;
        switch (instr.dimension) {
          case TextureDimension::k1D: {
            // https://msdn.microsoft.com/en-us/library/windows/desktop/bb944006.aspx
            // "Because the runtime does not support 1D textures, the compiler
            // will use a 2D texture with the knowledge that the y-coordinate is
            // unimportant."
            offset = b.makeCompositeConstant(
                vec2_int_type_,
                {b.makeIntConstant(int(offset_x)), b.makeIntConstant(0)});
          } break;
          case TextureDimension::k2D: {
            offset = b.makeCompositeConstant(
                vec2_int_type_, {b.makeIntConstant(int(offset_x)),
                                 b.makeIntConstant(int(offset_y))});
          } break;
          case TextureDimension::k3D: {
            offset = b.makeCompositeConstant(
                vec3_int_type_, {b.makeIntConstant(int(offset_x)),
                                 b.makeIntConstant(int(offset_y)),
                                 b.makeIntConstant(int(offset_z))});
          } break;
          case TextureDimension::kCube: {
            // FIXME(DrChat): Is this the correct dimension? I forget
            offset = b.makeCompositeConstant(
                vec3_int_type_, {b.makeIntConstant(int(offset_x)),
                                 b.makeIntConstant(int(offset_y)),
                                 b.makeIntConstant(int(offset_z))});
          } break;
        }

        params.offset = offset;
      }

      dest =
          b.createTextureCall(spv::NoPrecision, vec4_float_type_, false, false,
                              false, false, is_vertex_shader(), params);
    } break;

    case FetchOpcode::kGetTextureGradients: {
      Id src_x = b.createCompositeExtract(src, float_type_, 0);
      Id src_y = b.createCompositeExtract(src, float_type_, 1);

      dest = b.createCompositeConstruct(
          vec4_float_type_,
          {
              b.createUnaryOp(spv::OpDPdx, float_type_, src_x),
              b.createUnaryOp(spv::OpDPdy, float_type_, src_x),
              b.createUnaryOp(spv::OpDPdx, float_type_, src_y),
              b.createUnaryOp(spv::OpDPdy, float_type_, src_y),
          });
    } break;

    case FetchOpcode::kGetTextureWeights: {
      // fract(src0 * textureSize);
      auto texture_index =
          b.makeUintConstant(tex_binding_map_[instr.operands[1].storage_index]);
      auto texture_ptr =
          b.createAccessChain(spv::StorageClass::StorageClassUniformConstant,
                              tex_[dim_idx], std::vector<Id>({texture_index}));
      auto texture = b.createLoad(texture_ptr);
      auto image =
          b.createUnaryOp(spv::OpImage, b.getImageType(texture), texture);

      switch (instr.dimension) {
        case TextureDimension::k1D:
        case TextureDimension::k2D: {
          spv::Builder::TextureParameters params;
          std::memset(&params, 0, sizeof(params));
          params.sampler = image;
          params.lod = b.makeIntConstant(0);
          auto size = b.createTextureQueryCall(spv::Op::OpImageQuerySizeLod,
                                               params, true);
          size =
              b.createUnaryOp(spv::Op::OpConvertUToF, vec2_float_type_, size);

          auto weight =
              b.createBinOp(spv::Op::OpFMul, vec2_float_type_, size, src);
          weight = CreateGlslStd450InstructionCall(
              spv::NoPrecision, vec2_float_type_, spv::GLSLstd450::kFract,
              {weight});

          dest = b.createOp(spv::Op::OpVectorShuffle, vec4_float_type_,
                            {weight, vec4_float_zero_, 0, 1, 2, 2});
        } break;

        default:
          // TODO(DrChat): The rest of these.
          assert_unhandled_case(instr.dimension);
          break;
      }
    } break;

    case FetchOpcode::kGetTextureComputedLod: {
      // TODO(DrChat): Verify if this implementation is correct.
      // This is only valid in pixel shaders.
      assert_true(is_pixel_shader());

      auto texture_index =
          b.makeUintConstant(tex_binding_map_[instr.operands[1].storage_index]);
      auto texture_ptr =
          b.createAccessChain(spv::StorageClass::StorageClassUniformConstant,
                              tex_[dim_idx], std::vector<Id>({texture_index}));
      auto texture = b.createLoad(texture_ptr);

      if (instr.dimension == TextureDimension::k1D) {
        // Upgrade 1D src coordinate into 2D
        src = b.createCompositeConstruct(vec2_float_type_,
                                         {src, b.makeFloatConstant(0.f)});
      }

      spv::Builder::TextureParameters params = {};
      params.sampler = texture;
      params.coords = src;
      auto lod =
          b.createTextureQueryCall(spv::Op::OpImageQueryLod, params, false);

      dest = b.createCompositeExtract(lod, float_type_, 1);
      dest = b.smearScalar(spv::NoPrecision, dest, vec4_float_type_);
    } break;

    case FetchOpcode::kSetTextureLod: {
      // <lod register> = src1.x (MIP level)
      // ... immediately after
      // tfetch UseRegisterLOD=true
      b.createStore(src, lod_);
    } break;

    default:
      // TODO: the rest of these
      assert_unhandled_case(instr.opcode);
      break;
  }

  if (dest) {
    b.createStore(dest, pv_);
    StoreToResult(dest, instr.result);
  }
}

void SpirvShaderTranslator::ProcessAluInstruction(
    const ParsedAluInstruction& instr) {
  auto& b = *builder_;
  switch (instr.type) {
    case ParsedAluInstruction::Type::kNop:
      b.createNoResultOp(spv::Op::OpNop);
      break;
    case ParsedAluInstruction::Type::kVector:
      ProcessVectorAluInstruction(instr);
      break;
    case ParsedAluInstruction::Type::kScalar:
      ProcessScalarAluInstruction(instr);
      break;
  }
}

spv::Function* SpirvShaderTranslator::CreateCubeFunction() {
  auto& b = *builder_;
  spv::Block* function_block = nullptr;
  auto function = b.makeFunctionEntry(spv::NoPrecision, vec4_float_type_,
                                      "cube", {vec4_float_type_},
                                      {{spv::NoPrecision}}, &function_block);
  auto src = function->getParamId(0);
  auto face_id = b.createVariable(spv::StorageClass::StorageClassFunction,
                                  float_type_, "face_id");
  auto sc = b.createVariable(spv::StorageClass::StorageClassFunction,
                             float_type_, "sc");
  auto tc = b.createVariable(spv::StorageClass::StorageClassFunction,
                             float_type_, "tc");
  auto ma = b.createVariable(spv::StorageClass::StorageClassFunction,
                             float_type_, "ma");

  // Pseudocode:
  /*
  vec4 cube(vec4 src1) {
  vec3 src = vec3(src1.y, src1.x, src1.z);
  vec3 abs_src = abs(src);
  int face_id;
  float sc;
  float tc;
  float ma;
  if (abs_src.x > abs_src.y && abs_src.x > abs_src.z) {
    if (src.x > 0.0) {
      face_id = 0; sc = -abs_src.z; tc = -abs_src.y; ma = abs_src.x;
    } else {
      face_id = 1; sc =  abs_src.z; tc = -abs_src.y; ma = abs_src.x;
    }
  } else if (abs_src.y > abs_src.x && abs_src.y > abs_src.z) {
    if (src.y > 0.0) {
      face_id = 2; sc =  abs_src.x; tc =  abs_src.z; ma = abs_src.y;
    } else {
      face_id = 3; sc =  abs_src.x; tc = -abs_src.z; ma = abs_src.y;
    }
  } else {
    if (src.z > 0.0) {
      face_id = 4; sc =  abs_src.x; tc = -abs_src.y; ma = abs_src.z;
    } else {
      face_id = 5; sc = -abs_src.x; tc = -abs_src.y; ma = abs_src.z;
    }
  }
  float s = (sc / ma + 1.0) / 2.0;
  float t = (tc / ma + 1.0) / 2.0;
  return vec4(t, s, 2.0 * ma, float(face_id));
  }
  */

  auto abs_src = CreateGlslStd450InstructionCall(
      spv::NoPrecision, vec4_float_type_, spv::GLSLstd450::kFAbs, {src});
  auto abs_src_x = b.createCompositeExtract(abs_src, float_type_, 0);
  auto abs_src_y = b.createCompositeExtract(abs_src, float_type_, 1);
  auto abs_src_z = b.createCompositeExtract(abs_src, float_type_, 2);
  auto neg_src_x = b.createUnaryOp(spv::Op::OpFNegate, float_type_, abs_src_x);
  auto neg_src_y = b.createUnaryOp(spv::Op::OpFNegate, float_type_, abs_src_y);
  auto neg_src_z = b.createUnaryOp(spv::Op::OpFNegate, float_type_, abs_src_z);

  //  Case 1: abs(src).x > abs(src).yz
  {
    auto x_gt_y = b.createBinOp(spv::Op::OpFOrdGreaterThan, bool_type_,
                                abs_src_x, abs_src_y);
    auto x_gt_z = b.createBinOp(spv::Op::OpFOrdGreaterThan, bool_type_,
                                abs_src_x, abs_src_z);
    auto c1 = b.createBinOp(spv::Op::OpLogicalAnd, bool_type_, x_gt_y, x_gt_z);
    spv::Builder::If if1(c1, 0, b);

    //  sc =  abs(src).y
    b.createStore(abs_src_y, sc);
    //  ma =  abs(src).x
    b.createStore(abs_src_x, ma);

    auto src_x = b.createCompositeExtract(src, float_type_, 0);
    auto c2 = b.createBinOp(spv::Op::OpFOrdGreaterThan, bool_type_, src_x,
                            b.makeFloatConstant(0));
    //  src.x > 0:
    //    face_id = 2
    //    tc = -abs(src).z
    //  src.x <= 0:
    //    face_id = 3
    //    tc =  abs(src).z
    auto tmp_face_id =
        b.createTriOp(spv::Op::OpSelect, float_type_, c2,
                      b.makeFloatConstant(2), b.makeFloatConstant(3));
    auto tmp_tc =
        b.createTriOp(spv::Op::OpSelect, float_type_, c2, neg_src_z, abs_src_z);

    b.createStore(tmp_face_id, face_id);
    b.createStore(tmp_tc, tc);

    if1.makeEndIf();
  }

  //  Case 2: abs(src).y > abs(src).xz
  {
    auto y_gt_x = b.createBinOp(spv::Op::OpFOrdGreaterThan, bool_type_,
                                abs_src_y, abs_src_x);
    auto y_gt_z = b.createBinOp(spv::Op::OpFOrdGreaterThan, bool_type_,
                                abs_src_y, abs_src_z);
    auto c1 = b.createBinOp(spv::Op::OpLogicalAnd, bool_type_, y_gt_x, y_gt_z);
    spv::Builder::If if1(c1, 0, b);

    //  tc = -abs(src).x
    b.createStore(neg_src_x, tc);
    //  ma =  abs(src).y
    b.createStore(abs_src_y, ma);

    auto src_y = b.createCompositeExtract(src, float_type_, 1);
    auto c2 = b.createBinOp(spv::Op::OpFOrdGreaterThan, bool_type_, src_y,
                            b.makeFloatConstant(0));
    //  src.y > 0:
    //    face_id = 0
    //    sc = -abs(src).z
    //  src.y <= 0:
    //    face_id = 1
    //    sc =  abs(src).z
    auto tmp_face_id =
        b.createTriOp(spv::Op::OpSelect, float_type_, c2,
                      b.makeFloatConstant(0), b.makeFloatConstant(1));
    auto tmp_sc =
        b.createTriOp(spv::Op::OpSelect, float_type_, c2, neg_src_z, abs_src_z);

    b.createStore(tmp_face_id, face_id);
    b.createStore(tmp_sc, sc);

    if1.makeEndIf();
  }

  //  Case 3: abs(src).z > abs(src).yx
  {
    auto z_gt_x = b.createBinOp(spv::Op::OpFOrdGreaterThan, bool_type_,
                                abs_src_z, abs_src_x);
    auto z_gt_y = b.createBinOp(spv::Op::OpFOrdGreaterThan, bool_type_,
                                abs_src_z, abs_src_y);
    auto c1 = b.createBinOp(spv::Op::OpLogicalAnd, bool_type_, z_gt_x, z_gt_y);
    spv::Builder::If if1(c1, 0, b);

    //  tc = -abs(src).x
    b.createStore(neg_src_x, tc);
    //  ma =  abs(src).z
    b.createStore(abs_src_z, ma);

    auto src_z = b.createCompositeExtract(src, float_type_, 2);
    auto c2 = b.createBinOp(spv::Op::OpFOrdGreaterThan, bool_type_, src_z,
                            b.makeFloatConstant(0));
    //  src.z > 0:
    //    face_id = 4
    //    sc = -abs(src).y
    //  src.z <= 0:
    //    face_id = 5
    //    sc =  abs(src).y
    auto tmp_face_id =
        b.createTriOp(spv::Op::OpSelect, float_type_, c2,
                      b.makeFloatConstant(4), b.makeFloatConstant(5));
    auto tmp_sc =
        b.createTriOp(spv::Op::OpSelect, float_type_, c2, neg_src_y, abs_src_y);

    b.createStore(tmp_face_id, face_id);
    b.createStore(tmp_sc, sc);

    if1.makeEndIf();
  }

  //  s = (sc / ma + 1.0) / 2.0
  auto s = b.createBinOp(spv::Op::OpFDiv, float_type_, b.createLoad(sc),
                         b.createLoad(ma));
  s = b.createBinOp(spv::Op::OpFAdd, float_type_, s, b.makeFloatConstant(1.0));
  s = b.createBinOp(spv::Op::OpFDiv, float_type_, s, b.makeFloatConstant(2.0));

  //  t = (tc / ma + 1.0) / 2.0
  auto t = b.createBinOp(spv::Op::OpFDiv, float_type_, b.createLoad(tc),
                         b.createLoad(ma));
  t = b.createBinOp(spv::Op::OpFAdd, float_type_, t, b.makeFloatConstant(1.0));
  t = b.createBinOp(spv::Op::OpFDiv, float_type_, t, b.makeFloatConstant(2.0));

  auto ma_times_two = b.createBinOp(spv::Op::OpFMul, float_type_,
                                    b.createLoad(ma), b.makeFloatConstant(2.0));

  //  dest = vec4(t, s, 2.0 * ma, face_id)
  auto ret = b.createCompositeConstruct(
      vec4_float_type_,
      std::vector<Id>({t, s, ma_times_two, b.createLoad(face_id)}));
  b.makeReturn(false, ret);

  return function;
}

void SpirvShaderTranslator::ProcessVectorAluInstruction(
    const ParsedAluInstruction& instr) {
  auto& b = *builder_;

  // Close the open predicated block if this instr isn't predicated or the
  // conditions do not match.
  if (open_predicated_block_ &&
      (!instr.is_predicated ||
       instr.predicate_condition != predicated_block_cond_)) {
    b.createBranch(predicated_block_end_);
    b.setBuildPoint(predicated_block_end_);
    open_predicated_block_ = false;
    predicated_block_cond_ = false;
    predicated_block_end_ = nullptr;
  }

  if (!open_predicated_block_ && instr.is_predicated) {
    Id pred_cond =
        b.createBinOp(spv::Op::OpLogicalEqual, bool_type_, b.createLoad(p0_),
                      b.makeBoolConstant(instr.predicate_condition));
    auto block = &b.makeNewBlock();
    open_predicated_block_ = true;
    predicated_block_cond_ = instr.predicate_condition;
    predicated_block_end_ = &b.makeNewBlock();

    b.createSelectionMerge(predicated_block_end_,
                           spv::SelectionControlMaskNone);
    b.createConditionalBranch(pred_cond, block, predicated_block_end_);
    b.setBuildPoint(block);
  }

  // TODO: If we have identical operands, reuse previous one.
  Id sources[3] = {0};
  Id dest = vec4_float_zero_;
  for (size_t i = 0; i < instr.operand_count; i++) {
    sources[i] = LoadFromOperand(instr.operands[i]);
  }

  bool close_predicated_block = false;
  switch (instr.vector_opcode) {
    case AluVectorOpcode::kAdd: {
      dest = b.createBinOp(spv::Op::OpFAdd, vec4_float_type_, sources[0],
                           sources[1]);
    } break;

    case AluVectorOpcode::kCndEq: {
      // dest = src0 == 0.0 ? src1 : src2;
      auto c = b.createBinOp(spv::Op::OpFOrdEqual, vec4_bool_type_, sources[0],
                             vec4_float_zero_);
      dest = b.createTriOp(spv::Op::OpSelect, vec4_float_type_, c, sources[1],
                           sources[2]);
    } break;

    case AluVectorOpcode::kCndGe: {
      // dest = src0 >= 0.0 ? src1 : src2;
      auto c = b.createBinOp(spv::Op::OpFOrdGreaterThanEqual, vec4_bool_type_,
                             sources[0], vec4_float_zero_);
      dest = b.createTriOp(spv::Op::OpSelect, vec4_float_type_, c, sources[1],
                           sources[2]);
    } break;

    case AluVectorOpcode::kCndGt: {
      // dest = src0 > 0.0 ? src1 : src2;
      auto c = b.createBinOp(spv::Op::OpFOrdGreaterThan, vec4_bool_type_,
                             sources[0], vec4_float_zero_);
      dest = b.createTriOp(spv::Op::OpSelect, vec4_float_type_, c, sources[1],
                           sources[2]);
    } break;

    case AluVectorOpcode::kCube: {
      dest =
          b.createFunctionCall(cube_function_, std::vector<Id>({sources[1]}));
    } break;

    case AluVectorOpcode::kDst: {
      auto src0_y = b.createCompositeExtract(sources[0], float_type_, 1);
      auto src1_y = b.createCompositeExtract(sources[1], float_type_, 1);
      auto dst_y = b.createBinOp(spv::Op::OpFMul, float_type_, src0_y, src1_y);

      auto src0_z = b.createCompositeExtract(sources[0], float_type_, 2);
      auto src1_w = b.createCompositeExtract(sources[1], float_type_, 3);
      dest = b.createCompositeConstruct(
          vec4_float_type_,
          std::vector<Id>({b.makeFloatConstant(1.f), dst_y, src0_z, src1_w}));
    } break;

    case AluVectorOpcode::kDp2Add: {
      auto src0_xy = b.createOp(spv::Op::OpVectorShuffle, vec2_float_type_,
                                {sources[0], sources[0], 0, 1});
      auto src1_xy = b.createOp(spv::Op::OpVectorShuffle, vec2_float_type_,
                                {sources[1], sources[1], 0, 1});
      auto src2_x = b.createCompositeExtract(sources[2], float_type_, 0);
      dest = b.createBinOp(spv::Op::OpDot, float_type_, src0_xy, src1_xy);
      dest = b.createBinOp(spv::Op::OpFAdd, float_type_, dest, src2_x);
      dest = b.smearScalar(spv::NoPrecision, dest, vec4_float_type_);
    } break;

    case AluVectorOpcode::kDp3: {
      auto src0_xyz = b.createOp(spv::Op::OpVectorShuffle, vec3_float_type_,
                                 {sources[0], sources[0], 0, 1, 2});
      auto src1_xyz = b.createOp(spv::Op::OpVectorShuffle, vec3_float_type_,
                                 {sources[1], sources[1], 0, 1, 2});
      dest = b.createBinOp(spv::Op::OpDot, float_type_, src0_xyz, src1_xyz);
      dest = b.smearScalar(spv::NoPrecision, dest, vec4_float_type_);
    } break;

    case AluVectorOpcode::kDp4: {
      dest = b.createBinOp(spv::Op::OpDot, float_type_, sources[0], sources[1]);
      dest = b.smearScalar(spv::NoPrecision, dest, vec4_float_type_);
    } break;

    case AluVectorOpcode::kFloor: {
      dest = CreateGlslStd450InstructionCall(spv::NoPrecision, vec4_float_type_,
                                             spv::GLSLstd450::kFloor,
                                             {sources[0]});
    } break;

    case AluVectorOpcode::kFrc: {
      dest = CreateGlslStd450InstructionCall(spv::NoPrecision, vec4_float_type_,
                                             spv::GLSLstd450::kFract,
                                             {sources[0]});
    } break;

    case AluVectorOpcode::kKillEq: {
      auto continue_block = &b.makeNewBlock();
      auto kill_block = &b.makeNewBlock();
      auto cond = b.createBinOp(spv::Op::OpFOrdEqual, vec4_bool_type_,
                                sources[0], sources[1]);
      cond = b.createUnaryOp(spv::Op::OpAny, bool_type_, cond);
      b.createConditionalBranch(cond, kill_block, continue_block);

      b.setBuildPoint(kill_block);
      b.createNoResultOp(spv::Op::OpKill);

      b.setBuildPoint(continue_block);
      dest = vec4_float_zero_;
    } break;

    case AluVectorOpcode::kKillGe: {
      auto continue_block = &b.makeNewBlock();
      auto kill_block = &b.makeNewBlock();
      auto cond = b.createBinOp(spv::Op::OpFOrdGreaterThanEqual,
                                vec4_bool_type_, sources[0], sources[1]);
      cond = b.createUnaryOp(spv::Op::OpAny, bool_type_, cond);
      b.createConditionalBranch(cond, kill_block, continue_block);

      b.setBuildPoint(kill_block);
      b.createNoResultOp(spv::Op::OpKill);

      b.setBuildPoint(continue_block);
      dest = vec4_float_zero_;
    } break;

    case AluVectorOpcode::kKillGt: {
      auto continue_block = &b.makeNewBlock();
      auto kill_block = &b.makeNewBlock();
      auto cond = b.createBinOp(spv::Op::OpFOrdGreaterThan, vec4_bool_type_,
                                sources[0], sources[1]);
      cond = b.createUnaryOp(spv::Op::OpAny, bool_type_, cond);
      b.createConditionalBranch(cond, kill_block, continue_block);

      b.setBuildPoint(kill_block);
      b.createNoResultOp(spv::Op::OpKill);

      b.setBuildPoint(continue_block);
      dest = vec4_float_zero_;
    } break;

    case AluVectorOpcode::kKillNe: {
      auto continue_block = &b.makeNewBlock();
      auto kill_block = &b.makeNewBlock();
      auto cond = b.createBinOp(spv::Op::OpFOrdNotEqual, vec4_bool_type_,
                                sources[0], sources[1]);
      cond = b.createUnaryOp(spv::Op::OpAny, bool_type_, cond);
      b.createConditionalBranch(cond, kill_block, continue_block);

      b.setBuildPoint(kill_block);
      b.createNoResultOp(spv::Op::OpKill);

      b.setBuildPoint(continue_block);
      dest = vec4_float_zero_;
    } break;

    case AluVectorOpcode::kMad: {
      dest = b.createBinOp(spv::Op::OpFMul, vec4_float_type_, sources[0],
                           sources[1]);
      dest = b.createBinOp(spv::Op::OpFAdd, vec4_float_type_, dest, sources[2]);
    } break;

    case AluVectorOpcode::kMax4: {
      auto src0_x = b.createCompositeExtract(sources[0], float_type_, 0);
      auto src0_y = b.createCompositeExtract(sources[0], float_type_, 1);
      auto src0_z = b.createCompositeExtract(sources[0], float_type_, 2);
      auto src0_w = b.createCompositeExtract(sources[0], float_type_, 3);

      auto max_xy = CreateGlslStd450InstructionCall(
          spv::NoPrecision, float_type_, spv::GLSLstd450::kFMax,
          {src0_x, src0_y});
      auto max_zw = CreateGlslStd450InstructionCall(
          spv::NoPrecision, float_type_, spv::GLSLstd450::kFMax,
          {src0_z, src0_w});
      auto max_xyzw = CreateGlslStd450InstructionCall(
          spv::NoPrecision, float_type_, spv::GLSLstd450::kFMax,
          {max_xy, max_zw});

      // FIXME: Docs say this only updates pv.x?
      dest = b.smearScalar(spv::NoPrecision, max_xyzw, vec4_float_type_);
    } break;

    case AluVectorOpcode::kMaxA: {
      // a0 = clamp(floor(src0.w + 0.5), -256, 255)
      auto addr = b.createCompositeExtract(sources[0], float_type_, 3);
      addr = b.createBinOp(spv::Op::OpFAdd, float_type_, addr,
                           b.makeFloatConstant(0.5f));
      addr = b.createUnaryOp(spv::Op::OpConvertFToS, int_type_, addr);
      addr = CreateGlslStd450InstructionCall(
          spv::NoPrecision, int_type_, spv::GLSLstd450::kSClamp,
          {addr, b.makeIntConstant(-256), b.makeIntConstant(255)});
      b.createStore(addr, a0_);

      // dest = src0 >= src1 ? src0 : src1
      dest = CreateGlslStd450InstructionCall(spv::NoPrecision, vec4_float_type_,
                                             spv::GLSLstd450::kFMax,
                                             {sources[0], sources[1]});
    } break;

    case AluVectorOpcode::kMax: {
      if (sources[0] == sources[1]) {
        // mov dst, src
        dest = sources[0];
        break;
      }

      dest = CreateGlslStd450InstructionCall(spv::NoPrecision, vec4_float_type_,
                                             spv::GLSLstd450::kFMax,
                                             {sources[0], sources[1]});
    } break;

    case AluVectorOpcode::kMin: {
      if (sources[0] == sources[1]) {
        // mov dst, src
        dest = sources[0];
        break;
      }

      dest = CreateGlslStd450InstructionCall(spv::NoPrecision, vec4_float_type_,
                                             spv::GLSLstd450::kFMin,
                                             {sources[0], sources[1]});
    } break;

    case AluVectorOpcode::kMul: {
      dest = b.createBinOp(spv::Op::OpFMul, vec4_float_type_, sources[0],
                           sources[1]);
    } break;

    case AluVectorOpcode::kSetpEqPush: {
      auto c0 = b.createBinOp(spv::Op::OpFOrdEqual, vec4_bool_type_, sources[0],
                              vec4_float_zero_);
      auto c1 = b.createBinOp(spv::Op::OpFOrdEqual, vec4_bool_type_, sources[1],
                              vec4_float_zero_);
      auto c_and =
          b.createBinOp(spv::Op::OpLogicalAnd, vec4_bool_type_, c0, c1);
      auto c_and_x = b.createCompositeExtract(c_and, bool_type_, 0);
      c_and_x = b.smearScalar(spv::NoPrecision, c_and_x, vec4_bool_type_);
      auto c_and_w = b.createCompositeExtract(c_and, bool_type_, 3);

      // p0
      b.createStore(c_and_w, p0_);
      close_predicated_block = true;

      // dest
      auto s0_x = b.createCompositeExtract(sources[0], float_type_, 0);
      s0_x = b.createBinOp(spv::Op::OpFAdd, float_type_, s0_x,
                           b.makeFloatConstant(1.f));
      auto s0 = b.smearScalar(spv::NoPrecision, s0_x, vec4_float_type_);

      dest = b.createTriOp(spv::Op::OpSelect, vec4_float_type_, c_and_x,
                           vec4_float_zero_, s0);
    } break;

    case AluVectorOpcode::kSetpGePush: {
      auto c0 = b.createBinOp(spv::Op::OpFOrdEqual, vec4_bool_type_, sources[0],
                              vec4_float_zero_);
      auto c1 = b.createBinOp(spv::Op::OpFOrdGreaterThanEqual, vec4_bool_type_,
                              sources[1], vec4_float_zero_);
      auto c_and =
          b.createBinOp(spv::Op::OpLogicalAnd, vec4_bool_type_, c0, c1);
      auto c_and_x = b.createCompositeExtract(c_and, bool_type_, 0);
      c_and_x = b.smearScalar(spv::NoPrecision, c_and_x, vec4_bool_type_);
      auto c_and_w = b.createCompositeExtract(c_and, bool_type_, 3);

      // p0
      b.createStore(c_and_w, p0_);
      close_predicated_block = true;

      // dest
      auto s0_x = b.createCompositeExtract(sources[0], float_type_, 0);
      s0_x = b.createBinOp(spv::Op::OpFAdd, float_type_, s0_x,
                           b.makeFloatConstant(1.f));
      auto s0 = b.smearScalar(spv::NoPrecision, s0_x, vec4_float_type_);

      dest = b.createTriOp(spv::Op::OpSelect, vec4_float_type_, c_and_x,
                           vec4_float_zero_, s0);
    } break;

    case AluVectorOpcode::kSetpGtPush: {
      auto c0 = b.createBinOp(spv::Op::OpFOrdEqual, vec4_bool_type_, sources[0],
                              vec4_float_zero_);
      auto c1 = b.createBinOp(spv::Op::OpFOrdGreaterThan, vec4_bool_type_,
                              sources[1], vec4_float_zero_);
      auto c_and =
          b.createBinOp(spv::Op::OpLogicalAnd, vec4_bool_type_, c0, c1);
      auto c_and_x = b.createCompositeExtract(c_and, bool_type_, 0);
      c_and_x = b.smearScalar(spv::NoPrecision, c_and_x, vec4_bool_type_);
      auto c_and_w = b.createCompositeExtract(c_and, bool_type_, 3);

      // p0
      b.createStore(c_and_w, p0_);
      close_predicated_block = true;

      // dest
      auto s0_x = b.createCompositeExtract(sources[0], float_type_, 0);
      s0_x = b.createBinOp(spv::Op::OpFAdd, float_type_, s0_x,
                           b.makeFloatConstant(1.f));
      auto s0 = b.smearScalar(spv::NoPrecision, s0_x, vec4_float_type_);

      dest = b.createTriOp(spv::Op::OpSelect, vec4_float_type_, c_and_x,
                           vec4_float_zero_, s0);
    } break;

    case AluVectorOpcode::kSetpNePush: {
      auto c0 = b.createBinOp(spv::Op::OpFOrdNotEqual, vec4_bool_type_,
                              sources[0], vec4_float_zero_);
      auto c1 = b.createBinOp(spv::Op::OpFOrdEqual, vec4_bool_type_, sources[1],
                              vec4_float_zero_);
      auto c_and =
          b.createBinOp(spv::Op::OpLogicalAnd, vec4_bool_type_, c0, c1);
      auto c_and_x = b.createCompositeExtract(c_and, bool_type_, 0);
      c_and_x = b.smearScalar(spv::NoPrecision, c_and_x, vec4_bool_type_);
      auto c_and_w = b.createCompositeExtract(c_and, bool_type_, 3);

      // p0
      b.createStore(c_and_w, p0_);
      close_predicated_block = true;

      // dest
      auto s0_x = b.createCompositeExtract(sources[0], float_type_, 0);
      s0_x = b.createBinOp(spv::Op::OpFAdd, float_type_, s0_x,
                           b.makeFloatConstant(1.f));
      auto s0 = b.smearScalar(spv::NoPrecision, s0_x, vec4_float_type_);

      dest = b.createTriOp(spv::Op::OpSelect, vec4_float_type_, c_and_x,
                           vec4_float_zero_, s0);
    } break;

    case AluVectorOpcode::kSeq: {
      // foreach(el) src0 == src1 ? 1.0 : 0.0
      auto c = b.createBinOp(spv::Op::OpFOrdEqual, vec4_bool_type_, sources[0],
                             sources[1]);
      dest = b.createTriOp(spv::Op::OpSelect, vec4_float_type_, c,
                           vec4_float_one_, vec4_float_zero_);
    } break;

    case AluVectorOpcode::kSge: {
      // foreach(el) src0 >= src1 ? 1.0 : 0.0
      auto c = b.createBinOp(spv::Op::OpFOrdGreaterThanEqual, vec4_bool_type_,
                             sources[0], sources[1]);
      dest = b.createTriOp(spv::Op::OpSelect, vec4_float_type_, c,
                           vec4_float_one_, vec4_float_zero_);
    } break;

    case AluVectorOpcode::kSgt: {
      // foreach(el) src0 > src1 ? 1.0 : 0.0
      auto c = b.createBinOp(spv::Op::OpFOrdGreaterThan, vec4_bool_type_,
                             sources[0], sources[1]);
      dest = b.createTriOp(spv::Op::OpSelect, vec4_float_type_, c,
                           vec4_float_one_, vec4_float_zero_);
    } break;

    case AluVectorOpcode::kSne: {
      // foreach(el) src0 != src1 ? 1.0 : 0.0
      auto c = b.createBinOp(spv::Op::OpFOrdNotEqual, vec4_bool_type_,
                             sources[0], sources[1]);
      dest = b.createTriOp(spv::Op::OpSelect, vec4_float_type_, c,
                           vec4_float_one_, vec4_float_zero_);
    } break;

    case AluVectorOpcode::kTrunc: {
      dest = CreateGlslStd450InstructionCall(spv::NoPrecision, vec4_float_type_,
                                             GLSLstd450::kTrunc, {sources[0]});
    } break;

    default:
      assert_unhandled_case(instr.vector_opcode);
      break;
  }

  assert_not_zero(dest);
  assert_true(b.getTypeId(dest) == vec4_float_type_);
  if (dest) {
    b.createStore(dest, pv_);
    StoreToResult(dest, instr.result);
  }

  if (close_predicated_block && open_predicated_block_) {
    b.createBranch(predicated_block_end_);
    b.setBuildPoint(predicated_block_end_);
    open_predicated_block_ = false;
    predicated_block_cond_ = false;
    predicated_block_end_ = nullptr;
  }
}

void SpirvShaderTranslator::ProcessScalarAluInstruction(
    const ParsedAluInstruction& instr) {
  auto& b = *builder_;

  // Close the open predicated block if this instr isn't predicated or the
  // conditions do not match.
  if (open_predicated_block_ &&
      (!instr.is_predicated ||
       instr.predicate_condition != predicated_block_cond_)) {
    b.createBranch(predicated_block_end_);
    b.setBuildPoint(predicated_block_end_);
    open_predicated_block_ = false;
    predicated_block_cond_ = false;
    predicated_block_end_ = nullptr;
  }

  if (!open_predicated_block_ && instr.is_predicated) {
    Id pred_cond =
        b.createBinOp(spv::Op::OpLogicalEqual, bool_type_, b.createLoad(p0_),
                      b.makeBoolConstant(instr.predicate_condition));
    auto block = &b.makeNewBlock();
    open_predicated_block_ = true;
    predicated_block_cond_ = instr.predicate_condition;
    predicated_block_end_ = &b.makeNewBlock();

    b.createSelectionMerge(predicated_block_end_,
                           spv::SelectionControlMaskNone);
    b.createConditionalBranch(pred_cond, block, predicated_block_end_);
    b.setBuildPoint(block);
  }

  // TODO: If we have identical operands, reuse previous one.
  Id sources[3] = {0};
  Id dest = b.makeFloatConstant(0);
  for (size_t i = 0, x = 0; i < instr.operand_count; i++) {
    auto src = LoadFromOperand(instr.operands[i]);

    // Pull components out of the vector operands and use them as sources.
    if (instr.operands[i].component_count > 1) {
      for (int j = 0; j < instr.operands[i].component_count; j++) {
        sources[x++] = b.createCompositeExtract(src, float_type_, j);
      }
    } else {
      sources[x++] = src;
    }
  }

  bool close_predicated_block = false;
  switch (instr.scalar_opcode) {
    case AluScalarOpcode::kAdds:
    case AluScalarOpcode::kAddsc0:
    case AluScalarOpcode::kAddsc1: {
      // dest = src0 + src1
      dest =
          b.createBinOp(spv::Op::OpFAdd, float_type_, sources[0], sources[1]);
    } break;

    case AluScalarOpcode::kAddsPrev: {
      // dest = src0 + ps
      dest = b.createBinOp(spv::Op::OpFAdd, float_type_, sources[0],
                           b.createLoad(ps_));
    } break;

    case AluScalarOpcode::kCos: {
      // dest = cos(src0)
      dest = CreateGlslStd450InstructionCall(spv::NoPrecision, float_type_,
                                             GLSLstd450::kCos, {sources[0]});
    } break;

    case AluScalarOpcode::kExp: {
      dest = CreateGlslStd450InstructionCall(spv::NoPrecision, float_type_,
                                             GLSLstd450::kExp2, {sources[0]});
    } break;

    case AluScalarOpcode::kFloors: {
      dest = CreateGlslStd450InstructionCall(spv::NoPrecision, float_type_,
                                             GLSLstd450::kFloor, {sources[0]});
    } break;

    case AluScalarOpcode::kFrcs: {
      dest = CreateGlslStd450InstructionCall(spv::NoPrecision, float_type_,
                                             GLSLstd450::kFract, {sources[0]});
    } break;

    case AluScalarOpcode::kKillsEq: {
      auto continue_block = &b.makeNewBlock();
      auto kill_block = &b.makeNewBlock();
      auto cond = b.createBinOp(spv::Op::OpFOrdEqual, bool_type_, sources[0],
                                b.makeFloatConstant(0.f));
      b.createConditionalBranch(cond, kill_block, continue_block);

      b.setBuildPoint(kill_block);
      b.createNoResultOp(spv::Op::OpKill);

      b.setBuildPoint(continue_block);
      dest = b.makeFloatConstant(0.f);
    } break;

    case AluScalarOpcode::kKillsGe: {
      auto continue_block = &b.makeNewBlock();
      auto kill_block = &b.makeNewBlock();
      auto cond = b.createBinOp(spv::Op::OpFOrdGreaterThanEqual, bool_type_,
                                sources[0], b.makeFloatConstant(0.f));
      b.createConditionalBranch(cond, kill_block, continue_block);

      b.setBuildPoint(kill_block);
      b.createNoResultOp(spv::Op::OpKill);

      b.setBuildPoint(continue_block);
      dest = b.makeFloatConstant(0.f);
    } break;

    case AluScalarOpcode::kKillsGt: {
      auto continue_block = &b.makeNewBlock();
      auto kill_block = &b.makeNewBlock();
      auto cond = b.createBinOp(spv::Op::OpFOrdGreaterThan, bool_type_,
                                sources[0], b.makeFloatConstant(0.f));
      b.createConditionalBranch(cond, kill_block, continue_block);

      b.setBuildPoint(kill_block);
      b.createNoResultOp(spv::Op::OpKill);

      b.setBuildPoint(continue_block);
      dest = b.makeFloatConstant(0.f);
    } break;

    case AluScalarOpcode::kKillsNe: {
      auto continue_block = &b.makeNewBlock();
      auto kill_block = &b.makeNewBlock();
      auto cond = b.createBinOp(spv::Op::OpFOrdNotEqual, bool_type_, sources[0],
                                b.makeFloatConstant(0.f));
      b.createConditionalBranch(cond, kill_block, continue_block);

      b.setBuildPoint(kill_block);
      b.createNoResultOp(spv::Op::OpKill);

      b.setBuildPoint(continue_block);
      dest = b.makeFloatConstant(0.f);
    } break;

    case AluScalarOpcode::kKillsOne: {
      auto continue_block = &b.makeNewBlock();
      auto kill_block = &b.makeNewBlock();
      auto cond = b.createBinOp(spv::Op::OpFOrdEqual, bool_type_, sources[0],
                                b.makeFloatConstant(1.f));
      b.createConditionalBranch(cond, kill_block, continue_block);

      b.setBuildPoint(kill_block);
      b.createNoResultOp(spv::Op::OpKill);

      b.setBuildPoint(continue_block);
      dest = b.makeFloatConstant(0.f);
    } break;

    case AluScalarOpcode::kLogc: {
      auto t = CreateGlslStd450InstructionCall(
          spv::NoPrecision, float_type_, spv::GLSLstd450::kLog2, {sources[0]});

      // FIXME: We don't check to see if t == -INF, we just check for INF
      auto c = b.createUnaryOp(spv::Op::OpIsInf, bool_type_, t);
      dest = b.createTriOp(spv::Op::OpSelect, float_type_, c,
                           b.makeFloatConstant(-FLT_MAX), t);
    } break;

    case AluScalarOpcode::kLog: {
      dest = CreateGlslStd450InstructionCall(
          spv::NoPrecision, float_type_, spv::GLSLstd450::kLog2, {sources[0]});
    } break;

    case AluScalarOpcode::kMaxAsf: {
      auto addr =
          b.createUnaryOp(spv::Op::OpConvertFToS, int_type_, sources[0]);
      addr = CreateGlslStd450InstructionCall(
          spv::NoPrecision, int_type_, spv::GLSLstd450::kSClamp,
          {addr, b.makeIntConstant(-256), b.makeIntConstant(255)});
      b.createStore(addr, a0_);

      // dest = src0 >= src1 ? src0 : src1
      dest = CreateGlslStd450InstructionCall(spv::NoPrecision, float_type_,
                                             spv::GLSLstd450::kFMax,
                                             {sources[0], sources[1]});
    } break;

    case AluScalarOpcode::kMaxAs: {
      // a0 = clamp(floor(src0 + 0.5), -256, 255)
      auto addr = b.createBinOp(spv::Op::OpFAdd, float_type_, sources[0],
                                b.makeFloatConstant(0.5f));
      addr = b.createUnaryOp(spv::Op::OpConvertFToS, int_type_, addr);
      addr = CreateGlslStd450InstructionCall(
          spv::NoPrecision, int_type_, spv::GLSLstd450::kSClamp,
          {addr, b.makeIntConstant(-256), b.makeIntConstant(255)});
      b.createStore(addr, a0_);

      // dest = src0 >= src1 ? src0 : src1
      dest = CreateGlslStd450InstructionCall(spv::NoPrecision, float_type_,
                                             spv::GLSLstd450::kFMax,
                                             {sources[0], sources[1]});
    } break;

    case AluScalarOpcode::kMaxs: {
      if (sources[0] == sources[1]) {
        // mov dst, src
        dest = sources[0];
      }

      // dest = max(src0, src1)
      dest = CreateGlslStd450InstructionCall(spv::NoPrecision, float_type_,
                                             GLSLstd450::kFMax,
                                             {sources[0], sources[1]});
    } break;

    case AluScalarOpcode::kMins: {
      if (sources[0] == sources[1]) {
        // mov dst, src
        dest = sources[0];
      }

      // dest = min(src0, src1)
      dest = CreateGlslStd450InstructionCall(spv::NoPrecision, float_type_,
                                             GLSLstd450::kFMin,
                                             {sources[0], sources[1]});
    } break;

    case AluScalarOpcode::kMuls:
    case AluScalarOpcode::kMulsc0:
    case AluScalarOpcode::kMulsc1: {
      // dest = src0 * src1
      dest =
          b.createBinOp(spv::Op::OpFMul, float_type_, sources[0], sources[1]);
    } break;

    case AluScalarOpcode::kMulsPrev: {
      // dest = src0 * ps
      dest = b.createBinOp(spv::Op::OpFMul, float_type_, sources[0],
                           b.createLoad(ps_));
    } break;

    case AluScalarOpcode::kMulsPrev2: {
      // TODO: Uh... see GLSL translator for impl.
    } break;

    case AluScalarOpcode::kRcpc: {
      dest = b.createBinOp(spv::Op::OpFDiv, float_type_,
                           b.makeFloatConstant(1.f), sources[0]);
      dest = CreateGlslStd450InstructionCall(
          spv::NoPrecision, float_type_, spv::GLSLstd450::kFClamp,
          {dest, b.makeFloatConstant(-FLT_MAX), b.makeFloatConstant(FLT_MAX)});
    } break;

    case AluScalarOpcode::kRcpf: {
      dest = b.createBinOp(spv::Op::OpFDiv, float_type_,
                           b.makeFloatConstant(1.f), sources[0]);
      auto c = b.createUnaryOp(spv::Op::OpIsInf, bool_type_, dest);
      dest = b.createTriOp(spv::Op::OpSelect, float_type_, c,
                           b.makeFloatConstant(0.f), dest);
    } break;

    case AluScalarOpcode::kRcp: {
      // dest = src0 != 0.0 ? 1.0 / src0 : 0.0;
      auto c = b.createBinOp(spv::Op::OpFOrdEqual, bool_type_, sources[0],
                             b.makeFloatConstant(0.f));
      auto d = b.createBinOp(spv::Op::OpFDiv, float_type_,
                             b.makeFloatConstant(1.f), sources[0]);
      dest = b.createTriOp(spv::Op::OpSelect, float_type_, c,
                           b.makeFloatConstant(0.f), d);
    } break;

    case AluScalarOpcode::kRsqc: {
      dest = CreateGlslStd450InstructionCall(spv::NoPrecision, float_type_,
                                             spv::GLSLstd450::kInverseSqrt,
                                             {sources[0]});
      dest = CreateGlslStd450InstructionCall(
          spv::NoPrecision, float_type_, spv::GLSLstd450::kFClamp,
          {dest, b.makeFloatConstant(-FLT_MAX), b.makeFloatConstant(FLT_MAX)});
    } break;

    case AluScalarOpcode::kRsqf: {
      dest = CreateGlslStd450InstructionCall(spv::NoPrecision, float_type_,
                                             spv::GLSLstd450::kInverseSqrt,
                                             {sources[0]});
      auto c1 = b.createUnaryOp(spv::Op::OpIsInf, bool_type_, dest);
      auto c2 = b.createUnaryOp(spv::Op::OpIsNan, bool_type_, dest);
      auto c = b.createBinOp(spv::Op::OpLogicalOr, bool_type_, c1, c2);
      dest = b.createTriOp(spv::Op::OpSelect, float_type_, c,
                           b.makeFloatConstant(0.f), dest);
    } break;

    case AluScalarOpcode::kRsq: {
      // dest = src0 > 0.0 ? inversesqrt(src0) : 0.0;
      auto c = b.createBinOp(spv::Op::OpFOrdLessThanEqual, bool_type_,
                             sources[0], b.makeFloatConstant(0.f));
      auto d = CreateGlslStd450InstructionCall(spv::NoPrecision, float_type_,
                                               spv::GLSLstd450::kInverseSqrt,
                                               {sources[0]});
      dest = b.createTriOp(spv::Op::OpSelect, float_type_, c,
                           b.makeFloatConstant(0.f), d);
    } break;

    case AluScalarOpcode::kSeqs: {
      // dest = src0 == 0.0 ? 1.0 : 0.0;
      auto cond = b.createBinOp(spv::Op::OpFOrdEqual, bool_type_, sources[0],
                                b.makeFloatConstant(0.f));
      dest = b.createTriOp(spv::Op::OpSelect, float_type_, cond,
                           b.makeFloatConstant(1.f), b.makeFloatConstant(0.f));
    } break;

    case AluScalarOpcode::kSges: {
      // dest = src0 >= 0.0 ? 1.0 : 0.0;
      auto cond = b.createBinOp(spv::Op::OpFOrdGreaterThanEqual, bool_type_,
                                sources[0], b.makeFloatConstant(0.f));
      dest = b.createTriOp(spv::Op::OpSelect, float_type_, cond,
                           b.makeFloatConstant(1.f), b.makeFloatConstant(0.f));
    } break;

    case AluScalarOpcode::kSgts: {
      // dest = src0 > 0.0 ? 1.0 : 0.0;
      auto cond = b.createBinOp(spv::Op::OpFOrdGreaterThan, bool_type_,
                                sources[0], b.makeFloatConstant(0.f));
      dest = b.createTriOp(spv::Op::OpSelect, float_type_, cond,
                           b.makeFloatConstant(1.f), b.makeFloatConstant(0.f));
    } break;

    case AluScalarOpcode::kSnes: {
      // dest = src0 != 0.0 ? 1.0 : 0.0;
      auto cond = b.createBinOp(spv::Op::OpFOrdNotEqual, bool_type_, sources[0],
                                b.makeFloatConstant(0.f));
      dest = b.createTriOp(spv::Op::OpSelect, float_type_, cond,
                           b.makeFloatConstant(1.f), b.makeFloatConstant(0.f));
    } break;

    case AluScalarOpcode::kSetpClr: {
      b.createStore(b.makeBoolConstant(false), p0_);
      close_predicated_block = true;
      dest = b.makeFloatConstant(FLT_MAX);
    } break;

    case AluScalarOpcode::kSetpEq: {
      auto cond = b.createBinOp(spv::Op::OpFOrdEqual, bool_type_, sources[0],
                                b.makeFloatConstant(0.f));
      // p0 = cond
      b.createStore(cond, p0_);
      close_predicated_block = true;

      // dest = cond ? 0.f : 1.f;
      dest = b.createTriOp(spv::Op::OpSelect, float_type_, cond,
                           b.makeFloatConstant(0.f), b.makeFloatConstant(1.f));
    } break;

    case AluScalarOpcode::kSetpGe: {
      auto cond = b.createBinOp(spv::Op::OpFOrdGreaterThanEqual, bool_type_,
                                sources[0], b.makeFloatConstant(0.f));
      // p0 = cond
      b.createStore(cond, p0_);
      close_predicated_block = true;

      // dest = cond ? 0.f : 1.f;
      dest = b.createTriOp(spv::Op::OpSelect, float_type_, cond,
                           b.makeFloatConstant(0.f), b.makeFloatConstant(1.f));
    } break;

    case AluScalarOpcode::kSetpGt: {
      auto cond = b.createBinOp(spv::Op::OpFOrdGreaterThan, bool_type_,
                                sources[0], b.makeFloatConstant(0.f));
      // p0 = cond
      b.createStore(cond, p0_);
      close_predicated_block = true;

      // dest = cond ? 0.f : 1.f;
      dest = b.createTriOp(spv::Op::OpSelect, float_type_, cond,
                           b.makeFloatConstant(0.f), b.makeFloatConstant(1.f));
    } break;

    case AluScalarOpcode::kSetpInv: {
      // p0 = src0 == 1.0
      auto cond = b.createBinOp(spv::Op::OpFOrdEqual, bool_type_, sources[0],
                                b.makeFloatConstant(1.f));
      b.createStore(cond, p0_);
      close_predicated_block = true;

      // if (!cond) dest = src0 == 0.0 ? 1.0 : src0;
      auto dst_cond = b.createBinOp(spv::Op::OpFOrdEqual, bool_type_,
                                    sources[0], b.makeFloatConstant(0.f));
      auto dst_false = b.createTriOp(spv::Op::OpSelect, float_type_, dst_cond,
                                     b.makeFloatConstant(1.f), sources[0]);
      dest = b.createTriOp(spv::Op::OpSelect, float_type_, cond,
                           b.makeFloatConstant(0.f), dst_false);
    } break;

    case AluScalarOpcode::kSetpNe: {
      auto cond = b.createBinOp(spv::Op::OpFOrdNotEqual, bool_type_, sources[0],
                                b.makeFloatConstant(0.f));

      // p0 = cond
      b.createStore(cond, p0_);
      close_predicated_block = true;

      // dest = cond ? 0.f : 1.f;
      dest = b.createTriOp(spv::Op::OpSelect, float_type_, cond,
                           b.makeFloatConstant(0.f), b.makeFloatConstant(1.f));
    } break;

    case AluScalarOpcode::kSetpPop: {
      auto src = b.createBinOp(spv::Op::OpFSub, float_type_, sources[0],
                               b.makeFloatConstant(1.f));
      auto c = b.createBinOp(spv::Op::OpFOrdLessThanEqual, bool_type_, src,
                             b.makeFloatConstant(0.f));
      b.createStore(c, p0_);
      close_predicated_block = true;

      dest = CreateGlslStd450InstructionCall(
          spv::NoPrecision, float_type_, GLSLstd450::kFMax,
          {sources[0], b.makeFloatConstant(0.f)});
    } break;

    case AluScalarOpcode::kSetpRstr: {
      auto c = b.createBinOp(spv::Op::OpFOrdEqual, bool_type_, sources[0],
                             b.makeFloatConstant(0.f));
      b.createStore(c, p0_);
      close_predicated_block = true;
      dest = sources[0];
    } break;

    case AluScalarOpcode::kSin: {
      dest = CreateGlslStd450InstructionCall(spv::NoPrecision, float_type_,
                                             GLSLstd450::kSin, {sources[0]});
    } break;

    case AluScalarOpcode::kSqrt: {
      dest = CreateGlslStd450InstructionCall(spv::NoPrecision, float_type_,
                                             GLSLstd450::kSqrt, {sources[0]});
    } break;

    case AluScalarOpcode::kSubs:
    case AluScalarOpcode::kSubsc0:
    case AluScalarOpcode::kSubsc1: {
      dest =
          b.createBinOp(spv::Op::OpFSub, float_type_, sources[0], sources[1]);
    } break;

    case AluScalarOpcode::kSubsPrev: {
      dest = b.createBinOp(spv::Op::OpFSub, float_type_, sources[0],
                           b.createLoad(ps_));
    } break;

    case AluScalarOpcode::kTruncs: {
      dest = CreateGlslStd450InstructionCall(spv::NoPrecision, float_type_,
                                             GLSLstd450::kTrunc, {sources[0]});
    } break;

    default:
      assert_unhandled_case(instr.scalar_opcode);
      break;
  }

  assert_not_zero(dest);
  assert_true(b.getTypeId(dest) == float_type_);
  if (dest) {
    b.createStore(dest, ps_);
    StoreToResult(dest, instr.result);
  }

  if (close_predicated_block && open_predicated_block_) {
    b.createBranch(predicated_block_end_);
    b.setBuildPoint(predicated_block_end_);
    open_predicated_block_ = false;
    predicated_block_cond_ = false;
    predicated_block_end_ = nullptr;
  }
}

Id SpirvShaderTranslator::CreateGlslStd450InstructionCall(
    spv::Decoration precision, Id result_type, GLSLstd450 instruction_ordinal,
    std::vector<Id> args) {
  return builder_->createBuiltinCall(result_type, glsl_std_450_instruction_set_,
                                     static_cast<int>(instruction_ordinal),
                                     args);
}

Id SpirvShaderTranslator::LoadFromOperand(const InstructionOperand& op) {
  auto& b = *builder_;

  Id storage_pointer = 0;
  Id storage_type = vec4_float_type_;
  spv::StorageClass storage_class;
  Id storage_index = 0;             // Storage index at lowest level
  std::vector<Id> storage_offsets;  // Offsets in nested arrays -> storage

  // Out of the 512 constant registers pixel shaders get the last 256.
  uint32_t storage_base = 0;
  if (op.storage_source == InstructionStorageSource::kConstantFloat) {
    storage_base = is_pixel_shader() ? 256 : 0;
  }

  switch (op.storage_addressing_mode) {
    case InstructionStorageAddressingMode::kStatic: {
      storage_index = b.makeUintConstant(storage_base + op.storage_index);
    } break;
    case InstructionStorageAddressingMode::kAddressAbsolute: {
      // storage_index + a0
      storage_index =
          b.createBinOp(spv::Op::OpIAdd, uint_type_, b.createLoad(a0_),
                        b.makeUintConstant(storage_base + op.storage_index));
    } break;
    case InstructionStorageAddressingMode::kAddressRelative: {
      // storage_index + aL.x
      auto idx = b.createCompositeExtract(b.createLoad(aL_), uint_type_, 0);
      storage_index =
          b.createBinOp(spv::Op::OpIAdd, uint_type_, idx,
                        b.makeUintConstant(storage_base + op.storage_index));
    } break;
    default:
      assert_always();
      break;
  }

  switch (op.storage_source) {
    case InstructionStorageSource::kRegister:
      storage_pointer = registers_ptr_;
      storage_class = spv::StorageClass::StorageClassFunction;
      storage_type = vec4_float_type_;
      storage_offsets.push_back(storage_index);
      assert_true(uint32_t(op.storage_index) < register_count());
      break;
    case InstructionStorageSource::kConstantFloat:
      storage_pointer = consts_;
      storage_class = spv::StorageClass::StorageClassUniform;
      storage_type = vec4_float_type_;
      storage_offsets.push_back(b.makeUintConstant(0));
      storage_offsets.push_back(storage_index);
      break;
    case InstructionStorageSource::kVertexFetchConstant:
    case InstructionStorageSource::kTextureFetchConstant:
      // Should not reach this.
      assert_always();
      break;
    default:
      assert_always();
      break;
  }

  if (!storage_pointer) {
    return b.createUndefined(vec4_float_type_);
  }

  storage_pointer =
      b.createAccessChain(storage_class, storage_pointer, storage_offsets);
  auto storage_value = b.createLoad(storage_pointer);
  assert_true(b.getTypeId(storage_value) == vec4_float_type_);

  if (op.component_count == 1) {
    // Don't bother handling constant 0/1 fetches, as they're invalid in scalar
    // opcodes.
    uint32_t index = 0;
    switch (op.components[0]) {
      case SwizzleSource::kX:
        index = 0;
        break;
      case SwizzleSource::kY:
        index = 1;
        break;
      case SwizzleSource::kZ:
        index = 2;
        break;
      case SwizzleSource::kW:
        index = 3;
        break;
      case SwizzleSource::k0:
        assert_always();
        break;
      case SwizzleSource::k1:
        assert_always();
        break;
    }

    storage_value = b.createCompositeExtract(storage_value, float_type_, index);
    storage_type = float_type_;
  }

  if (op.is_absolute_value) {
    storage_value = CreateGlslStd450InstructionCall(
        spv::NoPrecision, storage_type, GLSLstd450::kFAbs, {storage_value});
  }
  if (op.is_negated) {
    storage_value =
        b.createUnaryOp(spv::Op::OpFNegate, storage_type, storage_value);
  }

  // swizzle
  if (op.component_count > 1 && !op.is_standard_swizzle()) {
    std::vector<uint32_t> operands;
    operands.push_back(storage_value);
    operands.push_back(b.makeCompositeConstant(
        vec2_float_type_,
        std::vector<Id>({b.makeFloatConstant(0.f), b.makeFloatConstant(1.f)})));

    // Components start from left and are duplicated rightwards
    // e.g. count = 1, xxxx / count = 2, xyyy ...
    for (int i = 0; i < 4; i++) {
      auto swiz = op.components[i];
      if (i > op.component_count - 1) {
        swiz = op.components[op.component_count - 1];
      }

      switch (swiz) {
        case SwizzleSource::kX:
          operands.push_back(0);
          break;
        case SwizzleSource::kY:
          operands.push_back(1);
          break;
        case SwizzleSource::kZ:
          operands.push_back(2);
          break;
        case SwizzleSource::kW:
          operands.push_back(3);
          break;
        case SwizzleSource::k0:
          operands.push_back(4);
          break;
        case SwizzleSource::k1:
          operands.push_back(5);
          break;
      }
    }

    storage_value =
        b.createOp(spv::Op::OpVectorShuffle, storage_type, operands);
  }

  return storage_value;
}

void SpirvShaderTranslator::StoreToResult(Id source_value_id,
                                          const InstructionResult& result) {
  auto& b = *builder_;

  if (result.storage_target == InstructionStorageTarget::kNone) {
    // No-op?
    return;
  }

  if (!result.has_any_writes()) {
    return;
  }

  Id storage_pointer = 0;
  Id storage_type = vec4_float_type_;
  spv::StorageClass storage_class;
  Id storage_index = 0;             // Storage index at lowest level
  std::vector<Id> storage_offsets;  // Offsets in nested arrays -> storage

  switch (result.storage_addressing_mode) {
    case InstructionStorageAddressingMode::kStatic: {
      storage_index = b.makeUintConstant(result.storage_index);
    } break;
    case InstructionStorageAddressingMode::kAddressAbsolute: {
      // storage_index + a0
      storage_index =
          b.createBinOp(spv::Op::OpIAdd, uint_type_, b.createLoad(a0_),
                        b.makeUintConstant(result.storage_index));
    } break;
    case InstructionStorageAddressingMode::kAddressRelative: {
      // storage_index + aL.x
      auto idx = b.createCompositeExtract(b.createLoad(aL_), uint_type_, 0);
      storage_index = b.createBinOp(spv::Op::OpIAdd, uint_type_, idx,
                                    b.makeUintConstant(result.storage_index));
    } break;
    default:
      assert_always();
      return;
  }

  bool storage_array;
  switch (result.storage_target) {
    case InstructionStorageTarget::kRegister:
      storage_pointer = registers_ptr_;
      storage_class = spv::StorageClass::StorageClassFunction;
      storage_type = vec4_float_type_;
      storage_offsets.push_back(storage_index);
      storage_array = true;
      assert_true(uint32_t(result.storage_index) < register_count());
      break;
    case InstructionStorageTarget::kInterpolant:
      assert_true(is_vertex_shader());
      storage_pointer = interpolators_;
      storage_class = spv::StorageClass::StorageClassOutput;
      storage_type = vec4_float_type_;
      storage_offsets.push_back(storage_index);
      storage_array = true;
      break;
    case InstructionStorageTarget::kPosition:
      assert_true(is_vertex_shader());
      assert_not_zero(pos_);
      storage_pointer = pos_;
      storage_class = spv::StorageClass::StorageClassOutput;
      storage_type = vec4_float_type_;
      storage_offsets.push_back(0);
      storage_array = false;
      break;
    case InstructionStorageTarget::kPointSize:
      assert_true(is_vertex_shader());
      storage_pointer = point_size_;
      storage_class = spv::StorageClass::StorageClassOutput;
      storage_type = float_type_;
      storage_offsets.push_back(0);
      storage_array = false;
      break;
    case InstructionStorageTarget::kColorTarget:
      assert_true(is_pixel_shader());
      assert_not_zero(frag_outputs_);
      storage_pointer = frag_outputs_;
      storage_class = spv::StorageClass::StorageClassOutput;
      storage_type = vec4_float_type_;
      storage_offsets.push_back(storage_index);
      storage_array = true;
      break;
    case InstructionStorageTarget::kDepth:
      assert_true(is_pixel_shader());
      storage_pointer = frag_depth_;
      storage_class = spv::StorageClass::StorageClassOutput;
      storage_type = float_type_;
      storage_offsets.push_back(0);
      storage_array = false;
      break;
    case InstructionStorageTarget::kNone:
      assert_always();
      break;
    default:
      assert_unhandled_case(result.storage_target);
      break;
  }

  if (!storage_pointer) {
    assert_always();
    return;
  }

  if (storage_array) {
    storage_pointer =
        b.createAccessChain(storage_class, storage_pointer, storage_offsets);
  }

  bool source_is_scalar = b.isScalar(source_value_id);
  bool storage_is_scalar = b.isScalarType(b.getDerefTypeId(storage_pointer));
  spv::Id source_type = b.getTypeId(source_value_id);

  // Only load from storage if we need it later.
  Id storage_value = 0;
  if ((source_is_scalar && !storage_is_scalar) || !result.has_all_writes()) {
    storage_value = b.createLoad(storage_pointer);
  }

  // Clamp the input value.
  if (result.is_clamped) {
    source_value_id = CreateGlslStd450InstructionCall(
        spv::NoPrecision, source_type, spv::GLSLstd450::kFClamp,
        {source_value_id,
         b.smearScalar(spv::NoPrecision, b.makeFloatConstant(0.f), source_type),
         b.smearScalar(spv::NoPrecision, b.makeFloatConstant(1.f),
                       source_type)});
  }

  // destination swizzle
  if (!result.is_standard_swizzle() && !source_is_scalar) {
    std::vector<uint32_t> operands;
    operands.push_back(source_value_id);
    operands.push_back(b.makeCompositeConstant(
        vec2_float_type_,
        std::vector<Id>({b.makeFloatConstant(0.f), b.makeFloatConstant(1.f)})));

    // Components start from left and are duplicated rightwards
    // e.g. count = 1, xxxx / count = 2, xyyy ...
    uint32_t source_components = b.getNumComponents(source_value_id);
    for (int i = 0; i < 4; i++) {
      if (!result.write_mask[i]) {
        // Undefined / don't care.
        operands.push_back(0);
        continue;
      }

      auto swiz = result.components[i];
      switch (swiz) {
        case SwizzleSource::kX:
          operands.push_back(0);
          break;
        case SwizzleSource::kY:
          operands.push_back(1);
          break;
        case SwizzleSource::kZ:
          operands.push_back(2);
          break;
        case SwizzleSource::kW:
          operands.push_back(3);
          break;
        case SwizzleSource::k0:
          operands.push_back(source_components + 0);
          break;
        case SwizzleSource::k1:
          operands.push_back(source_components + 1);
          break;
      }
    }

    source_value_id =
        b.createOp(spv::Op::OpVectorShuffle, vec4_float_type_, operands);
  }

  // write mask
  if (!result.has_all_writes() && !source_is_scalar && !storage_is_scalar) {
    std::vector<uint32_t> operands;
    operands.push_back(source_value_id);
    operands.push_back(storage_value);

    for (int i = 0; i < b.getNumTypeComponents(storage_type); i++) {
      operands.push_back(
          result.write_mask[i] ? i : b.getNumComponents(source_value_id) + i);
    }

    source_value_id =
        b.createOp(spv::Op::OpVectorShuffle, storage_type, operands);
  } else if (source_is_scalar && !storage_is_scalar) {
    assert_true(result.num_writes() >= 1);

    if (result.has_all_writes()) {
      source_value_id =
          b.smearScalar(spv::NoPrecision, source_value_id, storage_type);
    } else {
      // Find first enabled component
      uint32_t index = 0;
      for (uint32_t i = 0; i < 4; i++) {
        if (result.write_mask[i]) {
          index = i;
          break;
        }
      }
      source_value_id = b.createCompositeInsert(source_value_id, storage_value,
                                                storage_type, index);
    }
  } else if (!source_is_scalar && storage_is_scalar) {
    // Num writes /needs/ to be 1, and let's assume it's the first element.
    assert_true(result.num_writes() == 1);

    for (uint32_t i = 0; i < 4; i++) {
      if (result.write_mask[i]) {
        source_value_id =
            b.createCompositeExtract(source_value_id, storage_type, 0);
        break;
      }
    }
  }

  // Perform store into the pointer.
  assert_true(b.getNumComponents(source_value_id) ==
              b.getNumTypeComponents(storage_type));

  assert_true(b.getTypeId(source_value_id) ==
              b.getDerefTypeId(storage_pointer));
  b.createStore(source_value_id, storage_pointer);
}

}  // namespace gpu
}  // namespace xe
