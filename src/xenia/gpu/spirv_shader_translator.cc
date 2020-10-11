/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/spirv_shader_translator.h"

#include <memory>
#include <vector>

#include "third_party/glslang/SPIRV/GLSL.std.450.h"

namespace xe {
namespace gpu {

SpirvShaderTranslator::SpirvShaderTranslator(bool supports_clip_distance,
                                             bool supports_cull_distance)
    : supports_clip_distance_(supports_clip_distance),
      supports_cull_distance_(supports_cull_distance) {}

void SpirvShaderTranslator::Reset() {
  ShaderTranslator::Reset();

  builder_.reset();
}

void SpirvShaderTranslator::StartTranslation() {
  // TODO(Triang3l): Once tool ID (likely 26) is registered in SPIRV-Headers,
  // use it instead.
  // TODO(Triang3l): Logger.
  builder_ = std::make_unique<spv::Builder>(0x10000, 0xFFFF0001, nullptr);

  builder_->addCapability(IsSpirvTessEvalShader() ? spv::CapabilityTessellation
                                                  : spv::CapabilityShader);
  ext_inst_glsl_std_450_ = builder_->import("GLSL.std.450");
  builder_->setMemoryModel(spv::AddressingModelLogical,
                           spv::MemoryModelGLSL450);
  builder_->setSource(spv::SourceLanguageUnknown, 0);

  type_void_ = builder_->makeVoidType();
  type_float_ = builder_->makeFloatType(32);
  type_float2_ = builder_->makeVectorType(type_float_, 2);
  type_float3_ = builder_->makeVectorType(type_float_, 3);
  type_float4_ = builder_->makeVectorType(type_float_, 4);
  type_int_ = builder_->makeIntType(32);

  if (IsSpirvVertexOrTessEvalShader()) {
    StartVertexOrTessEvalShaderBeforeMain();
  }

  // Begin the main function.
  std::vector<spv::Id> main_param_types;
  std::vector<std::vector<spv::Decoration>> main_precisions;
  spv::Block* main_entry;
  builder_->makeFunctionEntry(spv::NoPrecision, type_void_, "main",
                              main_param_types, main_precisions, &main_entry);

  // Begin ucode translation.
  if (register_count()) {
    var_main_registers_ = builder_->createVariable(
        spv::NoPrecision, spv::StorageClassFunction,
        builder_->makeArrayType(
            type_float4_, builder_->makeUintConstant(register_count()), 0),
        "xe_r");
  }
}

std::vector<uint8_t> SpirvShaderTranslator::CompleteTranslation() {
  if (IsSpirvVertexOrTessEvalShader()) {
    CompleteVertexOrTessEvalShaderInMain();
  }

  // End the main function..
  builder_->leaveFunction();

  // TODO(Triang3l): Avoid copy?
  std::vector<unsigned int> module_uints;
  builder_->dump(module_uints);
  std::vector<uint8_t> module_bytes;
  module_bytes.reserve(sizeof(unsigned int) * module_uints.size());
  module_bytes.insert(module_bytes.cend(),
                      reinterpret_cast<const uint8_t*>(module_uints.data()),
                      reinterpret_cast<const uint8_t*>(module_uints.data()) +
                          sizeof(unsigned int) * module_uints.size());
  return module_bytes;
}

void SpirvShaderTranslator::StartVertexOrTessEvalShaderBeforeMain() {
  // Create the inputs.
  if (IsSpirvTessEvalShader()) {
    input_vertex_index_ = builder_->createVariable(
        spv::NoPrecision, spv::StorageClassInput, type_int_, "gl_PrimitiveID");
    builder_->addDecoration(input_vertex_index_, spv::DecorationBuiltIn,
                            spv::BuiltInPrimitiveId);
  } else {
    input_primitive_id_ = builder_->createVariable(
        spv::NoPrecision, spv::StorageClassInput, type_int_, "gl_VertexIndex");
    builder_->addDecoration(input_primitive_id_, spv::DecorationBuiltIn,
                            spv::BuiltInVertexIndex);
  }

  // Create the entire GLSL 4.50 gl_PerVertex output similar to what glslang
  // does. Members (like gl_PointSize) don't need to be used, and also
  // ClipDistance and CullDistance may exist even if the device doesn't support
  // them, as long as the capabilities aren't enabled, and nothing is stored to
  // them.
  if (supports_clip_distance_) {
    builder_->addCapability(spv::CapabilityClipDistance);
  }
  if (supports_cull_distance_) {
    builder_->addCapability(spv::CapabilityCullDistance);
  }
  std::vector<spv::Id> struct_per_vertex_members;
  struct_per_vertex_members.reserve(kOutputPerVertexMemberCount);
  struct_per_vertex_members.push_back(type_float4_);
  struct_per_vertex_members.push_back(type_float_);
  // TODO(Triang3l): Specialization constant for ucp_cull_only_ena, for 6 + 1
  // or 1 + 7 array sizes.
  struct_per_vertex_members.push_back(builder_->makeArrayType(
      type_float_, builder_->makeUintConstant(supports_clip_distance_ ? 6 : 1),
      0));
  struct_per_vertex_members.push_back(
      builder_->makeArrayType(type_float_, builder_->makeUintConstant(1), 0));
  spv::Id type_struct_per_vertex =
      builder_->makeStructType(struct_per_vertex_members, "gl_PerVertex");
  builder_->addMemberDecoration(type_struct_per_vertex,
                                kOutputPerVertexMemberPosition,
                                spv::DecorationBuiltIn, spv::BuiltInPosition);
  builder_->addMemberDecoration(type_struct_per_vertex,
                                kOutputPerVertexMemberPointSize,
                                spv::DecorationBuiltIn, spv::BuiltInPointSize);
  builder_->addMemberDecoration(
      type_struct_per_vertex, kOutputPerVertexMemberClipDistance,
      spv::DecorationBuiltIn, spv::BuiltInClipDistance);
  builder_->addMemberDecoration(
      type_struct_per_vertex, kOutputPerVertexMemberCullDistance,
      spv::DecorationBuiltIn, spv::BuiltInCullDistance);
  builder_->addDecoration(type_struct_per_vertex, spv::DecorationBlock);
  output_per_vertex_ =
      builder_->createVariable(spv::NoPrecision, spv::StorageClassOutput,
                               type_struct_per_vertex, "xe_out_gl_PerVertex");
}

void SpirvShaderTranslator::CompleteVertexOrTessEvalShaderInMain() {}

}  // namespace gpu
}  // namespace xe
