/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/spirv_shader_translator.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "third_party/fmt/include/fmt/format.h"
#include "third_party/glslang/SPIRV/GLSL.std.450.h"
#include "xenia/base/assert.h"
#include "xenia/base/math.h"
#include "xenia/base/string_buffer.h"
#include "xenia/gpu/spirv_shader.h"

namespace xe {
namespace gpu {

SpirvShaderTranslator::Features::Features(bool all)
    : spirv_version(all ? spv::Spv_1_5 : spv::Spv_1_0),
      max_storage_buffer_range(all ? UINT32_MAX : (128 * 1024 * 1024)),
      clip_distance(all),
      cull_distance(all),
      demote_to_helper_invocation(all),
      fragment_shader_sample_interlock(all),
      full_draw_index_uint32(all),
      image_view_format_swizzle(all),
      signed_zero_inf_nan_preserve_float32(all),
      denorm_flush_to_zero_float32(all) {}

SpirvShaderTranslator::Features::Features(
    const ui::vulkan::VulkanProvider& provider)
    : max_storage_buffer_range(
          provider.device_properties().limits.maxStorageBufferRange),
      clip_distance(provider.device_features().shaderClipDistance),
      cull_distance(provider.device_features().shaderCullDistance),
      demote_to_helper_invocation(
          provider.device_extensions().ext_shader_demote_to_helper_invocation &&
          provider.device_shader_demote_to_helper_invocation_features()
              .shaderDemoteToHelperInvocation),
      fragment_shader_sample_interlock(
          provider.device_extensions().ext_fragment_shader_interlock &&
          provider.device_fragment_shader_interlock_features()
              .fragmentShaderSampleInterlock),
      full_draw_index_uint32(provider.device_features().fullDrawIndexUint32) {
  uint32_t device_version = provider.device_properties().apiVersion;
  const ui::vulkan::VulkanProvider::DeviceExtensions& device_extensions =
      provider.device_extensions();
  if (device_version >= VK_MAKE_VERSION(1, 2, 0)) {
    spirv_version = spv::Spv_1_5;
  } else if (device_extensions.khr_spirv_1_4) {
    spirv_version = spv::Spv_1_4;
  } else if (device_version >= VK_MAKE_VERSION(1, 1, 0)) {
    spirv_version = spv::Spv_1_3;
  } else {
    spirv_version = spv::Spv_1_0;
  }
  const VkPhysicalDevicePortabilitySubsetFeaturesKHR*
      device_portability_subset_features =
          provider.device_portability_subset_features();
  if (device_portability_subset_features) {
    image_view_format_swizzle =
        bool(device_portability_subset_features->imageViewFormatSwizzle);
  } else {
    image_view_format_swizzle = true;
  }
  if (spirv_version >= spv::Spv_1_4 ||
      device_extensions.khr_shader_float_controls) {
    const VkPhysicalDeviceFloatControlsPropertiesKHR&
        float_controls_properties = provider.device_float_controls_properties();
    signed_zero_inf_nan_preserve_float32 =
        bool(float_controls_properties.shaderSignedZeroInfNanPreserveFloat32);
    denorm_flush_to_zero_float32 =
        bool(float_controls_properties.shaderDenormFlushToZeroFloat32);
  } else {
    signed_zero_inf_nan_preserve_float32 = false;
    denorm_flush_to_zero_float32 = false;
  }
}

uint64_t SpirvShaderTranslator::GetDefaultVertexShaderModification(
    uint32_t dynamic_addressable_register_count,
    Shader::HostVertexShaderType host_vertex_shader_type) const {
  Modification shader_modification;
  shader_modification.vertex.dynamic_addressable_register_count =
      dynamic_addressable_register_count;
  shader_modification.vertex.host_vertex_shader_type = host_vertex_shader_type;
  return shader_modification.value;
}

uint64_t SpirvShaderTranslator::GetDefaultPixelShaderModification(
    uint32_t dynamic_addressable_register_count) const {
  Modification shader_modification;
  shader_modification.pixel.dynamic_addressable_register_count =
      dynamic_addressable_register_count;
  return shader_modification.value;
}

std::vector<uint8_t> SpirvShaderTranslator::CreateDepthOnlyFragmentShader() {
  is_depth_only_fragment_shader_ = true;
  // TODO(Triang3l): Handle in a nicer way (is_depth_only_fragment_shader_ is a
  // leftover from when a Shader object wasn't used during translation).
  Shader shader(xenos::ShaderType::kPixel, 0, nullptr, 0);
  StringBuffer instruction_disassembly_buffer;
  shader.AnalyzeUcode(instruction_disassembly_buffer);
  Shader::Translation& translation = *shader.GetOrCreateTranslation(0);
  TranslateAnalyzedShader(translation);
  is_depth_only_fragment_shader_ = false;
  return translation.translated_binary();
}

void SpirvShaderTranslator::Reset() {
  ShaderTranslator::Reset();

  builder_.reset();

  uniform_float_constants_ = spv::NoResult;

  input_point_coordinates_ = spv::NoResult;
  input_fragment_coordinates_ = spv::NoResult;
  input_front_facing_ = spv::NoResult;
  input_sample_mask_ = spv::NoResult;
  std::fill(input_output_interpolators_.begin(),
            input_output_interpolators_.end(), spv::NoResult);
  output_point_coordinates_ = spv::NoResult;
  output_point_size_ = spv::NoResult;

  sampler_bindings_.clear();
  texture_bindings_.clear();

  main_interface_.clear();
  var_main_registers_ = spv::NoResult;
  var_main_point_size_edge_flag_kill_vertex_ = spv::NoResult;
  var_main_kill_pixel_ = spv::NoResult;
  var_main_fsi_color_written_ = spv::NoResult;

  main_switch_op_.reset();
  main_switch_next_pc_phi_operands_.clear();

  cf_exec_conditional_merge_ = nullptr;
  cf_instruction_predicate_merge_ = nullptr;
}

uint32_t SpirvShaderTranslator::GetModificationRegisterCount() const {
  Modification modification = GetSpirvShaderModification();
  return is_vertex_shader()
             ? modification.vertex.dynamic_addressable_register_count
             : modification.pixel.dynamic_addressable_register_count;
}

void SpirvShaderTranslator::StartTranslation() {
  // TODO(Triang3l): Logger.
  builder_ = std::make_unique<SpirvBuilder>(
      features_.spirv_version, (kSpirvMagicToolId << 16) | 1, nullptr);

  builder_->addCapability(IsSpirvTessEvalShader() ? spv::CapabilityTessellation
                                                  : spv::CapabilityShader);
  if (features_.spirv_version < spv::Spv_1_4) {
    if (features_.signed_zero_inf_nan_preserve_float32 ||
        features_.denorm_flush_to_zero_float32) {
      builder_->addExtension("SPV_KHR_float_controls");
    }
  }
  ext_inst_glsl_std_450_ = builder_->import("GLSL.std.450");
  builder_->setMemoryModel(spv::AddressingModelLogical,
                           spv::MemoryModelGLSL450);
  builder_->setSource(spv::SourceLanguageUnknown, 0);

  type_void_ = builder_->makeVoidType();
  type_bool_ = builder_->makeBoolType();
  type_bool2_ = builder_->makeVectorType(type_bool_, 2);
  type_bool3_ = builder_->makeVectorType(type_bool_, 3);
  type_bool4_ = builder_->makeVectorType(type_bool_, 4);
  type_int_ = builder_->makeIntType(32);
  type_int2_ = builder_->makeVectorType(type_int_, 2);
  type_int3_ = builder_->makeVectorType(type_int_, 3);
  type_int4_ = builder_->makeVectorType(type_int_, 4);
  type_uint_ = builder_->makeUintType(32);
  type_uint2_ = builder_->makeVectorType(type_uint_, 2);
  type_uint3_ = builder_->makeVectorType(type_uint_, 3);
  type_uint4_ = builder_->makeVectorType(type_uint_, 4);
  type_float_ = builder_->makeFloatType(32);
  type_float2_ = builder_->makeVectorType(type_float_, 2);
  type_float3_ = builder_->makeVectorType(type_float_, 3);
  type_float4_ = builder_->makeVectorType(type_float_, 4);

  const_int_0_ = builder_->makeIntConstant(0);
  id_vector_temp_.clear();
  for (uint32_t i = 0; i < 4; ++i) {
    id_vector_temp_.push_back(const_int_0_);
  }
  const_int4_0_ = builder_->makeCompositeConstant(type_int4_, id_vector_temp_);
  const_uint_0_ = builder_->makeUintConstant(0);
  id_vector_temp_.clear();
  for (uint32_t i = 0; i < 4; ++i) {
    id_vector_temp_.push_back(const_uint_0_);
  }
  const_uint4_0_ =
      builder_->makeCompositeConstant(type_uint4_, id_vector_temp_);
  const_float_0_ = builder_->makeFloatConstant(0.0f);
  id_vector_temp_.clear();
  id_vector_temp_.push_back(const_float_0_);
  for (uint32_t i = 1; i < 4; ++i) {
    id_vector_temp_.push_back(const_float_0_);
    const_float_vectors_0_[i] = builder_->makeCompositeConstant(
        type_float_vectors_[i], id_vector_temp_);
  }
  const_float_1_ = builder_->makeFloatConstant(1.0f);
  id_vector_temp_.clear();
  id_vector_temp_.push_back(const_float_1_);
  for (uint32_t i = 1; i < 4; ++i) {
    id_vector_temp_.push_back(const_float_1_);
    const_float_vectors_1_[i] = builder_->makeCompositeConstant(
        type_float_vectors_[i], id_vector_temp_);
  }
  id_vector_temp_.clear();
  id_vector_temp_.push_back(const_float_0_);
  id_vector_temp_.push_back(const_float_1_);
  const_float2_0_1_ =
      builder_->makeCompositeConstant(type_float2_, id_vector_temp_);

  // Common uniform buffer - system constants.
  struct SystemConstant {
    const char* name;
    size_t offset;
    spv::Id type;
  };
  spv::Id type_float4_array_4 = builder_->makeArrayType(
      type_float4_, builder_->makeUintConstant(4), sizeof(float) * 4);
  builder_->addDecoration(type_float4_array_4, spv::DecorationArrayStride,
                          sizeof(float) * 4);
  spv::Id type_uint4_array_2 = builder_->makeArrayType(
      type_uint4_, builder_->makeUintConstant(2), sizeof(uint32_t) * 4);
  builder_->addDecoration(type_uint4_array_2, spv::DecorationArrayStride,
                          sizeof(uint32_t) * 4);
  spv::Id type_uint4_array_4 = builder_->makeArrayType(
      type_uint4_, builder_->makeUintConstant(4), sizeof(uint32_t) * 4);
  builder_->addDecoration(type_uint4_array_4, spv::DecorationArrayStride,
                          sizeof(uint32_t) * 4);
  const SystemConstant system_constants[] = {
      {"flags", offsetof(SystemConstants, flags), type_uint_},
      {"vertex_index_load_address",
       offsetof(SystemConstants, vertex_index_load_address), type_uint_},
      {"vertex_index_endian", offsetof(SystemConstants, vertex_index_endian),
       type_uint_},
      {"vertex_base_index", offsetof(SystemConstants, vertex_base_index),
       type_int_},
      {"ndc_scale", offsetof(SystemConstants, ndc_scale), type_float3_},
      {"point_vertex_diameter_min",
       offsetof(SystemConstants, point_vertex_diameter_min), type_float_},
      {"ndc_offset", offsetof(SystemConstants, ndc_offset), type_float3_},
      {"point_vertex_diameter_max",
       offsetof(SystemConstants, point_vertex_diameter_max), type_float_},
      {"point_constant_diameter",
       offsetof(SystemConstants, point_constant_diameter), type_float2_},
      {"point_screen_diameter_to_ndc_radius",
       offsetof(SystemConstants, point_screen_diameter_to_ndc_radius),
       type_float2_},
      {"texture_swizzled_signs",
       offsetof(SystemConstants, texture_swizzled_signs), type_uint4_array_2},
      {"texture_swizzles", offsetof(SystemConstants, texture_swizzles),
       type_uint4_array_4},
      {"alpha_test_reference", offsetof(SystemConstants, alpha_test_reference),
       type_float_},
      {"edram_32bpp_tile_pitch_dwords_scaled",
       offsetof(SystemConstants, edram_32bpp_tile_pitch_dwords_scaled),
       type_uint_},
      {"edram_depth_base_dwords_scaled",
       offsetof(SystemConstants, edram_depth_base_dwords_scaled), type_uint_},
      {"color_exp_bias", offsetof(SystemConstants, color_exp_bias),
       type_float4_},
      {"edram_poly_offset_front_scale",
       offsetof(SystemConstants, edram_poly_offset_front_scale), type_float_},
      {"edram_poly_offset_back_scale",
       offsetof(SystemConstants, edram_poly_offset_back_scale), type_float_},
      {"edram_poly_offset_front_offset",
       offsetof(SystemConstants, edram_poly_offset_front_offset), type_float_},
      {"edram_poly_offset_back_offset",
       offsetof(SystemConstants, edram_poly_offset_back_offset), type_float_},
      {"edram_stencil_front", offsetof(SystemConstants, edram_stencil_front),
       type_uint2_},
      {"edram_stencil_back", offsetof(SystemConstants, edram_stencil_back),
       type_uint2_},
      {"edram_rt_base_dwords_scaled",
       offsetof(SystemConstants, edram_rt_base_dwords_scaled), type_uint4_},
      {"edram_rt_format_flags",
       offsetof(SystemConstants, edram_rt_format_flags), type_uint4_},
      {"edram_rt_blend_factors_ops",
       offsetof(SystemConstants, edram_rt_blend_factors_ops), type_uint4_},
      {"edram_rt_keep_mask", offsetof(SystemConstants, edram_rt_keep_mask),
       type_uint4_array_2},
      {"edram_rt_clamp", offsetof(SystemConstants, edram_rt_clamp),
       type_float4_array_4},
      {"edram_blend_constant", offsetof(SystemConstants, edram_blend_constant),
       type_float4_},
  };
  id_vector_temp_.clear();
  id_vector_temp_.reserve(xe::countof(system_constants));
  for (size_t i = 0; i < xe::countof(system_constants); ++i) {
    id_vector_temp_.push_back(system_constants[i].type);
  }
  spv::Id type_system_constants =
      builder_->makeStructType(id_vector_temp_, "XeSystemConstants");
  for (size_t i = 0; i < xe::countof(system_constants); ++i) {
    const SystemConstant& system_constant = system_constants[i];
    builder_->addMemberName(type_system_constants, static_cast<unsigned int>(i),
                            system_constant.name);
    builder_->addMemberDecoration(
        type_system_constants, static_cast<unsigned int>(i),
        spv::DecorationOffset, int(system_constant.offset));
  }
  builder_->addDecoration(type_system_constants, spv::DecorationBlock);
  uniform_system_constants_ = builder_->createVariable(
      spv::NoPrecision, spv::StorageClassUniform, type_system_constants,
      "xe_uniform_system_constants");
  builder_->addDecoration(uniform_system_constants_,
                          spv::DecorationDescriptorSet,
                          int(kDescriptorSetConstants));
  builder_->addDecoration(uniform_system_constants_, spv::DecorationBinding,
                          int(kConstantBufferSystem));
  if (features_.spirv_version >= spv::Spv_1_4) {
    main_interface_.push_back(uniform_system_constants_);
  }

  if (!is_depth_only_fragment_shader_) {
    // Common uniform buffer - float constants.
    uint32_t float_constant_count =
        current_shader().constant_register_map().float_count;
    if (float_constant_count) {
      id_vector_temp_.clear();
      id_vector_temp_.push_back(builder_->makeArrayType(
          type_float4_, builder_->makeUintConstant(float_constant_count),
          sizeof(float) * 4));
      // Currently (as of October 24, 2020) makeArrayType only uses the stride
      // to check if deduplication can be done - the array stride decoration
      // needs to be applied explicitly.
      builder_->addDecoration(id_vector_temp_.back(),
                              spv::DecorationArrayStride, sizeof(float) * 4);
      spv::Id type_float_constants =
          builder_->makeStructType(id_vector_temp_, "XeFloatConstants");
      builder_->addMemberName(type_float_constants, 0, "float_constants");
      builder_->addMemberDecoration(type_float_constants, 0,
                                    spv::DecorationOffset, 0);
      builder_->addDecoration(type_float_constants, spv::DecorationBlock);
      uniform_float_constants_ = builder_->createVariable(
          spv::NoPrecision, spv::StorageClassUniform, type_float_constants,
          "xe_uniform_float_constants");
      builder_->addDecoration(uniform_float_constants_,
                              spv::DecorationDescriptorSet,
                              int(kDescriptorSetConstants));
      builder_->addDecoration(
          uniform_float_constants_, spv::DecorationBinding,
          int(is_pixel_shader() ? kConstantBufferFloatPixel
                                : kConstantBufferFloatVertex));
      if (features_.spirv_version >= spv::Spv_1_4) {
        main_interface_.push_back(uniform_float_constants_);
      }
    }

    // Common uniform buffer - bool and loop constants.
    // Uniform buffers must have std140 packing, so using arrays of 4-component
    // vectors instead of scalar arrays because the latter would have padding to
    // 16 bytes in each element.
    id_vector_temp_.clear();
    // 256 bool constants.
    id_vector_temp_.push_back(builder_->makeArrayType(
        type_uint4_, builder_->makeUintConstant(2), sizeof(uint32_t) * 4));
    builder_->addDecoration(id_vector_temp_.back(), spv::DecorationArrayStride,
                            sizeof(uint32_t) * 4);
    // 32 loop constants.
    id_vector_temp_.push_back(builder_->makeArrayType(
        type_uint4_, builder_->makeUintConstant(8), sizeof(uint32_t) * 4));
    builder_->addDecoration(id_vector_temp_.back(), spv::DecorationArrayStride,
                            sizeof(uint32_t) * 4);
    spv::Id type_bool_loop_constants =
        builder_->makeStructType(id_vector_temp_, "XeBoolLoopConstants");
    builder_->addMemberName(type_bool_loop_constants, 0, "bool_constants");
    builder_->addMemberDecoration(type_bool_loop_constants, 0,
                                  spv::DecorationOffset, 0);
    builder_->addMemberName(type_bool_loop_constants, 1, "loop_constants");
    builder_->addMemberDecoration(type_bool_loop_constants, 1,
                                  spv::DecorationOffset, sizeof(uint32_t) * 8);
    builder_->addDecoration(type_bool_loop_constants, spv::DecorationBlock);
    uniform_bool_loop_constants_ = builder_->createVariable(
        spv::NoPrecision, spv::StorageClassUniform, type_bool_loop_constants,
        "xe_uniform_bool_loop_constants");
    builder_->addDecoration(uniform_bool_loop_constants_,
                            spv::DecorationDescriptorSet,
                            int(kDescriptorSetConstants));
    builder_->addDecoration(uniform_bool_loop_constants_,
                            spv::DecorationBinding,
                            int(kConstantBufferBoolLoop));
    if (features_.spirv_version >= spv::Spv_1_4) {
      main_interface_.push_back(uniform_bool_loop_constants_);
    }

    // Common uniform buffer - fetch constants (32 x 6 uints packed in std140 as
    // 4-component vectors).
    id_vector_temp_.clear();
    id_vector_temp_.push_back(builder_->makeArrayType(
        type_uint4_, builder_->makeUintConstant(32 * 6 / 4),
        sizeof(uint32_t) * 4));
    builder_->addDecoration(id_vector_temp_.back(), spv::DecorationArrayStride,
                            sizeof(uint32_t) * 4);
    spv::Id type_fetch_constants =
        builder_->makeStructType(id_vector_temp_, "XeFetchConstants");
    builder_->addMemberName(type_fetch_constants, 0, "fetch_constants");
    builder_->addMemberDecoration(type_fetch_constants, 0,
                                  spv::DecorationOffset, 0);
    builder_->addDecoration(type_fetch_constants, spv::DecorationBlock);
    uniform_fetch_constants_ = builder_->createVariable(
        spv::NoPrecision, spv::StorageClassUniform, type_fetch_constants,
        "xe_uniform_fetch_constants");
    builder_->addDecoration(uniform_fetch_constants_,
                            spv::DecorationDescriptorSet,
                            int(kDescriptorSetConstants));
    builder_->addDecoration(uniform_fetch_constants_, spv::DecorationBinding,
                            int(kConstantBufferFetch));
    if (features_.spirv_version >= spv::Spv_1_4) {
      main_interface_.push_back(uniform_fetch_constants_);
    }

    // Common storage buffers - shared memory uint[], each 128 MB or larger,
    // depending on what's possible on the device.
    id_vector_temp_.clear();
    id_vector_temp_.push_back(builder_->makeRuntimeArray(type_uint_));
    // Storage buffers have std430 packing, no padding to 4-component vectors.
    builder_->addDecoration(id_vector_temp_.back(), spv::DecorationArrayStride,
                            sizeof(uint32_t));
    spv::Id type_shared_memory =
        builder_->makeStructType(id_vector_temp_, "XeSharedMemory");
    builder_->addMemberName(type_shared_memory, 0, "shared_memory");
    builder_->addMemberDecoration(type_shared_memory, 0,
                                  spv::DecorationRestrict);
    // TODO(Triang3l): Make writable when memexport is implemented.
    builder_->addMemberDecoration(type_shared_memory, 0,
                                  spv::DecorationNonWritable);
    builder_->addMemberDecoration(type_shared_memory, 0, spv::DecorationOffset,
                                  0);
    builder_->addDecoration(type_shared_memory,
                            features_.spirv_version >= spv::Spv_1_3
                                ? spv::DecorationBlock
                                : spv::DecorationBufferBlock);
    unsigned int shared_memory_binding_count =
        1 << GetSharedMemoryStorageBufferCountLog2();
    if (shared_memory_binding_count > 1) {
      type_shared_memory = builder_->makeArrayType(
          type_shared_memory,
          builder_->makeUintConstant(shared_memory_binding_count), 0);
    }
    buffers_shared_memory_ = builder_->createVariable(
        spv::NoPrecision,
        features_.spirv_version >= spv::Spv_1_3 ? spv::StorageClassStorageBuffer
                                                : spv::StorageClassUniform,
        type_shared_memory, "xe_shared_memory");
    builder_->addDecoration(buffers_shared_memory_,
                            spv::DecorationDescriptorSet,
                            int(kDescriptorSetSharedMemoryAndEdram));
    builder_->addDecoration(buffers_shared_memory_, spv::DecorationBinding, 0);
    if (features_.spirv_version >= spv::Spv_1_4) {
      main_interface_.push_back(buffers_shared_memory_);
    }
  }

  if (is_vertex_shader()) {
    StartVertexOrTessEvalShaderBeforeMain();
  } else if (is_pixel_shader()) {
    StartFragmentShaderBeforeMain();
  }

  // Begin the main function.
  std::vector<spv::Id> main_param_types;
  std::vector<std::vector<spv::Decoration>> main_precisions;
  spv::Block* function_main_entry;
  function_main_ = builder_->makeFunctionEntry(
      spv::NoPrecision, type_void_, "main", main_param_types, main_precisions,
      &function_main_entry);

  // Load the flags system constant since it may be used in many places.
  id_vector_temp_.clear();
  id_vector_temp_.push_back(builder_->makeIntConstant(kSystemConstantFlags));
  main_system_constant_flags_ = builder_->createLoad(
      builder_->createAccessChain(spv::StorageClassUniform,
                                  uniform_system_constants_, id_vector_temp_),
      spv::NoPrecision);

  if (!is_depth_only_fragment_shader_) {
    // Begin ucode translation. Initialize everything, even without defined
    // defaults, for safety.
    var_main_predicate_ = builder_->createVariable(
        spv::NoPrecision, spv::StorageClassFunction, type_bool_,
        "xe_var_predicate", builder_->makeBoolConstant(false));
    var_main_loop_count_ = builder_->createVariable(
        spv::NoPrecision, spv::StorageClassFunction, type_uint4_,
        "xe_var_loop_count", const_uint4_0_);
    var_main_address_register_ = builder_->createVariable(
        spv::NoPrecision, spv::StorageClassFunction, type_int_,
        "xe_var_address_register", const_int_0_);
    var_main_loop_address_ = builder_->createVariable(
        spv::NoPrecision, spv::StorageClassFunction, type_int4_,
        "xe_var_loop_address", const_int4_0_);
    var_main_previous_scalar_ = builder_->createVariable(
        spv::NoPrecision, spv::StorageClassFunction, type_float_,
        "xe_var_previous_scalar", const_float_0_);
    var_main_vfetch_address_ = builder_->createVariable(
        spv::NoPrecision, spv::StorageClassFunction, type_int_,
        "xe_var_vfetch_address", const_int_0_);
    var_main_tfetch_lod_ = builder_->createVariable(
        spv::NoPrecision, spv::StorageClassFunction, type_float_,
        "xe_var_tfetch_lod", const_float_0_);
    var_main_tfetch_gradients_h_ = builder_->createVariable(
        spv::NoPrecision, spv::StorageClassFunction, type_float3_,
        "xe_var_tfetch_gradients_h", const_float3_0_);
    var_main_tfetch_gradients_v_ = builder_->createVariable(
        spv::NoPrecision, spv::StorageClassFunction, type_float3_,
        "xe_var_tfetch_gradients_v", const_float3_0_);
    if (register_count()) {
      spv::Id type_register_array = builder_->makeArrayType(
          type_float4_, builder_->makeUintConstant(register_count()), 0);
      var_main_registers_ =
          builder_->createVariable(spv::NoPrecision, spv::StorageClassFunction,
                                   type_register_array, "xe_var_registers");
    }
  }

  // Write the execution model-specific prologue with access to variables in the
  // main function.
  if (is_vertex_shader()) {
    StartVertexOrTessEvalShaderInMain();
  } else if (is_pixel_shader()) {
    StartFragmentShaderInMain();
  }

  if (is_depth_only_fragment_shader_) {
    return;
  }

  // Open the main loop.
  spv::Block& main_loop_pre_header = *builder_->getBuildPoint();
  main_loop_header_ = &builder_->makeNewBlock();
  spv::Block& main_loop_body = builder_->makeNewBlock();
  // Added later because the body has nested control flow, but according to the
  // specification:
  // "The order of blocks in a function must satisfy the rule that blocks appear
  //  before all blocks they dominate."
  main_loop_continue_ =
      new spv::Block(builder_->getUniqueId(), *function_main_);
  main_loop_merge_ = new spv::Block(builder_->getUniqueId(), *function_main_);
  builder_->createBranch(main_loop_header_);

  // If no jumps, don't create a switch, but still create a loop so exece can
  // break.
  bool has_main_switch = !current_shader().label_addresses().empty();

  // Main loop header - based on whether it's the first iteration (entered from
  // the function or from the continuation), choose the program counter.
  builder_->setBuildPoint(main_loop_header_);
  spv::Id main_loop_pc_current = spv::NoResult;
  if (has_main_switch) {
    // OpPhi must be the first in the block.
    id_vector_temp_.clear();
    id_vector_temp_.push_back(const_int_0_);
    id_vector_temp_.push_back(main_loop_pre_header.getId());
    main_loop_pc_next_ = builder_->getUniqueId();
    id_vector_temp_.push_back(main_loop_pc_next_);
    id_vector_temp_.push_back(main_loop_continue_->getId());
    main_loop_pc_current =
        builder_->createOp(spv::OpPhi, type_int_, id_vector_temp_);
  }
  uint_vector_temp_.clear();
  builder_->createLoopMerge(main_loop_merge_, main_loop_continue_,
                            spv::LoopControlDontUnrollMask, uint_vector_temp_);
  builder_->createBranch(&main_loop_body);

  // Main loop body.
  builder_->setBuildPoint(&main_loop_body);
  if (has_main_switch) {
    // Create the program counter switch with cases for every label and for
    // label 0.
    main_switch_header_ = builder_->getBuildPoint();
    main_switch_merge_ =
        new spv::Block(builder_->getUniqueId(), *function_main_);
    builder_->createSelectionMerge(main_switch_merge_,
                                   spv::SelectionControlDontFlattenMask);
    main_switch_op_ = std::make_unique<spv::Instruction>(spv::OpSwitch);
    main_switch_op_->addIdOperand(main_loop_pc_current);
    main_switch_op_->addIdOperand(main_switch_merge_->getId());
    // The default case (the merge here) must have the header as a predecessor.
    main_switch_merge_->addPredecessor(main_switch_header_);
    // The instruction will be inserted later, when all cases are filled.
    // Insert and enter case 0.
    spv::Block* main_switch_case_0_block =
        new spv::Block(builder_->getUniqueId(), *function_main_);
    main_switch_op_->addImmediateOperand(0);
    main_switch_op_->addIdOperand(main_switch_case_0_block->getId());
    // Every switch case must have the OpSelectionMerge/OpSwitch block as a
    // predecessor.
    main_switch_case_0_block->addPredecessor(main_switch_header_);
    function_main_->addBlock(main_switch_case_0_block);
    builder_->setBuildPoint(main_switch_case_0_block);
  }
}

std::vector<uint8_t> SpirvShaderTranslator::CompleteTranslation() {
  if (!is_depth_only_fragment_shader_) {
    // Close flow control within the last switch case.
    CloseExecConditionals();
    bool has_main_switch = !current_shader().label_addresses().empty();
    // After the final exec (if it happened to be not exece, which would already
    // have a break branch), break from the switch if it exists, or from the
    // loop it doesn't.
    if (!builder_->getBuildPoint()->isTerminated()) {
      builder_->createBranch(has_main_switch ? main_switch_merge_
                                             : main_loop_merge_);
    }
    if (has_main_switch) {
      // Insert the switch instruction with all cases added as operands.
      builder_->setBuildPoint(main_switch_header_);
      builder_->getBuildPoint()->addInstruction(std::move(main_switch_op_));
      // Build the main switch merge, breaking out of the loop after falling
      // through the end or breaking from exece (only continuing if a jump -
      // from a guest loop or from jmp/call - was made).
      function_main_->addBlock(main_switch_merge_);
      builder_->setBuildPoint(main_switch_merge_);
      builder_->createBranch(main_loop_merge_);
    }

    // Main loop continuation - choose the program counter based on the path
    // taken (-1 if not from a jump as a safe fallback, which would result in
    // not hitting any switch case and reaching the final break in the body).
    function_main_->addBlock(main_loop_continue_);
    builder_->setBuildPoint(main_loop_continue_);
    if (has_main_switch) {
      // OpPhi, if added, must be the first in the block.
      // If labels were added, but not jumps (for example, due to the call
      // instruction not being implemented as of October 18, 2020), send an
      // impossible program counter value (-1) to the OpPhi at the next
      // iteration.
      if (main_switch_next_pc_phi_operands_.empty()) {
        main_switch_next_pc_phi_operands_.push_back(
            builder_->makeIntConstant(-1));
      }
      std::unique_ptr<spv::Instruction> main_loop_pc_next_op =
          std::make_unique<spv::Instruction>(
              main_loop_pc_next_, type_int_,
              main_switch_next_pc_phi_operands_.size() >= 2
                  ? spv::OpPhi
                  : spv::OpCopyObject);
      for (spv::Id operand : main_switch_next_pc_phi_operands_) {
        main_loop_pc_next_op->addIdOperand(operand);
      }
      builder_->getBuildPoint()->addInstruction(
          std::move(main_loop_pc_next_op));
    }
    builder_->createBranch(main_loop_header_);

    // Add the main loop merge block and go back to the function.
    function_main_->addBlock(main_loop_merge_);
    builder_->setBuildPoint(main_loop_merge_);
  }

  if (is_vertex_shader()) {
    CompleteVertexOrTessEvalShaderInMain();
  } else if (is_pixel_shader()) {
    CompleteFragmentShaderInMain();
  }

  // End the main function.
  builder_->leaveFunction();

  // Make the main function the entry point.
  spv::ExecutionModel execution_model;
  if (is_pixel_shader()) {
    execution_model = spv::ExecutionModelFragment;
    builder_->addExecutionMode(function_main_,
                               spv::ExecutionModeOriginUpperLeft);
    if (IsExecutionModeEarlyFragmentTests()) {
      builder_->addExecutionMode(function_main_,
                                 spv::ExecutionModeEarlyFragmentTests);
    }
    if (edram_fragment_shader_interlock_) {
      // Accessing per-sample values, so interlocking just when there's common
      // coverage is enough if the device exposes that.
      if (features_.fragment_shader_sample_interlock) {
        builder_->addCapability(
            spv::CapabilityFragmentShaderSampleInterlockEXT);
        builder_->addExecutionMode(function_main_,
                                   spv::ExecutionModeSampleInterlockOrderedEXT);
      } else {
        builder_->addCapability(spv::CapabilityFragmentShaderPixelInterlockEXT);
        builder_->addExecutionMode(function_main_,
                                   spv::ExecutionModePixelInterlockOrderedEXT);
      }
    }
  } else {
    assert_true(is_vertex_shader());
    execution_model = IsSpirvTessEvalShader()
                          ? spv::ExecutionModelTessellationEvaluation
                          : spv::ExecutionModelVertex;
  }
  if (features_.denorm_flush_to_zero_float32) {
    // Flush to zero, similar to the real hardware, also for things like Shader
    // Model 3 multiplication emulation.
    builder_->addCapability(spv::CapabilityDenormFlushToZero);
    builder_->addExecutionMode(function_main_,
                               spv::ExecutionModeDenormFlushToZero, 32);
  }
  if (features_.signed_zero_inf_nan_preserve_float32) {
    // Signed zero used to get VFACE from ps_param_gen, also special behavior
    // for infinity in certain instructions (such as logarithm, reciprocal,
    // muls_prev2).
    builder_->addCapability(spv::CapabilitySignedZeroInfNanPreserve);
    builder_->addExecutionMode(function_main_,
                               spv::ExecutionModeSignedZeroInfNanPreserve, 32);
  }
  spv::Instruction* entry_point =
      builder_->addEntryPoint(execution_model, function_main_, "main");
  for (spv::Id interface_id : main_interface_) {
    entry_point->addIdOperand(interface_id);
  }

  if (!is_depth_only_fragment_shader_) {
    // Specify the binding indices for samplers when the number of textures is
    // known, as samplers are located after images in the texture descriptor
    // set.
    size_t texture_binding_count = texture_bindings_.size();
    size_t sampler_binding_count = sampler_bindings_.size();
    for (size_t i = 0; i < sampler_binding_count; ++i) {
      builder_->addDecoration(sampler_bindings_[i].variable,
                              spv::DecorationBinding,
                              int(texture_binding_count + i));
    }
  }

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

void SpirvShaderTranslator::PostTranslation() {
  Shader::Translation& translation = current_translation();
  if (!translation.is_valid()) {
    return;
  }
  SpirvShader* spirv_shader = dynamic_cast<SpirvShader*>(&translation.shader());
  if (spirv_shader && !spirv_shader->bindings_setup_entered_.test_and_set(
                          std::memory_order_relaxed)) {
    spirv_shader->texture_bindings_.clear();
    spirv_shader->texture_bindings_.reserve(texture_bindings_.size());
    for (const TextureBinding& translator_binding : texture_bindings_) {
      SpirvShader::TextureBinding& shader_binding =
          spirv_shader->texture_bindings_.emplace_back();
      // For a stable hash.
      std::memset(&shader_binding, 0, sizeof(shader_binding));
      shader_binding.fetch_constant = translator_binding.fetch_constant;
      shader_binding.dimension = translator_binding.dimension;
      shader_binding.is_signed = translator_binding.is_signed;
      spirv_shader->used_texture_mask_ |= UINT32_C(1)
                                          << translator_binding.fetch_constant;
    }
    spirv_shader->sampler_bindings_.clear();
    spirv_shader->sampler_bindings_.reserve(sampler_bindings_.size());
    for (const SamplerBinding& translator_binding : sampler_bindings_) {
      SpirvShader::SamplerBinding& shader_binding =
          spirv_shader->sampler_bindings_.emplace_back();
      shader_binding.fetch_constant = translator_binding.fetch_constant;
      shader_binding.mag_filter = translator_binding.mag_filter;
      shader_binding.min_filter = translator_binding.min_filter;
      shader_binding.mip_filter = translator_binding.mip_filter;
      shader_binding.aniso_filter = translator_binding.aniso_filter;
    }
  }
}

void SpirvShaderTranslator::ProcessLabel(uint32_t cf_index) {
  if (cf_index == 0) {
    // 0 already added in the beginning.
    return;
  }

  assert_false(current_shader().label_addresses().empty());

  // Close flow control within the previous switch case.
  CloseExecConditionals();

  spv::Function& function = builder_->getBuildPoint()->getParent();
  // Create the next switch case and fallthrough to it.
  spv::Block* new_case = new spv::Block(builder_->getUniqueId(), function);
  main_switch_op_->addImmediateOperand(cf_index);
  main_switch_op_->addIdOperand(new_case->getId());
  // Every switch case must have the OpSelectionMerge/OpSwitch block as a
  // predecessor.
  new_case->addPredecessor(main_switch_header_);
  // The previous block may have already been terminated if was exece.
  if (!builder_->getBuildPoint()->isTerminated()) {
    builder_->createBranch(new_case);
  }
  function.addBlock(new_case);
  builder_->setBuildPoint(new_case);
}

void SpirvShaderTranslator::ProcessExecInstructionBegin(
    const ParsedExecInstruction& instr) {
  UpdateExecConditionals(instr.type, instr.bool_constant_index,
                         instr.condition);
}

void SpirvShaderTranslator::ProcessExecInstructionEnd(
    const ParsedExecInstruction& instr) {
  if (instr.is_end) {
    // Break out of the main switch (if exists) and the main loop.
    CloseInstructionPredication();
    if (!builder_->getBuildPoint()->isTerminated()) {
      builder_->createBranch(current_shader().label_addresses().empty()
                                 ? main_loop_merge_
                                 : main_switch_merge_);
    }
  }
  UpdateExecConditionals(instr.type, instr.bool_constant_index,
                         instr.condition);
}

void SpirvShaderTranslator::ProcessLoopStartInstruction(
    const ParsedLoopStartInstruction& instr) {
  // loop il<idx>, L<idx> - loop with loop data il<idx>, end @ L<idx>

  // Loop control is outside execs - actually close the last exec.
  CloseExecConditionals();

  EnsureBuildPointAvailable();

  id_vector_temp_.clear();
  // Loop constants (member 1).
  id_vector_temp_.push_back(builder_->makeIntConstant(1));
  // 4-component vector.
  id_vector_temp_.push_back(
      builder_->makeIntConstant(int(instr.loop_constant_index >> 2)));
  // Scalar within the vector.
  id_vector_temp_.push_back(
      builder_->makeIntConstant(int(instr.loop_constant_index & 3)));
  // Count (unsigned) in bits 0:7 of the loop constant (struct member 1),
  // initial aL (unsigned) in 8:15.
  spv::Id loop_constant =
      builder_->createLoad(builder_->createAccessChain(
                               spv::StorageClassUniform,
                               uniform_bool_loop_constants_, id_vector_temp_),
                           spv::NoPrecision);

  spv::Id const_int_8 = builder_->makeIntConstant(8);

  // Push the count to the loop count stack - move XYZ to YZW and set X to the
  // new iteration count (swizzling the way glslang does it for similar GLSL).
  spv::Id loop_count_stack_old =
      builder_->createLoad(var_main_loop_count_, spv::NoPrecision);
  spv::Id loop_count_new =
      builder_->createTriOp(spv::OpBitFieldUExtract, type_uint_, loop_constant,
                            const_int_0_, const_int_8);
  id_vector_temp_.clear();
  id_vector_temp_.push_back(loop_count_new);
  for (unsigned int i = 0; i < 3; ++i) {
    id_vector_temp_.push_back(
        builder_->createCompositeExtract(loop_count_stack_old, type_uint_, i));
  }
  builder_->createStore(
      builder_->createCompositeConstruct(type_uint4_, id_vector_temp_),
      var_main_loop_count_);

  // Push aL - keep the same value as in the previous loop if repeating, or the
  // new one otherwise.
  spv::Id address_relative_stack_old =
      builder_->createLoad(var_main_loop_address_, spv::NoPrecision);
  id_vector_temp_.clear();
  if (instr.is_repeat) {
    id_vector_temp_.emplace_back();
  } else {
    id_vector_temp_.push_back(builder_->createUnaryOp(
        spv::OpBitcast, type_int_,
        builder_->createTriOp(spv::OpBitFieldUExtract, type_uint_,
                              loop_constant, const_int_8, const_int_8)));
  }
  for (unsigned int i = 0; i < 3; ++i) {
    id_vector_temp_.push_back(builder_->createCompositeExtract(
        address_relative_stack_old, type_int_, i));
  }
  if (instr.is_repeat) {
    id_vector_temp_[0] = id_vector_temp_[1];
  }
  builder_->createStore(
      builder_->createCompositeConstruct(type_int4_, id_vector_temp_),
      var_main_loop_address_);

  // Break (jump to the skip label) if the loop counter is 0 (since the
  // condition is checked in the end).
  spv::Block& head_block = *builder_->getBuildPoint();
  spv::Id loop_count_zero = builder_->createBinOp(
      spv::OpIEqual, type_bool_, loop_count_new, const_uint_0_);
  spv::Block& skip_block = builder_->makeNewBlock();
  spv::Block& body_block = builder_->makeNewBlock();
  builder_->createSelectionMerge(&body_block, spv::SelectionControlMaskNone);
  {
    std::unique_ptr<spv::Instruction> branch_conditional_op =
        std::make_unique<spv::Instruction>(spv::OpBranchConditional);
    branch_conditional_op->addIdOperand(loop_count_zero);
    branch_conditional_op->addIdOperand(skip_block.getId());
    branch_conditional_op->addIdOperand(body_block.getId());
    // More likely to enter than to skip.
    branch_conditional_op->addImmediateOperand(1);
    branch_conditional_op->addImmediateOperand(2);
    head_block.addInstruction(std::move(branch_conditional_op));
  }
  skip_block.addPredecessor(&head_block);
  body_block.addPredecessor(&head_block);
  builder_->setBuildPoint(&skip_block);
  main_switch_next_pc_phi_operands_.push_back(
      builder_->makeIntConstant(int(instr.loop_skip_address)));
  main_switch_next_pc_phi_operands_.push_back(
      builder_->getBuildPoint()->getId());
  builder_->createBranch(main_loop_continue_);
  builder_->setBuildPoint(&body_block);
}

void SpirvShaderTranslator::ProcessLoopEndInstruction(
    const ParsedLoopEndInstruction& instr) {
  // endloop il<idx>, L<idx> - end loop w/ data il<idx>, head @ L<idx>

  // Loop control is outside execs - actually close the last exec.
  CloseExecConditionals();

  EnsureBuildPointAvailable();

  // Subtract 1 from the loop counter (will store later).
  spv::Id loop_count_stack_old =
      builder_->createLoad(var_main_loop_count_, spv::NoPrecision);
  spv::Id loop_count = builder_->createBinOp(
      spv::OpISub, type_uint_,
      builder_->createCompositeExtract(loop_count_stack_old, type_uint_, 0),
      builder_->makeUintConstant(1));
  spv::Id address_relative_stack_old =
      builder_->createLoad(var_main_loop_address_, spv::NoPrecision);

  // Predicated break works like break if (loop_count == 0 || [!]p0).
  // Three options, due to logical operations usage (so OpLogicalNot is not
  // required):
  // - Continue if (loop_count != 0).
  // - Continue if (loop_count != 0 && p0), if breaking if !p0.
  // - Break if (loop_count == 0 || p0), if breaking if p0.
  bool break_is_true = instr.is_predicated_break && instr.predicate_condition;
  spv::Id condition =
      builder_->createBinOp(break_is_true ? spv::OpIEqual : spv::OpINotEqual,
                            type_bool_, loop_count, const_uint_0_);
  if (instr.is_predicated_break) {
    condition = builder_->createBinOp(
        instr.predicate_condition ? spv::OpLogicalOr : spv::OpLogicalAnd,
        type_bool_, condition,
        builder_->createLoad(var_main_predicate_, spv::NoPrecision));
  }

  spv::Block& body_block = *builder_->getBuildPoint();
  spv::Block& continue_block = builder_->makeNewBlock();
  spv::Block& break_block = builder_->makeNewBlock();
  builder_->createSelectionMerge(&break_block, spv::SelectionControlMaskNone);
  {
    std::unique_ptr<spv::Instruction> branch_conditional_op =
        std::make_unique<spv::Instruction>(spv::OpBranchConditional);
    branch_conditional_op->addIdOperand(condition);
    // More likely to continue than to break.
    if (break_is_true) {
      branch_conditional_op->addIdOperand(break_block.getId());
      branch_conditional_op->addIdOperand(continue_block.getId());
      branch_conditional_op->addImmediateOperand(1);
      branch_conditional_op->addImmediateOperand(2);
    } else {
      branch_conditional_op->addIdOperand(continue_block.getId());
      branch_conditional_op->addIdOperand(break_block.getId());
      branch_conditional_op->addImmediateOperand(2);
      branch_conditional_op->addImmediateOperand(1);
    }
    body_block.addInstruction(std::move(branch_conditional_op));
  }
  continue_block.addPredecessor(&body_block);
  break_block.addPredecessor(&body_block);

  // Continue case.
  builder_->setBuildPoint(&continue_block);
  // Store the loop count with 1 subtracted.
  builder_->createStore(builder_->createCompositeInsert(
                            loop_count, loop_count_stack_old, type_uint4_, 0),
                        var_main_loop_count_);
  // Extract the value to add to aL (signed, in bits 16:23 of the loop
  // constant).
  id_vector_temp_.clear();
  // Loop constants (member 1).
  id_vector_temp_.push_back(builder_->makeIntConstant(1));
  // 4-component vector.
  id_vector_temp_.push_back(
      builder_->makeIntConstant(int(instr.loop_constant_index >> 2)));
  // Scalar within the vector.
  id_vector_temp_.push_back(
      builder_->makeIntConstant(int(instr.loop_constant_index & 3)));
  spv::Id loop_constant =
      builder_->createLoad(builder_->createAccessChain(
                               spv::StorageClassUniform,
                               uniform_bool_loop_constants_, id_vector_temp_),
                           spv::NoPrecision);
  spv::Id address_relative_old = builder_->createCompositeExtract(
      address_relative_stack_old, type_int_, 0);
  builder_->createStore(
      builder_->createCompositeInsert(
          builder_->createBinOp(
              spv::OpIAdd, type_int_, address_relative_old,
              builder_->createTriOp(
                  spv::OpBitFieldSExtract, type_int_,
                  builder_->createUnaryOp(spv::OpBitcast, type_int_,
                                          loop_constant),
                  builder_->makeIntConstant(16), builder_->makeIntConstant(8))),
          address_relative_stack_old, type_int4_, 0),
      var_main_loop_address_);
  // Jump back to the beginning of the loop body.
  main_switch_next_pc_phi_operands_.push_back(
      builder_->makeIntConstant(int(instr.loop_body_address)));
  main_switch_next_pc_phi_operands_.push_back(
      builder_->getBuildPoint()->getId());
  builder_->createBranch(main_loop_continue_);

  // Break case.
  builder_->setBuildPoint(&break_block);
  // Pop the current loop off the loop counter and the relative address stacks -
  // move YZW to XYZ and set W to 0.
  id_vector_temp_.clear();
  for (unsigned int i = 1; i < 4; ++i) {
    id_vector_temp_.push_back(
        builder_->createCompositeExtract(loop_count_stack_old, type_uint_, i));
  }
  id_vector_temp_.push_back(const_uint_0_);
  builder_->createStore(
      builder_->createCompositeConstruct(type_uint4_, id_vector_temp_),
      var_main_loop_count_);
  id_vector_temp_.clear();
  for (unsigned int i = 1; i < 4; ++i) {
    id_vector_temp_.push_back(builder_->createCompositeExtract(
        address_relative_stack_old, type_int_, i));
  }
  id_vector_temp_.push_back(const_int_0_);
  builder_->createStore(
      builder_->createCompositeConstruct(type_int4_, id_vector_temp_),
      var_main_loop_address_);
  // Now going to fall through to the next control flow instruction.
}

void SpirvShaderTranslator::ProcessJumpInstruction(
    const ParsedJumpInstruction& instr) {
  // Treat like exec, merge with execs if possible, since it's an if too.
  ParsedExecInstruction::Type type;
  if (instr.type == ParsedJumpInstruction::Type::kConditional) {
    type = ParsedExecInstruction::Type::kConditional;
  } else if (instr.type == ParsedJumpInstruction::Type::kPredicated) {
    type = ParsedExecInstruction::Type::kPredicated;
  } else {
    type = ParsedExecInstruction::Type::kUnconditional;
  }
  UpdateExecConditionals(type, instr.bool_constant_index, instr.condition);

  // UpdateExecConditionals may not necessarily close the instruction-level
  // predicate check (it's not necessary if the execs are merged), but here the
  // instruction itself is on the control flow level, so the predicate check is
  // on the control flow level too.
  CloseInstructionPredication();

  if (builder_->getBuildPoint()->isTerminated()) {
    // Unreachable for some reason.
    return;
  }
  main_switch_next_pc_phi_operands_.push_back(
      builder_->makeIntConstant(int(instr.target_address)));
  main_switch_next_pc_phi_operands_.push_back(
      builder_->getBuildPoint()->getId());
  builder_->createBranch(main_loop_continue_);
}

spv::Id SpirvShaderTranslator::SpirvSmearScalarResultOrConstant(
    spv::Id scalar, spv::Id vector_type) {
  bool is_constant = builder_->isConstant(scalar);
  bool is_spec_constant = builder_->isSpecConstant(scalar);
  if (!is_constant && !is_spec_constant) {
    return builder_->smearScalar(spv::NoPrecision, scalar, vector_type);
  }
  assert_true(builder_->getTypeClass(builder_->getTypeId(scalar)) ==
              builder_->getTypeClass(builder_->getScalarTypeId(vector_type)));
  if (!builder_->isVectorType(vector_type)) {
    assert_true(builder_->isScalarType(vector_type));
    return scalar;
  }
  int num_components = builder_->getNumTypeComponents(vector_type);
  id_vector_temp_util_.clear();
  for (int i = 0; i < num_components; ++i) {
    id_vector_temp_util_.push_back(scalar);
  }
  return builder_->makeCompositeConstant(vector_type, id_vector_temp_util_,
                                         is_spec_constant);
}

uint32_t SpirvShaderTranslator::GetPsParamGenInterpolator() const {
  assert_true(is_pixel_shader());
  Modification modification = GetSpirvShaderModification();
  // param_gen_interpolator is already 4 bits, no need for an interpolator count
  // safety check.
  return (modification.pixel.param_gen_enable &&
          modification.pixel.param_gen_interpolator < register_count())
             ? modification.pixel.param_gen_interpolator
             : UINT32_MAX;
}

void SpirvShaderTranslator::EnsureBuildPointAvailable() {
  if (!builder_->getBuildPoint()->isTerminated()) {
    return;
  }
  spv::Block& new_block = builder_->makeNewBlock();
  new_block.setUnreachable();
  builder_->setBuildPoint(&new_block);
}

void SpirvShaderTranslator::StartVertexOrTessEvalShaderBeforeMain() {
  // Create the inputs.
  if (IsSpirvTessEvalShader()) {
    input_primitive_id_ = builder_->createVariable(
        spv::NoPrecision, spv::StorageClassInput, type_int_, "gl_PrimitiveID");
    builder_->addDecoration(input_primitive_id_, spv::DecorationBuiltIn,
                            spv::BuiltInPrimitiveId);
    main_interface_.push_back(input_primitive_id_);
  } else {
    input_vertex_index_ = builder_->createVariable(
        spv::NoPrecision, spv::StorageClassInput, type_int_, "gl_VertexIndex");
    builder_->addDecoration(input_vertex_index_, spv::DecorationBuiltIn,
                            spv::BuiltInVertexIndex);
    main_interface_.push_back(input_vertex_index_);
  }

  uint32_t output_location = 0;

  // Create the interpolator outputs.
  {
    uint32_t interpolators_remaining = GetModificationInterpolatorMask();
    uint32_t interpolator_index;
    while (xe::bit_scan_forward(interpolators_remaining, &interpolator_index)) {
      interpolators_remaining &= ~(UINT32_C(1) << interpolator_index);
      spv::Id interpolator = builder_->createVariable(
          spv::NoPrecision, spv::StorageClassOutput, type_float4_,
          fmt::format("xe_out_interpolator_{}", interpolator_index).c_str());
      input_output_interpolators_[interpolator_index] = interpolator;
      builder_->addDecoration(interpolator, spv::DecorationLocation,
                              int(output_location));
      builder_->addDecoration(interpolator, spv::DecorationInvariant);
      main_interface_.push_back(interpolator);
      ++output_location;
    }
  }

  Modification shader_modification = GetSpirvShaderModification();

  if (shader_modification.vertex.output_point_parameters) {
    if (shader_modification.vertex.host_vertex_shader_type ==
        Shader::HostVertexShaderType::kPointListAsTriangleStrip) {
      // Create the point coordinates output.
      output_point_coordinates_ =
          builder_->createVariable(spv::NoPrecision, spv::StorageClassOutput,
                                   type_float2_, "xe_out_point_coordinates");
      builder_->addDecoration(output_point_coordinates_,
                              spv::DecorationLocation, int(output_location));
      builder_->addDecoration(output_point_coordinates_,
                              spv::DecorationInvariant);
      main_interface_.push_back(output_point_coordinates_);
      ++output_location;
    } else {
      // Create the point size output. Not using gl_PointSize from gl_PerVertex
      // not to rely on the shaderTessellationAndGeometryPointSize feature, and
      // also because the value written to gl_PointSize must be greater than
      // zero.
      output_point_size_ =
          builder_->createVariable(spv::NoPrecision, spv::StorageClassOutput,
                                   type_float_, "xe_out_point_size");
      builder_->addDecoration(output_point_size_, spv::DecorationLocation,
                              int(output_location));
      builder_->addDecoration(output_point_size_, spv::DecorationInvariant);
      main_interface_.push_back(output_point_size_);
      ++output_location;
    }
  }

  // Create the gl_PerVertex output for used system outputs.
  std::vector<spv::Id> struct_per_vertex_members;
  struct_per_vertex_members.reserve(kOutputPerVertexMemberCount);
  struct_per_vertex_members.push_back(type_float4_);
  spv::Id type_struct_per_vertex =
      builder_->makeStructType(struct_per_vertex_members, "gl_PerVertex");
  builder_->addMemberName(type_struct_per_vertex,
                          kOutputPerVertexMemberPosition, "gl_Position");
  builder_->addMemberDecoration(type_struct_per_vertex,
                                kOutputPerVertexMemberPosition,
                                spv::DecorationBuiltIn, spv::BuiltInPosition);
  builder_->addDecoration(type_struct_per_vertex, spv::DecorationBlock);
  output_per_vertex_ = builder_->createVariable(
      spv::NoPrecision, spv::StorageClassOutput, type_struct_per_vertex, "");
  builder_->addDecoration(output_per_vertex_, spv::DecorationInvariant);
  main_interface_.push_back(output_per_vertex_);
}

void SpirvShaderTranslator::StartVertexOrTessEvalShaderInMain() {
  // The edge flag isn't used for any purpose by the translator.
  if (current_shader().writes_point_size_edge_flag_kill_vertex() & 0b101) {
    id_vector_temp_.clear();
    // Set the point size to a negative value to tell the point sprite expansion
    // that it should use the default point size if the vertex shader does not
    // override it.
    id_vector_temp_.push_back(builder_->makeFloatConstant(-1.0f));
    // The edge flag is ignored.
    id_vector_temp_.push_back(const_float_0_);
    // Don't kill by default (zero bits 0:30).
    id_vector_temp_.push_back(const_float_0_);
    var_main_point_size_edge_flag_kill_vertex_ = builder_->createVariable(
        spv::NoPrecision, spv::StorageClassFunction, type_float3_,
        "xe_var_point_size_edge_flag_kill_vertex",
        builder_->makeCompositeConstant(type_float3_, id_vector_temp_));
  }

  // Zero general-purpose registers to prevent crashes when the game
  // references them after only initializing them conditionally.
  for (uint32_t i = 0; i < register_count(); ++i) {
    id_vector_temp_.clear();
    id_vector_temp_.push_back(builder_->makeIntConstant(int(i)));
    builder_->createStore(
        const_float4_0_,
        builder_->createAccessChain(spv::StorageClassFunction,
                                    var_main_registers_, id_vector_temp_));
  }

  // Zero the interpolators.
  {
    uint32_t interpolators_remaining = GetModificationInterpolatorMask();
    uint32_t interpolator_index;
    while (xe::bit_scan_forward(interpolators_remaining, &interpolator_index)) {
      interpolators_remaining &= ~(UINT32_C(1) << interpolator_index);
      builder_->createStore(const_float4_0_,
                            input_output_interpolators_[interpolator_index]);
    }
  }

  Modification shader_modification = GetSpirvShaderModification();

  // TODO(Triang3l): For HostVertexShaderType::kRectangeListAsTriangleStrip,
  // start the vertex loop, and load the index there.

  // Load the vertex index or the tessellation parameters.
  if (register_count()) {
    // TODO(Triang3l): Barycentric coordinates and patch index.
    if (IsSpirvVertexShader()) {
      spv::Id vertex_index = builder_->createUnaryOp(
          spv::OpBitcast, type_uint_,
          builder_->createLoad(input_vertex_index_, spv::NoPrecision));
      if (shader_modification.vertex.host_vertex_shader_type ==
          Shader::HostVertexShaderType::kPointListAsTriangleStrip) {
        // Load the point index, autogenerated or indirectly from the index
        // buffer.
        // Extract the primitive index from the two-triangle strip vertex index.
        spv::Id const_uint_2 = builder_->makeUintConstant(2);
        vertex_index = builder_->createBinOp(
            spv::OpShiftRightLogical, type_uint_, vertex_index, const_uint_2);
        // Check if the index needs to be loaded from the index buffer.
        spv::Id load_vertex_index = builder_->createBinOp(
            spv::OpINotEqual, type_bool_,
            builder_->createBinOp(
                spv::OpBitwiseAnd, type_uint_, main_system_constant_flags_,
                builder_->makeUintConstant(static_cast<unsigned int>(
                    kSysFlag_ComputeOrPrimitiveVertexIndexLoad))),
            const_uint_0_);
        spv::Block& block_load_vertex_index_pre = *builder_->getBuildPoint();
        spv::Block& block_load_vertex_index_start = builder_->makeNewBlock();
        spv::Block& block_load_vertex_index_merge = builder_->makeNewBlock();
        builder_->createSelectionMerge(&block_load_vertex_index_merge,
                                       spv::SelectionControlDontFlattenMask);
        builder_->createConditionalBranch(load_vertex_index,
                                          &block_load_vertex_index_start,
                                          &block_load_vertex_index_merge);
        builder_->setBuildPoint(&block_load_vertex_index_start);
        // Check if the index is 32-bit.
        spv::Id vertex_index_is_32bit = builder_->createBinOp(
            spv::OpINotEqual, type_bool_,
            builder_->createBinOp(
                spv::OpBitwiseAnd, type_uint_, main_system_constant_flags_,
                builder_->makeUintConstant(static_cast<unsigned int>(
                    kSysFlag_ComputeOrPrimitiveVertexIndexLoad32Bit))),
            const_uint_0_);
        // Calculate the vertex index address in the shared memory.
        id_vector_temp_.clear();
        id_vector_temp_.push_back(
            builder_->makeIntConstant(kSystemConstantVertexIndexLoadAddress));
        spv::Id vertex_index_address = builder_->createBinOp(
            spv::OpIAdd, type_uint_,
            builder_->createLoad(
                builder_->createAccessChain(spv::StorageClassUniform,
                                            uniform_system_constants_,
                                            id_vector_temp_),
                spv::NoPrecision),
            builder_->createBinOp(
                spv::OpShiftLeftLogical, type_uint_, vertex_index,
                builder_->createTriOp(spv::OpSelect, type_uint_,
                                      vertex_index_is_32bit, const_uint_2,
                                      builder_->makeUintConstant(1))));
        // Load the 32 bits containing the whole vertex index or two 16-bit
        // vertex indices.
        // TODO(Triang3l): Bounds checking.
        spv::Id loaded_vertex_index =
            LoadUint32FromSharedMemory(builder_->createUnaryOp(
                spv::OpBitcast, type_int_,
                builder_->createBinOp(spv::OpShiftRightLogical, type_uint_,
                                      vertex_index_address, const_uint_2)));
        // Extract the 16-bit index from the loaded 32 bits if needed.
        loaded_vertex_index = builder_->createTriOp(
            spv::OpSelect, type_uint_, vertex_index_is_32bit,
            loaded_vertex_index,
            builder_->createTriOp(
                spv::OpBitFieldUExtract, type_uint_, loaded_vertex_index,
                builder_->createBinOp(
                    spv::OpShiftLeftLogical, type_uint_,
                    builder_->createBinOp(spv::OpBitwiseAnd, type_uint_,
                                          vertex_index_address, const_uint_2),
                    builder_->makeUintConstant(4 - 1)),
                builder_->makeUintConstant(16)));
        // Endian-swap the loaded index.
        id_vector_temp_.clear();
        id_vector_temp_.push_back(
            builder_->makeIntConstant(kSystemConstantVertexIndexEndian));
        loaded_vertex_index = EndianSwap32Uint(
            loaded_vertex_index,
            builder_->createLoad(
                builder_->createAccessChain(spv::StorageClassUniform,
                                            uniform_system_constants_,
                                            id_vector_temp_),
                spv::NoPrecision));
        // Get the actual build point for phi.
        spv::Block& block_load_vertex_index_end = *builder_->getBuildPoint();
        builder_->createBranch(&block_load_vertex_index_merge);
        // Select between the loaded index and the original index from Vulkan.
        builder_->setBuildPoint(&block_load_vertex_index_merge);
        {
          std::unique_ptr<spv::Instruction> loaded_vertex_index_phi_op =
              std::make_unique<spv::Instruction>(builder_->getUniqueId(),
                                                 type_uint_, spv::OpPhi);
          loaded_vertex_index_phi_op->addIdOperand(loaded_vertex_index);
          loaded_vertex_index_phi_op->addIdOperand(
              block_load_vertex_index_end.getId());
          loaded_vertex_index_phi_op->addIdOperand(vertex_index);
          loaded_vertex_index_phi_op->addIdOperand(
              block_load_vertex_index_pre.getId());
          vertex_index = loaded_vertex_index_phi_op->getResultId();
          builder_->getBuildPoint()->addInstruction(
              std::move(loaded_vertex_index_phi_op));
        }
      } else {
        // TODO(Triang3l): Close line loop primitive.
        // Load the unswapped index as uint for swapping, or for indirect
        // loading if needed.
        if (!features_.full_draw_index_uint32) {
          // Check if the full 32-bit index needs to be loaded indirectly.
          spv::Id load_vertex_index = builder_->createBinOp(
              spv::OpINotEqual, type_bool_,
              builder_->createBinOp(
                  spv::OpBitwiseAnd, type_uint_, main_system_constant_flags_,
                  builder_->makeUintConstant(
                      static_cast<unsigned int>(kSysFlag_VertexIndexLoad))),
              const_uint_0_);
          spv::Block& block_load_vertex_index_pre = *builder_->getBuildPoint();
          spv::Block& block_load_vertex_index_start = builder_->makeNewBlock();
          spv::Block& block_load_vertex_index_merge = builder_->makeNewBlock();
          builder_->createSelectionMerge(&block_load_vertex_index_merge,
                                         spv::SelectionControlDontFlattenMask);
          builder_->createConditionalBranch(load_vertex_index,
                                            &block_load_vertex_index_start,
                                            &block_load_vertex_index_merge);
          builder_->setBuildPoint(&block_load_vertex_index_start);
          // Load the 32-bit index.
          // TODO(Triang3l): Bounds checking.
          id_vector_temp_.clear();
          id_vector_temp_.push_back(
              builder_->makeIntConstant(kSystemConstantVertexIndexLoadAddress));
          spv::Id loaded_vertex_index =
              LoadUint32FromSharedMemory(builder_->createUnaryOp(
                  spv::OpBitcast, type_int_,
                  builder_->createBinOp(
                      spv::OpIAdd, type_uint_,
                      builder_->createBinOp(
                          spv::OpShiftRightLogical, type_uint_,
                          builder_->createLoad(
                              builder_->createAccessChain(
                                  spv::StorageClassUniform,
                                  uniform_system_constants_, id_vector_temp_),
                              spv::NoPrecision),
                          builder_->makeUintConstant(2)),
                      vertex_index)));
          // Get the actual build point for phi.
          spv::Block& block_load_vertex_index_end = *builder_->getBuildPoint();
          builder_->createBranch(&block_load_vertex_index_merge);
          // Select between the loaded index and the original index from Vulkan.
          builder_->setBuildPoint(&block_load_vertex_index_merge);
          {
            std::unique_ptr<spv::Instruction> loaded_vertex_index_phi_op =
                std::make_unique<spv::Instruction>(builder_->getUniqueId(),
                                                   type_uint_, spv::OpPhi);
            loaded_vertex_index_phi_op->addIdOperand(loaded_vertex_index);
            loaded_vertex_index_phi_op->addIdOperand(
                block_load_vertex_index_end.getId());
            loaded_vertex_index_phi_op->addIdOperand(vertex_index);
            loaded_vertex_index_phi_op->addIdOperand(
                block_load_vertex_index_pre.getId());
            vertex_index = loaded_vertex_index_phi_op->getResultId();
            builder_->getBuildPoint()->addInstruction(
                std::move(loaded_vertex_index_phi_op));
          }
        }
        // Endian-swap the index.
        id_vector_temp_.clear();
        id_vector_temp_.push_back(
            builder_->makeIntConstant(kSystemConstantVertexIndexEndian));
        vertex_index = EndianSwap32Uint(
            vertex_index, builder_->createLoad(
                              builder_->createAccessChain(
                                  spv::StorageClassUniform,
                                  uniform_system_constants_, id_vector_temp_),
                              spv::NoPrecision));
      }
      // Convert the index to a signed integer.
      vertex_index =
          builder_->createUnaryOp(spv::OpBitcast, type_int_, vertex_index);
      // Add the base to the index.
      id_vector_temp_.clear();
      id_vector_temp_.push_back(
          builder_->makeIntConstant(kSystemConstantVertexBaseIndex));
      vertex_index = builder_->createBinOp(
          spv::OpIAdd, type_int_, vertex_index,
          builder_->createLoad(builder_->createAccessChain(
                                   spv::StorageClassUniform,
                                   uniform_system_constants_, id_vector_temp_),
                               spv::NoPrecision));
      // Write the index to r0.x as float.
      id_vector_temp_.clear();
      id_vector_temp_.push_back(const_int_0_);
      id_vector_temp_.push_back(const_int_0_);
      builder_->createStore(
          builder_->createUnaryOp(spv::OpConvertSToF, type_float_,
                                  vertex_index),
          builder_->createAccessChain(spv::StorageClassFunction,
                                      var_main_registers_, id_vector_temp_));
    }
  }
}

void SpirvShaderTranslator::CompleteVertexOrTessEvalShaderInMain() {
  id_vector_temp_.clear();
  id_vector_temp_.push_back(
      builder_->makeIntConstant(kOutputPerVertexMemberPosition));
  spv::Id position_ptr = builder_->createAccessChain(
      spv::StorageClassOutput, output_per_vertex_, id_vector_temp_);
  spv::Id guest_position = builder_->createLoad(position_ptr, spv::NoPrecision);

  // Check if the shader already returns W, not 1/W, and if it doesn't, turn 1/W
  // into W.
  spv::Id position_w =
      builder_->createCompositeExtract(guest_position, type_float_, 3);
  spv::Id is_w_not_reciprocal = builder_->createBinOp(
      spv::OpINotEqual, type_bool_,
      builder_->createBinOp(
          spv::OpBitwiseAnd, type_uint_, main_system_constant_flags_,
          builder_->makeUintConstant(
              static_cast<unsigned int>(kSysFlag_WNotReciprocal))),
      const_uint_0_);
  spv::Id guest_position_w_inv = builder_->createNoContractionBinOp(
      spv::OpFDiv, type_float_, const_float_1_, position_w);
  position_w =
      builder_->createTriOp(spv::OpSelect, type_float_, is_w_not_reciprocal,
                            position_w, guest_position_w_inv);

  spv::Id position_xyz;

  // Open a scope since position_xy and position_z won't be synchronized anymore
  // after position_xyz is built and modified later.
  {
    // Check if the shader returns XY/W rather than XY, and if it does, revert
    // that.
    uint_vector_temp_.clear();
    uint_vector_temp_.push_back(0);
    uint_vector_temp_.push_back(1);
    spv::Id position_xy = builder_->createRvalueSwizzle(
        spv::NoPrecision, type_float2_, guest_position, uint_vector_temp_);
    spv::Id is_xy_divided_by_w = builder_->createBinOp(
        spv::OpINotEqual, type_bool_,
        builder_->createBinOp(
            spv::OpBitwiseAnd, type_uint_, main_system_constant_flags_,
            builder_->makeUintConstant(
                static_cast<unsigned int>(kSysFlag_XYDividedByW))),
        const_uint_0_);
    spv::Id guest_position_xy_mul_w = builder_->createNoContractionBinOp(
        spv::OpVectorTimesScalar, type_float2_, position_xy, position_w);
    position_xy = builder_->createTriOp(
        spv::OpSelect, type_float2_,
        builder_->smearScalar(spv::NoPrecision, is_xy_divided_by_w,
                              type_bool2_),
        guest_position_xy_mul_w, position_xy);

    // Check if the shader returns Z/W rather than Z, and if it does, revert
    // that.
    spv::Id position_z =
        builder_->createCompositeExtract(guest_position, type_float_, 2);
    spv::Id is_z_divided_by_w = builder_->createBinOp(
        spv::OpINotEqual, type_bool_,
        builder_->createBinOp(
            spv::OpBitwiseAnd, type_uint_, main_system_constant_flags_,
            builder_->makeUintConstant(
                static_cast<unsigned int>(kSysFlag_ZDividedByW))),
        const_uint_0_);
    spv::Id guest_position_z_mul_w = builder_->createNoContractionBinOp(
        spv::OpFMul, type_float_, position_z, position_w);
    position_z =
        builder_->createTriOp(spv::OpSelect, type_float_, is_z_divided_by_w,
                              guest_position_z_mul_w, position_z);

    // Build XYZ of the position with W format handled.
    {
      std::unique_ptr<spv::Instruction> composite_construct_op =
          std::make_unique<spv::Instruction>(
              builder_->getUniqueId(), type_float3_, spv::OpCompositeConstruct);
      composite_construct_op->addIdOperand(position_xy);
      composite_construct_op->addIdOperand(position_z);
      position_xyz = composite_construct_op->getResultId();
      builder_->getBuildPoint()->addInstruction(
          std::move(composite_construct_op));
    }
  }

  // Apply the NDC scale and offset for guest to host viewport transformation.
  id_vector_temp_.clear();
  id_vector_temp_.push_back(builder_->makeIntConstant(kSystemConstantNdcScale));
  spv::Id ndc_scale = builder_->createLoad(
      builder_->createAccessChain(spv::StorageClassUniform,
                                  uniform_system_constants_, id_vector_temp_),
      spv::NoPrecision);
  position_xyz = builder_->createNoContractionBinOp(spv::OpFMul, type_float3_,
                                                    position_xyz, ndc_scale);
  id_vector_temp_.clear();
  id_vector_temp_.push_back(
      builder_->makeIntConstant(kSystemConstantNdcOffset));
  spv::Id ndc_offset = builder_->createLoad(
      builder_->createAccessChain(spv::StorageClassUniform,
                                  uniform_system_constants_, id_vector_temp_),
      spv::NoPrecision);
  spv::Id ndc_offset_mul_w = builder_->createNoContractionBinOp(
      spv::OpVectorTimesScalar, type_float3_, ndc_offset, position_w);
  position_xyz = builder_->createNoContractionBinOp(
      spv::OpFAdd, type_float3_, position_xyz, ndc_offset_mul_w);

  // Write the point size.
  if (output_point_size_ != spv::NoResult) {
    spv::Id point_size;
    if (current_shader().writes_point_size_edge_flag_kill_vertex() & 0b001) {
      assert_true(var_main_point_size_edge_flag_kill_vertex_ != spv::NoResult);
      id_vector_temp_.clear();
      // X vector component.
      id_vector_temp_.push_back(const_int_0_);
      point_size = builder_->createLoad(
          builder_->createAccessChain(
              spv::StorageClassFunction,
              var_main_point_size_edge_flag_kill_vertex_, id_vector_temp_),
          spv::NoPrecision);
    } else {
      // Not statically overridden - write a negative value.
      point_size = builder_->makeFloatConstant(-1.0f);
    }
    builder_->createStore(point_size, output_point_size_);
  }

  Modification shader_modification = GetSpirvShaderModification();

  // Expand the point sprite.
  if (shader_modification.vertex.host_vertex_shader_type ==
      Shader::HostVertexShaderType::kPointListAsTriangleStrip) {
    // Top-left, bottom-left, top-right, bottom-right order (chosen arbitrarily,
    // simply based on counterclockwise meaning front with
    // frontFace = VkFrontFace(0), but faceness is ignored for non-polygon
    // primitive types).
    id_vector_temp_.clear();
    id_vector_temp_.push_back(builder_->makeUintConstant(0b10));
    id_vector_temp_.push_back(builder_->makeUintConstant(0b01));
    spv::Id point_vertex_positive = builder_->createBinOp(
        spv::OpINotEqual, type_bool2_,
        builder_->createBinOp(
            spv::OpBitwiseAnd, type_uint2_,
            builder_->smearScalar(spv::NoPrecision,
                                  builder_->createUnaryOp(
                                      spv::OpBitcast, type_uint_,
                                      builder_->createLoad(input_vertex_index_,
                                                           spv::NoPrecision)),
                                  type_uint2_),
            builder_->createCompositeConstruct(type_uint2_, id_vector_temp_)),
        SpirvSmearScalarResultOrConstant(const_uint_0_, type_uint2_));

    // Load the point diameter in guest pixels, with the override from the
    // vertex shader if provided.
    id_vector_temp_.clear();
    id_vector_temp_.push_back(
        builder_->makeIntConstant(kSystemConstantPointConstantDiameter));
    spv::Id point_guest_diameter = builder_->createLoad(
        builder_->createAccessChain(spv::StorageClassUniform,
                                    uniform_system_constants_, id_vector_temp_),
        spv::NoPrecision);
    if (current_shader().writes_point_size_edge_flag_kill_vertex() & 0b001) {
      assert_true(var_main_point_size_edge_flag_kill_vertex_ != spv::NoResult);
      id_vector_temp_.clear();
      id_vector_temp_.push_back(const_int_0_);
      spv::Id point_vertex_diameter = builder_->createLoad(
          builder_->createAccessChain(
              spv::StorageClassFunction,
              var_main_point_size_edge_flag_kill_vertex_, id_vector_temp_),
          spv::NoPrecision);
      // The vertex shader's header writes -1.0 to point_size by default, so any
      // non-negative value means that it was overwritten by the translated
      // vertex shader, and needs to be used instead of the constant size. The
      // per-vertex diameter has already been clamped earlier in translation
      // (combined with making it non-negative).
      point_guest_diameter = builder_->createTriOp(
          spv::OpSelect, type_float2_,
          builder_->smearScalar(
              spv::NoPrecision,
              builder_->createBinOp(spv::OpFOrdGreaterThanEqual, type_bool_,
                                    point_vertex_diameter, const_float_0_),
              type_bool2_),
          builder_->smearScalar(spv::NoPrecision, point_vertex_diameter,
                                type_float2_),
          point_guest_diameter);
    }
    // Transform the diameter in the guest screen coordinates to radius in the
    // normalized device coordinates.
    id_vector_temp_.clear();
    id_vector_temp_.push_back(builder_->makeIntConstant(
        kSystemConstantPointScreenDiameterToNdcRadius));
    spv::Id point_radius = builder_->createNoContractionBinOp(
        spv::OpFMul, type_float2_, point_guest_diameter,
        builder_->createLoad(builder_->createAccessChain(
                                 spv::StorageClassUniform,
                                 uniform_system_constants_, id_vector_temp_),
                             spv::NoPrecision));
    // Transform the radius from the normalized device coordinates to the clip
    // space.
    point_radius = builder_->createNoContractionBinOp(
        spv::OpVectorTimesScalar, type_float2_, point_radius, position_w);

    // Expand the point sprite in the direction for the current host vertex.
    uint_vector_temp_.clear();
    uint_vector_temp_.push_back(0);
    uint_vector_temp_.push_back(1);
    spv::Id point_position_xy = builder_->createNoContractionBinOp(
        spv::OpFAdd, type_float2_,
        builder_->createRvalueSwizzle(spv::NoPrecision, type_float2_,
                                      position_xyz, uint_vector_temp_),
        builder_->createTriOp(spv::OpSelect, type_float2_,
                              point_vertex_positive, point_radius,
                              builder_->createNoContractionUnaryOp(
                                  spv::OpFNegate, type_float2_, point_radius)));
    // Store the position.
    spv::Id position;
    {
      // Bypass the `getNumTypeConstituents(typeId) == (int)constituents.size()`
      // assertion in createCompositeConstruct, OpCompositeConstruct can
      // construct vectors not only from scalars, but also from other vectors.
      std::unique_ptr<spv::Instruction> composite_construct_op =
          std::make_unique<spv::Instruction>(
              builder_->getUniqueId(), type_float4_, spv::OpCompositeConstruct);
      composite_construct_op->addIdOperand(point_position_xy);
      composite_construct_op->addIdOperand(
          builder_->createCompositeExtract(position_xyz, type_float_, 2));
      composite_construct_op->addIdOperand(position_w);
      position = composite_construct_op->getResultId();
      builder_->getBuildPoint()->addInstruction(
          std::move(composite_construct_op));
    }
    builder_->createStore(position, position_ptr);

    // Write the point coordinates.
    if (output_point_coordinates_ != spv::NoResult) {
      builder_->createStore(
          builder_->createTriOp(spv::OpSelect, type_float2_,
                                point_vertex_positive, const_float2_1_,
                                const_float2_0_),
          output_point_coordinates_);
    }

    // TODO(Triang3l): For points, handle ps_ucp_mode (take the guest clip space
    // coordinates instead of the host ones, calculate the distances to the user
    // clip planes, cull using the distance from the center for modes 0, 1 and
    // 2, cull and clip per-vertex for modes 2 and 3) in clip and cull
    // distances.
  } else {
    // Store the position converted to the host.
    spv::Id position;
    {
      // Bypass the `getNumTypeConstituents(typeId) == (int)constituents.size()`
      // assertion in createCompositeConstruct, OpCompositeConstruct can
      // construct vectors not only from scalars, but also from other vectors.
      std::unique_ptr<spv::Instruction> composite_construct_op =
          std::make_unique<spv::Instruction>(
              builder_->getUniqueId(), type_float4_, spv::OpCompositeConstruct);
      composite_construct_op->addIdOperand(position_xyz);
      composite_construct_op->addIdOperand(position_w);
      position = composite_construct_op->getResultId();
      builder_->getBuildPoint()->addInstruction(
          std::move(composite_construct_op));
    }
    builder_->createStore(position, position_ptr);
  }
}

void SpirvShaderTranslator::StartFragmentShaderBeforeMain() {
  Modification shader_modification = GetSpirvShaderModification();

  if (edram_fragment_shader_interlock_) {
    builder_->addExtension("SPV_EXT_fragment_shader_interlock");

    // EDRAM buffer uint[].
    id_vector_temp_.clear();
    id_vector_temp_.push_back(builder_->makeRuntimeArray(type_uint_));
    // Storage buffers have std430 packing, no padding to 4-component vectors.
    builder_->addDecoration(id_vector_temp_.back(), spv::DecorationArrayStride,
                            sizeof(uint32_t));
    spv::Id type_edram = builder_->makeStructType(id_vector_temp_, "XeEdram");
    builder_->addMemberName(type_edram, 0, "edram");
    builder_->addMemberDecoration(type_edram, 0, spv::DecorationCoherent);
    builder_->addMemberDecoration(type_edram, 0, spv::DecorationRestrict);
    builder_->addMemberDecoration(type_edram, 0, spv::DecorationOffset, 0);
    builder_->addDecoration(type_edram, features_.spirv_version >= spv::Spv_1_3
                                            ? spv::DecorationBlock
                                            : spv::DecorationBufferBlock);
    buffer_edram_ = builder_->createVariable(
        spv::NoPrecision,
        features_.spirv_version >= spv::Spv_1_3 ? spv::StorageClassStorageBuffer
                                                : spv::StorageClassUniform,
        type_edram, "xe_edram");
    builder_->addDecoration(buffer_edram_, spv::DecorationDescriptorSet,
                            int(kDescriptorSetSharedMemoryAndEdram));
    builder_->addDecoration(buffer_edram_, spv::DecorationBinding, 1);
    if (features_.spirv_version >= spv::Spv_1_4) {
      main_interface_.push_back(buffer_edram_);
    }
  }

  bool param_gen_needed = !is_depth_only_fragment_shader_ &&
                          GetPsParamGenInterpolator() != UINT32_MAX;

  if (!is_depth_only_fragment_shader_) {
    uint32_t input_location = 0;

    // Interpolator inputs.
    {
      uint32_t interpolators_remaining = GetModificationInterpolatorMask();
      uint32_t interpolator_index;
      while (
          xe::bit_scan_forward(interpolators_remaining, &interpolator_index)) {
        interpolators_remaining &= ~(UINT32_C(1) << interpolator_index);
        spv::Id interpolator = builder_->createVariable(
            spv::NoPrecision, spv::StorageClassInput, type_float4_,
            fmt::format("xe_in_interpolator_{}", interpolator_index).c_str());
        input_output_interpolators_[interpolator_index] = interpolator;
        builder_->addDecoration(interpolator, spv::DecorationLocation,
                                int(input_location));
        if (shader_modification.pixel.interpolators_centroid &
            (UINT32_C(1) << interpolator_index)) {
          builder_->addDecoration(interpolator, spv::DecorationCentroid);
        }
        main_interface_.push_back(interpolator);
        ++input_location;
      }
    }

    // Point coordinate input.
    if (shader_modification.pixel.param_gen_point) {
      if (param_gen_needed) {
        input_point_coordinates_ =
            builder_->createVariable(spv::NoPrecision, spv::StorageClassInput,
                                     type_float2_, "xe_in_point_coordinates");
        builder_->addDecoration(input_point_coordinates_,
                                spv::DecorationLocation, int(input_location));
        main_interface_.push_back(input_point_coordinates_);
      }
      ++input_location;
    }
  }

  // Fragment coordinates.
  // TODO(Triang3l): More conditions - alpha to coverage (if RT 0 is written,
  // and there's no early depth / stencil), depth writing in the fragment shader
  // (per-sample if supported).
  if (edram_fragment_shader_interlock_ || param_gen_needed) {
    input_fragment_coordinates_ = builder_->createVariable(
        spv::NoPrecision, spv::StorageClassInput, type_float4_, "gl_FragCoord");
    builder_->addDecoration(input_fragment_coordinates_, spv::DecorationBuiltIn,
                            spv::BuiltInFragCoord);
    main_interface_.push_back(input_fragment_coordinates_);
  }

  // Is front facing.
  if (edram_fragment_shader_interlock_ ||
      (param_gen_needed &&
       !GetSpirvShaderModification().pixel.param_gen_point)) {
    input_front_facing_ = builder_->createVariable(
        spv::NoPrecision, spv::StorageClassInput, type_bool_, "gl_FrontFacing");
    builder_->addDecoration(input_front_facing_, spv::DecorationBuiltIn,
                            spv::BuiltInFrontFacing);
    main_interface_.push_back(input_front_facing_);
  }

  // Sample mask input.
  if (edram_fragment_shader_interlock_) {
    // SampleMask depends on SampleRateShading in some SPIR-V revisions.
    builder_->addCapability(spv::CapabilitySampleRateShading);
    input_sample_mask_ = builder_->createVariable(
        spv::NoPrecision, spv::StorageClassInput,
        builder_->makeArrayType(type_int_, builder_->makeUintConstant(1), 0),
        "gl_SampleMaskIn");
    builder_->addDecoration(input_sample_mask_, spv::DecorationFlat);
    builder_->addDecoration(input_sample_mask_, spv::DecorationBuiltIn,
                            spv::BuiltInSampleMask);
    main_interface_.push_back(input_sample_mask_);
  }

  if (!is_depth_only_fragment_shader_) {
    // Framebuffer color attachment outputs.
    if (!edram_fragment_shader_interlock_) {
      std::fill(output_or_var_fragment_data_.begin(),
                output_or_var_fragment_data_.end(), spv::NoResult);
      static const char* const kFragmentDataOutputNames[] = {
          "xe_out_fragment_data_0",
          "xe_out_fragment_data_1",
          "xe_out_fragment_data_2",
          "xe_out_fragment_data_3",
      };
      uint32_t color_targets_remaining =
          current_shader().writes_color_targets();
      uint32_t color_target_index;
      while (
          xe::bit_scan_forward(color_targets_remaining, &color_target_index)) {
        color_targets_remaining &= ~(UINT32_C(1) << color_target_index);
        spv::Id output_fragment_data_rt = builder_->createVariable(
            spv::NoPrecision, spv::StorageClassOutput, type_float4_,
            kFragmentDataOutputNames[color_target_index]);
        output_or_var_fragment_data_[color_target_index] =
            output_fragment_data_rt;
        builder_->addDecoration(output_fragment_data_rt,
                                spv::DecorationLocation,
                                int(color_target_index));
        // Make invariant as pixel shaders may be used for various precise
        // computations.
        builder_->addDecoration(output_fragment_data_rt,
                                spv::DecorationInvariant);
        main_interface_.push_back(output_fragment_data_rt);
      }
    }
  }
}

void SpirvShaderTranslator::StartFragmentShaderInMain() {
  // Set up pixel killing from within the translated shader without affecting
  // the control flow (unlike with OpKill), similarly to how pixel killing works
  // on the Xenos, and also keeping a single critical section exit and return
  // for safety across different Vulkan implementations with fragment shader
  // interlock.
  if (current_shader().kills_pixels()) {
    if (features_.demote_to_helper_invocation) {
      // TODO(Triang3l): Promoted to SPIR-V 1.6 - don't add the extension there.
      builder_->addExtension("SPV_EXT_demote_to_helper_invocation");
      builder_->addCapability(spv::CapabilityDemoteToHelperInvocationEXT);
    } else {
      var_main_kill_pixel_ = builder_->createVariable(
          spv::NoPrecision, spv::StorageClassFunction, type_bool_,
          "xe_var_kill_pixel", builder_->makeBoolConstant(false));
    }
    // For killing with fragment shader interlock when demotion is supported,
    // using OpIsHelperInvocationEXT to avoid allocating a variable in addition
    // to the execution mask GPUs naturally have.
  }

  if (edram_fragment_shader_interlock_) {
    // Initialize color output variables with fragment shader interlock.
    std::fill(output_or_var_fragment_data_.begin(),
              output_or_var_fragment_data_.end(), spv::NoResult);
    var_main_fsi_color_written_ = spv::NoResult;
    uint32_t color_targets_written = current_shader().writes_color_targets();
    if (color_targets_written) {
      static const char* const kFragmentDataVariableNames[] = {
          "xe_var_fragment_data_0",
          "xe_var_fragment_data_1",
          "xe_var_fragment_data_2",
          "xe_var_fragment_data_3",
      };
      uint32_t color_targets_remaining = color_targets_written;
      uint32_t color_target_index;
      while (
          xe::bit_scan_forward(color_targets_remaining, &color_target_index)) {
        color_targets_remaining &= ~(UINT32_C(1) << color_target_index);
        output_or_var_fragment_data_[color_target_index] =
            builder_->createVariable(
                spv::NoPrecision, spv::StorageClassFunction, type_float4_,
                kFragmentDataVariableNames[color_target_index],
                const_float4_0_);
      }
      var_main_fsi_color_written_ = builder_->createVariable(
          spv::NoPrecision, spv::StorageClassFunction, type_uint_,
          "xe_var_fsi_color_written", const_uint_0_);
    }
  }

  if (edram_fragment_shader_interlock_ && FSI_IsDepthStencilEarly()) {
    spv::Id msaa_samples = LoadMsaaSamplesFromFlags();
    FSI_LoadSampleMask(msaa_samples);
    FSI_LoadEdramOffsets(msaa_samples);
    builder_->createNoResultOp(spv::OpBeginInvocationInterlockEXT);
    FSI_DepthStencilTest(msaa_samples, false);
    if (!is_depth_only_fragment_shader_) {
      // Skip the rest of the shader if the whole quad (due to derivatives) has
      // failed the depth / stencil test, and there are no depth and stencil
      // values to conditionally write after running the shader to check if
      // samples don't additionally need to be discarded.
      spv::Id quad_needs_execution = builder_->createBinOp(
          spv::OpINotEqual, type_bool_, main_fsi_sample_mask_, const_uint_0_);
      // TODO(Triang3l): Use GroupNonUniformQuad operations where supported.
      // If none of the pixels in the quad passed the depth / stencil test, the
      // value of (any samples covered ? 1.0f : 0.0f) for the current pixel will
      // be 0.0f, and since it will be 0.0f in other pixels too, the derivatives
      // will be zero as well.
      builder_->addCapability(spv::CapabilityDerivativeControl);
      // Query the horizontally adjacent pixel.
      quad_needs_execution = builder_->createBinOp(
          spv::OpLogicalOr, type_bool_, quad_needs_execution,
          builder_->createBinOp(
              spv::OpFOrdNotEqual, type_bool_,
              builder_->createUnaryOp(
                  spv::OpDPdxFine, type_float_,
                  builder_->createTriOp(spv::OpSelect, type_float_,
                                        quad_needs_execution, const_float_1_,
                                        const_float_0_)),
              const_float_0_));
      // Query the vertically adjacent pair of pixels.
      quad_needs_execution = builder_->createBinOp(
          spv::OpLogicalOr, type_bool_, quad_needs_execution,
          builder_->createBinOp(
              spv::OpFOrdNotEqual, type_bool_,
              builder_->createUnaryOp(
                  spv::OpDPdyCoarse, type_float_,
                  builder_->createTriOp(spv::OpSelect, type_float_,
                                        quad_needs_execution, const_float_1_,
                                        const_float_0_)),
              const_float_0_));
      spv::Block& main_fsi_early_depth_stencil_execute_quad =
          builder_->makeNewBlock();
      main_fsi_early_depth_stencil_execute_quad_merge_ =
          &builder_->makeNewBlock();
      builder_->createSelectionMerge(
          main_fsi_early_depth_stencil_execute_quad_merge_,
          spv::SelectionControlDontFlattenMask);
      builder_->createConditionalBranch(
          quad_needs_execution, &main_fsi_early_depth_stencil_execute_quad,
          main_fsi_early_depth_stencil_execute_quad_merge_);
      builder_->setBuildPoint(&main_fsi_early_depth_stencil_execute_quad);
    }
  }

  if (is_depth_only_fragment_shader_) {
    return;
  }

  uint32_t param_gen_interpolator = GetPsParamGenInterpolator();

  // Zero general-purpose registers to prevent crashes when the game
  // references them after only initializing them conditionally, and copy
  // interpolants to GPRs.
  uint32_t interpolator_mask = GetModificationInterpolatorMask();
  for (uint32_t i = 0; i < register_count(); ++i) {
    if (i == param_gen_interpolator) {
      continue;
    }
    id_vector_temp_.clear();
    id_vector_temp_.push_back(builder_->makeIntConstant(int(i)));
    builder_->createStore(
        (i < xenos::kMaxInterpolators &&
         (interpolator_mask & (UINT32_C(1) << i)))
            ? builder_->createLoad(input_output_interpolators_[i],
                                   spv::NoPrecision)
            : const_float4_0_,
        builder_->createAccessChain(spv::StorageClassFunction,
                                    var_main_registers_, id_vector_temp_));
  }

  // Pixel parameters.
  if (param_gen_interpolator != UINT32_MAX) {
    Modification modification = GetSpirvShaderModification();
    // Rounding the position down, and taking the absolute value, so in case the
    // host GPU for some reason has quads used for derivative calculation at odd
    // locations, the left and top edges will have correct derivative magnitude
    // and LODs.
    // Assuming that if PsParamGen is needed at all, param_gen_point is always
    // set for point primitives, and is always disabled for other primitive
    // types.
    // OpFNegate requires sign bit flipping even for 0.0 (in this case, the
    // first column or row of pixels) only since SPIR-V 1.5 revision 2 (not the
    // base 1.5).
    // TODO(Triang3l): When SPIR-V 1.6 is used in Xenia, see if OpFNegate can be
    // used there, should be cheaper because it may be implemented as a hardware
    // instruction modifier, though it respects the rule for subnormal numbers -
    // see the actual hardware instructions in both OpBitwiseXor and OpFNegate
    // cases.
    spv::Id const_sign_bit = builder_->makeUintConstant(UINT32_C(1) << 31);
    // TODO(Triang3l): Resolution scale inversion.
    // X - pixel X .0 in the magnitude, is back-facing in the sign bit.
    assert_true(input_fragment_coordinates_ != spv::NoResult);
    id_vector_temp_.clear();
    id_vector_temp_.push_back(const_int_0_);
    spv::Id param_gen_x = builder_->createUnaryBuiltinCall(
        type_float_, ext_inst_glsl_std_450_, GLSLstd450FAbs,
        builder_->createUnaryBuiltinCall(
            type_float_, ext_inst_glsl_std_450_, GLSLstd450Floor,
            builder_->createLoad(
                builder_->createAccessChain(spv::StorageClassInput,
                                            input_fragment_coordinates_,
                                            id_vector_temp_),
                spv::NoPrecision)));
    if (!modification.pixel.param_gen_point) {
      assert_true(input_front_facing_ != spv::NoResult);
      param_gen_x = builder_->createTriOp(
          spv::OpSelect, type_float_,
          builder_->createBinOp(
              spv::OpLogicalOr, type_bool_,
              builder_->createBinOp(
                  spv::OpIEqual, type_bool_,
                  builder_->createBinOp(
                      spv::OpBitwiseAnd, type_uint_,
                      main_system_constant_flags_,
                      builder_->makeUintConstant(kSysFlag_PrimitivePolygonal)),
                  const_uint_0_),
              builder_->createLoad(input_front_facing_, spv::NoPrecision)),
          param_gen_x,
          builder_->createUnaryOp(
              spv::OpBitcast, type_float_,
              builder_->createBinOp(
                  spv::OpBitwiseXor, type_uint_,
                  builder_->createUnaryOp(spv::OpBitcast, type_uint_,
                                          param_gen_x),
                  const_sign_bit)));
    }
    // Y - pixel Y .0 in the magnitude, is point in the sign bit.
    id_vector_temp_.clear();
    id_vector_temp_.push_back(builder_->makeIntConstant(1));
    spv::Id param_gen_y = builder_->createUnaryBuiltinCall(
        type_float_, ext_inst_glsl_std_450_, GLSLstd450FAbs,
        builder_->createUnaryBuiltinCall(
            type_float_, ext_inst_glsl_std_450_, GLSLstd450Floor,
            builder_->createLoad(
                builder_->createAccessChain(spv::StorageClassInput,
                                            input_fragment_coordinates_,
                                            id_vector_temp_),
                spv::NoPrecision)));
    if (modification.pixel.param_gen_point) {
      param_gen_y = builder_->createUnaryOp(
          spv::OpBitcast, type_float_,
          builder_->createBinOp(
              spv::OpBitwiseXor, type_uint_,
              builder_->createUnaryOp(spv::OpBitcast, type_uint_, param_gen_y),
              const_sign_bit));
    }
    // Z - point S in the magnitude, is line in the sign bit.
    // W - point T in the magnitude.
    spv::Id param_gen_z, param_gen_w;
    if (modification.pixel.param_gen_point) {
      assert_true(input_point_coordinates_ != spv::NoResult);
      // Saturate to avoid negative point coordinates if the center of the pixel
      // is not covered, and extrapolation is done.
      spv::Id param_gen_point_coordinates = builder_->createTriBuiltinCall(
          type_float2_, ext_inst_glsl_std_450_, GLSLstd450NClamp,
          builder_->createLoad(input_point_coordinates_, spv::NoPrecision),
          const_float2_0_, const_float2_1_);
      param_gen_z = builder_->createCompositeExtract(
          param_gen_point_coordinates, type_float_, 0);
      param_gen_w = builder_->createCompositeExtract(
          param_gen_point_coordinates, type_float_, 1);
    } else {
      param_gen_z = builder_->createUnaryOp(
          spv::OpBitcast, type_float_,
          builder_->createTriOp(
              spv::OpSelect, type_uint_,
              builder_->createBinOp(
                  spv::OpINotEqual, type_bool_,
                  builder_->createBinOp(
                      spv::OpBitwiseAnd, type_uint_,
                      main_system_constant_flags_,
                      builder_->makeUintConstant(kSysFlag_PrimitiveLine)),
                  const_uint_0_),
              const_sign_bit, const_uint_0_));
      param_gen_w = const_float_0_;
    }
    // Store the pixel parameters.
    id_vector_temp_.clear();
    id_vector_temp_.push_back(param_gen_x);
    id_vector_temp_.push_back(param_gen_y);
    id_vector_temp_.push_back(param_gen_z);
    id_vector_temp_.push_back(param_gen_w);
    spv::Id param_gen =
        builder_->createCompositeConstruct(type_float4_, id_vector_temp_);
    id_vector_temp_.clear();
    id_vector_temp_.push_back(
        builder_->makeIntConstant(int(param_gen_interpolator)));
    builder_->createStore(param_gen, builder_->createAccessChain(
                                         spv::StorageClassFunction,
                                         var_main_registers_, id_vector_temp_));
  }

  if (!edram_fragment_shader_interlock_) {
    // Initialize the colors for safety.
    for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
      spv::Id output_fragment_data_rt = output_or_var_fragment_data_[i];
      if (output_fragment_data_rt != spv::NoResult) {
        builder_->createStore(const_float4_0_, output_fragment_data_rt);
      }
    }
  }
}

void SpirvShaderTranslator::UpdateExecConditionals(
    ParsedExecInstruction::Type type, uint32_t bool_constant_index,
    bool condition) {
  // Check if we can merge the new exec with the previous one, or the jump with
  // the previous exec. The instruction-level predicate check is also merged in
  // this case.
  if (type == ParsedExecInstruction::Type::kConditional) {
    // Can merge conditional with conditional, as long as the bool constant and
    // the expected values are the same.
    if (cf_exec_conditional_merge_ &&
        cf_exec_bool_constant_or_predicate_ == bool_constant_index &&
        cf_exec_condition_ == condition) {
      return;
    }
  } else if (type == ParsedExecInstruction::Type::kPredicated) {
    // Can merge predicated with predicated if the conditions are the same and
    // the previous exec hasn't modified the predicate register.
    if (!cf_exec_predicate_written_ && cf_exec_conditional_merge_ &&
        cf_exec_bool_constant_or_predicate_ == kCfExecBoolConstantPredicate &&
        cf_exec_condition_ == condition) {
      return;
    }
  } else {
    // Can merge unconditional with unconditional.
    assert_true(type == ParsedExecInstruction::Type::kUnconditional);
    if (!cf_exec_conditional_merge_) {
      return;
    }
  }

  CloseExecConditionals();

  if (type == ParsedExecInstruction::Type::kUnconditional) {
    return;
  }

  EnsureBuildPointAvailable();
  spv::Id condition_id;
  if (type == ParsedExecInstruction::Type::kConditional) {
    id_vector_temp_.clear();
    // Bool constants (member 0).
    id_vector_temp_.push_back(const_int_0_);
    // 128-bit vector.
    id_vector_temp_.push_back(
        builder_->makeIntConstant(int(bool_constant_index >> 7)));
    // 32-bit scalar of a 128-bit vector.
    id_vector_temp_.push_back(
        builder_->makeIntConstant(int((bool_constant_index >> 5) & 3)));
    spv::Id bool_constant_scalar =
        builder_->createLoad(builder_->createAccessChain(
                                 spv::StorageClassUniform,
                                 uniform_bool_loop_constants_, id_vector_temp_),
                             spv::NoPrecision);
    condition_id = builder_->createBinOp(
        spv::OpINotEqual, type_bool_,
        builder_->createBinOp(
            spv::OpBitwiseAnd, type_uint_, bool_constant_scalar,
            builder_->makeUintConstant(uint32_t(1)
                                       << (bool_constant_index & 31))),
        const_uint_0_);
    cf_exec_bool_constant_or_predicate_ = bool_constant_index;
  } else if (type == ParsedExecInstruction::Type::kPredicated) {
    condition_id = builder_->createLoad(var_main_predicate_, spv::NoPrecision);
    cf_exec_bool_constant_or_predicate_ = kCfExecBoolConstantPredicate;
  } else {
    assert_unhandled_case(type);
    return;
  }
  cf_exec_condition_ = condition;
  cf_exec_conditional_merge_ = new spv::Block(
      builder_->getUniqueId(), builder_->getBuildPoint()->getParent());
  builder_->createSelectionMerge(cf_exec_conditional_merge_,
                                 spv::SelectionControlDontFlattenMask);
  spv::Block& inner_block = builder_->makeNewBlock();
  builder_->createConditionalBranch(
      condition_id, condition ? &inner_block : cf_exec_conditional_merge_,
      condition ? cf_exec_conditional_merge_ : &inner_block);
  builder_->setBuildPoint(&inner_block);
}

void SpirvShaderTranslator::UpdateInstructionPredication(bool predicated,
                                                         bool condition) {
  if (!predicated) {
    CloseInstructionPredication();
    return;
  }

  if (cf_instruction_predicate_merge_) {
    if (cf_instruction_predicate_condition_ == condition) {
      // Already in the needed instruction-level conditional.
      return;
    }
    CloseInstructionPredication();
  }

  // If the instruction predicate condition is the same as the exec predicate
  // condition, no need to open a check. However, if there was a `setp` prior
  // to this instruction, the predicate value now may be different than it was
  // in the beginning of the exec.
  if (!cf_exec_predicate_written_ && cf_exec_conditional_merge_ &&
      cf_exec_bool_constant_or_predicate_ == kCfExecBoolConstantPredicate &&
      cf_exec_condition_ == condition) {
    return;
  }

  cf_instruction_predicate_condition_ = condition;
  EnsureBuildPointAvailable();
  spv::Id predicate_id =
      builder_->createLoad(var_main_predicate_, spv::NoPrecision);
  spv::Block& predicated_block = builder_->makeNewBlock();
  cf_instruction_predicate_merge_ = new spv::Block(
      builder_->getUniqueId(), builder_->getBuildPoint()->getParent());
  builder_->createSelectionMerge(cf_instruction_predicate_merge_,
                                 spv::SelectionControlMaskNone);
  builder_->createConditionalBranch(
      predicate_id,
      condition ? &predicated_block : cf_instruction_predicate_merge_,
      condition ? cf_instruction_predicate_merge_ : &predicated_block);
  builder_->setBuildPoint(&predicated_block);
}

void SpirvShaderTranslator::CloseInstructionPredication() {
  if (!cf_instruction_predicate_merge_) {
    return;
  }
  spv::Block& inner_block = *builder_->getBuildPoint();
  if (!inner_block.isTerminated()) {
    builder_->createBranch(cf_instruction_predicate_merge_);
  }
  inner_block.getParent().addBlock(cf_instruction_predicate_merge_);
  builder_->setBuildPoint(cf_instruction_predicate_merge_);
  cf_instruction_predicate_merge_ = nullptr;
}

void SpirvShaderTranslator::CloseExecConditionals() {
  // Within the exec - instruction-level predicate check.
  CloseInstructionPredication();
  // Exec level.
  if (cf_exec_conditional_merge_) {
    spv::Block& inner_block = *builder_->getBuildPoint();
    if (!inner_block.isTerminated()) {
      builder_->createBranch(cf_exec_conditional_merge_);
    }
    inner_block.getParent().addBlock(cf_exec_conditional_merge_);
    builder_->setBuildPoint(cf_exec_conditional_merge_);
    cf_exec_conditional_merge_ = nullptr;
  }
  // Nothing relies on the predicate value being unchanged now.
  cf_exec_predicate_written_ = false;
}

spv::Id SpirvShaderTranslator::GetStorageAddressingIndex(
    InstructionStorageAddressingMode addressing_mode, uint32_t storage_index,
    bool is_float_constant) {
  const Shader::ConstantRegisterMap& constant_register_map =
      current_shader().constant_register_map();
  EnsureBuildPointAvailable();
  spv::Id base_pointer = spv::NoResult;
  switch (addressing_mode) {
    case InstructionStorageAddressingMode::kAbsolute: {
      uint32_t static_storage_index = storage_index;
      if (is_float_constant) {
        static_storage_index =
            constant_register_map.GetPackedFloatConstantIndex(storage_index);
        assert_true(static_storage_index != UINT32_MAX);
        if (static_storage_index == UINT32_MAX) {
          static_storage_index = 0;
        }
      }
      return builder_->makeIntConstant(int(static_storage_index));
    }
    case InstructionStorageAddressingMode::kAddressRegisterRelative:
      base_pointer = var_main_address_register_;
      break;
    case InstructionStorageAddressingMode::kLoopRelative:
      // Load X component.
      id_vector_temp_util_.clear();
      id_vector_temp_util_.push_back(const_int_0_);
      base_pointer = builder_->createAccessChain(spv::StorageClassFunction,
                                                 var_main_loop_address_,
                                                 id_vector_temp_util_);
      break;
  }
  assert_true(!is_float_constant ||
              constant_register_map.float_dynamic_addressing);
  assert_true(base_pointer != spv::NoResult);
  spv::Id index = builder_->createLoad(base_pointer, spv::NoPrecision);
  if (storage_index) {
    index =
        builder_->createBinOp(spv::OpIAdd, type_int_, index,
                              builder_->makeIntConstant(int(storage_index)));
  }
  return index;
}

spv::Id SpirvShaderTranslator::LoadOperandStorage(
    const InstructionOperand& operand) {
  spv::Id index = GetStorageAddressingIndex(
      operand.storage_addressing_mode, operand.storage_index,
      operand.storage_source == InstructionStorageSource::kConstantFloat);
  EnsureBuildPointAvailable();
  spv::Id vec4_pointer = spv::NoResult;
  switch (operand.storage_source) {
    case InstructionStorageSource::kRegister:
      assert_true(var_main_registers_ != spv::NoResult);
      id_vector_temp_util_.clear();
      // Array element.
      id_vector_temp_util_.push_back(index);
      vec4_pointer = builder_->createAccessChain(
          spv::StorageClassFunction, var_main_registers_, id_vector_temp_util_);
      break;
    case InstructionStorageSource::kConstantFloat:
      assert_true(uniform_float_constants_ != spv::NoResult);
      id_vector_temp_util_.clear();
      // The first and the only structure member.
      id_vector_temp_util_.push_back(const_int_0_);
      // Array element.
      id_vector_temp_util_.push_back(index);
      vec4_pointer = builder_->createAccessChain(spv::StorageClassUniform,
                                                 uniform_float_constants_,
                                                 id_vector_temp_util_);
      break;
    default:
      assert_unhandled_case(operand.storage_source);
  }
  assert_true(vec4_pointer != spv::NoResult);
  return builder_->createLoad(vec4_pointer, spv::NoPrecision);
}

spv::Id SpirvShaderTranslator::ApplyOperandModifiers(
    spv::Id operand_value, const InstructionOperand& original_operand,
    bool invert_negate, bool force_absolute) {
  spv::Id type = builder_->getTypeId(operand_value);
  assert_true(type != spv::NoType);
  if (type == spv::NoType) {
    return operand_value;
  }
  if (original_operand.is_absolute_value || force_absolute) {
    EnsureBuildPointAvailable();
    operand_value = builder_->createUnaryBuiltinCall(
        type, ext_inst_glsl_std_450_, GLSLstd450FAbs, operand_value);
  }
  if (original_operand.is_negated != invert_negate) {
    EnsureBuildPointAvailable();
    operand_value = builder_->createNoContractionUnaryOp(spv::OpFNegate, type,
                                                         operand_value);
  }
  return operand_value;
}

spv::Id SpirvShaderTranslator::GetUnmodifiedOperandComponents(
    spv::Id operand_storage, const InstructionOperand& original_operand,
    uint32_t components) {
  assert_not_zero(components);
  if (!components) {
    return spv::NoResult;
  }
  assert_true(components <= 0b1111);
  if (components == 0b1111 && original_operand.IsStandardSwizzle()) {
    return operand_storage;
  }
  EnsureBuildPointAvailable();
  uint32_t component_count = xe::bit_count(components);
  if (component_count == 1) {
    uint32_t scalar_index;
    xe::bit_scan_forward(components, &scalar_index);
    return builder_->createCompositeExtract(
        operand_storage, type_float_,
        static_cast<unsigned int>(original_operand.GetComponent(scalar_index)) -
            static_cast<unsigned int>(SwizzleSource::kX));
  }
  uint_vector_temp_util_.clear();
  uint32_t components_remaining = components;
  uint32_t component_index;
  while (xe::bit_scan_forward(components_remaining, &component_index)) {
    components_remaining &= ~(uint32_t(1) << component_index);
    uint_vector_temp_util_.push_back(
        static_cast<unsigned int>(
            original_operand.GetComponent(component_index)) -
        static_cast<unsigned int>(SwizzleSource::kX));
  }
  return builder_->createRvalueSwizzle(spv::NoPrecision,
                                       type_float_vectors_[component_count - 1],
                                       operand_storage, uint_vector_temp_util_);
}

void SpirvShaderTranslator::GetOperandScalarXY(
    spv::Id operand_storage, const InstructionOperand& original_operand,
    spv::Id& a_out, spv::Id& b_out, bool invert_negate, bool force_absolute) {
  spv::Id a = GetOperandComponents(operand_storage, original_operand, 0b0001,
                                   invert_negate, force_absolute);
  a_out = a;
  b_out = original_operand.GetComponent(0) != original_operand.GetComponent(1)
              ? GetOperandComponents(operand_storage, original_operand, 0b0010,
                                     invert_negate, force_absolute)
              : a;
}

spv::Id SpirvShaderTranslator::GetAbsoluteOperand(
    spv::Id operand_storage, const InstructionOperand& original_operand) {
  if (original_operand.is_absolute_value && !original_operand.is_negated) {
    return operand_storage;
  }
  EnsureBuildPointAvailable();
  return builder_->createUnaryBuiltinCall(builder_->getTypeId(operand_storage),
                                          ext_inst_glsl_std_450_,
                                          GLSLstd450FAbs, operand_storage);
}

void SpirvShaderTranslator::StoreResult(const InstructionResult& result,
                                        spv::Id value) {
  uint32_t used_write_mask = result.GetUsedWriteMask();
  if (!used_write_mask) {
    return;
  }

  EnsureBuildPointAvailable();

  spv::Id target_pointer = spv::NoResult;
  switch (result.storage_target) {
    case InstructionStorageTarget::kNone:
      break;
    case InstructionStorageTarget::kRegister: {
      assert_true(var_main_registers_ != spv::NoResult);
      // Must call GetStorageAddressingIndex first because of
      // id_vector_temp_util_ usage in it.
      spv::Id register_index = GetStorageAddressingIndex(
          result.storage_addressing_mode, result.storage_index);
      id_vector_temp_util_.clear();
      // Array element.
      id_vector_temp_util_.push_back(register_index);
      target_pointer = builder_->createAccessChain(
          spv::StorageClassFunction, var_main_registers_, id_vector_temp_util_);
    } break;
    case InstructionStorageTarget::kInterpolator: {
      assert_true(is_vertex_shader());
      target_pointer = input_output_interpolators_[result.storage_index];
      // Unused interpolators are spv::NoResult in input_output_interpolators_.
    } break;
    case InstructionStorageTarget::kPosition: {
      assert_true(is_vertex_shader());
      id_vector_temp_util_.clear();
      id_vector_temp_util_.push_back(
          builder_->makeIntConstant(kOutputPerVertexMemberPosition));
      target_pointer = builder_->createAccessChain(
          spv::StorageClassOutput, output_per_vertex_, id_vector_temp_util_);
    } break;
    case InstructionStorageTarget::kPointSizeEdgeFlagKillVertex: {
      assert_true(is_vertex_shader());
      assert_zero(used_write_mask & 0b1000);
      target_pointer = var_main_point_size_edge_flag_kill_vertex_;
    } break;
    case InstructionStorageTarget::kColor: {
      assert_true(is_pixel_shader());
      assert_not_zero(used_write_mask);
      assert_true(current_shader().writes_color_target(result.storage_index));
      target_pointer = output_or_var_fragment_data_[result.storage_index];
      if (edram_fragment_shader_interlock_) {
        assert_true(var_main_fsi_color_written_ != spv::NoResult);
        builder_->createStore(
            builder_->createBinOp(
                spv::OpBitwiseOr, type_uint_,
                builder_->createLoad(var_main_fsi_color_written_,
                                     spv::NoPrecision),
                builder_->makeUintConstant(uint32_t(1)
                                           << result.storage_index)),
            var_main_fsi_color_written_);
      }
    } break;
    default:
      // TODO(Triang3l): All storage targets.
      break;
  }
  if (target_pointer == spv::NoResult) {
    return;
  }

  uint32_t constant_values;
  uint32_t constant_components =
      result.GetUsedConstantComponents(constant_values);
  if (value == spv::NoResult) {
    // The instruction processing function decided that nothing useful needs to
    // be stored for some reason, however, some components still need to be
    // written on the guest side - fill them with zeros.
    constant_components = used_write_mask;
  }
  uint32_t non_constant_components = used_write_mask & ~constant_components;

  unsigned int value_num_components =
      value != spv::NoResult
          ? static_cast<unsigned int>(builder_->getNumComponents(value))
          : 0;

  if (result.is_clamped && non_constant_components) {
    // Apply the saturation modifier to the result.
    value = builder_->createTriBuiltinCall(
        type_float_vectors_[value_num_components - 1], ext_inst_glsl_std_450_,
        GLSLstd450NClamp, value,
        const_float_vectors_0_[value_num_components - 1],
        const_float_vectors_1_[value_num_components - 1]);
  }

  // The value contains either result.GetUsedResultComponents() in a condensed
  // way, or a scalar to be replicated. Decompress them to create a mapping from
  // guest result components to the ones in the value vector.
  uint32_t used_result_components = result.GetUsedResultComponents();
  unsigned int result_unswizzled_value_components[4] = {};
  if (value_num_components > 1) {
    unsigned int value_component = 0;
    uint32_t used_result_components_remaining = used_result_components;
    uint32_t result_component;
    while (xe::bit_scan_forward(used_result_components_remaining,
                                &result_component)) {
      used_result_components_remaining &= ~(uint32_t(1) << result_component);
      result_unswizzled_value_components[result_component] =
          std::min(value_component++, value_num_components - 1);
    }
  }

  // Get swizzled mapping of non-constant components to the components of
  // `value`.
  unsigned int result_swizzled_value_components[4] = {};
  for (uint32_t i = 0; i < 4; ++i) {
    if (!(non_constant_components & (1 << i))) {
      continue;
    }
    SwizzleSource swizzle = result.components[i];
    assert_true(swizzle >= SwizzleSource::kX && swizzle <= SwizzleSource::kW);
    result_swizzled_value_components[i] =
        result_unswizzled_value_components[uint32_t(swizzle) -
                                           uint32_t(SwizzleSource::kX)];
  }

  spv::Id target_type = builder_->getDerefTypeId(target_pointer);
  unsigned int target_num_components =
      builder_->getNumTypeComponents(target_type);
  assert_true(
      target_num_components ==
      GetInstructionStorageTargetUsedComponentCount(result.storage_target));
  uint32_t target_component_mask = (1 << target_num_components) - 1;
  assert_zero(used_write_mask & ~target_component_mask);

  spv::Id value_to_store;
  if (target_component_mask == used_write_mask) {
    // All components are overwritten - no need to load the original value.
    // Possible cases:
    // * Non-constants only.
    //   * Vector target.
    //     * Vector source.
    //       * Identity swizzle - store directly.
    //       * Non-identity swizzle - shuffle.
    //     * Scalar source - smear.
    //   * Scalar target.
    //     * Vector source - extract.
    //     * Scalar source - store directly.
    // * Constants only.
    //   * Vector target - make composite constant.
    //   * Scalar target - store directly.
    // * Mixed non-constants and constants (only for vector targets - scalar
    //   targets fully covered by the previous cases).
    //   * Vector source - shuffle with {0, 1} also applying swizzle.
    //   * Scalar source - construct composite.
    if (!constant_components) {
      if (target_num_components > 1) {
        if (value_num_components > 1) {
          // Non-constants only - vector target, vector source.
          bool is_identity_swizzle =
              target_num_components == value_num_components;
          for (uint32_t i = 0; is_identity_swizzle && i < target_num_components;
               ++i) {
            is_identity_swizzle &= result_swizzled_value_components[i] == i;
          }
          if (is_identity_swizzle) {
            value_to_store = value;
          } else {
            uint_vector_temp_util_.clear();
            uint_vector_temp_util_.insert(
                uint_vector_temp_util_.cend(), result_swizzled_value_components,
                result_swizzled_value_components + target_num_components);
            value_to_store = builder_->createRvalueSwizzle(
                spv::NoPrecision, target_type, value, uint_vector_temp_util_);
          }
        } else {
          // Non-constants only - vector target, scalar source.
          value_to_store =
              builder_->smearScalar(spv::NoPrecision, value, target_type);
        }
      } else {
        if (value_num_components > 1) {
          // Non-constants only - scalar target, vector source.
          value_to_store = builder_->createCompositeExtract(
              value, type_float_, result_swizzled_value_components[0]);
        } else {
          // Non-constants only - scalar target, scalar source.
          value_to_store = value;
        }
      }
    } else if (!non_constant_components) {
      if (target_num_components > 1) {
        // Constants only - vector target.
        id_vector_temp_util_.clear();
        for (uint32_t i = 0; i < target_num_components; ++i) {
          id_vector_temp_util_.push_back(
              (constant_values & (1 << i)) ? const_float_1_ : const_float_0_);
        }
        value_to_store =
            builder_->makeCompositeConstant(target_type, id_vector_temp_util_);
      } else {
        // Constants only - scalar target.
        value_to_store =
            (constant_values & 0b0001) ? const_float_1_ : const_float_0_;
      }
    } else {
      assert_true(target_num_components > 1);
      if (value_num_components > 1) {
        // Mixed non-constants and constants - vector source.
        std::unique_ptr<spv::Instruction> shuffle_op =
            std::make_unique<spv::Instruction>(
                builder_->getUniqueId(), target_type, spv::OpVectorShuffle);
        shuffle_op->addIdOperand(value);
        shuffle_op->addIdOperand(const_float2_0_1_);
        for (uint32_t i = 0; i < target_num_components; ++i) {
          shuffle_op->addImmediateOperand(
              (constant_components & (1 << i))
                  ? value_num_components + ((constant_values >> i) & 1)
                  : result_swizzled_value_components[i]);
        }
        value_to_store = shuffle_op->getResultId();
        builder_->getBuildPoint()->addInstruction(std::move(shuffle_op));
      } else {
        // Mixed non-constants and constants - scalar source.
        id_vector_temp_util_.clear();
        for (uint32_t i = 0; i < target_num_components; ++i) {
          if (constant_components & (1 << i)) {
            id_vector_temp_util_.push_back(
                (constant_values & (1 << i)) ? const_float_1_ : const_float_0_);
          } else {
            id_vector_temp_util_.push_back(value);
          }
        }
        value_to_store = builder_->createCompositeConstruct(
            target_type, id_vector_temp_util_);
      }
    }
  } else {
    // Only certain components are overwritten.
    // Scalar targets are always overwritten fully, can't reach this case for
    // them.
    assert_true(target_num_components > 1);
    value_to_store = builder_->createLoad(target_pointer, spv::NoPrecision);
    // Two steps:
    // 1) Insert constants by shuffling (first so dependency chain of step 2 is
    //    simpler if constants are written first).
    // 2) Insert value components - via shuffling for vector source, via
    //    composite inserts for scalar value.
    if (constant_components) {
      std::unique_ptr<spv::Instruction> shuffle_op =
          std::make_unique<spv::Instruction>(builder_->getUniqueId(),
                                             target_type, spv::OpVectorShuffle);
      shuffle_op->addIdOperand(value_to_store);
      shuffle_op->addIdOperand(const_float2_0_1_);
      for (uint32_t i = 0; i < target_num_components; ++i) {
        shuffle_op->addImmediateOperand((constant_components & (1 << i))
                                            ? target_num_components +
                                                  ((constant_values >> i) & 1)
                                            : i);
      }
      value_to_store = shuffle_op->getResultId();
      builder_->getBuildPoint()->addInstruction(std::move(shuffle_op));
    }
    if (non_constant_components) {
      if (value_num_components > 1) {
        std::unique_ptr<spv::Instruction> shuffle_op =
            std::make_unique<spv::Instruction>(
                builder_->getUniqueId(), target_type, spv::OpVectorShuffle);
        shuffle_op->addIdOperand(value_to_store);
        shuffle_op->addIdOperand(value);
        for (uint32_t i = 0; i < target_num_components; ++i) {
          shuffle_op->addImmediateOperand(
              (non_constant_components & (1 << i))
                  ? target_num_components + result_swizzled_value_components[i]
                  : i);
        }
        value_to_store = shuffle_op->getResultId();
        builder_->getBuildPoint()->addInstruction(std::move(shuffle_op));
      } else {
        for (uint32_t i = 0; i < target_num_components; ++i) {
          if (non_constant_components & (1 << i)) {
            value_to_store = builder_->createCompositeInsert(
                value, value_to_store, target_type, i);
          }
        }
      }
    }
  }

  if (result.storage_target ==
          InstructionStorageTarget::kPointSizeEdgeFlagKillVertex &&
      used_write_mask & 0b001) {
    // Make the point size non-negative as negative is used to indicate that the
    // default size must be used, and also clamp it to the bounds the way the
    // R400 (Adreno 200, to be more precise) hardware clamps it (functionally
    // like a signed 32-bit integer, -NaN and -Infinity...-0 to the minimum,
    // +NaN to the maximum).
    spv::Id point_size = builder_->createUnaryOp(
        spv::OpBitcast, type_int_,
        builder_->createCompositeExtract(value_to_store, type_float_, 0));
    id_vector_temp_util_.clear();
    id_vector_temp_util_.push_back(
        builder_->makeIntConstant(kSystemConstantPointVertexDiameterMin));
    spv::Id point_vertex_diameter_min = builder_->createUnaryOp(
        spv::OpBitcast, type_int_,
        builder_->createLoad(
            builder_->createAccessChain(spv::StorageClassUniform,
                                        uniform_system_constants_,
                                        id_vector_temp_util_),
            spv::NoPrecision));
    point_size = builder_->createBinBuiltinCall(
        type_int_, ext_inst_glsl_std_450_, GLSLstd450SMax,
        point_vertex_diameter_min, point_size);
    id_vector_temp_util_.clear();
    id_vector_temp_util_.push_back(
        builder_->makeIntConstant(kSystemConstantPointVertexDiameterMax));
    spv::Id point_vertex_diameter_max = builder_->createUnaryOp(
        spv::OpBitcast, type_int_,
        builder_->createLoad(
            builder_->createAccessChain(spv::StorageClassUniform,
                                        uniform_system_constants_,
                                        id_vector_temp_util_),
            spv::NoPrecision));
    point_size = builder_->createBinBuiltinCall(
        type_int_, ext_inst_glsl_std_450_, GLSLstd450SMin,
        point_vertex_diameter_max, point_size);
    value_to_store = builder_->createCompositeInsert(
        builder_->createUnaryOp(spv::OpBitcast, type_float_, point_size),
        value_to_store, type_float3_, 0);
  }

  builder_->createStore(value_to_store, target_pointer);
}

spv::Id SpirvShaderTranslator::EndianSwap32Uint(spv::Id value, spv::Id endian) {
  spv::Id type = builder_->getTypeId(value);
  spv::Id const_uint_8_scalar = builder_->makeUintConstant(8);
  spv::Id const_uint_00ff00ff_scalar = builder_->makeUintConstant(0x00FF00FF);
  spv::Id const_uint_16_scalar = builder_->makeUintConstant(16);
  spv::Id const_uint_8_typed, const_uint_00ff00ff_typed, const_uint_16_typed;
  int num_components = builder_->getNumTypeComponents(type);
  if (num_components > 1) {
    id_vector_temp_.clear();
    id_vector_temp_.insert(id_vector_temp_.cend(), num_components,
                           const_uint_8_scalar);
    const_uint_8_typed = builder_->makeCompositeConstant(type, id_vector_temp_);
    id_vector_temp_.clear();
    id_vector_temp_.insert(id_vector_temp_.cend(), num_components,
                           const_uint_00ff00ff_scalar);
    const_uint_00ff00ff_typed =
        builder_->makeCompositeConstant(type, id_vector_temp_);
    id_vector_temp_.clear();
    id_vector_temp_.insert(id_vector_temp_.cend(), num_components,
                           const_uint_16_scalar);
    const_uint_16_typed =
        builder_->makeCompositeConstant(type, id_vector_temp_);
  } else {
    const_uint_8_typed = const_uint_8_scalar;
    const_uint_00ff00ff_typed = const_uint_00ff00ff_scalar;
    const_uint_16_typed = const_uint_16_scalar;
  }

  // 8-in-16 or one half of 8-in-32 (doing 8-in-16 swap).
  spv::Id is_8in16 = builder_->createBinOp(
      spv::OpIEqual, type_bool_, endian,
      builder_->makeUintConstant(
          static_cast<unsigned int>(xenos::Endian::k8in16)));
  spv::Id is_8in32 = builder_->createBinOp(
      spv::OpIEqual, type_bool_, endian,
      builder_->makeUintConstant(
          static_cast<unsigned int>(xenos::Endian::k8in32)));
  spv::Id is_8in16_or_8in32 =
      builder_->createBinOp(spv::OpLogicalOr, type_bool_, is_8in16, is_8in32);
  spv::Block& block_pre_8in16 = *builder_->getBuildPoint();
  assert_false(block_pre_8in16.isTerminated());
  spv::Block& block_8in16 = builder_->makeNewBlock();
  spv::Block& block_8in16_merge = builder_->makeNewBlock();
  builder_->createSelectionMerge(&block_8in16_merge,
                                 spv::SelectionControlMaskNone);
  builder_->createConditionalBranch(is_8in16_or_8in32, &block_8in16,
                                    &block_8in16_merge);
  builder_->setBuildPoint(&block_8in16);
  spv::Id swapped_8in16 = builder_->createBinOp(
      spv::OpBitwiseOr, type,
      builder_->createBinOp(
          spv::OpBitwiseAnd, type,
          builder_->createBinOp(spv::OpShiftRightLogical, type, value,
                                const_uint_8_typed),
          const_uint_00ff00ff_typed),
      builder_->createBinOp(
          spv::OpShiftLeftLogical, type,
          builder_->createBinOp(spv::OpBitwiseAnd, type, value,
                                const_uint_00ff00ff_typed),
          const_uint_8_typed));
  builder_->createBranch(&block_8in16_merge);
  builder_->setBuildPoint(&block_8in16_merge);
  {
    std::unique_ptr<spv::Instruction> phi_op =
        std::make_unique<spv::Instruction>(builder_->getUniqueId(), type,
                                           spv::OpPhi);
    phi_op->addIdOperand(swapped_8in16);
    phi_op->addIdOperand(block_8in16.getId());
    phi_op->addIdOperand(value);
    phi_op->addIdOperand(block_pre_8in16.getId());
    value = phi_op->getResultId();
    builder_->getBuildPoint()->addInstruction(std::move(phi_op));
  }

  // 16-in-32 or another half of 8-in-32 (doing 16-in-32 swap).
  spv::Id is_16in32 = builder_->createBinOp(
      spv::OpIEqual, type_bool_, endian,
      builder_->makeUintConstant(
          static_cast<unsigned int>(xenos::Endian::k16in32)));
  spv::Id is_8in32_or_16in32 =
      builder_->createBinOp(spv::OpLogicalOr, type_bool_, is_8in32, is_16in32);
  spv::Block& block_pre_16in32 = *builder_->getBuildPoint();
  spv::Block& block_16in32 = builder_->makeNewBlock();
  spv::Block& block_16in32_merge = builder_->makeNewBlock();
  builder_->createSelectionMerge(&block_16in32_merge,
                                 spv::SelectionControlMaskNone);
  builder_->createConditionalBranch(is_8in32_or_16in32, &block_16in32,
                                    &block_16in32_merge);
  builder_->setBuildPoint(&block_16in32);
  spv::Id swapped_16in32 = builder_->createQuadOp(
      spv::OpBitFieldInsert, type,
      builder_->createBinOp(spv::OpShiftRightLogical, type, value,
                            const_uint_16_typed),
      value, builder_->makeIntConstant(16), builder_->makeIntConstant(16));
  builder_->createBranch(&block_16in32_merge);
  builder_->setBuildPoint(&block_16in32_merge);
  {
    std::unique_ptr<spv::Instruction> phi_op =
        std::make_unique<spv::Instruction>(builder_->getUniqueId(), type,
                                           spv::OpPhi);
    phi_op->addIdOperand(swapped_16in32);
    phi_op->addIdOperand(block_16in32.getId());
    phi_op->addIdOperand(value);
    phi_op->addIdOperand(block_pre_16in32.getId());
    value = phi_op->getResultId();
    builder_->getBuildPoint()->addInstruction(std::move(phi_op));
  }

  return value;
}

spv::Id SpirvShaderTranslator::LoadUint32FromSharedMemory(
    spv::Id address_dwords_int) {
  spv::Block& head_block = *builder_->getBuildPoint();
  assert_false(head_block.isTerminated());

  spv::StorageClass storage_class = features_.spirv_version >= spv::Spv_1_3
                                        ? spv::StorageClassStorageBuffer
                                        : spv::StorageClassUniform;
  uint32_t buffer_count_log2 = GetSharedMemoryStorageBufferCountLog2();
  if (!buffer_count_log2) {
    // Single binding - load directly.
    id_vector_temp_.clear();
    // The only SSBO struct member.
    id_vector_temp_.push_back(const_int_0_);
    id_vector_temp_.push_back(address_dwords_int);
    return builder_->createLoad(
        builder_->createAccessChain(storage_class, buffers_shared_memory_,
                                    id_vector_temp_),
        spv::NoPrecision);
  }

  // The memory is split into multiple bindings - check which binding to load
  // from. 29 is log2(512 MB), but addressing in dwords (4 B). Not indexing the
  // array with the variable itself because it needs VK_EXT_descriptor_indexing.
  uint32_t binding_address_bits = (29 - 2) - buffer_count_log2;
  spv::Id binding_index = builder_->createBinOp(
      spv::OpShiftRightLogical, type_uint_,
      builder_->createUnaryOp(spv::OpBitcast, type_uint_, address_dwords_int),
      builder_->makeUintConstant(binding_address_bits));
  spv::Id binding_address = builder_->createBinOp(
      spv::OpBitwiseAnd, type_int_, address_dwords_int,
      builder_->makeIntConstant(
          int((uint32_t(1) << binding_address_bits) - 1)));
  uint32_t buffer_count = 1 << buffer_count_log2;
  spv::Block* switch_case_blocks[512 / 128];
  for (uint32_t i = 0; i < buffer_count; ++i) {
    switch_case_blocks[i] = &builder_->makeNewBlock();
  }
  spv::Block& switch_merge_block = builder_->makeNewBlock();
  spv::Id value_phi_result = builder_->getUniqueId();
  std::unique_ptr<spv::Instruction> value_phi_op =
      std::make_unique<spv::Instruction>(value_phi_result, type_uint_,
                                         spv::OpPhi);
  builder_->createSelectionMerge(&switch_merge_block,
                                 spv::SelectionControlDontFlattenMask);
  {
    std::unique_ptr<spv::Instruction> switch_op =
        std::make_unique<spv::Instruction>(spv::OpSwitch);
    switch_op->addIdOperand(binding_index);
    // Highest binding index is the default case.
    switch_op->addIdOperand(switch_case_blocks[buffer_count - 1]->getId());
    switch_case_blocks[buffer_count - 1]->addPredecessor(&head_block);
    for (uint32_t i = 0; i < buffer_count - 1; ++i) {
      switch_op->addImmediateOperand(int(i));
      switch_op->addIdOperand(switch_case_blocks[i]->getId());
      switch_case_blocks[i]->addPredecessor(&head_block);
    }
    builder_->getBuildPoint()->addInstruction(std::move(switch_op));
  }
  for (uint32_t i = 0; i < buffer_count; ++i) {
    builder_->setBuildPoint(switch_case_blocks[i]);
    id_vector_temp_.clear();
    id_vector_temp_.push_back(builder_->makeIntConstant(int(i)));
    // The only SSBO struct member.
    id_vector_temp_.push_back(const_int_0_);
    id_vector_temp_.push_back(binding_address);
    value_phi_op->addIdOperand(builder_->createLoad(
        builder_->createAccessChain(storage_class, buffers_shared_memory_,
                                    id_vector_temp_),
        spv::NoPrecision));
    value_phi_op->addIdOperand(switch_case_blocks[i]->getId());
    builder_->createBranch(&switch_merge_block);
  }
  builder_->setBuildPoint(&switch_merge_block);
  builder_->getBuildPoint()->addInstruction(std::move(value_phi_op));
  return value_phi_result;
}

spv::Id SpirvShaderTranslator::PWLGammaToLinear(spv::Id gamma,
                                                bool gamma_pre_saturated) {
  spv::Id value_type = builder_->getTypeId(gamma);
  assert_true(builder_->isFloatType(builder_->getScalarTypeId(value_type)));
  bool is_vector = builder_->isVectorType(value_type);
  assert_true(is_vector || builder_->isFloatType(value_type));
  int num_components = builder_->getNumTypeComponents(value_type);
  assert_true(num_components < 4);
  spv::Id bool_type = type_bool_vectors_[num_components - 1];

  spv::Id const_vector_0 = const_float_vectors_0_[num_components - 1];
  spv::Id const_vector_1 = SpirvSmearScalarResultOrConstant(
      builder_->makeFloatConstant(1.0f), value_type);

  if (!gamma_pre_saturated) {
    // Saturate, flushing NaN to 0.
    gamma = builder_->createTriBuiltinCall(value_type, ext_inst_glsl_std_450_,
                                           GLSLstd450NClamp, gamma,
                                           const_vector_0, const_vector_1);
  }

  spv::Id is_piece_at_least_3 = builder_->createBinOp(
      spv::OpFOrdGreaterThanEqual, bool_type, gamma,
      SpirvSmearScalarResultOrConstant(
          builder_->makeFloatConstant(192.0f / 255.0f), value_type));
  spv::Id scale_3_or_2 = builder_->createTriOp(
      spv::OpSelect, value_type, is_piece_at_least_3,
      SpirvSmearScalarResultOrConstant(
          builder_->makeFloatConstant(8.0f / 1024.0f), value_type),
      SpirvSmearScalarResultOrConstant(
          builder_->makeFloatConstant(4.0f / 1024.0f), value_type));
  spv::Id offset_3_or_2 = builder_->createTriOp(
      spv::OpSelect, value_type, is_piece_at_least_3,
      SpirvSmearScalarResultOrConstant(builder_->makeFloatConstant(-1024.0f),
                                       value_type),
      SpirvSmearScalarResultOrConstant(builder_->makeFloatConstant(-256.0f),
                                       value_type));

  spv::Id is_piece_at_least_1 = builder_->createBinOp(
      spv::OpFOrdGreaterThanEqual, bool_type, gamma,
      SpirvSmearScalarResultOrConstant(
          builder_->makeFloatConstant(64.0f / 255.0f), value_type));
  spv::Id scale_1_or_0 = builder_->createTriOp(
      spv::OpSelect, value_type, is_piece_at_least_1,
      SpirvSmearScalarResultOrConstant(
          builder_->makeFloatConstant(2.0f / 1024.0f), value_type),
      SpirvSmearScalarResultOrConstant(
          builder_->makeFloatConstant(1.0f / 1024.0f), value_type));
  spv::Id offset_1_or_0 = builder_->createTriOp(
      spv::OpSelect, value_type, is_piece_at_least_1,
      SpirvSmearScalarResultOrConstant(builder_->makeFloatConstant(-64.0f),
                                       value_type),
      const_vector_0);

  spv::Id is_piece_at_least_2 = builder_->createBinOp(
      spv::OpFOrdGreaterThanEqual, bool_type, gamma,
      SpirvSmearScalarResultOrConstant(
          builder_->makeFloatConstant(96.0f / 255.0f), value_type));
  spv::Id scale =
      builder_->createTriOp(spv::OpSelect, value_type, is_piece_at_least_2,
                            scale_3_or_2, scale_1_or_0);
  spv::Id offset =
      builder_->createTriOp(spv::OpSelect, value_type, is_piece_at_least_2,
                            offset_3_or_2, offset_1_or_0);

  spv::Op value_times_scalar_opcode =
      is_vector ? spv::OpVectorTimesScalar : spv::OpFMul;
  // linear = gamma * (255.0f * 1024.0f) * scale + offset
  spv::Id linear = builder_->createNoContractionBinOp(
      spv::OpFAdd, value_type,
      builder_->createNoContractionBinOp(
          spv::OpFMul, value_type,
          builder_->createNoContractionBinOp(
              value_times_scalar_opcode, value_type, gamma,
              builder_->makeFloatConstant(255.0f * 1024.0f)),
          scale),
      offset);
  // linear += trunc(linear * scale)
  linear = builder_->createNoContractionBinOp(
      spv::OpFAdd, value_type, linear,
      builder_->createUnaryBuiltinCall(
          value_type, ext_inst_glsl_std_450_, GLSLstd450Trunc,
          builder_->createNoContractionBinOp(spv::OpFMul, value_type, linear,
                                             scale)));
  // linear *= 1.0f / 1023.0f
  linear = builder_->createNoContractionBinOp(
      value_times_scalar_opcode, value_type, linear,
      builder_->makeFloatConstant(1.0f / 1023.0f));
  return linear;
}

spv::Id SpirvShaderTranslator::LinearToPWLGamma(spv::Id linear,
                                                bool linear_pre_saturated) {
  spv::Id value_type = builder_->getTypeId(linear);
  assert_true(builder_->isFloatType(builder_->getScalarTypeId(value_type)));
  bool is_vector = builder_->isVectorType(value_type);
  assert_true(is_vector || builder_->isFloatType(value_type));
  int num_components = builder_->getNumTypeComponents(value_type);
  assert_true(num_components < 4);
  spv::Id bool_type = type_bool_vectors_[num_components - 1];

  spv::Id const_vector_0 = const_float_vectors_0_[num_components - 1];
  spv::Id const_vector_1 = SpirvSmearScalarResultOrConstant(
      builder_->makeFloatConstant(1.0f), value_type);

  if (!linear_pre_saturated) {
    // Saturate, flushing NaN to 0.
    linear = builder_->createTriBuiltinCall(value_type, ext_inst_glsl_std_450_,
                                            GLSLstd450NClamp, linear,
                                            const_vector_0, const_vector_1);
  }

  spv::Id is_piece_at_least_3 = builder_->createBinOp(
      spv::OpFOrdGreaterThanEqual, bool_type, linear,
      SpirvSmearScalarResultOrConstant(
          builder_->makeFloatConstant(512.0f / 1023.0f), value_type));
  spv::Id scale_3_or_2 = builder_->createTriOp(
      spv::OpSelect, value_type, is_piece_at_least_3,
      SpirvSmearScalarResultOrConstant(
          builder_->makeFloatConstant(1023.0f / 8.0f), value_type),
      SpirvSmearScalarResultOrConstant(
          builder_->makeFloatConstant(1023.0f / 4.0f), value_type));
  spv::Id offset_3_or_2 = builder_->createTriOp(
      spv::OpSelect, value_type, is_piece_at_least_3,
      SpirvSmearScalarResultOrConstant(
          builder_->makeFloatConstant(128.0f / 255.0f), value_type),
      SpirvSmearScalarResultOrConstant(
          builder_->makeFloatConstant(64.0f / 255.0f), value_type));

  spv::Id is_piece_at_least_1 = builder_->createBinOp(
      spv::OpFOrdGreaterThanEqual, bool_type, linear,
      SpirvSmearScalarResultOrConstant(
          builder_->makeFloatConstant(64.0f / 1023.0f), value_type));
  spv::Id scale_1_or_0 = builder_->createTriOp(
      spv::OpSelect, value_type, is_piece_at_least_1,
      SpirvSmearScalarResultOrConstant(
          builder_->makeFloatConstant(1023.0f / 2.0f), value_type),
      SpirvSmearScalarResultOrConstant(builder_->makeFloatConstant(1023.0f),
                                       value_type));
  spv::Id offset_1_or_0 = builder_->createTriOp(
      spv::OpSelect, value_type, is_piece_at_least_1,
      SpirvSmearScalarResultOrConstant(
          builder_->makeFloatConstant(32.0f / 255.0f), value_type),
      const_vector_0);

  spv::Id is_piece_at_least_2 = builder_->createBinOp(
      spv::OpFOrdGreaterThanEqual, bool_type, linear,
      SpirvSmearScalarResultOrConstant(
          builder_->makeFloatConstant(128.0f / 1023.0f), value_type));
  spv::Id scale =
      builder_->createTriOp(spv::OpSelect, value_type, is_piece_at_least_2,
                            scale_3_or_2, scale_1_or_0);
  spv::Id offset =
      builder_->createTriOp(spv::OpSelect, value_type, is_piece_at_least_2,
                            offset_3_or_2, offset_1_or_0);

  // gamma = trunc(linear * scale) * (1.0f / 255.0f) + offset
  return builder_->createNoContractionBinOp(
      spv::OpFAdd, value_type,
      builder_->createNoContractionBinOp(
          is_vector ? spv::OpVectorTimesScalar : spv::OpFMul, value_type,
          builder_->createUnaryBuiltinCall(
              value_type, ext_inst_glsl_std_450_, GLSLstd450Trunc,
              builder_->createNoContractionBinOp(spv::OpFMul, value_type,
                                                 linear, scale)),
          builder_->makeFloatConstant(1.0f / 255.0f)),
      offset);
}

}  // namespace gpu
}  // namespace xe
