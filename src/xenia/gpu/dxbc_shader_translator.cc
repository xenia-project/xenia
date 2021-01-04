/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/dxbc_shader_translator.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>

#include "third_party/dxbc/DXBCChecksum.h"
#include "third_party/dxbc/d3d12TokenizedProgramFormat.hpp"

#include "xenia/base/assert.h"
#include "xenia/base/cvar.h"
#include "xenia/base/math.h"
#include "xenia/gpu/dxbc_shader.h"

DEFINE_bool(dxbc_switch, true,
            "Use switch rather than if for flow control. Turning this off or "
            "on may improve stability, though this heavily depends on the "
            "driver - on AMD, it's recommended to have this set to true, as "
            "Halo 3 appears to crash when if is used for flow control "
            "(possibly the shader compiler tries to flatten them). On Intel "
            "HD Graphics, this is ignored because of a crash with the switch "
            "instruction.",
            "GPU");
DEFINE_bool(dxbc_source_map, false,
            "Disassemble Xenos instructions as comments in the resulting DXBC "
            "for debugging.",
            "GPU");

namespace xe {
namespace gpu {
using namespace ucode;

// Notes about operands:
//
// Reading and writing:
// - r# (temporary registers) are 4-component and can be used anywhere.
// - v# (inputs) are 4-component and read-only.
// - o# (outputs) are 4-component and write-only.
// - oDepth (pixel shader depth output) is 1-component and write-only.
// - x# (indexable temporary registers) are 4-component and can be accessed
//   either via a mov load or a mov store (and those movs are counted as
//   ArrayInstructions in STAT, not as MovInstructions), even though the D3D11.3
//   functional specification says x# can be used wherever r# can be used, but
//   FXC emits only mov load/store in simple tests.
//
// Indexing:
// - Constant buffers use 3D indices in CBx[y][z] format, where x is the ID of
//   the binding (CB#), y is the register to access within its space, z is the
//   4-component vector to access within the register binding.
//   For example, if the requested vector is located in the beginning of the
//   second buffer in the descriptor array at b2, which is assigned to CB1, the
//   index would be CB1[3][0].
// - Resources and samplers use 2D indices, where the first dimension is the
//   S#/T#/U# binding index, and the second is the s#/t#/u# register index
//   within its space.

DxbcShaderTranslator::DxbcShaderTranslator(uint32_t vendor_id,
                                           bool bindless_resources_used,
                                           bool edram_rov_used,
                                           bool force_emit_source_map)
    : a_(shader_code_, stat_),
      vendor_id_(vendor_id),
      bindless_resources_used_(bindless_resources_used),
      edram_rov_used_(edram_rov_used) {
  emit_source_map_ = force_emit_source_map || cvars::dxbc_source_map;
  // Don't allocate again and again for the first shader.
  shader_code_.reserve(8192);
  shader_object_.reserve(16384);
}
DxbcShaderTranslator::~DxbcShaderTranslator() = default;

std::vector<uint8_t> DxbcShaderTranslator::CreateDepthOnlyPixelShader() {
  is_depth_only_pixel_shader_ = true;
  // TODO(Triang3l): Handle in a nicer way (is_depth_only_pixel_shader_ is a
  // leftover from when a Shader object wasn't used during translation).
  Shader shader(xenos::ShaderType::kPixel, 0, nullptr, 0);
  shader.AnalyzeUcode(instruction_disassembly_buffer_);
  Shader::Translation& translation = *shader.GetOrCreateTranslation(0);
  TranslateAnalyzedShader(translation);
  is_depth_only_pixel_shader_ = false;
  return translation.translated_binary();
}

uint64_t DxbcShaderTranslator::GetDefaultModification(
    xenos::ShaderType shader_type, uint32_t dynamic_addressable_register_count,
    Shader::HostVertexShaderType host_vertex_shader_type) const {
  Modification shader_modification;
  shader_modification.dynamic_addressable_register_count =
      dynamic_addressable_register_count;
  switch (shader_type) {
    case xenos::ShaderType::kVertex:
      shader_modification.host_vertex_shader_type = host_vertex_shader_type;
      break;
    case xenos::ShaderType::kPixel:
      shader_modification.depth_stencil_mode =
          Modification::DepthStencilMode::kNoModifiers;
      break;
  }
  return shader_modification.value;
}

void DxbcShaderTranslator::Reset() {
  ShaderTranslator::Reset();

  shader_code_.clear();

  cbuffer_count_ = 0;
  // System constants always used in prologues/epilogues.
  cbuffer_index_system_constants_ = cbuffer_count_++;
  cbuffer_index_float_constants_ = kBindingIndexUnallocated;
  cbuffer_index_bool_loop_constants_ = kBindingIndexUnallocated;
  cbuffer_index_fetch_constants_ = kBindingIndexUnallocated;
  cbuffer_index_descriptor_indices_ = kBindingIndexUnallocated;

  system_constants_used_ = 0;

  in_domain_location_used_ = 0;
  in_primitive_id_used_ = false;
  in_control_point_index_used_ = false;
  in_position_used_ = 0;
  in_front_face_used_ = false;

  system_temp_count_current_ = 0;
  system_temp_count_max_ = 0;

  cf_exec_bool_constant_ = kCfExecBoolConstantNone;
  cf_exec_predicated_ = false;
  cf_instruction_predicate_if_open_ = false;
  cf_exec_predicate_written_ = false;

  srv_count_ = 0;
  srv_index_shared_memory_ = kBindingIndexUnallocated;
  srv_index_bindless_textures_2d_ = kBindingIndexUnallocated;
  srv_index_bindless_textures_3d_ = kBindingIndexUnallocated;
  srv_index_bindless_textures_cube_ = kBindingIndexUnallocated;

  texture_bindings_.clear();
  texture_bindings_for_bindful_srv_indices_.clear();

  uav_count_ = 0;
  uav_index_shared_memory_ = kBindingIndexUnallocated;
  uav_index_edram_ = kBindingIndexUnallocated;

  sampler_bindings_.clear();

  memexport_alloc_current_count_ = 0;

  std::memset(&stat_, 0, sizeof(stat_));
}

uint32_t DxbcShaderTranslator::GetModificationRegisterCount() const {
  return GetDxbcShaderModification().dynamic_addressable_register_count;
}

bool DxbcShaderTranslator::UseSwitchForControlFlow() const {
  // Xenia crashes on Intel HD Graphics 4000 with switch.
  return cvars::dxbc_switch && vendor_id_ != 0x8086;
}

uint32_t DxbcShaderTranslator::PushSystemTemp(uint32_t zero_mask,
                                              uint32_t count) {
  uint32_t register_index = system_temp_count_current_;
  if (!is_depth_only_pixel_shader_ &&
      !current_shader().uses_register_dynamic_addressing()) {
    // Guest shader registers first if they're not in x0. Depth-only pixel
    // shader is a special case of the DXBC translator usage, where there are no
    // GPRs because there's no shader to translate, and a guest shader is not
    // loaded.
    register_index += register_count();
  }
  system_temp_count_current_ += count;
  system_temp_count_max_ =
      std::max(system_temp_count_max_, system_temp_count_current_);
  zero_mask &= 0b1111;
  if (zero_mask) {
    for (uint32_t i = 0; i < count; ++i) {
      a_.OpMov(dxbc::Dest::R(register_index + i, zero_mask), dxbc::Src::LU(0));
    }
  }
  return register_index;
}

void DxbcShaderTranslator::PopSystemTemp(uint32_t count) {
  assert_true(count <= system_temp_count_current_);
  system_temp_count_current_ -= std::min(count, system_temp_count_current_);
}

void DxbcShaderTranslator::ConvertPWLGamma(
    bool to_gamma, int32_t source_temp, uint32_t source_temp_component,
    uint32_t target_temp, uint32_t target_temp_component, uint32_t piece_temp,
    uint32_t piece_temp_component, uint32_t accumulator_temp,
    uint32_t accumulator_temp_component) {
  assert_true(source_temp != target_temp ||
              source_temp_component != target_temp_component ||
              ((target_temp != accumulator_temp ||
                target_temp_component != accumulator_temp_component) &&
               (target_temp != piece_temp ||
                target_temp_component != piece_temp_component)));
  assert_true(piece_temp != source_temp ||
              piece_temp_component != source_temp_component);
  assert_true(accumulator_temp != source_temp ||
              accumulator_temp_component != source_temp_component);
  assert_true(piece_temp != accumulator_temp ||
              piece_temp_component != accumulator_temp_component);
  dxbc::Src source_src(dxbc::Src::R(source_temp).Select(source_temp_component));
  dxbc::Dest piece_dest(dxbc::Dest::R(piece_temp, 1 << piece_temp_component));
  dxbc::Src piece_src(dxbc::Src::R(piece_temp).Select(piece_temp_component));
  dxbc::Dest accumulator_dest(
      dxbc::Dest::R(accumulator_temp, 1 << accumulator_temp_component));
  dxbc::Src accumulator_src(
      dxbc::Src::R(accumulator_temp).Select(accumulator_temp_component));
  // For each piece:
  // 1) Calculate how far we are on it. Multiply by 1/width, subtract
  //    start/width and saturate.
  // 2) Add the contribution of the piece - multiply the position on the piece
  //    by its slope*width and accumulate.
  // Piece 1.
  a_.OpMul(piece_dest, source_src,
           dxbc::Src::LF(to_gamma ? (1.0f / 0.0625f) : (1.0f / 0.25f)), true);
  a_.OpMul(accumulator_dest, piece_src,
           dxbc::Src::LF(to_gamma ? (4.0f * 0.0625f) : (0.25f * 0.25f)));
  // Piece 2.
  a_.OpMAd(piece_dest, source_src,
           dxbc::Src::LF(to_gamma ? (1.0f / 0.0625f) : (1.0f / 0.125f)),
           dxbc::Src::LF(to_gamma ? (-0.0625f / 0.0625f) : (-0.25f / 0.125f)),
           true);
  a_.OpMAd(accumulator_dest, piece_src,
           dxbc::Src::LF(to_gamma ? (2.0f * 0.0625f) : (0.5f * 0.125f)),
           accumulator_src);
  // Piece 3.
  a_.OpMAd(piece_dest, source_src,
           dxbc::Src::LF(to_gamma ? (1.0f / 0.375f) : (1.0f / 0.375f)),
           dxbc::Src::LF(to_gamma ? (-0.125f / 0.375f) : (-0.375f / 0.375f)),
           true);
  a_.OpMAd(accumulator_dest, piece_src,
           dxbc::Src::LF(to_gamma ? (1.0f * 0.375f) : (1.0f * 0.375f)),
           accumulator_src);
  // Piece 4.
  a_.OpMAd(piece_dest, source_src,
           dxbc::Src::LF(to_gamma ? (1.0f / 0.5f) : (1.0f / 0.25f)),
           dxbc::Src::LF(to_gamma ? (-0.5f / 0.5f) : (-0.75f / 0.25f)), true);
  a_.OpMAd(dxbc::Dest::R(target_temp, 1 << target_temp_component), piece_src,
           dxbc::Src::LF(to_gamma ? (0.5f * 0.5f) : (2.0f * 0.25f)),
           accumulator_src);
}

void DxbcShaderTranslator::StartVertexShader_LoadVertexIndex() {
  if (register_count() < 1) {
    return;
  }

  bool uses_register_dynamic_addressing =
      current_shader().uses_register_dynamic_addressing();

  // Writing the index to X of GPR 0 - either directly if not using indexable
  // registers, or via a system temporary register.
  uint32_t reg;
  if (uses_register_dynamic_addressing) {
    reg = PushSystemTemp();
  } else {
    reg = 0;
  }

  dxbc::Dest index_dest(dxbc::Dest::R(reg, 0b0001));
  dxbc::Src index_src(dxbc::Src::R(reg, dxbc::Src::kXXXX));

  // Check if the closing vertex of a non-indexed line loop is being processed.
  system_constants_used_ |= 1ull << kSysConst_LineLoopClosingIndex_Index;
  a_.OpINE(
      index_dest,
      dxbc::Src::V(uint32_t(InOutRegister::kVSInVertexIndex), dxbc::Src::kXXXX),
      dxbc::Src::CB(cbuffer_index_system_constants_,
                    uint32_t(CbufferRegister::kSystemConstants),
                    kSysConst_LineLoopClosingIndex_Vec)
          .Select(kSysConst_LineLoopClosingIndex_Comp));
  // Zero the index if processing the closing vertex of a line loop, or do
  // nothing (replace 0 with 0) if not needed.
  a_.OpAnd(
      index_dest,
      dxbc::Src::V(uint32_t(InOutRegister::kVSInVertexIndex), dxbc::Src::kXXXX),
      index_src);

  {
    // Swap the vertex index's endianness.
    system_constants_used_ |= 1ull << kSysConst_VertexIndexEndian_Index;
    dxbc::Src endian_src(
        dxbc::Src::CB(cbuffer_index_system_constants_,
                      uint32_t(CbufferRegister::kSystemConstants),
                      kSysConst_VertexIndexEndian_Vec)
            .Select(kSysConst_VertexIndexEndian_Comp));
    dxbc::Dest swap_temp_dest(dxbc::Dest::R(reg, 0b0010));
    dxbc::Src swap_temp_src(dxbc::Src::R(reg, dxbc::Src::kYYYY));

    // 8-in-16 or one half of 8-in-32.
    a_.OpSwitch(endian_src);
    a_.OpCase(dxbc::Src::LU(uint32_t(xenos::Endian::k8in16)));
    a_.OpCase(dxbc::Src::LU(uint32_t(xenos::Endian::k8in32)));
    // Temp = X0Z0.
    a_.OpAnd(swap_temp_dest, index_src, dxbc::Src::LU(0x00FF00FF));
    // Index = YZW0.
    a_.OpUShR(index_dest, index_src, dxbc::Src::LU(8));
    // Index = Y0W0.
    a_.OpAnd(index_dest, index_src, dxbc::Src::LU(0x00FF00FF));
    // Index = YXWZ.
    a_.OpUMAd(index_dest, swap_temp_src, dxbc::Src::LU(256), index_src);
    a_.OpBreak();
    a_.OpEndSwitch();

    // 16-in-32 or another half of 8-in-32.
    a_.OpSwitch(endian_src);
    a_.OpCase(dxbc::Src::LU(uint32_t(xenos::Endian::k8in32)));
    a_.OpCase(dxbc::Src::LU(uint32_t(xenos::Endian::k16in32)));
    // Temp = ZW00.
    a_.OpUShR(swap_temp_dest, index_src, dxbc::Src::LU(16));
    // Index = ZWXY.
    a_.OpBFI(index_dest, dxbc::Src::LU(16), dxbc::Src::LU(16), index_src,
             swap_temp_src);
    a_.OpBreak();
    a_.OpEndSwitch();

    if (!uses_register_dynamic_addressing) {
      // Break register dependency.
      a_.OpMov(swap_temp_dest, dxbc::Src::LF(0.0f));
    }
  }

  // Add the base vertex index.
  system_constants_used_ |= 1ull << kSysConst_VertexBaseIndex_Index;
  a_.OpIAdd(index_dest, index_src,
            dxbc::Src::CB(cbuffer_index_system_constants_,
                          uint32_t(CbufferRegister::kSystemConstants),
                          kSysConst_VertexBaseIndex_Vec)
                .Select(kSysConst_VertexBaseIndex_Comp));

  // Convert to float.
  a_.OpIToF(index_dest, index_src);

  if (uses_register_dynamic_addressing) {
    // Store to indexed GPR 0 in x0[0].
    a_.OpMov(dxbc::Dest::X(0, 0, 0b0001), index_src);
    PopSystemTemp();
  }
}

void DxbcShaderTranslator::StartVertexOrDomainShader() {
  bool uses_register_dynamic_addressing =
      current_shader().uses_register_dynamic_addressing();

  // Zero the interpolators.
  for (uint32_t i = 0; i < xenos::kMaxInterpolators; ++i) {
    a_.OpMov(dxbc::Dest::O(uint32_t(InOutRegister::kVSDSOutInterpolators) + i),
             dxbc::Src::LF(0.0f));
  }

  // Remember that x# are only accessible via mov load or store - use a
  // temporary variable if need to do any computations!
  Shader::HostVertexShaderType host_vertex_shader_type =
      GetDxbcShaderModification().host_vertex_shader_type;
  switch (host_vertex_shader_type) {
    case Shader::HostVertexShaderType::kVertex:
      StartVertexShader_LoadVertexIndex();
      break;

    case Shader::HostVertexShaderType::kTriangleDomainCPIndexed:
      assert_true(register_count() >= 2);
      if (register_count() >= 1) {
        // Copy the domain location to r0.xyz.
        // ZYX swizzle according to Call of Duty 3 and Viva Pinata.
        in_domain_location_used_ |= 0b0111;
        a_.OpMov(uses_register_dynamic_addressing ? dxbc::Dest::X(0, 0, 0b0111)
                                                  : dxbc::Dest::R(0, 0b0111),
                 dxbc::Src::VDomain(0b000110));
        if (register_count() >= 2) {
          // Copy the control point indices (already swapped and converted to
          // float by the host vertex and hull shaders) to r1.xyz.
          dxbc::Dest control_point_index_dest(uses_register_dynamic_addressing
                                                  ? dxbc::Dest::X(0, 1)
                                                  : dxbc::Dest::R(1));
          in_control_point_index_used_ = true;
          for (uint32_t i = 0; i < 3; ++i) {
            a_.OpMov(control_point_index_dest.Mask(1 << i),
                     dxbc::Src::VICP(
                         i, uint32_t(InOutRegister::kDSInControlPointIndex),
                         dxbc::Src::kXXXX));
          }
        }
      }
      break;

    case Shader::HostVertexShaderType::kTriangleDomainPatchIndexed:
      assert_true(register_count() >= 2);
      if (register_count() >= 1) {
        // Copy the domain location to r0.xyz.
        // ZYX swizzle with r1.y == 0, according to the water shader in
        // Banjo-Kazooie: Nuts & Bolts.
        in_domain_location_used_ |= 0b0111;
        a_.OpMov(uses_register_dynamic_addressing ? dxbc::Dest::X(0, 0, 0b0111)
                                                  : dxbc::Dest::R(0, 0b0111),
                 dxbc::Src::VDomain(0b000110));
        if (register_count() >= 2) {
          // Copy the primitive index to r1.x as a float.
          uint32_t primitive_id_temp =
              uses_register_dynamic_addressing ? PushSystemTemp() : 1;
          in_primitive_id_used_ = true;
          a_.OpUToF(dxbc::Dest::R(primitive_id_temp, 0b0001),
                    dxbc::Src::VPrim());
          if (uses_register_dynamic_addressing) {
            a_.OpMov(dxbc::Dest::X(0, 1, 0b0001),
                     dxbc::Src::R(primitive_id_temp, dxbc::Src::kXXXX));
            // Release primitive_id_temp.
            PopSystemTemp();
          }
          // Write the swizzle of the barycentric coordinates to r1.y. It
          // appears that the tessellator offloads the reordering of coordinates
          // for edges to game shaders.
          //
          // In Banjo-Kazooie: Nuts & Bolts, the water shader multiplies the
          // first control point's position by r0.z, the second CP's by r0.y,
          // and the third CP's by r0.x. But before doing that it swizzles
          // r0.xyz the following way depending on the value in r1.y:
          // - ZXY for 1.0.
          // - YZX for 2.0.
          // - XZY for 4.0.
          // - YXZ for 5.0.
          // - ZYX for 6.0.
          // Possibly, the logic here is that the value itself is the amount of
          // rotation of the swizzle to the right, and 1 << 2 is set when the
          // swizzle needs to be flipped before rotating.
          //
          // Direct3D 12 passes the coordinates in a consistent order, so can
          // just use the identity swizzle.
          a_.OpMov(uses_register_dynamic_addressing
                       ? dxbc::Dest::X(0, 1, 0b0010)
                       : dxbc::Dest::R(1, 0b0010),
                   dxbc::Src::LF(0.0f));
        }
      }
      break;

    case Shader::HostVertexShaderType::kQuadDomainCPIndexed:
      assert_true(register_count() >= 2);
      if (register_count() >= 1) {
        // Copy the domain location to r0.xy.
        in_domain_location_used_ |= 0b0011;
        a_.OpMov(uses_register_dynamic_addressing ? dxbc::Dest::X(0, 0, 0b0011)
                                                  : dxbc::Dest::R(0, 0b0011),
                 dxbc::Src::VDomain());
        // Control point indices according to the shader from the main menu of
        // Defender, which starts from `cndeq r2, c255.xxxy, r1.xyzz, r0.zzzz`,
        // where c255.x is 0, and c255.y is 1.
        // r0.z for (1 - r0.x) * (1 - r0.y)
        // r1.x for r0.x * (1 - r0.y)
        // r1.y for r0.x * r0.y
        // r1.z for (1 - r0.x) * r0.y
        in_control_point_index_used_ = true;
        a_.OpMov(
            uses_register_dynamic_addressing ? dxbc::Dest::X(0, 0, 0b0100)
                                             : dxbc::Dest::R(0, 0b0100),
            dxbc::Src::VICP(0, uint32_t(InOutRegister::kDSInControlPointIndex),
                            dxbc::Src::kXXXX));
        if (register_count() >= 2) {
          dxbc::Dest r1_dest(uses_register_dynamic_addressing
                                 ? dxbc::Dest::X(0, 1)
                                 : dxbc::Dest::R(1));
          for (uint32_t i = 0; i < 3; ++i) {
            a_.OpMov(r1_dest.Mask(1 << i),
                     dxbc::Src::VICP(
                         1 + i, uint32_t(InOutRegister::kDSInControlPointIndex),
                         dxbc::Src::kXXXX));
          }
        }
      }
      break;

    case Shader::HostVertexShaderType::kQuadDomainPatchIndexed:
      assert_true(register_count() >= 2);
      if (register_count() >= 1) {
        // Copy the domain location to r0.yz.
        // XY swizzle according to the ground shader in Viva Pinata.
        in_domain_location_used_ |= 0b0011;
        a_.OpMov(uses_register_dynamic_addressing ? dxbc::Dest::X(0, 0, 0b0110)
                                                  : dxbc::Dest::R(0, 0b0110),
                 dxbc::Src::VDomain(0b010000));
        // Copy the primitive index to r0.x as a float.
        uint32_t primitive_id_temp =
            uses_register_dynamic_addressing ? PushSystemTemp() : 0;
        in_primitive_id_used_ = true;
        a_.OpUToF(dxbc::Dest::R(primitive_id_temp, 0b0001), dxbc::Src::VPrim());
        if (uses_register_dynamic_addressing) {
          a_.OpMov(dxbc::Dest::X(0, 0, 0b0001),
                   dxbc::Src::R(primitive_id_temp, dxbc::Src::kXXXX));
          // Release primitive_id_temp.
          PopSystemTemp();
        }
        if (register_count() >= 2) {
          // Write the swizzle of the UV coordinates to r1.x. It appears that
          // the tessellator offloads the reordering of coordinates for edges to
          // game shaders.
          //
          // In Viva Pinata, if we assume that r0.y is U and r0.z is V, the
          // factors each control point value is multiplied by are the
          // following:
          // - (1-u)*(1-v), u*(1-v), (1-u)*v, u*v for 0.0 (identity swizzle).
          // - u*(1-v), (1-u)*(1-v), u*v, (1-u)*v for 1.0 (YXWZ).
          // - u*v, (1-u)*v, u*(1-v), (1-u)*(1-v) for 2.0 (WZYX).
          // - (1-u)*v, u*v, (1-u)*(1-v), u*(1-v) for 3.0 (ZWXY).
          //
          // Direct3D 12 passes the coordinates in a consistent order, so can
          // just use the identity swizzle.
          a_.OpMov(uses_register_dynamic_addressing
                       ? dxbc::Dest::X(0, 1, 0b0001)
                       : dxbc::Dest::R(1, 0b0001),
                   dxbc::Src::LF(0.0f));
        }
      }
      break;

    default:
      // TODO(Triang3l): Support line and non-adaptive quad patches.
      assert_unhandled_case(host_vertex_shader_type);
      EmitTranslationError(
          "Unsupported host vertex shader type in StartVertexOrDomainShader");
      break;
  }
}

void DxbcShaderTranslator::StartPixelShader() {
  if (edram_rov_used_) {
    // Load the EDRAM addresses and the coverage.
    StartPixelShader_LoadROVParameters();

    // Do early 2x2 quad rejection if it makes sense.
    if (ROV_IsDepthStencilEarly()) {
      ROV_DepthStencilTest();
    }
  }

  // If not translating anything, we only need the depth.
  if (is_depth_only_pixel_shader_) {
    return;
  }

  bool uses_register_dynamic_addressing =
      current_shader().uses_register_dynamic_addressing();

  if (!edram_rov_used_ && current_shader().writes_depth()) {
    // Initialize the depth output if used, which must be written to regardless
    // of the taken execution path.
    a_.OpMov(dxbc::Dest::ODepth(), dxbc::Src::LF(0.0f));
  }

  uint32_t interpolator_count =
      std::min(xenos::kMaxInterpolators, register_count());
  if (interpolator_count != 0) {
    // Copy interpolants to GPRs.
    if (edram_rov_used_) {
      uint32_t centroid_temp =
          uses_register_dynamic_addressing ? PushSystemTemp() : UINT32_MAX;
      system_constants_used_ |= 1ull
                                << kSysConst_InterpolatorSamplingPattern_Index;
      dxbc::Src sampling_pattern_src(
          dxbc::Src::CB(cbuffer_index_system_constants_,
                        uint32_t(CbufferRegister::kSystemConstants),
                        kSysConst_InterpolatorSamplingPattern_Vec)
              .Select(kSysConst_InterpolatorSamplingPattern_Comp));
      for (uint32_t i = 0; i < interpolator_count; ++i) {
        // With GPR dynamic addressing, first evaluate to centroid_temp r#, then
        // store to the x#.
        uint32_t centroid_register =
            uses_register_dynamic_addressing ? centroid_temp : i;
        // Check if the input needs to be interpolated at center (if the bit is
        // set).
        a_.OpAnd(dxbc::Dest::R(centroid_register, 0b0001), sampling_pattern_src,
                 dxbc::Src::LU(uint32_t(1) << i));
        a_.OpIf(bool(xenos::SampleLocation::kCenter),
                dxbc::Src::R(centroid_register, dxbc::Src::kXXXX));
        // At center.
        a_.OpMov(uses_register_dynamic_addressing ? dxbc::Dest::X(0, i)
                                                  : dxbc::Dest::R(i),
                 dxbc::Src::V(uint32_t(InOutRegister::kPSInInterpolators) + i));
        a_.OpElse();
        // At centroid. Not really important that 2x MSAA is emulated using
        // ForcedSampleCount 4 - what matters is that the sample position will
        // be within the primitive, and the value will not be extrapolated.
        a_.OpEvalCentroid(
            dxbc::Dest::R(centroid_register),
            dxbc::Src::V(uint32_t(InOutRegister::kPSInInterpolators) + i));
        if (uses_register_dynamic_addressing) {
          a_.OpMov(dxbc::Dest::X(0, i), dxbc::Src::R(centroid_register));
        }
        a_.OpEndIf();
      }
      if (centroid_temp != UINT32_MAX) {
        PopSystemTemp();
      }
    } else {
      // SSAA instead of MSAA without ROV - everything is interpolated at
      // samples, can't extrapolate.
      for (uint32_t i = 0; i < interpolator_count; ++i) {
        a_.OpMov(uses_register_dynamic_addressing ? dxbc::Dest::X(0, i)
                                                  : dxbc::Dest::R(i),
                 dxbc::Src::V(uint32_t(InOutRegister::kPSInInterpolators) + i));
      }
    }

    // Write pixel parameters - screen (XY absolute value) and point sprite (ZW
    // absolute value) coordinates, facing (X sign bit) - to the specified
    // interpolator register (ps_param_gen).
    system_constants_used_ |= 1ull << kSysConst_PSParamGen_Index;
    dxbc::Src param_gen_index_src(
        dxbc::Src::CB(cbuffer_index_system_constants_,
                      uint32_t(CbufferRegister::kSystemConstants),
                      kSysConst_PSParamGen_Vec)
            .Select(kSysConst_PSParamGen_Comp));
    uint32_t param_gen_temp = PushSystemTemp();
    // Check if pixel parameters need to be written.
    a_.OpULT(dxbc::Dest::R(param_gen_temp, 0b0001), param_gen_index_src,
             dxbc::Src::LU(interpolator_count));
    a_.OpIf(true, dxbc::Src::R(param_gen_temp, dxbc::Src::kXXXX));
    {
      // XY - floored pixel position (Direct3D VPOS) in the absolute value,
      // faceness as X sign bit. Using Z as scratch register now.
      if (edram_rov_used_) {
        // Get XY address of the current host pixel as float.
        in_position_used_ |= 0b0011;
        a_.OpRoundZ(dxbc::Dest::R(param_gen_temp, 0b0011),
                    dxbc::Src::V(uint32_t(InOutRegister::kPSInPosition)));
        // Revert resolution scale - after truncating, so if the pixel position
        // is passed to tfetch (assuming the game doesn't round it by itself),
        // it will be sampled with higher resolution too.
        // Check if resolution scale is 2x2 and multiply by 0.5 in this case.
        system_constants_used_ |= 1ull
                                  << kSysConst_EdramResolutionSquareScale_Index;
        a_.OpIEq(dxbc::Dest::R(param_gen_temp, 0b0100),
                 dxbc::Src::CB(cbuffer_index_system_constants_,
                               uint32_t(CbufferRegister::kSystemConstants),
                               kSysConst_EdramResolutionSquareScale_Vec)
                     .Select(kSysConst_EdramResolutionSquareScale_Comp),
                 dxbc::Src::LU(4));
        a_.OpIf(true, dxbc::Src::R(param_gen_temp, dxbc::Src::kZZZZ));
        {
          a_.OpMul(dxbc::Dest::R(param_gen_temp, 0b0011),
                   dxbc::Src::R(param_gen_temp), dxbc::Src::LF(0.5f));
        }
        a_.OpEndIf();
      } else {
        // Get XY address of the current SSAA sample by converting
        // SV_Position.xy to an integer.
        in_position_used_ |= 0b0011;
        a_.OpFToU(dxbc::Dest::R(param_gen_temp, 0b0011),
                  dxbc::Src::V(uint32_t(InOutRegister::kPSInPosition)));
        // Undo SSAA that is used instead of MSAA - since it's used as a
        // workaround for MSAA emulation, guest pixel position must be the same
        // for all samples, so this should be done is integers (or before
        // truncating).
        system_constants_used_ |= 1ull << kSysConst_SampleCountLog2_Index;
        a_.OpUShR(
            dxbc::Dest::R(param_gen_temp, 0b0011), dxbc::Src::R(param_gen_temp),
            dxbc::Src::CB(cbuffer_index_system_constants_,
                          uint32_t(CbufferRegister::kSystemConstants),
                          kSysConst_SampleCountLog2_Vec,
                          kSysConst_SampleCountLog2_Comp |
                              ((kSysConst_SampleCountLog2_Comp + 1) << 2)));
        // Convert the integer position to float Direct3D 9 VPOS.
        a_.OpUToF(dxbc::Dest::R(param_gen_temp, 0b0011),
                  dxbc::Src::R(param_gen_temp));
      }
      // Check if faceness applies to the current primitive type.
      system_constants_used_ |= 1ull << kSysConst_Flags_Index;
      a_.OpAnd(dxbc::Dest::R(param_gen_temp, 0b0100),
               dxbc::Src::CB(cbuffer_index_system_constants_,
                             uint32_t(CbufferRegister::kSystemConstants),
                             kSysConst_Flags_Vec)
                   .Select(kSysConst_Flags_Comp),
               dxbc::Src::LU(kSysFlag_PrimitivePolygonal));
      a_.OpIf(true, dxbc::Src::R(param_gen_temp, dxbc::Src::kZZZZ));
      {
        // Negate modifier flips the sign bit even for 0 - set it to minus for
        // backfaces.
        in_front_face_used_ = true;
        a_.OpMovC(dxbc::Dest::R(param_gen_temp, 0b0001),
                  dxbc::Src::V(uint32_t(InOutRegister::kPSInFrontFace),
                               dxbc::Src::kXXXX),
                  dxbc::Src::R(param_gen_temp, dxbc::Src::kXXXX),
                  -dxbc::Src::R(param_gen_temp, dxbc::Src::kXXXX));
      }
      a_.OpEndIf();
      // ZW - UV within a point sprite in the absolute value, at centroid if
      // requested for the interpolator.
      dxbc::Dest point_coord_r_zw_dest(dxbc::Dest::R(param_gen_temp, 0b1100));
      dxbc::Src point_coord_v_xxxy_src(dxbc::Src::V(
          uint32_t(InOutRegister::kPSInPointParameters), 0b01000000));
      if (edram_rov_used_) {
        system_constants_used_ |=
            1ull << kSysConst_InterpolatorSamplingPattern_Index;
        a_.OpUBFE(dxbc::Dest::R(param_gen_temp, 0b0100), dxbc::Src::LU(1),
                  param_gen_index_src,
                  dxbc::Src::CB(cbuffer_index_system_constants_,
                                uint32_t(CbufferRegister::kSystemConstants),
                                kSysConst_InterpolatorSamplingPattern_Vec)
                      .Select(kSysConst_InterpolatorSamplingPattern_Comp));
        a_.OpIf(bool(xenos::SampleLocation::kCenter),
                dxbc::Src::R(param_gen_temp, dxbc::Src::kZZZZ));
        // At center.
        a_.OpMov(point_coord_r_zw_dest, point_coord_v_xxxy_src);
        a_.OpElse();
        // At centroid.
        a_.OpEvalCentroid(point_coord_r_zw_dest, point_coord_v_xxxy_src);
        a_.OpEndIf();
      } else {
        // At the SSAA sample.
        a_.OpMov(point_coord_r_zw_dest, point_coord_v_xxxy_src);
      }
      // Write ps_param_gen to the specified GPR.
      dxbc::Src param_gen_src(dxbc::Src::R(param_gen_temp));
      if (uses_register_dynamic_addressing) {
        // Copy the GPR number to r# for relative addressing.
        uint32_t param_gen_copy_temp = PushSystemTemp();
        a_.OpMov(dxbc::Dest::R(param_gen_copy_temp, 0b0001),
                 dxbc::Src::CB(cbuffer_index_system_constants_,
                               uint32_t(CbufferRegister::kSystemConstants),
                               kSysConst_PSParamGen_Vec)
                     .Select(kSysConst_PSParamGen_Comp));
        // Write to the GPR.
        a_.OpMov(dxbc::Dest::X(0, dxbc::Index(param_gen_copy_temp, 0)),
                 param_gen_src);
        // Release param_gen_copy_temp.
        PopSystemTemp();
      } else {
        if (interpolator_count == 1) {
          a_.OpMov(dxbc::Dest::R(0), param_gen_src);
        } else {
          // Write to the r# using binary search.
          uint32_t param_gen_copy_temp = PushSystemTemp();
          auto param_gen_copy_node = [&](uint32_t low, uint32_t high,
                                         const auto& self) -> void {
            assert_true(low < high);
            uint32_t mid = low + (high - low + 1) / 2;
            a_.OpULT(dxbc::Dest::R(param_gen_copy_temp, 0b0001),
                     param_gen_index_src, dxbc::Src::LU(mid));
            a_.OpIf(true, dxbc::Src::R(param_gen_copy_temp, dxbc::Src::kXXXX));
            {
              if (low + 1 == mid) {
                a_.OpMov(dxbc::Dest::R(low), param_gen_src);
              } else {
                self(low, mid - 1, self);
              }
            }
            a_.OpElse();
            {
              if (mid == high) {
                a_.OpMov(dxbc::Dest::R(mid), param_gen_src);
              } else {
                self(mid, high, self);
              }
            }
            a_.OpEndIf();
          };
          param_gen_copy_node(0, interpolator_count - 1, param_gen_copy_node);
          // Release param_gen_copy_temp.
          PopSystemTemp();
        }
      }
    }
    // Close the ps_param_gen check.
    a_.OpEndIf();
    // Release param_gen_temp.
    PopSystemTemp();
  }
}

void DxbcShaderTranslator::StartTranslation() {
  // Allocate global system temporary registers that may also be used in the
  // epilogue.
  if (is_vertex_shader()) {
    system_temp_position_ = PushSystemTemp(0b1111);
    system_temp_point_size_edge_flag_kill_vertex_ = PushSystemTemp(0b0100);
    // Set the point size to a negative value to tell the geometry shader that
    // it should use the global point size if the vertex shader does not
    // override it.
    a_.OpMov(
        dxbc::Dest::R(system_temp_point_size_edge_flag_kill_vertex_, 0b0001),
        dxbc::Src::LF(-1.0f));
  } else if (is_pixel_shader()) {
    if (edram_rov_used_) {
      // Will be initialized unconditionally.
      system_temp_rov_params_ = PushSystemTemp();
    }
    if (IsDepthStencilSystemTempUsed()) {
      // If the shader doesn't write to oDepth, and ROV is used, each
      // component will be written to if depth/stencil is enabled and the
      // respective sample is covered - so need to initialize now because the
      // first writes will be conditional.
      // If the shader writes to oDepth, this is oDepth of the shader, written
      // by the guest code, so initialize because assumptions can't be made
      // about the integrity of the guest code.
      system_temp_depth_stencil_ =
          PushSystemTemp(current_shader().writes_depth() ? 0b0001 : 0b1111);
    }
    uint32_t shader_writes_color_targets =
        current_shader().writes_color_targets();
    for (uint32_t i = 0; i < 4; ++i) {
      if (shader_writes_color_targets & (1 << i)) {
        system_temps_color_[i] = PushSystemTemp(0b1111);
      }
    }
  }

  if (!is_depth_only_pixel_shader_) {
    // Allocate temporary registers for memexport addresses and data.
    std::memset(system_temps_memexport_address_, 0xFF,
                sizeof(system_temps_memexport_address_));
    std::memset(system_temps_memexport_data_, 0xFF,
                sizeof(system_temps_memexport_data_));
    system_temp_memexport_written_ = UINT32_MAX;
    const uint8_t* memexports_written = current_shader().memexport_eM_written();
    for (uint32_t i = 0; i < Shader::kMaxMemExports; ++i) {
      uint32_t memexport_alloc_written = memexports_written[i];
      if (memexport_alloc_written == 0) {
        continue;
      }
      // If memexport is used at all, allocate a register containing whether eM#
      // have actually been written to.
      if (system_temp_memexport_written_ == UINT32_MAX) {
        system_temp_memexport_written_ = PushSystemTemp(0b1111);
      }
      system_temps_memexport_address_[i] = PushSystemTemp(0b1111);
      uint32_t memexport_data_index;
      while (xe::bit_scan_forward(memexport_alloc_written,
                                  &memexport_data_index)) {
        memexport_alloc_written &= ~(1u << memexport_data_index);
        system_temps_memexport_data_[i][memexport_data_index] =
            PushSystemTemp();
      }
    }

    // Allocate system temporary variables for the translated code. Since access
    // depends on the guest code (thus no guarantees), initialize everything
    // now (except for pv, it's an internal temporary variable, not accessible
    // by the guest).
    system_temp_result_ = PushSystemTemp();
    system_temp_ps_pc_p0_a0_ = PushSystemTemp(0b1111);
    system_temp_aL_ = PushSystemTemp(0b1111);
    system_temp_loop_count_ = PushSystemTemp(0b1111);
    system_temp_grad_h_lod_ = PushSystemTemp(0b1111);
    system_temp_grad_v_ = PushSystemTemp(0b0111);

    // Zero general-purpose registers to prevent crashes when the game
    // references them after only initializing them conditionally.
    for (uint32_t i = is_pixel_shader() ? xenos::kMaxInterpolators : 0;
         i < register_count(); ++i) {
      a_.OpMov(current_shader().uses_register_dynamic_addressing()
                   ? dxbc::Dest::X(0, i)
                   : dxbc::Dest::R(i),
               dxbc::Src::LF(0.0f));
    }
  }

  // Write stage-specific prologue.
  if (is_vertex_shader()) {
    StartVertexOrDomainShader();
  } else if (is_pixel_shader()) {
    StartPixelShader();
  }

  // If not translating anything, don't start the main loop.
  if (is_depth_only_pixel_shader_) {
    return;
  }

  // Start the main loop (for jumping to labels by setting pc and continuing).
  a_.OpLoop();
  // Switch and the first label (pc == 0).
  if (UseSwitchForControlFlow()) {
    a_.OpSwitch(dxbc::Src::R(system_temp_ps_pc_p0_a0_, dxbc::Src::kYYYY));
    a_.OpCase(dxbc::Src::LU(0));
  } else {
    a_.OpIf(false, dxbc::Src::R(system_temp_ps_pc_p0_a0_, dxbc::Src::kYYYY));
  }
}

void DxbcShaderTranslator::CompleteVertexOrDomainShader() {
  uint32_t temp = PushSystemTemp();
  dxbc::Dest temp_x_dest(dxbc::Dest::R(temp, 0b0001));
  dxbc::Src temp_x_src(dxbc::Src::R(temp, dxbc::Src::kXXXX));

  system_constants_used_ |= 1ull << kSysConst_Flags_Index;
  dxbc::Src flags_src(dxbc::Src::CB(cbuffer_index_system_constants_,
                                    uint32_t(CbufferRegister::kSystemConstants),
                                    kSysConst_Flags_Vec)
                          .Select(kSysConst_Flags_Comp));

  // Check if the shader already returns W, not 1/W, and if it doesn't, turn 1/W
  // into W. Using div rather than relaxed-precision rcp for safety.
  a_.OpAnd(temp_x_dest, flags_src, dxbc::Src::LU(kSysFlag_WNotReciprocal));
  a_.OpIf(false, temp_x_src);
  a_.OpDiv(dxbc::Dest::R(system_temp_position_, 0b1000), dxbc::Src::LF(1.0f),
           dxbc::Src::R(system_temp_position_, dxbc::Src::kWWWW));
  a_.OpEndIf();

  // Check if the shader returns XY/W rather than XY, and if it does, revert
  // that.
  // TODO(Triang3l): Check if having XY or Z pre-divided by W should result in
  // affine interpolation.
  a_.OpAnd(temp_x_dest, flags_src, dxbc::Src::LU(kSysFlag_XYDividedByW));
  a_.OpIf(true, temp_x_src);
  a_.OpMul(dxbc::Dest::R(system_temp_position_, 0b0011),
           dxbc::Src::R(system_temp_position_),
           dxbc::Src::R(system_temp_position_, dxbc::Src::kWWWW));
  a_.OpEndIf();

  // Check if the shader returns Z/W rather than Z, and if it does, revert that.
  // TODO(Triang3l): Check if having XY or Z pre-divided by W should result in
  // affine interpolation.
  a_.OpAnd(temp_x_dest, flags_src, dxbc::Src::LU(kSysFlag_ZDividedByW));
  a_.OpIf(true, temp_x_src);
  a_.OpMul(dxbc::Dest::R(system_temp_position_, 0b0100),
           dxbc::Src::R(system_temp_position_, dxbc::Src::kZZZZ),
           dxbc::Src::R(system_temp_position_, dxbc::Src::kWWWW));
  a_.OpEndIf();

  // Zero-initialize SV_ClipDistance# (for user clip planes) and SV_CullDistance
  // (for vertex kill) in case they're not needed.
  a_.OpMov(dxbc::Dest::O(uint32_t(InOutRegister::kVSDSOutClipDistance0123)),
           dxbc::Src::LF(0.0f));
  a_.OpMov(dxbc::Dest::O(
               uint32_t(InOutRegister::kVSDSOutClipDistance45AndCullDistance),
               0b0111),
           dxbc::Src::LF(0.0f));
  // Clip against user clip planes.
  // Not possible to handle UCP_CULL_ONLY_ENA with the same shader though, since
  // there can be only 8 SV_ClipDistance + SV_CullDistance values at most, but
  // 12 would be needed.
  system_constants_used_ |= 1ull << kSysConst_UserClipPlanes_Index;
  for (uint32_t i = 0; i < 6; ++i) {
    // Check if the clip plane is enabled - this `if` is needed, as opposed to
    // just zeroing the clip planes in the constants, so Infinity and NaN in the
    // position won't have any effect caused by this if clip planes are
    // disabled.
    a_.OpAnd(temp_x_dest, flags_src,
             dxbc::Src::LU(kSysFlag_UserClipPlane0 << i));
    a_.OpIf(true, temp_x_src);
    a_.OpDP4(dxbc::Dest::O(
                 uint32_t(InOutRegister::kVSDSOutClipDistance0123) + (i >> 2),
                 1 << (i & 3)),
             dxbc::Src::R(system_temp_position_),
             dxbc::Src::CB(cbuffer_index_system_constants_,
                           uint32_t(CbufferRegister::kSystemConstants),
                           kSysConst_UserClipPlanes_Vec + i));
    a_.OpEndIf();
  }

  // Apply scale for guest to host viewport and clip space conversion. Also, if
  // the vertex shader is multipass, the NDC scale constant can be used to set
  // position to NaN to kill all primitives.
  system_constants_used_ |= 1ull << kSysConst_NDCScale_Index;
  a_.OpMul(dxbc::Dest::R(system_temp_position_, 0b0111),
           dxbc::Src::R(system_temp_position_),
           dxbc::Src::CB(cbuffer_index_system_constants_,
                         uint32_t(CbufferRegister::kSystemConstants),
                         kSysConst_NDCScale_Vec,
                         kSysConst_NDCScale_Comp * 0b010101 + 0b100100));

  // Apply offset (multiplied by W) used for the same purposes.
  system_constants_used_ |= 1ull << kSysConst_NDCOffset_Index;
  a_.OpMAd(dxbc::Dest::R(system_temp_position_, 0b0111),
           dxbc::Src::CB(cbuffer_index_system_constants_,
                         uint32_t(CbufferRegister::kSystemConstants),
                         kSysConst_NDCOffset_Vec,
                         kSysConst_NDCOffset_Comp * 0b010101 + 0b100100),
           dxbc::Src::R(system_temp_position_, dxbc::Src::kWWWW),
           dxbc::Src::R(system_temp_position_));

  // Write Z and W of the position to a separate attribute so ROV output can get
  // per-sample depth.
  a_.OpMov(dxbc::Dest::O(uint32_t(InOutRegister::kVSDSOutClipSpaceZW), 0b0011),
           dxbc::Src::R(system_temp_position_, 0b1110));

  // Assuming SV_CullDistance was zeroed earlier in this function.
  // Kill the primitive if needed - check if the shader wants to kill.
  // TODO(Triang3l): Find if the condition is actually the flag being non-zero.
  a_.OpNE(temp_x_dest,
          dxbc::Src::R(system_temp_point_size_edge_flag_kill_vertex_,
                       dxbc::Src::kZZZZ),
          dxbc::Src::LF(0.0f));
  a_.OpIf(true, temp_x_src);
  {
    // Extract the killing condition.
    a_.OpAnd(temp_x_dest, flags_src,
             dxbc::Src::LU(kSysFlag_KillIfAnyVertexKilled));
    a_.OpIf(true, temp_x_src);
    {
      // Kill the primitive if any vertex is killed - write NaN to position.
      a_.OpMov(dxbc::Dest::R(system_temp_position_, 0b1000),
               dxbc::Src::LF(std::nanf("")));
    }
    a_.OpElse();
    {
      // Kill the primitive if all vertices are killed - set SV_CullDistance to
      // negative.
      a_.OpMov(
          dxbc::Dest::O(
              uint32_t(InOutRegister::kVSDSOutClipDistance45AndCullDistance),
              0b0100),
          dxbc::Src::LF(-1.0f));
    }
    a_.OpEndIf();
  }
  a_.OpEndIf();

  // Write the position to the output.
  a_.OpMov(dxbc::Dest::O(uint32_t(InOutRegister::kVSDSOutPosition)),
           dxbc::Src::R(system_temp_position_));

  // Zero the point coordinate (will be set in the geometry shader if needed)
  // and write the point size.
  a_.OpMov(
      dxbc::Dest::O(uint32_t(InOutRegister::kVSDSOutPointParameters), 0b0011),
      dxbc::Src::LF(0.0f));
  a_.OpMov(
      dxbc::Dest::O(uint32_t(InOutRegister::kVSDSOutPointParameters), 0b0100),
      dxbc::Src::R(system_temp_point_size_edge_flag_kill_vertex_,
                   dxbc::Src::kXXXX));

  // Release temp.
  PopSystemTemp();
}

void DxbcShaderTranslator::CompleteShaderCode() {
  if (!is_depth_only_pixel_shader_) {
    // Close the last exec, there's nothing to merge it with anymore, and we're
    // closing upper-level flow control blocks.
    CloseExecConditionals();
    // Close the last label and the switch.
    if (UseSwitchForControlFlow()) {
      a_.OpBreak();
      a_.OpEndSwitch();
    } else {
      a_.OpEndIf();
    }
    // End the main loop.
    a_.OpBreak();
    a_.OpEndLoop();

    // Release the following system temporary values so epilogue can reuse them:
    // - system_temp_result_.
    // - system_temp_ps_pc_p0_a0_.
    // - system_temp_aL_.
    // - system_temp_loop_count_.
    // - system_temp_grad_h_lod_.
    // - system_temp_grad_v_.
    PopSystemTemp(6);

    // Write memexported data to the shared memory UAV.
    ExportToMemory();

    // Release memexport temporary registers.
    for (int i = Shader::kMaxMemExports - 1; i >= 0; --i) {
      if (system_temps_memexport_address_[i] == UINT32_MAX) {
        continue;
      }
      // Release exported data registers.
      for (int j = 4; j >= 0; --j) {
        if (system_temps_memexport_data_[i][j] != UINT32_MAX) {
          PopSystemTemp();
        }
      }
      // Release the address register.
      PopSystemTemp();
    }
    if (system_temp_memexport_written_ != UINT32_MAX) {
      PopSystemTemp();
    }
  }

  // Write stage-specific epilogue.
  if (is_vertex_shader()) {
    CompleteVertexOrDomainShader();
  } else if (is_pixel_shader()) {
    CompletePixelShader();
  }

  // Return from `main`.
  a_.OpRet();

  if (is_vertex_shader()) {
    // Release system_temp_position_ and
    // system_temp_point_size_edge_flag_kill_vertex_.
    PopSystemTemp(2);
  } else if (is_pixel_shader()) {
    // Release system_temps_color_.
    uint32_t shader_writes_color_targets =
        current_shader().writes_color_targets();
    for (int32_t i = 3; i >= 0; --i) {
      if (shader_writes_color_targets & (1 << i)) {
        PopSystemTemp();
      }
    }
    if (IsDepthStencilSystemTempUsed()) {
      // Release system_temp_depth_stencil_.
      PopSystemTemp();
    }
    if (edram_rov_used_) {
      // Release system_temp_rov_params_.
      PopSystemTemp();
    }
  }
}

std::vector<uint8_t> DxbcShaderTranslator::CompleteTranslation() {
  // Write the code epilogue.
  CompleteShaderCode();

  shader_object_.clear();

  uint32_t has_pcsg = IsDxbcDomainShader() ? 1 : 0;

  // Write the shader object header.
  shader_object_.push_back('CBXD');
  // Checksum (set later).
  for (uint32_t i = 0; i < 4; ++i) {
    shader_object_.push_back(0);
  }
  shader_object_.push_back(1);
  // Size (set later).
  shader_object_.push_back(0);
  // 5 or 6 chunks - RDEF, ISGN, optionally PCSG, OSGN, SHEX, STAT.
  shader_object_.push_back(5 + has_pcsg);
  // Chunk offsets (set later).
  for (uint32_t i = 0; i < shader_object_[7]; ++i) {
    shader_object_.push_back(0);
  }

  uint32_t chunk_position_dwords;

  // Write Resource DEFinitions.
  chunk_position_dwords = uint32_t(shader_object_.size());
  shader_object_[8] = chunk_position_dwords * sizeof(uint32_t);
  shader_object_.push_back('FEDR');
  shader_object_.push_back(0);
  WriteResourceDefinitions();
  shader_object_[chunk_position_dwords + 1] =
      (uint32_t(shader_object_.size()) - chunk_position_dwords - 2) *
      sizeof(uint32_t);

  // Write Input SiGNature.
  chunk_position_dwords = uint32_t(shader_object_.size());
  shader_object_[9] = chunk_position_dwords * sizeof(uint32_t);
  shader_object_.push_back('NGSI');
  shader_object_.push_back(0);
  WriteInputSignature();
  shader_object_[chunk_position_dwords + 1] =
      (uint32_t(shader_object_.size()) - chunk_position_dwords - 2) *
      sizeof(uint32_t);

  // Write Patch Constant SiGnature.
  if (has_pcsg) {
    chunk_position_dwords = uint32_t(shader_object_.size());
    shader_object_[10] = chunk_position_dwords * sizeof(uint32_t);
    shader_object_.push_back('GSCP');
    shader_object_.push_back(0);
    WritePatchConstantSignature();
    shader_object_[chunk_position_dwords + 1] =
        (uint32_t(shader_object_.size()) - chunk_position_dwords - 2) *
        sizeof(uint32_t);
  }

  // Write Output SiGNature.
  chunk_position_dwords = uint32_t(shader_object_.size());
  shader_object_[10 + has_pcsg] = chunk_position_dwords * sizeof(uint32_t);
  shader_object_.push_back('NGSO');
  shader_object_.push_back(0);
  WriteOutputSignature();
  shader_object_[chunk_position_dwords + 1] =
      (uint32_t(shader_object_.size()) - chunk_position_dwords - 2) *
      sizeof(uint32_t);

  // Write SHader EXtended.
  chunk_position_dwords = uint32_t(shader_object_.size());
  shader_object_[11 + has_pcsg] = chunk_position_dwords * sizeof(uint32_t);
  shader_object_.push_back('XEHS');
  shader_object_.push_back(0);
  WriteShaderCode();
  shader_object_[chunk_position_dwords + 1] =
      (uint32_t(shader_object_.size()) - chunk_position_dwords - 2) *
      sizeof(uint32_t);

  // Write STATistics.
  chunk_position_dwords = uint32_t(shader_object_.size());
  shader_object_[12 + has_pcsg] = chunk_position_dwords * sizeof(uint32_t);
  shader_object_.push_back('TATS');
  shader_object_.push_back(sizeof(stat_));
  shader_object_.resize(shader_object_.size() +
                        sizeof(stat_) / sizeof(uint32_t));
  std::memcpy(&shader_object_[chunk_position_dwords + 2], &stat_,
              sizeof(stat_));

  // Fill the remaining fields of the header and copy bytes out.
  uint32_t shader_object_size =
      uint32_t(shader_object_.size() * sizeof(uint32_t));
  shader_object_[6] = shader_object_size;
  // The checksum includes the size field, so it must be the last.
  CalculateDXBCChecksum(reinterpret_cast<unsigned char*>(shader_object_.data()),
                        shader_object_size,
                        reinterpret_cast<unsigned int*>(&shader_object_[1]));
  // TODO(Triang3l): Avoid copy?
  std::vector<uint8_t> shader_object_bytes;
  shader_object_bytes.resize(shader_object_size);
  std::memcpy(shader_object_bytes.data(), shader_object_.data(),
              shader_object_size);
  return shader_object_bytes;
}

void DxbcShaderTranslator::PostTranslation() {
  Shader::Translation& translation = current_translation();
  if (!translation.is_valid()) {
    return;
  }
  DxbcShader* dxbc_shader = dynamic_cast<DxbcShader*>(&translation.shader());
  if (dxbc_shader && !dxbc_shader->bindings_setup_entered_.test_and_set(
                         std::memory_order_relaxed)) {
    dxbc_shader->texture_bindings_.clear();
    dxbc_shader->texture_bindings_.reserve(texture_bindings_.size());
    dxbc_shader->used_texture_mask_ = 0;
    for (const TextureBinding& translator_binding : texture_bindings_) {
      DxbcShader::TextureBinding& shader_binding =
          dxbc_shader->texture_bindings_.emplace_back();
      // For a stable hash.
      std::memset(&shader_binding, 0, sizeof(shader_binding));
      shader_binding.bindless_descriptor_index =
          translator_binding.bindless_descriptor_index;
      shader_binding.fetch_constant = translator_binding.fetch_constant;
      shader_binding.dimension = translator_binding.dimension;
      shader_binding.is_signed = translator_binding.is_signed;
      dxbc_shader->used_texture_mask_ |= 1u
                                         << translator_binding.fetch_constant;
    }
    dxbc_shader->sampler_bindings_.clear();
    dxbc_shader->sampler_bindings_.reserve(sampler_bindings_.size());
    for (const SamplerBinding& translator_binding : sampler_bindings_) {
      DxbcShader::SamplerBinding& shader_binding =
          dxbc_shader->sampler_bindings_.emplace_back();
      shader_binding.bindless_descriptor_index =
          translator_binding.bindless_descriptor_index;
      shader_binding.fetch_constant = translator_binding.fetch_constant;
      shader_binding.mag_filter = translator_binding.mag_filter;
      shader_binding.min_filter = translator_binding.min_filter;
      shader_binding.mip_filter = translator_binding.mip_filter;
      shader_binding.aniso_filter = translator_binding.aniso_filter;
    }
  }
}

void DxbcShaderTranslator::EmitInstructionDisassembly() {
  if (!emit_source_map_) {
    return;
  }

  const char* source = instruction_disassembly_buffer_.buffer();
  uint32_t length = uint32_t(instruction_disassembly_buffer_.length());
  // Trim leading spaces and trailing new line.
  while (length != 0 && source[0] == ' ') {
    ++source;
    --length;
  }
  while (length != 0 && source[length - 1] == '\n') {
    --length;
  }
  if (length == 0) {
    return;
  }

  uint32_t length_dwords =
      (length + 1 + (sizeof(uint32_t) - 1)) / sizeof(uint32_t);
  shader_code_.push_back(
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_CUSTOMDATA) |
      ENCODE_D3D10_SB_CUSTOMDATA_CLASS(D3D10_SB_CUSTOMDATA_COMMENT));
  shader_code_.push_back(2 + length_dwords);
  size_t offset_dwords = shader_code_.size();
  shader_code_.resize(offset_dwords + length_dwords);
  char* target = reinterpret_cast<char*>(&shader_code_[offset_dwords]);
  std::memcpy(target, source, length);
  target[length] = '\0';
  // Don't leave uninitialized data, and make sure multiple invocations of the
  // translator for the same Xenos shader give the same DXBC.
  std::memset(target + length + 1, dxbc::kAlignmentPadding,
              length_dwords * sizeof(uint32_t) - length - 1);
}

dxbc::Src DxbcShaderTranslator::LoadOperand(const InstructionOperand& operand,
                                            uint32_t needed_components,
                                            bool& temp_pushed_out) {
  temp_pushed_out = false;

  uint32_t first_needed_component;
  if (!xe::bit_scan_forward(needed_components, &first_needed_component)) {
    return dxbc::Src::LF(0.0f);
  }

  dxbc::Index index(operand.storage_index);
  switch (operand.storage_addressing_mode) {
    case InstructionStorageAddressingMode::kStatic:
      break;
    case InstructionStorageAddressingMode::kAddressAbsolute:
      index = dxbc::Index(system_temp_ps_pc_p0_a0_, 3, operand.storage_index);
      break;
    case InstructionStorageAddressingMode::kAddressRelative:
      index = dxbc::Index(system_temp_aL_, 0, operand.storage_index);
      break;
  }

  dxbc::Src src(dxbc::Src::LF(0.0f));
  switch (operand.storage_source) {
    case InstructionStorageSource::kRegister: {
      if (current_shader().uses_register_dynamic_addressing()) {
        // Load x#[#] to r# because x#[#] can be used only with mov.
        uint32_t temp = PushSystemTemp();
        temp_pushed_out = true;
        uint32_t used_swizzle_components = 0;
        for (uint32_t i = 0; i < uint32_t(operand.component_count); ++i) {
          if (!(needed_components & (1 << i))) {
            continue;
          }
          SwizzleSource component = operand.GetComponent(i);
          assert_true(component >= SwizzleSource::kX &&
                      component <= SwizzleSource::kW);
          used_swizzle_components |=
              1 << (uint32_t(component) - uint32_t(SwizzleSource::kX));
        }
        assert_not_zero(used_swizzle_components);
        a_.OpMov(dxbc::Dest::R(temp, used_swizzle_components),
                 dxbc::Src::X(0, index));
        src = dxbc::Src::R(temp);
      } else {
        assert_true(operand.storage_addressing_mode ==
                    InstructionStorageAddressingMode::kStatic);
        src = dxbc::Src::R(index.index_);
      }
    } break;
    case InstructionStorageSource::kConstantFloat: {
      if (cbuffer_index_float_constants_ == kBindingIndexUnallocated) {
        cbuffer_index_float_constants_ = cbuffer_count_++;
      }
      const Shader::ConstantRegisterMap& constant_register_map =
          current_shader().constant_register_map();
      if (operand.storage_addressing_mode ==
          InstructionStorageAddressingMode::kStatic) {
        uint32_t float_constant_index =
            constant_register_map.GetPackedFloatConstantIndex(
                operand.storage_index);
        assert_true(float_constant_index != UINT32_MAX);
        if (float_constant_index == UINT32_MAX) {
          return dxbc::Src::LF(0.0f);
        }
        index.index_ = float_constant_index;
      } else {
        assert_true(constant_register_map.float_dynamic_addressing);
      }
      src = dxbc::Src::CB(cbuffer_index_float_constants_,
                          uint32_t(CbufferRegister::kFloatConstants), index);
    } break;
    default:
      assert_unhandled_case(operand.storage_source);
      return dxbc::Src::LF(0.0f);
  }

  // Swizzle, skipping unneeded components similar to how FXC skips components,
  // by replacing them with the leftmost used one.
  uint32_t swizzle = 0;
  for (uint32_t i = 0; i < 4; ++i) {
    SwizzleSource component = operand.GetComponent(
        (needed_components & (1 << i)) ? i : first_needed_component);
    assert_true(component >= SwizzleSource::kX &&
                component <= SwizzleSource::kW);
    swizzle |= (uint32_t(component) - uint32_t(SwizzleSource::kX)) << (i * 2);
  }
  src = src.Swizzle(swizzle);

  return src.WithModifiers(operand.is_absolute_value, operand.is_negated);
}

void DxbcShaderTranslator::StoreResult(const InstructionResult& result,
                                       const dxbc::Src& src,
                                       bool can_store_memexport_address) {
  uint32_t used_write_mask = result.GetUsedWriteMask();
  if (!used_write_mask) {
    return;
  }

  // Get the destination address and type.
  dxbc::Dest dest(dxbc::Dest::Null());
  bool is_clamped = result.is_clamped;
  switch (result.storage_target) {
    case InstructionStorageTarget::kNone:
      return;
    case InstructionStorageTarget::kRegister:
      if (current_shader().uses_register_dynamic_addressing()) {
        dxbc::Index register_index(result.storage_index);
        switch (result.storage_addressing_mode) {
          case InstructionStorageAddressingMode::kStatic:
            break;
          case InstructionStorageAddressingMode::kAddressAbsolute:
            register_index =
                dxbc::Index(system_temp_ps_pc_p0_a0_, 3, result.storage_index);
            break;
          case InstructionStorageAddressingMode::kAddressRelative:
            register_index =
                dxbc::Index(system_temp_aL_, 0, result.storage_index);
            break;
        }
        dest = dxbc::Dest::X(0, register_index);
      } else {
        assert_true(result.storage_addressing_mode ==
                    InstructionStorageAddressingMode::kStatic);
        dest = dxbc::Dest::R(result.storage_index);
      }
      break;
    case InstructionStorageTarget::kInterpolator:
      dest = dxbc::Dest::O(uint32_t(InOutRegister::kVSDSOutInterpolators) +
                           result.storage_index);
      break;
    case InstructionStorageTarget::kPosition:
      dest = dxbc::Dest::R(system_temp_position_);
      break;
    case InstructionStorageTarget::kPointSizeEdgeFlagKillVertex:
      assert_zero(used_write_mask & 0b1000);
      dest = dxbc::Dest::R(system_temp_point_size_edge_flag_kill_vertex_);
      break;
    case InstructionStorageTarget::kExportAddress:
      // Validate memexport writes (Halo 3 has some weird invalid ones).
      if (!can_store_memexport_address || memexport_alloc_current_count_ == 0 ||
          memexport_alloc_current_count_ > Shader::kMaxMemExports ||
          system_temps_memexport_address_[memexport_alloc_current_count_ - 1] ==
              UINT32_MAX) {
        return;
      }
      dest = dxbc::Dest::R(
          system_temps_memexport_address_[memexport_alloc_current_count_ - 1]);
      break;
    case InstructionStorageTarget::kExportData: {
      // Validate memexport writes (Halo 3 has some weird invalid ones).
      if (memexport_alloc_current_count_ == 0 ||
          memexport_alloc_current_count_ > Shader::kMaxMemExports ||
          system_temps_memexport_data_[memexport_alloc_current_count_ - 1]
                                      [result.storage_index] == UINT32_MAX) {
        return;
      }
      dest = dxbc::Dest::R(
          system_temps_memexport_data_[memexport_alloc_current_count_ - 1]
                                      [result.storage_index]);
      // Mark that the eM# has been written to and needs to be exported.
      assert_not_zero(used_write_mask);
      uint32_t memexport_index = memexport_alloc_current_count_ - 1;
      a_.OpOr(dxbc::Dest::R(system_temp_memexport_written_,
                            1 << (memexport_index >> 2)),
              dxbc::Src::R(system_temp_memexport_written_)
                  .Select(memexport_index >> 2),
              dxbc::Src::LU(uint32_t(1) << (result.storage_index +
                                            ((memexport_index & 3) << 3))));
    } break;
    case InstructionStorageTarget::kColor:
      assert_not_zero(used_write_mask);
      assert_true(current_shader().writes_color_target(result.storage_index));
      dest = dxbc::Dest::R(system_temps_color_[result.storage_index]);
      if (edram_rov_used_) {
        // For ROV output, mark that the color has been written to.
        // According to:
        // https://docs.microsoft.com/en-us/windows/desktop/direct3dhlsl/dx9-graphics-reference-asm-ps-registers-output-color
        // if a color target hasn't been written to - including due to flow
        // control - the render target must not be modified (the unwritten
        // components of a written target are undefined, not sure if this
        // behavior is respected on the real GPU, but the ROV code currently
        // doesn't preserve unmodified components).
        a_.OpOr(dxbc::Dest::R(system_temp_rov_params_, 0b0001),
                dxbc::Src::R(system_temp_rov_params_, dxbc::Src::kXXXX),
                dxbc::Src::LU(uint32_t(1) << (8 + result.storage_index)));
      }
      break;
    case InstructionStorageTarget::kDepth:
      // Writes X to scalar oDepth or to X of system_temp_depth_stencil_, no
      // additional swizzling needed.
      assert_true(used_write_mask == 0b0001);
      assert_true(current_shader().writes_depth());
      if (IsDepthStencilSystemTempUsed()) {
        dest = dxbc::Dest::R(system_temp_depth_stencil_);
      } else {
        dest = dxbc::Dest::ODepth();
      }
      // Depth outside [0, 1] is not safe for use with the ROV code and with
      // 20e4-as-32 conversion. Though 20e4 float depth can store values between
      // 1 and 2, it's a very unusual case. Direct3D 10+ SV_Depth, however, can
      // accept any values, including specials, when the depth buffer is
      // floating-point; but depth is clamped to the viewport bounds anyway.
      is_clamped = true;
      break;
  }
  if (dest.type_ == dxbc::OperandType::kNull) {
    return;
  }

  // Write.
  uint32_t src_additional_swizzle = 0;
  uint32_t constant_mask = 0, constant_1_mask = 0;
  for (uint32_t i = 0; i < 4; ++i) {
    if (!(used_write_mask & (1 << i))) {
      continue;
    }
    SwizzleSource component = result.components[i];
    if (component >= SwizzleSource::kX && component <= SwizzleSource::kW) {
      src_additional_swizzle |=
          (uint32_t(component) - uint32_t(SwizzleSource::kX)) << (i * 2);
    } else {
      constant_mask |= 1 << i;
      if (component == SwizzleSource::k1) {
        constant_1_mask |= 1 << i;
      }
    }
  }
  if (used_write_mask != constant_mask) {
    a_.OpMov(dest.Mask(used_write_mask & ~constant_mask),
             src.SwizzleSwizzled(src_additional_swizzle), is_clamped);
  }
  if (constant_mask) {
    a_.OpMov(dest.Mask(constant_mask),
             dxbc::Src::LF(float(constant_1_mask & 1),
                           float((constant_1_mask >> 1) & 1),
                           float((constant_1_mask >> 2) & 1),
                           float((constant_1_mask >> 3) & 1)));
  }
}

void DxbcShaderTranslator::UpdateExecConditionalsAndEmitDisassembly(
    ParsedExecInstruction::Type type, uint32_t bool_constant_index,
    bool condition) {
  // Check if we can merge the new exec with the previous one, or the jump with
  // the previous exec. The instruction-level predicate check is also merged in
  // this case.
  bool merge = false;
  if (type == ParsedExecInstruction::Type::kConditional) {
    // Can merge conditional with conditional, as long as the bool constant and
    // the expected values are the same.
    if (cf_exec_bool_constant_ == bool_constant_index &&
        cf_exec_bool_constant_condition_ == condition) {
      merge = true;
    }
  } else if (type == ParsedExecInstruction::Type::kPredicated) {
    // Can merge predicated with predicated if the conditions are the same and
    // the previous exec hasn't modified the predicate register.
    if (!cf_exec_predicate_written_ && cf_exec_predicated_ &&
        cf_exec_predicate_condition_ == condition) {
      merge = true;
    }
  } else {
    // Can merge unconditional with unconditional.
    if (cf_exec_bool_constant_ == kCfExecBoolConstantNone &&
        !cf_exec_predicated_) {
      merge = true;
    }
  }

  if (merge) {
    // Emit the disassembly for the exec/jump merged with the previous one.
    EmitInstructionDisassembly();
    return;
  }

  CloseExecConditionals();

  // Emit the disassembly for the new exec/jump.
  EmitInstructionDisassembly();

  if (type == ParsedExecInstruction::Type::kConditional) {
    uint32_t bool_constant_test_temp = PushSystemTemp();
    // Check the bool constant value.
    if (cbuffer_index_bool_loop_constants_ == kBindingIndexUnallocated) {
      cbuffer_index_bool_loop_constants_ = cbuffer_count_++;
    }
    a_.OpAnd(dxbc::Dest::R(bool_constant_test_temp, 0b0001),
             dxbc::Src::CB(cbuffer_index_bool_loop_constants_,
                           uint32_t(CbufferRegister::kBoolLoopConstants),
                           bool_constant_index >> 7)
                 .Select((bool_constant_index >> 5) & 3),
             dxbc::Src::LU(uint32_t(1) << (bool_constant_index & 31)));
    // Open the new `if`.
    a_.OpIf(condition, dxbc::Src::R(bool_constant_test_temp, dxbc::Src::kXXXX));
    // Release bool_constant_test_temp.
    PopSystemTemp();
    cf_exec_bool_constant_ = bool_constant_index;
    cf_exec_bool_constant_condition_ = condition;
  } else if (type == ParsedExecInstruction::Type::kPredicated) {
    a_.OpIf(condition,
            dxbc::Src::R(system_temp_ps_pc_p0_a0_, dxbc::Src::kZZZZ));
    cf_exec_predicated_ = true;
    cf_exec_predicate_condition_ = condition;
  }
}

void DxbcShaderTranslator::CloseExecConditionals() {
  // Within the exec - instruction-level predicate check.
  CloseInstructionPredication();
  // Exec level.
  if (cf_exec_bool_constant_ != kCfExecBoolConstantNone ||
      cf_exec_predicated_) {
    a_.OpEndIf();
    cf_exec_bool_constant_ = kCfExecBoolConstantNone;
    cf_exec_predicated_ = false;
  }
  // Nothing relies on the predicate value being unchanged now.
  cf_exec_predicate_written_ = false;
}

void DxbcShaderTranslator::UpdateInstructionPredicationAndEmitDisassembly(
    bool predicated, bool condition) {
  if (!predicated) {
    CloseInstructionPredication();
    EmitInstructionDisassembly();
    return;
  }

  if (cf_instruction_predicate_if_open_) {
    if (cf_instruction_predicate_condition_ == condition) {
      // Already in the needed instruction-level `if`.
      EmitInstructionDisassembly();
      return;
    }
    CloseInstructionPredication();
  }

  // Emit the disassembly before opening (or not opening) the new conditional.
  EmitInstructionDisassembly();

  // If the instruction predicate condition is the same as the exec predicate
  // condition, no need to open a check. However, if there was a `setp` prior
  // to this instruction, the predicate value now may be different than it was
  // in the beginning of the exec.
  if (!cf_exec_predicate_written_ && cf_exec_predicated_ &&
      cf_exec_predicate_condition_ == condition) {
    return;
  }

  a_.OpIf(condition, dxbc::Src::R(system_temp_ps_pc_p0_a0_, dxbc::Src::kZZZZ));
  cf_instruction_predicate_if_open_ = true;
  cf_instruction_predicate_condition_ = condition;
}

void DxbcShaderTranslator::CloseInstructionPredication() {
  if (cf_instruction_predicate_if_open_) {
    a_.OpEndIf();
    cf_instruction_predicate_if_open_ = false;
  }
}

void DxbcShaderTranslator::JumpToLabel(uint32_t address) {
  a_.OpMov(dxbc::Dest::R(system_temp_ps_pc_p0_a0_, 0b0010),
           dxbc::Src::LU(address));
  a_.OpContinue();
}

void DxbcShaderTranslator::ProcessLabel(uint32_t cf_index) {
  if (cf_index == 0) {
    // 0 already added in the beginning.
    return;
  }
  // Close flow control on the deeper levels below - prevent attempts to merge
  // execs across labels.
  CloseExecConditionals();
  if (UseSwitchForControlFlow()) {
    // Fallthrough to the label from the previous one on the next iteration if
    // no `continue` was done. Can't simply fallthrough because in DXBC, a
    // non-empty switch case must end with a break.
    JumpToLabel(cf_index);
    // Close the previous label.
    a_.OpBreak();
    // Go to the next label.
    a_.OpCase(dxbc::Src::LU(cf_index));
  } else {
    // Close the previous label.
    a_.OpEndIf();
    // if (pc <= cf_index)
    uint32_t test_temp = PushSystemTemp();
    a_.OpUGE(dxbc::Dest::R(test_temp, 0b0001), dxbc::Src::LU(cf_index),
             dxbc::Src::R(system_temp_ps_pc_p0_a0_, dxbc::Src::kYYYY));
    a_.OpIf(true, dxbc::Src::R(test_temp, dxbc::Src::kXXXX));
    // Release test_temp.
    PopSystemTemp();
  }
}

void DxbcShaderTranslator::ProcessExecInstructionBegin(
    const ParsedExecInstruction& instr) {
  if (emit_source_map_) {
    instruction_disassembly_buffer_.Reset();
    instr.Disassemble(&instruction_disassembly_buffer_);
  }
  UpdateExecConditionalsAndEmitDisassembly(
      instr.type, instr.bool_constant_index, instr.condition);
}

void DxbcShaderTranslator::ProcessExecInstructionEnd(
    const ParsedExecInstruction& instr) {
  if (instr.is_end) {
    // Break out of the main loop.
    CloseInstructionPredication();
    if (UseSwitchForControlFlow()) {
      // Write an invalid value to pc.
      a_.OpMov(dxbc::Dest::R(system_temp_ps_pc_p0_a0_, 0b0010),
               dxbc::Src::LU(UINT32_MAX));
      // Go to the next iteration, where switch cases won't be reached.
      a_.OpContinue();
    } else {
      a_.OpBreak();
    }
  }
}

void DxbcShaderTranslator::ProcessLoopStartInstruction(
    const ParsedLoopStartInstruction& instr) {
  // loop il<idx>, L<idx> - loop with loop data il<idx>, end @ L<idx>

  // Loop control is outside execs - actually close the last exec.
  CloseExecConditionals();

  if (emit_source_map_) {
    instruction_disassembly_buffer_.Reset();
    instr.Disassemble(&instruction_disassembly_buffer_);
    EmitInstructionDisassembly();
  }

  // Count (unsigned) in bits 0:7 of the loop constant, initial aL (unsigned) in
  // 8:15. Starting from vector 2 because of bool constants.
  if (cbuffer_index_bool_loop_constants_ == kBindingIndexUnallocated) {
    cbuffer_index_bool_loop_constants_ = cbuffer_count_++;
  }
  dxbc::Src loop_constant_src(
      dxbc::Src::CB(cbuffer_index_bool_loop_constants_,
                    uint32_t(CbufferRegister::kBoolLoopConstants),
                    2 + (instr.loop_constant_index >> 2))
          .Select(instr.loop_constant_index & 3));

  // Push the count to the loop count stack - move XYZ to YZW and set X to this
  // loop count.
  a_.OpMov(dxbc::Dest::R(system_temp_loop_count_, 0b1110),
           dxbc::Src::R(system_temp_loop_count_, 0b10010000));
  a_.OpAnd(dxbc::Dest::R(system_temp_loop_count_, 0b0001), loop_constant_src,
           dxbc::Src::LU(UINT8_MAX));

  // Push aL - keep the same value as in the previous loop if repeating, or the
  // new one otherwise.
  a_.OpMov(dxbc::Dest::R(system_temp_aL_, instr.is_repeat ? 0b1111 : 0b1110),
           dxbc::Src::R(system_temp_aL_, 0b10010000));
  if (!instr.is_repeat) {
    a_.OpUBFE(dxbc::Dest::R(system_temp_aL_, 0b0001), dxbc::Src::LU(8),
              dxbc::Src::LU(8), loop_constant_src);
  }

  // Break if the loop counter is 0 (since the condition is checked in the end).
  a_.OpIf(false, dxbc::Src::R(system_temp_loop_count_, dxbc::Src::kXXXX));
  JumpToLabel(instr.loop_skip_address);
  a_.OpEndIf();
}

void DxbcShaderTranslator::ProcessLoopEndInstruction(
    const ParsedLoopEndInstruction& instr) {
  // endloop il<idx>, L<idx> - end loop w/ data il<idx>, head @ L<idx>

  // Loop control is outside execs - actually close the last exec.
  CloseExecConditionals();

  if (emit_source_map_) {
    instruction_disassembly_buffer_.Reset();
    instr.Disassemble(&instruction_disassembly_buffer_);
    EmitInstructionDisassembly();
  }

  // Subtract 1 from the loop counter.
  a_.OpIAdd(dxbc::Dest::R(system_temp_loop_count_, 0b0001),
            dxbc::Src::R(system_temp_loop_count_, dxbc::Src::kXXXX),
            dxbc::Src::LI(-1));

  if (instr.is_predicated_break) {
    // if (loop_count.x == 0 || [!]p0)
    uint32_t break_case_temp = PushSystemTemp();
    if (instr.predicate_condition) {
      // If p0 is non-zero, set the test value to 0 (since if_z is used,
      // otherwise check if the loop counter is zero).
      a_.OpMovC(dxbc::Dest::R(break_case_temp, 0b0001),
                dxbc::Src::R(system_temp_ps_pc_p0_a0_, dxbc::Src::kZZZZ),
                dxbc::Src::LU(0),
                dxbc::Src::R(system_temp_loop_count_, dxbc::Src::kXXXX));
    } else {
      // If p0 is zero, set the test value to 0 (since if_z is used, otherwise
      // check if the loop counter is zero).
      a_.OpMovC(dxbc::Dest::R(break_case_temp, 0b0001),
                dxbc::Src::R(system_temp_ps_pc_p0_a0_, dxbc::Src::kZZZZ),
                dxbc::Src::R(system_temp_loop_count_, dxbc::Src::kXXXX),
                dxbc::Src::LU(0));
    }
    a_.OpIf(false, dxbc::Src::R(break_case_temp, dxbc::Src::kXXXX));
    // Release break_case_temp.
    PopSystemTemp();
  } else {
    // if (loop_count.x == 0)
    a_.OpIf(false, dxbc::Src::R(system_temp_loop_count_, dxbc::Src::kXXXX));
  }
  {
    // Break case.
    // Pop the current loop off the loop counter and the relative address
    // stacks - move YZW to XYZ and set W to 0.
    a_.OpMov(dxbc::Dest::R(system_temp_loop_count_, 0b0111),
             dxbc::Src::R(system_temp_loop_count_, 0b111001));
    a_.OpMov(dxbc::Dest::R(system_temp_loop_count_, 0b1000), dxbc::Src::LU(0));
    a_.OpMov(dxbc::Dest::R(system_temp_aL_, 0b0111),
             dxbc::Src::R(system_temp_aL_, 0b111001));
    a_.OpMov(dxbc::Dest::R(system_temp_aL_, 0b1000), dxbc::Src::LI(0));
    // Now going to fall through to the next exec (no need to jump).
  }
  a_.OpElse();
  {
    // Continue case.
    uint32_t aL_add_temp = PushSystemTemp();
    // Extract the value to add to aL (signed, in bits 16:23 of the loop
    // constant). Starting from vector 2 because of bool constants.
    if (cbuffer_index_bool_loop_constants_ == kBindingIndexUnallocated) {
      cbuffer_index_bool_loop_constants_ = cbuffer_count_++;
    }
    a_.OpIBFE(dxbc::Dest::R(aL_add_temp, 0b0001), dxbc::Src::LU(8),
              dxbc::Src::LU(16),
              dxbc::Src::CB(cbuffer_index_bool_loop_constants_,
                            uint32_t(CbufferRegister::kBoolLoopConstants),
                            2 + (instr.loop_constant_index >> 2))
                  .Select(instr.loop_constant_index & 3));
    // Add the needed value to aL.
    a_.OpIAdd(dxbc::Dest::R(system_temp_aL_, 0b0001),
              dxbc::Src::R(system_temp_aL_, dxbc::Src::kXXXX),
              dxbc::Src::R(aL_add_temp, dxbc::Src::kXXXX));
    // Release aL_add_temp.
    PopSystemTemp();
    // Jump back to the beginning of the loop body.
    JumpToLabel(instr.loop_body_address);
  }
  a_.OpEndIf();
}

void DxbcShaderTranslator::ProcessJumpInstruction(
    const ParsedJumpInstruction& instr) {
  if (emit_source_map_) {
    instruction_disassembly_buffer_.Reset();
    instr.Disassemble(&instruction_disassembly_buffer_);
  }

  // Treat like exec, merge with execs if possible, since it's an if too.
  ParsedExecInstruction::Type type;
  if (instr.type == ParsedJumpInstruction::Type::kConditional) {
    type = ParsedExecInstruction::Type::kConditional;
  } else if (instr.type == ParsedJumpInstruction::Type::kPredicated) {
    type = ParsedExecInstruction::Type::kPredicated;
  } else {
    type = ParsedExecInstruction::Type::kUnconditional;
  }
  UpdateExecConditionalsAndEmitDisassembly(type, instr.bool_constant_index,
                                           instr.condition);

  // UpdateExecConditionalsAndEmitDisassembly may not necessarily close the
  // instruction-level predicate check (it's not necessary if the execs are
  // merged), but here the instruction itself is on the flow control level, so
  // the predicate check is on the flow control level too.
  CloseInstructionPredication();

  JumpToLabel(instr.target_address);
}

void DxbcShaderTranslator::ProcessAllocInstruction(
    const ParsedAllocInstruction& instr) {
  if (emit_source_map_) {
    instruction_disassembly_buffer_.Reset();
    instr.Disassemble(&instruction_disassembly_buffer_);
    EmitInstructionDisassembly();
  }

  if (instr.type == AllocType::kMemory) {
    ++memexport_alloc_current_count_;
  }
}

uint32_t DxbcShaderTranslator::AppendString(std::vector<uint32_t>& dest,
                                            const char* source) {
  size_t size = std::strlen(source) + 1;
  size_t size_aligned = xe::align(size, sizeof(uint32_t));
  size_t dest_position = dest.size();
  dest.resize(dest_position + size_aligned / sizeof(uint32_t));
  std::memcpy(&dest[dest_position], source, size);
  // Don't leave uninitialized data, and make sure multiple invocations of the
  // translator for the same Xenos shader give the same DXBC.
  std::memset(reinterpret_cast<uint8_t*>(&dest[dest_position]) + size,
              dxbc::kAlignmentPadding, size_aligned - size);
  return uint32_t(size_aligned);
}

const DxbcShaderTranslator::RdefType DxbcShaderTranslator::rdef_types_[size_t(
    DxbcShaderTranslator::RdefTypeIndex::kCount)] = {
    // kFloat
    {"float", dxbc::RdefVariableClass::kScalar, dxbc::RdefVariableType::kFloat,
     1, 1, 0, 0, RdefTypeIndex::kUnknown, nullptr},
    // kFloat2
    {"float2", dxbc::RdefVariableClass::kVector, dxbc::RdefVariableType::kFloat,
     1, 2, 0, 0, RdefTypeIndex::kUnknown, nullptr},
    // kFloat3
    {"float3", dxbc::RdefVariableClass::kVector, dxbc::RdefVariableType::kFloat,
     1, 3, 0, 0, RdefTypeIndex::kUnknown, nullptr},
    // kFloat4
    {"float4", dxbc::RdefVariableClass::kVector, dxbc::RdefVariableType::kFloat,
     1, 4, 0, 0, RdefTypeIndex::kUnknown, nullptr},
    // kInt
    {"int", dxbc::RdefVariableClass::kScalar, dxbc::RdefVariableType::kInt, 1,
     1, 0, 0, RdefTypeIndex::kUnknown, nullptr},
    // kUint
    {"uint", dxbc::RdefVariableClass::kScalar, dxbc::RdefVariableType::kUInt, 1,
     1, 0, 0, RdefTypeIndex::kUnknown, nullptr},
    // kUint2
    {"uint2", dxbc::RdefVariableClass::kVector, dxbc::RdefVariableType::kUInt,
     1, 2, 0, 0, RdefTypeIndex::kUnknown, nullptr},
    // kUint4
    {"uint4", dxbc::RdefVariableClass::kVector, dxbc::RdefVariableType::kUInt,
     1, 4, 0, 0, RdefTypeIndex::kUnknown, nullptr},
    // kFloat4Array4
    {nullptr, dxbc::RdefVariableClass::kVector, dxbc::RdefVariableType::kFloat,
     1, 4, 4, 0, RdefTypeIndex::kFloat4, nullptr},
    // kFloat4Array6
    {nullptr, dxbc::RdefVariableClass::kVector, dxbc::RdefVariableType::kFloat,
     1, 4, 6, 0, RdefTypeIndex::kFloat4, nullptr},
    // kFloat4ConstantArray - float constants - size written dynamically.
    {nullptr, dxbc::RdefVariableClass::kVector, dxbc::RdefVariableType::kFloat,
     1, 4, 0, 0, RdefTypeIndex::kFloat4, nullptr},
    // kUint4Array2
    {nullptr, dxbc::RdefVariableClass::kVector, dxbc::RdefVariableType::kUInt,
     1, 4, 2, 0, RdefTypeIndex::kUint4, nullptr},
    // kUint4Array8
    {nullptr, dxbc::RdefVariableClass::kVector, dxbc::RdefVariableType::kUInt,
     1, 4, 8, 0, RdefTypeIndex::kUint4, nullptr},
    // kUint4Array48
    {nullptr, dxbc::RdefVariableClass::kVector, dxbc::RdefVariableType::kUInt,
     1, 4, 48, 0, RdefTypeIndex::kUint4, nullptr},
    // kUint4DescriptorIndexArray - bindless descriptor indices - size written
    // dynamically.
    {nullptr, dxbc::RdefVariableClass::kVector, dxbc::RdefVariableType::kUInt,
     1, 4, 0, 0, RdefTypeIndex::kUint4, nullptr},
};

const DxbcShaderTranslator::SystemConstantRdef DxbcShaderTranslator::
    system_constant_rdef_[DxbcShaderTranslator::kSysConst_Count] = {
        {"xe_flags", RdefTypeIndex::kUint, sizeof(uint32_t)},
        {"xe_tessellation_factor_range", RdefTypeIndex::kFloat2,
         sizeof(float) * 2},
        {"xe_line_loop_closing_index", RdefTypeIndex::kUint, sizeof(uint32_t)},

        {"xe_vertex_index_endian", RdefTypeIndex::kUint, sizeof(uint32_t)},
        {"xe_vertex_base_index", RdefTypeIndex::kInt, sizeof(int32_t)},
        {"xe_point_size", RdefTypeIndex::kFloat2, sizeof(float) * 2},

        {"xe_point_size_min_max", RdefTypeIndex::kFloat2, sizeof(float) * 2},
        {"xe_point_screen_to_ndc", RdefTypeIndex::kFloat2, sizeof(float) * 2},

        {"xe_user_clip_planes", RdefTypeIndex::kFloat4Array6,
         sizeof(float) * 4 * 6},

        {"xe_ndc_scale", RdefTypeIndex::kFloat3, sizeof(float) * 3},
        {"xe_interpolator_sampling_pattern", RdefTypeIndex::kUint,
         sizeof(uint32_t)},

        {"xe_ndc_offset", RdefTypeIndex::kFloat3, sizeof(float) * 3},
        {"xe_ps_param_gen", RdefTypeIndex::kUint, sizeof(uint32_t)},

        {"xe_texture_swizzled_signs", RdefTypeIndex::kUint4Array2,
         sizeof(uint32_t) * 4 * 2},

        {"xe_sample_count_log2", RdefTypeIndex::kUint2, sizeof(uint32_t) * 2},
        {"xe_alpha_test_reference", RdefTypeIndex::kFloat, sizeof(float)},
        {"xe_alpha_to_mask", RdefTypeIndex::kUint, sizeof(uint32_t)},

        {"xe_color_exp_bias", RdefTypeIndex::kFloat4, sizeof(float) * 4},

        {"xe_color_output_map", RdefTypeIndex::kUint4, sizeof(uint32_t) * 4},

        {"xe_edram_resolution_square_scale", RdefTypeIndex::kUint,
         sizeof(uint32_t)},
        {"xe_edram_pitch_tiles", RdefTypeIndex::kUint, sizeof(uint32_t)},
        {"xe_edram_depth_range", RdefTypeIndex::kFloat2, sizeof(float) * 2},

        {"xe_edram_poly_offset_front", RdefTypeIndex::kFloat2,
         sizeof(float) * 2},
        {"xe_edram_poly_offset_back", RdefTypeIndex::kFloat2,
         sizeof(float) * 2},

        {"xe_edram_depth_base_dwords", RdefTypeIndex::kUint, sizeof(uint32_t),
         sizeof(float) * 3},

        {"xe_edram_stencil", RdefTypeIndex::kUint4Array2,
         sizeof(uint32_t) * 4 * 2},

        {"xe_edram_rt_base_dwords_scaled", RdefTypeIndex::kUint4,
         sizeof(uint32_t) * 4},

        {"xe_edram_rt_format_flags", RdefTypeIndex::kUint4,
         sizeof(uint32_t) * 4},

        {"xe_edram_rt_clamp", RdefTypeIndex::kFloat4Array4,
         sizeof(float) * 4 * 4},

        {"xe_edram_rt_keep_mask", RdefTypeIndex::kUint4Array2,
         sizeof(uint32_t) * 4 * 2},

        {"xe_edram_rt_blend_factors_ops", RdefTypeIndex::kUint4,
         sizeof(uint32_t) * 4},

        {"xe_edram_blend_constant", RdefTypeIndex::kFloat4, sizeof(float) * 4},
};

void DxbcShaderTranslator::WriteResourceDefinitions() {
  uint32_t chunk_position_dwords = uint32_t(shader_object_.size());
  uint32_t new_offset;

  const Shader::ConstantRegisterMap& constant_register_map =
      current_shader().constant_register_map();

  // ***************************************************************************
  // Header
  // ***************************************************************************

  // Constant buffer count.
  shader_object_.push_back(cbuffer_count_);
  // Constant buffer offset (set later).
  shader_object_.push_back(0);
  // Bindful resource count.
  uint32_t resource_count = srv_count_ + uav_count_ + cbuffer_count_;
  if (!sampler_bindings_.empty()) {
    if (bindless_resources_used_) {
      ++resource_count;
    } else {
      resource_count += uint32_t(sampler_bindings_.size());
    }
  }
  shader_object_.push_back(resource_count);
  // Bindful resource buffer offset (set later).
  shader_object_.push_back(0);
  if (IsDxbcVertexShader()) {
    // vs_5_1
    shader_object_.push_back(0xFFFE0501u);
  } else if (IsDxbcDomainShader()) {
    // ds_5_1
    shader_object_.push_back(0x44530501u);
  } else {
    assert_true(is_pixel_shader());
    // ps_5_1
    shader_object_.push_back(0xFFFF0501u);
  }
  // Compiler flags - default for SM 5.1 (no preshader, prefer flow control),
  // and also skip optimization and IEEE strictness.
  shader_object_.push_back(0x2504);
  // Generator offset (directly after the RDEF header in our case).
  shader_object_.push_back(60);
  // RD11, but with nibbles inverted (unlike in SM 5.0).
  shader_object_.push_back(0x25441313);
  // Unknown fields.
  shader_object_.push_back(60);
  shader_object_.push_back(24);
  // Was 32 in SM 5.0.
  shader_object_.push_back(40);
  shader_object_.push_back(40);
  shader_object_.push_back(36);
  shader_object_.push_back(12);
  shader_object_.push_back(0);
  // Generator name.
  AppendString(shader_object_, "Xenia");

  // ***************************************************************************
  // Constant types
  // ***************************************************************************

  // Type names.
  new_offset = (uint32_t(shader_object_.size()) - chunk_position_dwords) *
               sizeof(uint32_t);
  uint32_t type_name_offsets[size_t(RdefTypeIndex::kCount)];
  for (uint32_t i = 0; i < uint32_t(RdefTypeIndex::kCount); ++i) {
    const RdefType& type = rdef_types_[i];
    if (type.name == nullptr) {
      // Array - use the name of the element type.
      type_name_offsets[i] =
          type_name_offsets[uint32_t(type.array_element_type)];
      continue;
    }
    type_name_offsets[i] = new_offset;
    new_offset += AppendString(shader_object_, type.name);
  }
  // Types.
  uint32_t types_position_dwords = uint32_t(shader_object_.size());
  const uint32_t type_size_dwords = 9;
  uint32_t types_offset =
      (types_position_dwords - chunk_position_dwords) * sizeof(uint32_t);
  const uint32_t type_size = type_size_dwords * sizeof(uint32_t);
  for (uint32_t i = 0; i < uint32_t(RdefTypeIndex::kCount); ++i) {
    const RdefType& type = rdef_types_[i];
    shader_object_.push_back(uint32_t(type.variable_class) |
                             (uint32_t(type.variable_type) << 16));
    shader_object_.push_back(type.row_count | (type.column_count << 16));
    switch (RdefTypeIndex(i)) {
      case RdefTypeIndex::kFloat4ConstantArray:
        // Declaring a 0-sized array may not be safe, so write something valid
        // even if they aren't used.
        shader_object_.push_back(
            std::max(constant_register_map.float_count, uint32_t(1)));
        break;
      case RdefTypeIndex::kUint4DescriptorIndexArray:
        shader_object_.push_back(std::max(
            uint32_t((GetBindlessResourceCount() + 3) >> 2), uint32_t(1)));
        break;
      default:
        shader_object_.push_back(type.element_count |
                                 (type.struct_member_count << 16));
    }
    // Struct member offset (set later).
    shader_object_.push_back(0);
    // Unknown.
    shader_object_.push_back(0);
    shader_object_.push_back(0);
    shader_object_.push_back(0);
    shader_object_.push_back(0);
    shader_object_.push_back(type_name_offsets[i]);
  }

#if 0
  // Structure members. Structures are not used currently, but were used in the
  // past, so the code is kept here.
  for (uint32_t i = 0; i < uint32_t(RdefTypeIndex::kCount); ++i) {
    const RdefType& type = rdef_types_[i];
    const RdefStructMember* struct_members = type.struct_members;
    if (struct_members == nullptr) {
      continue;
    }
    uint32_t struct_member_position_dwords = uint32_t(shader_object_.size());
    shader_object_[types_position_dwords + i * type_size_dwords + 3] =
        (struct_member_position_dwords - chunk_position_dwords) *
        sizeof(uint32_t);
    uint32_t struct_member_count = type.struct_member_count;
    // Reserve space for names and write types and offsets.
    for (uint32_t j = 0; j < struct_member_count; ++j) {
      shader_object_.push_back(0);
      shader_object_.push_back(types_offset +
                               uint32_t(struct_members[j].type) * type_size);
      shader_object_.push_back(struct_members[j].offset);
    }
    // Write member names.
    new_offset = (uint32_t(shader_object_.size()) - chunk_position_dwords) *
                 sizeof(uint32_t);
    for (uint32_t j = 0; j < struct_member_count; ++j) {
      shader_object_[struct_member_position_dwords + j * 3] = new_offset;
      new_offset += AppendString(shader_object_, struct_members[j].name);
    }
  }
#endif

  // ***************************************************************************
  // Constants
  // ***************************************************************************

  // Names.
  new_offset = (uint32_t(shader_object_.size()) - chunk_position_dwords) *
               sizeof(uint32_t);
  uint32_t constant_name_offsets_system[kSysConst_Count];
  if (cbuffer_index_system_constants_ != kBindingIndexUnallocated) {
    for (uint32_t i = 0; i < kSysConst_Count; ++i) {
      constant_name_offsets_system[i] = new_offset;
      new_offset += AppendString(shader_object_, system_constant_rdef_[i].name);
    }
  }
  uint32_t constant_name_offset_float = new_offset;
  if (cbuffer_index_float_constants_ != kBindingIndexUnallocated) {
    new_offset += AppendString(shader_object_, "xe_float_constants");
  }
  uint32_t constant_name_offset_bool = new_offset;
  uint32_t constant_name_offset_loop = new_offset;
  if (cbuffer_index_bool_loop_constants_ != kBindingIndexUnallocated) {
    new_offset += AppendString(shader_object_, "xe_bool_constants");
    constant_name_offset_loop = new_offset;
    new_offset += AppendString(shader_object_, "xe_loop_constants");
  }
  uint32_t constant_name_offset_fetch = new_offset;
  if (cbuffer_index_fetch_constants_ != kBindingIndexUnallocated) {
    new_offset += AppendString(shader_object_, "xe_fetch_constants");
  }
  uint32_t constant_name_offset_descriptor_indices = new_offset;
  if (cbuffer_index_descriptor_indices_ != kBindingIndexUnallocated) {
    new_offset += AppendString(shader_object_, "xe_descriptor_indices");
  }

  const uint32_t constant_size = 10 * sizeof(uint32_t);

  // System constants.
  uint32_t constant_offset_system = new_offset;
  if (cbuffer_index_system_constants_ != kBindingIndexUnallocated) {
    uint32_t system_cbuffer_constant_offset = 0;
    for (uint32_t i = 0; i < kSysConst_Count; ++i) {
      const SystemConstantRdef& constant = system_constant_rdef_[i];
      shader_object_.push_back(constant_name_offsets_system[i]);
      shader_object_.push_back(system_cbuffer_constant_offset);
      shader_object_.push_back(constant.size);
      shader_object_.push_back((system_constants_used_ & (1ull << i))
                                   ? dxbc::kRdefVariableFlagUsed
                                   : 0);
      shader_object_.push_back(types_offset +
                               uint32_t(constant.type) * type_size);
      // Default value (always 0).
      shader_object_.push_back(0);
      // Unknown.
      shader_object_.push_back(0xFFFFFFFFu);
      shader_object_.push_back(0);
      shader_object_.push_back(0xFFFFFFFFu);
      shader_object_.push_back(0);
      system_cbuffer_constant_offset += constant.size + constant.padding_after;
      new_offset += constant_size;
    }
  }

  // Float constants.
  uint32_t constant_offset_float = new_offset;
  if (cbuffer_index_float_constants_ != kBindingIndexUnallocated) {
    assert_not_zero(constant_register_map.float_count);
    shader_object_.push_back(constant_name_offset_float);
    shader_object_.push_back(0);
    shader_object_.push_back(constant_register_map.float_count * 4 *
                             sizeof(float));
    shader_object_.push_back(dxbc::kRdefVariableFlagUsed);
    shader_object_.push_back(types_offset +
                             uint32_t(RdefTypeIndex::kFloat4ConstantArray) *
                                 type_size);
    shader_object_.push_back(0);
    shader_object_.push_back(0xFFFFFFFFu);
    shader_object_.push_back(0);
    shader_object_.push_back(0xFFFFFFFFu);
    shader_object_.push_back(0);
    new_offset += constant_size;
  }

  // Bool and loop constants.
  uint32_t constant_offset_bool_loop = new_offset;
  if (cbuffer_index_bool_loop_constants_ != kBindingIndexUnallocated) {
    shader_object_.push_back(constant_name_offset_bool);
    shader_object_.push_back(0);
    shader_object_.push_back(2 * 4 * sizeof(uint32_t));
    shader_object_.push_back(dxbc::kRdefVariableFlagUsed);
    shader_object_.push_back(types_offset +
                             uint32_t(RdefTypeIndex::kUint4Array2) * type_size);
    shader_object_.push_back(0);
    shader_object_.push_back(0xFFFFFFFFu);
    shader_object_.push_back(0);
    shader_object_.push_back(0xFFFFFFFFu);
    shader_object_.push_back(0);
    new_offset += constant_size;

    shader_object_.push_back(constant_name_offset_loop);
    shader_object_.push_back(2 * 4 * sizeof(uint32_t));
    shader_object_.push_back(8 * 4 * sizeof(uint32_t));
    shader_object_.push_back(dxbc::kRdefVariableFlagUsed);
    shader_object_.push_back(types_offset +
                             uint32_t(RdefTypeIndex::kUint4Array8) * type_size);
    shader_object_.push_back(0);
    shader_object_.push_back(0xFFFFFFFFu);
    shader_object_.push_back(0);
    shader_object_.push_back(0xFFFFFFFFu);
    shader_object_.push_back(0);
    new_offset += constant_size;
  }

  // Fetch constants.
  uint32_t constant_offset_fetch = new_offset;
  if (cbuffer_index_fetch_constants_ != kBindingIndexUnallocated) {
    shader_object_.push_back(constant_name_offset_fetch);
    shader_object_.push_back(0);
    shader_object_.push_back(32 * 6 * sizeof(uint32_t));
    shader_object_.push_back(dxbc::kRdefVariableFlagUsed);
    shader_object_.push_back(
        types_offset + uint32_t(RdefTypeIndex::kUint4Array48) * type_size);
    shader_object_.push_back(0);
    shader_object_.push_back(0xFFFFFFFFu);
    shader_object_.push_back(0);
    shader_object_.push_back(0xFFFFFFFFu);
    shader_object_.push_back(0);
    new_offset += constant_size;
  }

  // Bindless description indices.
  uint32_t constant_offset_descriptor_indices = new_offset;
  if (cbuffer_index_descriptor_indices_ != kBindingIndexUnallocated) {
    assert_not_zero(GetBindlessResourceCount());
    shader_object_.push_back(constant_name_offset_descriptor_indices);
    shader_object_.push_back(0);
    shader_object_.push_back(
        xe::align(GetBindlessResourceCount(), uint32_t(4)) * sizeof(uint32_t));
    shader_object_.push_back(dxbc::kRdefVariableFlagUsed);
    shader_object_.push_back(
        types_offset +
        uint32_t(RdefTypeIndex::kUint4DescriptorIndexArray) * type_size);
    shader_object_.push_back(0);
    shader_object_.push_back(0xFFFFFFFFu);
    shader_object_.push_back(0);
    shader_object_.push_back(0xFFFFFFFFu);
    shader_object_.push_back(0);
    new_offset += constant_size;
  }

  // ***************************************************************************
  // Constant buffers
  // ***************************************************************************

  // Write the names.
  new_offset = (uint32_t(shader_object_.size()) - chunk_position_dwords) *
               sizeof(uint32_t);
  uint32_t cbuffer_name_offset_system = new_offset;
  if (cbuffer_index_system_constants_ != kBindingIndexUnallocated) {
    new_offset += AppendString(shader_object_, "xe_system_cbuffer");
  }
  uint32_t cbuffer_name_offset_float = new_offset;
  if (cbuffer_index_float_constants_ != kBindingIndexUnallocated) {
    new_offset += AppendString(shader_object_, "xe_float_cbuffer");
  }
  uint32_t cbuffer_name_offset_bool_loop = new_offset;
  if (cbuffer_index_bool_loop_constants_ != kBindingIndexUnallocated) {
    new_offset += AppendString(shader_object_, "xe_bool_loop_cbuffer");
  }
  uint32_t cbuffer_name_offset_fetch = new_offset;
  if (cbuffer_index_fetch_constants_ != kBindingIndexUnallocated) {
    new_offset += AppendString(shader_object_, "xe_fetch_cbuffer");
  }
  uint32_t cbuffer_name_offset_descriptor_indices = new_offset;
  if (cbuffer_index_descriptor_indices_ != kBindingIndexUnallocated) {
    new_offset += AppendString(shader_object_, "xe_descriptor_indices_cbuffer");
  }

  // Write the offset to the header.
  shader_object_[chunk_position_dwords + 1] = new_offset;

  // Write all the constant buffers, sorted by their binding index.
  for (uint32_t i = 0; i < cbuffer_count_; ++i) {
    if (i == cbuffer_index_system_constants_) {
      shader_object_.push_back(cbuffer_name_offset_system);
      shader_object_.push_back(kSysConst_Count);
      shader_object_.push_back(constant_offset_system);
      shader_object_.push_back(
          uint32_t(xe::align(sizeof(SystemConstants), 4 * sizeof(uint32_t))));
      shader_object_.push_back(uint32_t(dxbc::RdefCbufferType::kCbuffer));
      // No D3D_SHADER_CBUFFER_FLAGS.
      shader_object_.push_back(0);
    } else if (i == cbuffer_index_float_constants_) {
      assert_not_zero(constant_register_map.float_count);
      shader_object_.push_back(cbuffer_name_offset_float);
      shader_object_.push_back(1);
      shader_object_.push_back(constant_offset_float);
      shader_object_.push_back(constant_register_map.float_count * 4 *
                               sizeof(float));
      shader_object_.push_back(uint32_t(dxbc::RdefCbufferType::kCbuffer));
      shader_object_.push_back(0);
    } else if (i == cbuffer_index_bool_loop_constants_) {
      shader_object_.push_back(cbuffer_name_offset_bool_loop);
      // Bool constants and loop constants are separate for easier debugging.
      shader_object_.push_back(2);
      shader_object_.push_back(constant_offset_bool_loop);
      shader_object_.push_back((2 + 8) * 4 * sizeof(uint32_t));
      shader_object_.push_back(uint32_t(dxbc::RdefCbufferType::kCbuffer));
      shader_object_.push_back(0);
    } else if (i == cbuffer_index_fetch_constants_) {
      shader_object_.push_back(cbuffer_name_offset_fetch);
      shader_object_.push_back(1);
      shader_object_.push_back(constant_offset_fetch);
      shader_object_.push_back(32 * 6 * sizeof(uint32_t));
      shader_object_.push_back(uint32_t(dxbc::RdefCbufferType::kCbuffer));
      shader_object_.push_back(0);
    } else if (i == cbuffer_index_descriptor_indices_) {
      assert_not_zero(GetBindlessResourceCount());
      shader_object_.push_back(cbuffer_name_offset_descriptor_indices);
      shader_object_.push_back(1);
      shader_object_.push_back(constant_offset_descriptor_indices);
      shader_object_.push_back(
          xe::align(GetBindlessResourceCount(), uint32_t(4)) *
          sizeof(uint32_t));
      shader_object_.push_back(uint32_t(dxbc::RdefCbufferType::kCbuffer));
      shader_object_.push_back(0);
    } else {
      assert_unhandled_case(i);
    }
  }

  // ***************************************************************************
  // Bindings, in s#, t#, u#, cb# order
  // ***************************************************************************

  // Write used resource names, except for constant buffers because we have
  // their names already.
  new_offset = (uint32_t(shader_object_.size()) - chunk_position_dwords) *
               sizeof(uint32_t);
  uint32_t sampler_name_offset = new_offset;
  if (!sampler_bindings_.empty()) {
    if (bindless_resources_used_) {
      new_offset += AppendString(shader_object_, "xe_samplers");
    } else {
      for (uint32_t i = 0; i < uint32_t(sampler_bindings_.size()); ++i) {
        new_offset +=
            AppendString(shader_object_, sampler_bindings_[i].name.c_str());
      }
    }
  }
  uint32_t shared_memory_srv_name_offset = new_offset;
  if (srv_index_shared_memory_ != kBindingIndexUnallocated) {
    new_offset += AppendString(shader_object_, "xe_shared_memory_srv");
  }
  uint32_t bindless_textures_2d_name_offset = new_offset;
  uint32_t bindless_textures_3d_name_offset = new_offset;
  uint32_t bindless_textures_cube_name_offset = new_offset;
  if (bindless_resources_used_) {
    if (srv_index_bindless_textures_2d_ != kBindingIndexUnallocated) {
      bindless_textures_2d_name_offset = new_offset;
      new_offset += AppendString(shader_object_, "xe_textures_2d");
    }
    if (srv_index_bindless_textures_3d_ != kBindingIndexUnallocated) {
      bindless_textures_3d_name_offset = new_offset;
      new_offset += AppendString(shader_object_, "xe_textures_3d");
    }
    if (srv_index_bindless_textures_cube_ != kBindingIndexUnallocated) {
      bindless_textures_cube_name_offset = new_offset;
      new_offset += AppendString(shader_object_, "xe_textures_cube");
    }
  } else {
    for (TextureBinding& texture_binding : texture_bindings_) {
      texture_binding.bindful_srv_rdef_name_offset = new_offset;
      new_offset += AppendString(shader_object_, texture_binding.name.c_str());
    }
  }
  uint32_t shared_memory_uav_name_offset = new_offset;
  if (uav_index_shared_memory_ != kBindingIndexUnallocated) {
    new_offset += AppendString(shader_object_, "xe_shared_memory_uav");
  }
  uint32_t edram_name_offset = new_offset;
  if (uav_index_edram_ != kBindingIndexUnallocated) {
    new_offset += AppendString(shader_object_, "xe_edram");
  }

  // Write the offset to the header.
  shader_object_[chunk_position_dwords + 3] = new_offset;

  // Samplers.
  if (!sampler_bindings_.empty()) {
    if (bindless_resources_used_) {
      // Bindless sampler heap.
      shader_object_.push_back(sampler_name_offset);
      shader_object_.push_back(uint32_t(dxbc::RdefInputType::kSampler));
      shader_object_.push_back(uint32_t(dxbc::RdefReturnType::kVoid));
      shader_object_.push_back(uint32_t(dxbc::RdefDimension::kUnknown));
      // Multisampling not applicable.
      shader_object_.push_back(0);
      // Registers s0:*.
      shader_object_.push_back(0);
      // Unbounded number of bindings.
      shader_object_.push_back(0);
      // No dxbc::RdefInputFlags.
      shader_object_.push_back(0);
      // Register space 0.
      shader_object_.push_back(0);
      // Sampler ID S0.
      shader_object_.push_back(0);
    } else {
      // Bindful samplers.
      uint32_t sampler_current_name_offset = sampler_name_offset;
      for (uint32_t i = 0; i < uint32_t(sampler_bindings_.size()); ++i) {
        const SamplerBinding& sampler_binding = sampler_bindings_[i];
        shader_object_.push_back(sampler_current_name_offset);
        shader_object_.push_back(uint32_t(dxbc::RdefInputType::kSampler));
        shader_object_.push_back(uint32_t(dxbc::RdefReturnType::kVoid));
        shader_object_.push_back(uint32_t(dxbc::RdefDimension::kUnknown));
        // Multisampling not applicable.
        shader_object_.push_back(0);
        // Register s[i].
        shader_object_.push_back(i);
        // One binding.
        shader_object_.push_back(1);
        // No dxbc::RdefInputFlags.
        shader_object_.push_back(0);
        // Register space 0.
        shader_object_.push_back(0);
        // Sampler ID S[i].
        shader_object_.push_back(i);
        sampler_current_name_offset +=
            GetStringLength(sampler_binding.name.c_str());
      }
    }
  }

  // Shader resource views, sorted by binding index.
  for (uint32_t i = 0; i < srv_count_; ++i) {
    if (i == srv_index_shared_memory_) {
      // Shared memory (when memexport isn't used in the pipeline).
      shader_object_.push_back(shared_memory_srv_name_offset);
      shader_object_.push_back(uint32_t(dxbc::RdefInputType::kByteAddress));
      shader_object_.push_back(uint32_t(dxbc::RdefReturnType::kMixed));
      shader_object_.push_back(uint32_t(dxbc::RdefDimension::kSRVBuffer));
      // Multisampling not applicable.
      shader_object_.push_back(0);
      shader_object_.push_back(uint32_t(SRVMainRegister::kSharedMemory));
      // One binding.
      shader_object_.push_back(1);
      // No dxbc::RdefInputFlags.
      shader_object_.push_back(0);
      shader_object_.push_back(uint32_t(SRVSpace::kMain));
    } else {
      uint32_t texture_name_offset;
      dxbc::RdefDimension texture_dimension;
      uint32_t texture_register;
      uint32_t texture_register_count;
      SRVSpace texture_register_space;
      if (bindless_resources_used_) {
        // Bindless texture heap.
        if (i == srv_index_bindless_textures_3d_) {
          texture_name_offset = bindless_textures_3d_name_offset;
          texture_dimension = dxbc::RdefDimension::kSRVTexture3D;
          texture_register_space = SRVSpace::kBindlessTextures3D;
        } else if (i == srv_index_bindless_textures_cube_) {
          texture_name_offset = bindless_textures_cube_name_offset;
          texture_dimension = dxbc::RdefDimension::kSRVTextureCube;
          texture_register_space = SRVSpace::kBindlessTexturesCube;
        } else {
          assert_true(i == srv_index_bindless_textures_2d_);
          texture_name_offset = bindless_textures_2d_name_offset;
          texture_dimension = dxbc::RdefDimension::kSRVTexture2DArray;
          texture_register_space = SRVSpace::kBindlessTextures2DArray;
        }
        texture_register = 0;
        texture_register_count = 0;
      } else {
        // Bindful texture.
        auto it = texture_bindings_for_bindful_srv_indices_.find(i);
        assert_true(it != texture_bindings_for_bindful_srv_indices_.end());
        uint32_t texture_binding_index = it->second;
        const TextureBinding& texture_binding =
            texture_bindings_[texture_binding_index];
        texture_name_offset = texture_binding.bindful_srv_rdef_name_offset;
        switch (texture_binding.dimension) {
          case xenos::FetchOpDimension::k3DOrStacked:
            texture_dimension = dxbc::RdefDimension::kSRVTexture3D;
            break;
          case xenos::FetchOpDimension::kCube:
            texture_dimension = dxbc::RdefDimension::kSRVTextureCube;
            break;
          default:
            assert_true(texture_binding.dimension ==
                        xenos::FetchOpDimension::k2D);
            texture_dimension = dxbc::RdefDimension::kSRVTexture2DArray;
        }
        texture_register = uint32_t(SRVMainRegister::kBindfulTexturesStart) +
                           texture_binding_index;
        texture_register_count = 1;
        texture_register_space = SRVSpace::kMain;
      }
      shader_object_.push_back(texture_name_offset);
      shader_object_.push_back(uint32_t(dxbc::RdefInputType::kTexture));
      shader_object_.push_back(uint32_t(dxbc::RdefReturnType::kFloat));
      shader_object_.push_back(uint32_t(texture_dimension));
      // Not multisampled.
      shader_object_.push_back(0xFFFFFFFFu);
      shader_object_.push_back(texture_register);
      shader_object_.push_back(texture_register_count);
      // 4-component.
      shader_object_.push_back(dxbc::kRdefInputFlagsComponents);
      shader_object_.push_back(uint32_t(texture_register_space));
    }
    // SRV ID T[i].
    shader_object_.push_back(i);
  }

  // Unordered access views, sorted by binding index.
  for (uint32_t i = 0; i < uav_count_; ++i) {
    if (i == uav_index_shared_memory_) {
      // Shared memory (when memexport is used in the pipeline).
      shader_object_.push_back(shared_memory_uav_name_offset);
      shader_object_.push_back(
          uint32_t(dxbc::RdefInputType::kUAVRWByteAddress));
      shader_object_.push_back(uint32_t(dxbc::RdefReturnType::kMixed));
      shader_object_.push_back(uint32_t(dxbc::RdefDimension::kUAVBuffer));
      // Multisampling not applicable.
      shader_object_.push_back(0);
      shader_object_.push_back(uint32_t(UAVRegister::kSharedMemory));
      // One binding.
      shader_object_.push_back(1);
      // No dxbc::RdefInputFlags.
      shader_object_.push_back(0);
      // Register space 0.
      shader_object_.push_back(0);
    } else if (i == uav_index_edram_) {
      // EDRAM R32_UINT buffer.
      shader_object_.push_back(edram_name_offset);
      shader_object_.push_back(uint32_t(dxbc::RdefInputType::kUAVRWTyped));
      shader_object_.push_back(uint32_t(dxbc::RdefReturnType::kUInt));
      shader_object_.push_back(uint32_t(dxbc::RdefDimension::kUAVBuffer));
      // Not multisampled.
      shader_object_.push_back(0xFFFFFFFFu);
      shader_object_.push_back(uint32_t(UAVRegister::kEdram));
      // One binding.
      shader_object_.push_back(1);
      // No dxbc::RdefInputFlags.
      shader_object_.push_back(0);
      // Register space 0.
      shader_object_.push_back(0);
    } else {
      assert_unhandled_case(i);
    }
    // UAV ID U[i].
    shader_object_.push_back(i);
  }

  // Constant buffers.
  for (uint32_t i = 0; i < cbuffer_count_; ++i) {
    uint32_t register_index = 0;
    if (i == cbuffer_index_system_constants_) {
      shader_object_.push_back(cbuffer_name_offset_system);
      register_index = uint32_t(CbufferRegister::kSystemConstants);
    } else if (i == cbuffer_index_float_constants_) {
      shader_object_.push_back(cbuffer_name_offset_float);
      register_index = uint32_t(CbufferRegister::kFloatConstants);
    } else if (i == cbuffer_index_bool_loop_constants_) {
      shader_object_.push_back(cbuffer_name_offset_bool_loop);
      register_index = uint32_t(CbufferRegister::kBoolLoopConstants);
    } else if (i == cbuffer_index_fetch_constants_) {
      shader_object_.push_back(cbuffer_name_offset_fetch);
      register_index = uint32_t(CbufferRegister::kFetchConstants);
    } else if (i == cbuffer_index_descriptor_indices_) {
      shader_object_.push_back(cbuffer_name_offset_descriptor_indices);
      register_index = uint32_t(CbufferRegister::kDescriptorIndices);
    } else {
      assert_unhandled_case(i);
    }
    shader_object_.push_back(uint32_t(dxbc::RdefInputType::kCbuffer));
    shader_object_.push_back(uint32_t(dxbc::RdefReturnType::kVoid));
    shader_object_.push_back(uint32_t(dxbc::RdefDimension::kUnknown));
    // Multisampling not applicable.
    shader_object_.push_back(0);
    shader_object_.push_back(register_index);
    // One binding.
    shader_object_.push_back(1);
    // Like `cbuffer`, don't need `ConstantBuffer<T>` properties.
    shader_object_.push_back(dxbc::kRdefInputFlagUserPacked);
    // Register space 0.
    shader_object_.push_back(0);
    // CBV ID CB[i].
    shader_object_.push_back(i);
  }
}

void DxbcShaderTranslator::WriteInputSignature() {
  // Because of shader_object_.resize(), pointers can't be kept persistently
  // here! Resize also zeroes the memory.
  uint32_t chunk_position = uint32_t(shader_object_.size());
  // Reserve space for the header.
  shader_object_.resize(shader_object_.size() +
                        sizeof(dxbc::Signature) / sizeof(uint32_t));
  uint32_t parameter_count = 0;
  constexpr size_t kParameterDwords =
      sizeof(dxbc::SignatureParameter) / sizeof(uint32_t);

  if (IsDxbcVertexShader()) {
    // Unswapped vertex index (SV_VertexID).
    size_t vertex_id_position = shader_object_.size();
    shader_object_.resize(shader_object_.size() + kParameterDwords);
    ++parameter_count;
    {
      auto& vertex_id = *reinterpret_cast<dxbc::SignatureParameter*>(
          shader_object_.data() + vertex_id_position);
      vertex_id.system_value = dxbc::Name::kVertexID;
      vertex_id.component_type = dxbc::SignatureRegisterComponentType::kUInt32;
      vertex_id.register_index = uint32_t(InOutRegister::kVSInVertexIndex);
      vertex_id.mask = 0b0001;
      vertex_id.always_reads_mask = (register_count() >= 1) ? 0b0001 : 0b0000;
    }

    // Semantic names.
    uint32_t semantic_offset =
        uint32_t((shader_object_.size() - chunk_position) * sizeof(uint32_t));
    {
      auto& vertex_id = *reinterpret_cast<dxbc::SignatureParameter*>(
          shader_object_.data() + vertex_id_position);
      vertex_id.semantic_name = semantic_offset;
    }
    semantic_offset += AppendString(shader_object_, "SV_VertexID");
  } else if (IsDxbcDomainShader()) {
    // Control point indices, byte-swapped, biased according to the base index
    // and converted to float by the host vertex and hull shaders
    // (XEVERTEXID). Needed even for patch-indexed tessellation modes because
    // hull and domain shaders have strict linkage requirements, all hull shader
    // outputs must be declared in a domain shader, and the same hull shaders
    // are used for control-point-indexed and patch-indexed tessellation modes.
    size_t control_point_index_position = shader_object_.size();
    shader_object_.resize(shader_object_.size() + kParameterDwords);
    ++parameter_count;
    {
      auto& control_point_index = *reinterpret_cast<dxbc::SignatureParameter*>(
          shader_object_.data() + control_point_index_position);
      control_point_index.component_type =
          dxbc::SignatureRegisterComponentType::kFloat32;
      control_point_index.register_index =
          uint32_t(InOutRegister::kDSInControlPointIndex);
      control_point_index.mask = 0b0001;
      control_point_index.always_reads_mask =
          in_control_point_index_used_ ? 0b0001 : 0b0000;
    }

    // Semantic names.
    uint32_t semantic_offset =
        uint32_t((shader_object_.size() - chunk_position) * sizeof(uint32_t));
    {
      auto& control_point_index = *reinterpret_cast<dxbc::SignatureParameter*>(
          shader_object_.data() + control_point_index_position);
      control_point_index.semantic_name = semantic_offset;
    }
    semantic_offset += AppendString(shader_object_, "XEVERTEXID");
  } else if (is_pixel_shader()) {
    // Written dynamically, so assume it's always used if it can be written to
    // any interpolator register.
    bool param_gen_used = !is_depth_only_pixel_shader_ && register_count() != 0;

    // Intepolators (TEXCOORD#).
    size_t interpolator_position = shader_object_.size();
    shader_object_.resize(shader_object_.size() +
                          xenos::kMaxInterpolators * kParameterDwords);
    parameter_count += xenos::kMaxInterpolators;
    {
      auto interpolators = reinterpret_cast<dxbc::SignatureParameter*>(
          shader_object_.data() + interpolator_position);
      for (uint32_t i = 0; i < xenos::kMaxInterpolators; ++i) {
        dxbc::SignatureParameter& interpolator = interpolators[i];
        interpolator.semantic_index = i;
        interpolator.component_type =
            dxbc::SignatureRegisterComponentType::kFloat32;
        interpolator.register_index =
            uint32_t(InOutRegister::kPSInInterpolators) + i;
        interpolator.mask = 0b1111;
        // Interpolators are copied to GPRs in the beginning of the shader. If
        // there's a register to copy to, this interpolator is used.
        interpolator.always_reads_mask =
            (!is_depth_only_pixel_shader_ && i < register_count()) ? 0b1111
                                                                   : 0b0000;
      }
    }

    // Point parameters for ps_param_gen - coordinate on the point and point
    // size as a float3 TEXCOORD (but the size in Z is not needed).
    size_t point_parameters_position = shader_object_.size();
    shader_object_.resize(shader_object_.size() + kParameterDwords);
    ++parameter_count;
    {
      auto& point_parameters = *reinterpret_cast<dxbc::SignatureParameter*>(
          shader_object_.data() + point_parameters_position);
      point_parameters.semantic_index = kPointParametersTexCoord;
      point_parameters.component_type =
          dxbc::SignatureRegisterComponentType::kFloat32;
      point_parameters.register_index =
          uint32_t(InOutRegister::kPSInPointParameters);
      point_parameters.mask = 0b0111;
      point_parameters.always_reads_mask = param_gen_used ? 0b0011 : 0b0000;
    }

    // Z and W in clip space, for getting per-sample depth with ROV (TEXCOORD#).
    size_t clip_space_zw_position = shader_object_.size();
    shader_object_.resize(shader_object_.size() + kParameterDwords);
    ++parameter_count;
    {
      auto& clip_space_zw = *reinterpret_cast<dxbc::SignatureParameter*>(
          shader_object_.data() + clip_space_zw_position);
      clip_space_zw.semantic_index = kClipSpaceZWTexCoord;
      clip_space_zw.component_type =
          dxbc::SignatureRegisterComponentType::kFloat32;
      clip_space_zw.register_index = uint32_t(InOutRegister::kPSInClipSpaceZW);
      clip_space_zw.mask = 0b0011;
      clip_space_zw.always_reads_mask = edram_rov_used_ ? 0b0011 : 0b0000;
    }

    // Pixel position. Z is not needed - ROV depth testing calculates the depth
    // from the clip space Z/W texcoord, and if oDepth is used, it must be
    // written to on every execution path anyway (SV_Position).
    size_t position_position = shader_object_.size();
    shader_object_.resize(shader_object_.size() + kParameterDwords);
    ++parameter_count;
    {
      auto& position = *reinterpret_cast<dxbc::SignatureParameter*>(
          shader_object_.data() + position_position);
      position.system_value = dxbc::Name::kPosition;
      position.component_type = dxbc::SignatureRegisterComponentType::kFloat32;
      position.register_index = uint32_t(InOutRegister::kPSInPosition);
      position.mask = 0b1111;
      position.always_reads_mask = in_position_used_;
    }

    // Is front face (SV_IsFrontFace).
    size_t is_front_face_position = shader_object_.size();
    shader_object_.resize(shader_object_.size() + kParameterDwords);
    ++parameter_count;
    {
      auto& is_front_face = *reinterpret_cast<dxbc::SignatureParameter*>(
          shader_object_.data() + is_front_face_position);
      is_front_face.system_value = dxbc::Name::kIsFrontFace;
      is_front_face.component_type =
          dxbc::SignatureRegisterComponentType::kUInt32;
      is_front_face.register_index = uint32_t(InOutRegister::kPSInFrontFace);
      is_front_face.mask = 0b0001;
      is_front_face.always_reads_mask = in_front_face_used_ ? 0b0001 : 0b0000;
    }

    // Semantic names.
    uint32_t semantic_offset =
        uint32_t((shader_object_.size() - chunk_position) * sizeof(uint32_t));
    {
      auto interpolators = reinterpret_cast<dxbc::SignatureParameter*>(
          shader_object_.data() + interpolator_position);
      for (uint32_t i = 0; i < xenos::kMaxInterpolators; ++i) {
        interpolators[i].semantic_name = semantic_offset;
      }
      auto& point_parameters = *reinterpret_cast<dxbc::SignatureParameter*>(
          shader_object_.data() + point_parameters_position);
      point_parameters.semantic_name = semantic_offset;
      auto& clip_space_zw = *reinterpret_cast<dxbc::SignatureParameter*>(
          shader_object_.data() + clip_space_zw_position);
      clip_space_zw.semantic_name = semantic_offset;
    }
    semantic_offset += AppendString(shader_object_, "TEXCOORD");
    {
      auto& position = *reinterpret_cast<dxbc::SignatureParameter*>(
          shader_object_.data() + position_position);
      position.semantic_name = semantic_offset;
    }
    semantic_offset += AppendString(shader_object_, "SV_Position");
    {
      auto& is_front_face = *reinterpret_cast<dxbc::SignatureParameter*>(
          shader_object_.data() + is_front_face_position);
      is_front_face.semantic_name = semantic_offset;
    }
    semantic_offset += AppendString(shader_object_, "SV_IsFrontFace");
  }

  // Header.
  {
    auto& header = *reinterpret_cast<dxbc::Signature*>(shader_object_.data() +
                                                       chunk_position);
    header.parameter_count = parameter_count;
    header.parameter_info_offset = sizeof(dxbc::Signature);
  }
}

void DxbcShaderTranslator::WritePatchConstantSignature() {
  assert_true(IsDxbcDomainShader());
  // Because of shader_object_.resize(), pointers can't be kept persistently
  // here! Resize also zeroes the memory.
  uint32_t chunk_position = uint32_t(shader_object_.size());
  // Reserve space for the header.
  shader_object_.resize(shader_object_.size() +
                        sizeof(dxbc::Signature) / sizeof(uint32_t));
  uint32_t parameter_count = 0;
  constexpr size_t kParameterDwords =
      sizeof(dxbc::SignatureParameter) / sizeof(uint32_t);

  // FXC always compiles with SV_TessFactor and SV_InsideTessFactor input, so
  // this is required even if not referenced (HS and DS have very strict
  // linkage, by the way, everything that HS outputs must be listed in DS
  // inputs).
  uint32_t tess_factor_edge_count = 0;
  dxbc::Name tess_factor_edge_system_value = dxbc::Name::kUndefined;
  uint32_t tess_factor_inside_count = 0;
  dxbc::Name tess_factor_inside_system_value = dxbc::Name::kUndefined;
  Shader::HostVertexShaderType host_vertex_shader_type =
      GetDxbcShaderModification().host_vertex_shader_type;
  switch (host_vertex_shader_type) {
    case Shader::HostVertexShaderType::kTriangleDomainCPIndexed:
    case Shader::HostVertexShaderType::kTriangleDomainPatchIndexed:
      tess_factor_edge_count = 3;
      tess_factor_edge_system_value = dxbc::Name::kFinalTriEdgeTessFactor;
      tess_factor_inside_count = 1;
      tess_factor_inside_system_value = dxbc::Name::kFinalTriInsideTessFactor;
      break;
    case Shader::HostVertexShaderType::kQuadDomainCPIndexed:
    case Shader::HostVertexShaderType::kQuadDomainPatchIndexed:
      tess_factor_edge_count = 4;
      tess_factor_edge_system_value = dxbc::Name::kFinalQuadEdgeTessFactor;
      tess_factor_inside_count = 2;
      tess_factor_inside_system_value = dxbc::Name::kFinalQuadInsideTessFactor;
      break;
    default:
      // TODO(Triang3l): Support line patches.
      assert_unhandled_case(host_vertex_shader_type);
      EmitTranslationError(
          "Unsupported host vertex shader type in WritePatchConstantSignature");
  }

  // Edge tessellation factors (SV_TessFactor).
  size_t tess_factor_edge_position = shader_object_.size();
  shader_object_.resize(shader_object_.size() +
                        tess_factor_edge_count * kParameterDwords);
  parameter_count += tess_factor_edge_count;
  {
    auto tess_factors_edge = reinterpret_cast<dxbc::SignatureParameter*>(
        shader_object_.data() + tess_factor_edge_position);
    for (uint32_t i = 0; i < tess_factor_edge_count; ++i) {
      dxbc::SignatureParameter& tess_factor_edge = tess_factors_edge[i];
      tess_factor_edge.semantic_index = i;
      tess_factor_edge.system_value = tess_factor_edge_system_value;
      tess_factor_edge.component_type =
          dxbc::SignatureRegisterComponentType::kFloat32;
      // Not using any of these, just assigning consecutive registers.
      tess_factor_edge.register_index = i;
      tess_factor_edge.mask = 0b0001;
    }
  }

  // Inside tessellation factors (SV_InsideTessFactor).
  size_t tess_factor_inside_position = shader_object_.size();
  shader_object_.resize(shader_object_.size() +
                        tess_factor_inside_count * kParameterDwords);
  parameter_count += tess_factor_inside_count;
  {
    auto tess_factors_inside = reinterpret_cast<dxbc::SignatureParameter*>(
        shader_object_.data() + tess_factor_inside_position);
    for (uint32_t i = 0; i < tess_factor_inside_count; ++i) {
      dxbc::SignatureParameter& tess_factor_inside = tess_factors_inside[i];
      tess_factor_inside.semantic_index = i;
      tess_factor_inside.system_value = tess_factor_inside_system_value;
      tess_factor_inside.component_type =
          dxbc::SignatureRegisterComponentType::kFloat32;
      // Not using any of these, just assigning consecutive registers.
      tess_factor_inside.register_index = tess_factor_edge_count + i;
      tess_factor_inside.mask = 0b0001;
    }
  }

  // Semantic names.
  uint32_t semantic_offset =
      uint32_t((shader_object_.size() - chunk_position) * sizeof(uint32_t));
  {
    auto tess_factors_edge = reinterpret_cast<dxbc::SignatureParameter*>(
        shader_object_.data() + tess_factor_edge_position);
    for (uint32_t i = 0; i < tess_factor_edge_count; ++i) {
      tess_factors_edge[i].semantic_name = semantic_offset;
    }
  }
  semantic_offset += AppendString(shader_object_, "SV_TessFactor");
  {
    auto tess_factors_inside = reinterpret_cast<dxbc::SignatureParameter*>(
        shader_object_.data() + tess_factor_inside_position);
    for (uint32_t i = 0; i < tess_factor_inside_count; ++i) {
      tess_factors_inside[i].semantic_name = semantic_offset;
    }
  }
  semantic_offset += AppendString(shader_object_, "SV_InsideTessFactor");

  // Header.
  {
    auto& header = *reinterpret_cast<dxbc::Signature*>(shader_object_.data() +
                                                       chunk_position);
    header.parameter_count = parameter_count;
    header.parameter_info_offset = sizeof(dxbc::Signature);
  }
}

void DxbcShaderTranslator::WriteOutputSignature() {
  // Because of shader_object_.resize(), pointers can't be kept persistently
  // here! Resize also zeroes the memory.
  uint32_t chunk_position = uint32_t(shader_object_.size());
  // Reserve space for the header.
  shader_object_.resize(shader_object_.size() +
                        sizeof(dxbc::Signature) / sizeof(uint32_t));
  uint32_t parameter_count = 0;
  constexpr size_t kParameterDwords =
      sizeof(dxbc::SignatureParameter) / sizeof(uint32_t);

  if (is_vertex_shader()) {
    // Intepolators (TEXCOORD#).
    size_t interpolator_position = shader_object_.size();
    shader_object_.resize(shader_object_.size() +
                          xenos::kMaxInterpolators * kParameterDwords);
    parameter_count += xenos::kMaxInterpolators;
    {
      auto interpolators = reinterpret_cast<dxbc::SignatureParameter*>(
          shader_object_.data() + interpolator_position);
      for (uint32_t i = 0; i < xenos::kMaxInterpolators; ++i) {
        dxbc::SignatureParameter& interpolator = interpolators[i];
        interpolator.semantic_index = i;
        interpolator.component_type =
            dxbc::SignatureRegisterComponentType::kFloat32;
        interpolator.register_index =
            uint32_t(InOutRegister::kVSDSOutInterpolators) + i;
        interpolator.mask = 0b1111;
      }
    }

    // Point parameters - coordinate on the point and point size as a float3
    // TEXCOORD. Always used because reset to (0, 0, -1).
    size_t point_parameters_position = shader_object_.size();
    shader_object_.resize(shader_object_.size() + kParameterDwords);
    ++parameter_count;
    {
      auto& point_parameters = *reinterpret_cast<dxbc::SignatureParameter*>(
          shader_object_.data() + point_parameters_position);
      point_parameters.semantic_index = kPointParametersTexCoord;
      point_parameters.component_type =
          dxbc::SignatureRegisterComponentType::kFloat32;
      point_parameters.register_index =
          uint32_t(InOutRegister::kVSDSOutPointParameters);
      point_parameters.mask = 0b0111;
      point_parameters.never_writes_mask = 0b1000;
    }

    // Z and W in clip space, for getting per-sample depth with ROV (TEXCOORD#).
    size_t clip_space_zw_position = shader_object_.size();
    shader_object_.resize(shader_object_.size() + kParameterDwords);
    ++parameter_count;
    {
      auto& clip_space_zw = *reinterpret_cast<dxbc::SignatureParameter*>(
          shader_object_.data() + clip_space_zw_position);
      clip_space_zw.semantic_index = kClipSpaceZWTexCoord;
      clip_space_zw.component_type =
          dxbc::SignatureRegisterComponentType::kFloat32;
      clip_space_zw.register_index =
          uint32_t(InOutRegister::kVSDSOutClipSpaceZW);
      clip_space_zw.mask = 0b0011;
      clip_space_zw.never_writes_mask = 0b1100;
    }

    // Position (SV_Position).
    size_t position_position = shader_object_.size();
    shader_object_.resize(shader_object_.size() + kParameterDwords);
    ++parameter_count;
    {
      auto& position = *reinterpret_cast<dxbc::SignatureParameter*>(
          shader_object_.data() + position_position);
      position.system_value = dxbc::Name::kPosition;
      position.component_type = dxbc::SignatureRegisterComponentType::kFloat32;
      position.register_index = uint32_t(InOutRegister::kVSDSOutPosition);
      position.mask = 0b1111;
    }

    // Clip (SV_ClipDistance) and cull (SV_CullDistance) distances.
    size_t clip_distance_0123_position = shader_object_.size();
    shader_object_.resize(shader_object_.size() + kParameterDwords);
    ++parameter_count;
    {
      auto& clip_distance_0123 = *reinterpret_cast<dxbc::SignatureParameter*>(
          shader_object_.data() + clip_distance_0123_position);
      clip_distance_0123.system_value = dxbc::Name::kClipDistance;
      clip_distance_0123.component_type =
          dxbc::SignatureRegisterComponentType::kFloat32;
      clip_distance_0123.register_index =
          uint32_t(InOutRegister::kVSDSOutClipDistance0123);
      clip_distance_0123.mask = 0b1111;
    }
    size_t clip_distance_45_position = shader_object_.size();
    shader_object_.resize(shader_object_.size() + kParameterDwords);
    ++parameter_count;
    {
      auto& clip_distance_45 = *reinterpret_cast<dxbc::SignatureParameter*>(
          shader_object_.data() + clip_distance_45_position);
      clip_distance_45.semantic_index = 1;
      clip_distance_45.system_value = dxbc::Name::kClipDistance;
      clip_distance_45.component_type =
          dxbc::SignatureRegisterComponentType::kFloat32;
      clip_distance_45.register_index =
          uint32_t(InOutRegister::kVSDSOutClipDistance45AndCullDistance);
      clip_distance_45.mask = 0b0011;
      clip_distance_45.never_writes_mask = 0b1100;
    }
    size_t cull_distance_position = shader_object_.size();
    shader_object_.resize(shader_object_.size() + kParameterDwords);
    ++parameter_count;
    {
      auto& cull_distance = *reinterpret_cast<dxbc::SignatureParameter*>(
          shader_object_.data() + cull_distance_position);
      cull_distance.system_value = dxbc::Name::kCullDistance;
      cull_distance.component_type =
          dxbc::SignatureRegisterComponentType::kFloat32;
      cull_distance.register_index =
          uint32_t(InOutRegister::kVSDSOutClipDistance45AndCullDistance);
      cull_distance.mask = 0b0100;
      cull_distance.never_writes_mask = 0b1011;
    }

    // Semantic names.
    uint32_t semantic_offset =
        uint32_t((shader_object_.size() - chunk_position) * sizeof(uint32_t));
    {
      auto interpolators = reinterpret_cast<dxbc::SignatureParameter*>(
          shader_object_.data() + interpolator_position);
      for (uint32_t i = 0; i < xenos::kMaxInterpolators; ++i) {
        interpolators[i].semantic_name = semantic_offset;
      }
      auto& point_parameters = *reinterpret_cast<dxbc::SignatureParameter*>(
          shader_object_.data() + point_parameters_position);
      point_parameters.semantic_name = semantic_offset;
      auto& clip_space_zw = *reinterpret_cast<dxbc::SignatureParameter*>(
          shader_object_.data() + clip_space_zw_position);
      clip_space_zw.semantic_name = semantic_offset;
    }
    semantic_offset += AppendString(shader_object_, "TEXCOORD");
    {
      auto& position = *reinterpret_cast<dxbc::SignatureParameter*>(
          shader_object_.data() + position_position);
      position.semantic_name = semantic_offset;
    }
    semantic_offset += AppendString(shader_object_, "SV_Position");
    {
      auto& clip_distance_0123 = *reinterpret_cast<dxbc::SignatureParameter*>(
          shader_object_.data() + clip_distance_0123_position);
      clip_distance_0123.semantic_name = semantic_offset;
      auto& clip_distance_45 = *reinterpret_cast<dxbc::SignatureParameter*>(
          shader_object_.data() + clip_distance_45_position);
      clip_distance_45.semantic_name = semantic_offset;
    }
    semantic_offset += AppendString(shader_object_, "SV_ClipDistance");
    {
      auto& cull_distance = *reinterpret_cast<dxbc::SignatureParameter*>(
          shader_object_.data() + cull_distance_position);
      cull_distance.semantic_name = semantic_offset;
    }
    semantic_offset += AppendString(shader_object_, "SV_CullDistance");
  } else if (is_pixel_shader()) {
    if (!edram_rov_used_) {
      // Color render targets (SV_Target#).
      size_t target_position = SIZE_MAX;
      if (current_shader().writes_color_targets()) {
        target_position = shader_object_.size();
        shader_object_.resize(shader_object_.size() + 4 * kParameterDwords);
        parameter_count += 4;
        auto targets = reinterpret_cast<dxbc::SignatureParameter*>(
            shader_object_.data() + target_position);
        for (uint32_t i = 0; i < 4; ++i) {
          dxbc::SignatureParameter& target = targets[i];
          target.semantic_index = i;
          target.component_type =
              dxbc::SignatureRegisterComponentType::kFloat32;
          target.register_index = i;
          target.mask = 0b1111;
          // All are always written because X360 RTs are dynamically remapped to
          // D3D12 RTs to make RT indices consecutive.
        }
      }

      // Depth (SV_Depth or SV_DepthLessEqual).
      Modification::DepthStencilMode depth_stencil_mode =
          GetDxbcShaderModification().depth_stencil_mode;
      size_t depth_position = SIZE_MAX;
      if (current_shader().writes_depth() || DSV_IsWritingFloat24Depth()) {
        depth_position = shader_object_.size();
        shader_object_.resize(shader_object_.size() + kParameterDwords);
        ++parameter_count;
        auto& depth = *reinterpret_cast<dxbc::SignatureParameter*>(
            shader_object_.data() + depth_position);
        depth.component_type = dxbc::SignatureRegisterComponentType::kFloat32;
        depth.register_index = UINT32_MAX;
        depth.mask = 0b0001;
        depth.never_writes_mask = 0b1110;
      }

      // Semantic names.
      uint32_t semantic_offset =
          uint32_t((shader_object_.size() - chunk_position) * sizeof(uint32_t));
      if (target_position != SIZE_MAX) {
        {
          auto targets = reinterpret_cast<dxbc::SignatureParameter*>(
              shader_object_.data() + target_position);
          for (uint32_t i = 0; i < 4; ++i) {
            targets[i].semantic_name = semantic_offset;
          }
        }
        semantic_offset += AppendString(shader_object_, "SV_Target");
      }
      if (depth_position != SIZE_MAX) {
        {
          auto& depth = *reinterpret_cast<dxbc::SignatureParameter*>(
              shader_object_.data() + depth_position);
          depth.semantic_name = semantic_offset;
        }
        const char* depth_semantic_name;
        if (!current_shader().writes_depth() &&
            GetDxbcShaderModification().depth_stencil_mode ==
                Modification::DepthStencilMode::kFloat24Truncating) {
          depth_semantic_name = "SV_DepthLessEqual";
        } else {
          depth_semantic_name = "SV_Depth";
        }
        semantic_offset += AppendString(shader_object_, depth_semantic_name);
      }
    }
  }

  // Header.
  {
    auto& header = *reinterpret_cast<dxbc::Signature*>(shader_object_.data() +
                                                       chunk_position);
    header.parameter_count = parameter_count;
    header.parameter_info_offset = sizeof(dxbc::Signature);
  }
}

void DxbcShaderTranslator::WriteShaderCode() {
  uint32_t chunk_position_dwords = uint32_t(shader_object_.size());

  uint32_t shader_type;
  if (IsDxbcVertexShader()) {
    shader_type = D3D10_SB_VERTEX_SHADER;
  } else if (IsDxbcDomainShader()) {
    shader_type = D3D11_SB_DOMAIN_SHADER;
  } else {
    assert_true(is_pixel_shader());
    shader_type = D3D10_SB_PIXEL_SHADER;
  }
  shader_object_.push_back(
      ENCODE_D3D10_SB_TOKENIZED_PROGRAM_VERSION_TOKEN(shader_type, 5, 1));
  // Reserve space for the length token.
  shader_object_.push_back(0);

  // Declarations (don't increase the instruction count stat, and only inputs
  // and outputs are counted in dcl_count).
  //
  // Binding declarations have 3D-indexed operands with XYZW swizzle, the first
  // index being the binding ID (local to the shader), the second being the
  // lower register index bound, and the third being the highest register index
  // bound. Also dcl_ instructions for bindings are followed by the register
  // space index.
  //
  // Inputs/outputs have 1D-indexed operands with a component mask and a
  // register index.

  Modification shader_modification = GetDxbcShaderModification();

  if (IsDxbcDomainShader()) {
    // Not using control point data since Xenos only has a vertex shader acting
    // as both vertex shader and domain shader.
    stat_.c_control_points = 3;
    stat_.tessellator_domain = dxbc::TessellatorDomain::kTriangle;
    switch (shader_modification.host_vertex_shader_type) {
      case Shader::HostVertexShaderType::kTriangleDomainCPIndexed:
      case Shader::HostVertexShaderType::kTriangleDomainPatchIndexed:
        stat_.c_control_points = 3;
        stat_.tessellator_domain = dxbc::TessellatorDomain::kTriangle;
        break;
      case Shader::HostVertexShaderType::kQuadDomainCPIndexed:
      case Shader::HostVertexShaderType::kQuadDomainPatchIndexed:
        stat_.c_control_points = 4;
        stat_.tessellator_domain = dxbc::TessellatorDomain::kQuad;
        break;
      default:
        // TODO(Triang3l): Support line patches.
        assert_unhandled_case(shader_modification.host_vertex_shader_type);
        EmitTranslationError(
            "Unsupported host vertex shader type in WriteShaderCode");
    }
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(
            D3D11_SB_OPCODE_DCL_INPUT_CONTROL_POINT_COUNT) |
        ENCODE_D3D11_SB_INPUT_CONTROL_POINT_COUNT(stat_.c_control_points) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_DCL_TESS_DOMAIN) |
        ENCODE_D3D11_SB_TESS_DOMAIN(uint32_t(stat_.tessellator_domain)) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1));
  }

  // Don't allow refactoring when converting to native code to maintain position
  // invariance (needed even in pixel shaders for oDepth invariance).
  uint32_t global_flags_opcode =
      ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_GLOBAL_FLAGS) |
      ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1);
  if (is_pixel_shader() &&
      GetDxbcShaderModification().depth_stencil_mode ==
          Modification::DepthStencilMode::kEarlyHint &&
      !edram_rov_used_ && current_shader().implicit_early_z_write_allowed()) {
    global_flags_opcode |= D3D11_SB_GLOBAL_FLAG_FORCE_EARLY_DEPTH_STENCIL;
  }
  shader_object_.push_back(global_flags_opcode);

  // Constant buffers, from most frequenly accessed to least frequently accessed
  // (the order is a hint to the driver according to the DXBC header).
  if (cbuffer_index_float_constants_ != kBindingIndexUnallocated) {
    const Shader::ConstantRegisterMap& constant_register_map =
        current_shader().constant_register_map();
    assert_not_zero(constant_register_map.float_count);
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER) |
        ENCODE_D3D10_SB_D3D10_SB_CONSTANT_BUFFER_ACCESS_PATTERN(
            constant_register_map.float_dynamic_addressing
                ? D3D10_SB_CONSTANT_BUFFER_DYNAMIC_INDEXED
                : D3D10_SB_CONSTANT_BUFFER_IMMEDIATE_INDEXED) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_object_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSwizzleXYZW, 3));
    shader_object_.push_back(cbuffer_index_float_constants_);
    shader_object_.push_back(uint32_t(CbufferRegister::kFloatConstants));
    shader_object_.push_back(uint32_t(CbufferRegister::kFloatConstants));
    shader_object_.push_back(constant_register_map.float_count);
    shader_object_.push_back(0);
  }
  if (cbuffer_index_system_constants_ != kBindingIndexUnallocated) {
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER) |
        ENCODE_D3D10_SB_D3D10_SB_CONSTANT_BUFFER_ACCESS_PATTERN(
            D3D10_SB_CONSTANT_BUFFER_IMMEDIATE_INDEXED) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_object_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSwizzleXYZW, 3));
    shader_object_.push_back(cbuffer_index_system_constants_);
    shader_object_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_object_.push_back(uint32_t(CbufferRegister::kSystemConstants));
    shader_object_.push_back((sizeof(SystemConstants) + 15) >> 4);
    shader_object_.push_back(0);
  }
  if (cbuffer_index_fetch_constants_ != kBindingIndexUnallocated) {
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER) |
        ENCODE_D3D10_SB_D3D10_SB_CONSTANT_BUFFER_ACCESS_PATTERN(
            D3D10_SB_CONSTANT_BUFFER_IMMEDIATE_INDEXED) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_object_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSwizzleXYZW, 3));
    shader_object_.push_back(cbuffer_index_fetch_constants_);
    shader_object_.push_back(uint32_t(CbufferRegister::kFetchConstants));
    shader_object_.push_back(uint32_t(CbufferRegister::kFetchConstants));
    shader_object_.push_back(48);
    shader_object_.push_back(0);
  }
  if (cbuffer_index_descriptor_indices_ != kBindingIndexUnallocated) {
    assert_not_zero(GetBindlessResourceCount());
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER) |
        ENCODE_D3D10_SB_D3D10_SB_CONSTANT_BUFFER_ACCESS_PATTERN(
            D3D10_SB_CONSTANT_BUFFER_IMMEDIATE_INDEXED) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_object_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSwizzleXYZW, 3));
    shader_object_.push_back(cbuffer_index_descriptor_indices_);
    shader_object_.push_back(uint32_t(CbufferRegister::kDescriptorIndices));
    shader_object_.push_back(uint32_t(CbufferRegister::kDescriptorIndices));
    shader_object_.push_back((GetBindlessResourceCount() + 3) >> 2);
    shader_object_.push_back(0);
  }
  if (cbuffer_index_bool_loop_constants_ != kBindingIndexUnallocated) {
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER) |
        ENCODE_D3D10_SB_D3D10_SB_CONSTANT_BUFFER_ACCESS_PATTERN(
            D3D10_SB_CONSTANT_BUFFER_IMMEDIATE_INDEXED) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
    shader_object_.push_back(EncodeVectorSwizzledOperand(
        D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, kSwizzleXYZW, 3));
    shader_object_.push_back(cbuffer_index_bool_loop_constants_);
    shader_object_.push_back(uint32_t(CbufferRegister::kBoolLoopConstants));
    shader_object_.push_back(uint32_t(CbufferRegister::kBoolLoopConstants));
    shader_object_.push_back(10);
    shader_object_.push_back(0);
  }

  // Samplers.
  if (!sampler_bindings_.empty()) {
    if (bindless_resources_used_) {
      // Bindless sampler heap.
      shader_object_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_SAMPLER) |
          ENCODE_D3D10_SB_SAMPLER_MODE(D3D10_SB_SAMPLER_MODE_DEFAULT) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(6));
      shader_object_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_SAMPLER, kSwizzleXYZW, 3));
      shader_object_.push_back(0);
      shader_object_.push_back(0);
      shader_object_.push_back(UINT32_MAX);
      shader_object_.push_back(0);
    } else {
      // Bindful samplers.
      for (uint32_t i = 0; i < uint32_t(sampler_bindings_.size()); ++i) {
        const SamplerBinding& sampler_binding = sampler_bindings_[i];
        shader_object_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_SAMPLER) |
            ENCODE_D3D10_SB_SAMPLER_MODE(D3D10_SB_SAMPLER_MODE_DEFAULT) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(6));
        shader_object_.push_back(EncodeVectorSwizzledOperand(
            D3D10_SB_OPERAND_TYPE_SAMPLER, kSwizzleXYZW, 3));
        shader_object_.push_back(i);
        shader_object_.push_back(i);
        shader_object_.push_back(i);
        shader_object_.push_back(0);
      }
    }
  }

  // Shader resource views, sorted by binding index.
  for (uint32_t i = 0; i < srv_count_; ++i) {
    if (i == srv_index_shared_memory_) {
      // Shared memory ByteAddressBuffer.
      shader_object_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_DCL_RESOURCE_RAW) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(6));
      shader_object_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_RESOURCE, kSwizzleXYZW, 3));
      shader_object_.push_back(srv_index_shared_memory_);
      shader_object_.push_back(uint32_t(SRVMainRegister::kSharedMemory));
      shader_object_.push_back(uint32_t(SRVMainRegister::kSharedMemory));
      shader_object_.push_back(uint32_t(SRVSpace::kMain));
    } else {
      // Texture or texture heap.
      D3D10_SB_RESOURCE_DIMENSION texture_srv_dimension;
      uint32_t texture_register_first, texture_register_last;
      SRVSpace texture_register_space;
      if (bindless_resources_used_) {
        // Bindless texture heap.
        texture_register_first = 0;
        texture_register_last = UINT32_MAX;
        if (i == srv_index_bindless_textures_3d_) {
          texture_srv_dimension = D3D10_SB_RESOURCE_DIMENSION_TEXTURE3D;
          texture_register_space = SRVSpace::kBindlessTextures3D;
        } else if (i == srv_index_bindless_textures_cube_) {
          texture_srv_dimension = D3D10_SB_RESOURCE_DIMENSION_TEXTURECUBE;
          texture_register_space = SRVSpace::kBindlessTexturesCube;
        } else {
          assert_true(i == srv_index_bindless_textures_2d_);
          texture_srv_dimension = D3D10_SB_RESOURCE_DIMENSION_TEXTURE2DARRAY;
          texture_register_space = SRVSpace::kBindlessTextures2DArray;
        }
      } else {
        // Bindful texture.
        auto it = texture_bindings_for_bindful_srv_indices_.find(i);
        assert_true(it != texture_bindings_for_bindful_srv_indices_.end());
        uint32_t texture_binding_index = it->second;
        const TextureBinding& texture_binding =
            texture_bindings_[texture_binding_index];
        switch (texture_binding.dimension) {
          case xenos::FetchOpDimension::k3DOrStacked:
            texture_srv_dimension = D3D10_SB_RESOURCE_DIMENSION_TEXTURE3D;
            break;
          case xenos::FetchOpDimension::kCube:
            texture_srv_dimension = D3D10_SB_RESOURCE_DIMENSION_TEXTURECUBE;
            break;
          default:
            assert_true(texture_binding.dimension ==
                        xenos::FetchOpDimension::k2D);
            texture_srv_dimension = D3D10_SB_RESOURCE_DIMENSION_TEXTURE2DARRAY;
        }
        texture_register_first = texture_register_last =
            uint32_t(SRVMainRegister::kBindfulTexturesStart) +
            texture_binding_index;
        texture_register_space = SRVSpace::kMain;
      }
      shader_object_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_RESOURCE) |
          ENCODE_D3D10_SB_RESOURCE_DIMENSION(texture_srv_dimension) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_object_.push_back(EncodeVectorSwizzledOperand(
          D3D10_SB_OPERAND_TYPE_RESOURCE, kSwizzleXYZW, 3));
      shader_object_.push_back(i);
      shader_object_.push_back(texture_register_first);
      shader_object_.push_back(texture_register_last);
      shader_object_.push_back(
          ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_FLOAT, 0) |
          ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_FLOAT, 1) |
          ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_FLOAT, 2) |
          ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_FLOAT, 3));
      shader_object_.push_back(uint32_t(texture_register_space));
    }
  }

  // Unordered access views, sorted by binding index.
  for (uint32_t i = 0; i < uav_count_; ++i) {
    if (i == uav_index_shared_memory_) {
      // Shared memory RWByteAddressBuffer.
      shader_object_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(
              D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_RAW) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(6));
      shader_object_.push_back(EncodeVectorSwizzledOperand(
          D3D11_SB_OPERAND_TYPE_UNORDERED_ACCESS_VIEW, kSwizzleXYZW, 3));
      shader_object_.push_back(uav_index_shared_memory_);
      shader_object_.push_back(uint32_t(UAVRegister::kSharedMemory));
      shader_object_.push_back(uint32_t(UAVRegister::kSharedMemory));
      shader_object_.push_back(0);
    } else if (i == uav_index_edram_) {
      // EDRAM buffer R32_UINT rasterizer-ordered view.
      shader_object_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(
              D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_TYPED) |
          ENCODE_D3D10_SB_RESOURCE_DIMENSION(
              D3D10_SB_RESOURCE_DIMENSION_BUFFER) |
          D3D11_SB_RASTERIZER_ORDERED_ACCESS |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7));
      shader_object_.push_back(EncodeVectorSwizzledOperand(
          D3D11_SB_OPERAND_TYPE_UNORDERED_ACCESS_VIEW, kSwizzleXYZW, 3));
      shader_object_.push_back(uav_index_edram_);
      shader_object_.push_back(uint32_t(UAVRegister::kEdram));
      shader_object_.push_back(uint32_t(UAVRegister::kEdram));
      shader_object_.push_back(
          ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_UINT, 0) |
          ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_UINT, 1) |
          ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_UINT, 2) |
          ENCODE_D3D10_SB_RESOURCE_RETURN_TYPE(D3D10_SB_RETURN_TYPE_UINT, 3));
      shader_object_.push_back(0);
    } else {
      assert_unhandled_case(i);
    }
  }

  // Inputs and outputs.
  if (is_vertex_shader()) {
    if (IsDxbcDomainShader()) {
      if (in_domain_location_used_) {
        // Domain location input.
        shader_object_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INPUT) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(2));
        shader_object_.push_back(
            EncodeVectorMaskedOperand(D3D11_SB_OPERAND_TYPE_INPUT_DOMAIN_POINT,
                                      in_domain_location_used_, 0));
        ++stat_.dcl_count;
      }
      if (in_primitive_id_used_) {
        // Primitive (patch) index input.
        shader_object_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INPUT) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(2));
        shader_object_.push_back(
            EncodeScalarOperand(D3D10_SB_OPERAND_TYPE_INPUT_PRIMITIVEID, 0));
        ++stat_.dcl_count;
      }
      if (in_control_point_index_used_) {
        // Control point indices as float input.
        uint32_t control_point_array_size;
        switch (shader_modification.host_vertex_shader_type) {
          case Shader::HostVertexShaderType::kTriangleDomainCPIndexed:
            control_point_array_size = 3;
            break;
          case Shader::HostVertexShaderType::kQuadDomainCPIndexed:
            control_point_array_size = 4;
            break;
          default:
            // TODO(Triang3l): Support line patches.
            assert_unhandled_case(shader_modification.host_vertex_shader_type);
            EmitTranslationError(
                "Unsupported host vertex shader type in "
                "StartVertexOrDomainShader");
            control_point_array_size = 0;
        }
        if (control_point_array_size) {
          shader_object_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INPUT) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4));
          shader_object_.push_back(EncodeVectorMaskedOperand(
              D3D11_SB_OPERAND_TYPE_INPUT_CONTROL_POINT, 0b0001, 2));
          shader_object_.push_back(control_point_array_size);
          shader_object_.push_back(
              uint32_t(InOutRegister::kDSInControlPointIndex));
          ++stat_.dcl_count;
        }
      }
    } else {
      if (register_count()) {
        // Unswapped vertex index input (only X component).
        shader_object_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INPUT_SGV) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4));
        shader_object_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_INPUT, 0b0001, 1));
        shader_object_.push_back(uint32_t(InOutRegister::kVSInVertexIndex));
        shader_object_.push_back(ENCODE_D3D10_SB_NAME(D3D10_SB_NAME_VERTEX_ID));
        ++stat_.dcl_count;
      }
    }
    // Interpolator output.
    for (uint32_t i = 0; i < xenos::kMaxInterpolators; ++i) {
      shader_object_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_OUTPUT) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
      shader_object_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_OUTPUT, 0b1111, 1));
      shader_object_.push_back(uint32_t(InOutRegister::kVSDSOutInterpolators) +
                               i);
      ++stat_.dcl_count;
    }
    // Point parameters output.
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_OUTPUT) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_object_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_OUTPUT, 0b0111, 1));
    shader_object_.push_back(uint32_t(InOutRegister::kVSDSOutPointParameters));
    ++stat_.dcl_count;
    // Clip space Z and W output.
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_OUTPUT) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
    shader_object_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_OUTPUT, 0b0011, 1));
    shader_object_.push_back(uint32_t(InOutRegister::kVSDSOutClipSpaceZW));
    ++stat_.dcl_count;
    // Position output.
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_OUTPUT_SIV) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4));
    shader_object_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_OUTPUT, 0b1111, 1));
    shader_object_.push_back(uint32_t(InOutRegister::kVSDSOutPosition));
    shader_object_.push_back(ENCODE_D3D10_SB_NAME(D3D10_SB_NAME_POSITION));
    ++stat_.dcl_count;
    // Clip distance outputs.
    for (uint32_t i = 0; i < 2; ++i) {
      shader_object_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_OUTPUT_SIV) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4));
      shader_object_.push_back(EncodeVectorMaskedOperand(
          D3D10_SB_OPERAND_TYPE_OUTPUT, i ? 0b0011 : 0b1111, 1));
      shader_object_.push_back(
          uint32_t(InOutRegister::kVSDSOutClipDistance0123) + i);
      shader_object_.push_back(
          ENCODE_D3D10_SB_NAME(D3D10_SB_NAME_CLIP_DISTANCE));
      ++stat_.dcl_count;
    }
    // Cull distance output.
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_OUTPUT_SIV) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4));
    shader_object_.push_back(
        EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_OUTPUT, 0b0100, 1));
    shader_object_.push_back(
        uint32_t(InOutRegister::kVSDSOutClipDistance45AndCullDistance));
    shader_object_.push_back(ENCODE_D3D10_SB_NAME(D3D10_SB_NAME_CULL_DISTANCE));
    ++stat_.dcl_count;
  } else if (is_pixel_shader()) {
    bool is_writing_float24_depth = DSV_IsWritingFloat24Depth();
    bool shader_writes_depth = current_shader().writes_depth();
    // Interpolator input.
    if (!is_depth_only_pixel_shader_) {
      uint32_t interpolator_count =
          std::min(xenos::kMaxInterpolators, register_count());
      for (uint32_t i = 0; i < interpolator_count; ++i) {
        shader_object_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INPUT_PS) |
            ENCODE_D3D10_SB_INPUT_INTERPOLATION_MODE(
                D3D10_SB_INTERPOLATION_LINEAR) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
        shader_object_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_INPUT, 0b1111, 1));
        shader_object_.push_back(uint32_t(InOutRegister::kPSInInterpolators) +
                                 i);
        ++stat_.dcl_count;
      }
      if (register_count()) {
        // Point parameters input (only coordinates, not size, needed).
        shader_object_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INPUT_PS) |
            ENCODE_D3D10_SB_INPUT_INTERPOLATION_MODE(
                D3D10_SB_INTERPOLATION_LINEAR) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
        shader_object_.push_back(
            EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_INPUT, 0b0011, 1));
        shader_object_.push_back(uint32_t(InOutRegister::kPSInPointParameters));
        ++stat_.dcl_count;
      }
    }
    if (edram_rov_used_) {
      // Z and W in clip space, for per-sample depth.
      shader_object_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INPUT_PS) |
          ENCODE_D3D10_SB_INPUT_INTERPOLATION_MODE(
              D3D10_SB_INTERPOLATION_LINEAR) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
      shader_object_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_INPUT, 0b0011, 1));
      shader_object_.push_back(uint32_t(InOutRegister::kPSInClipSpaceZW));
      ++stat_.dcl_count;
    }
    if (in_position_used_) {
      // Position input (XY needed for ps_param_gen, Z needed for non-ROV
      // float24 conversion; the ROV depth code calculates the depth the from
      // clip space Z and W with pull-mode per-sample interpolation instead).
      // At the cost of possibility of MSAA with pixel-rate shading, need
      // per-sample depth - otherwise intersections cannot be antialiased, and
      // with SV_DepthLessEqual, per-sample (or centroid, but this isn't
      // applicable here) position is mandatory. However, with depth output, on
      // the guest, there's only one depth value for the whole pixel.
      D3D10_SB_INTERPOLATION_MODE position_interpolation_mode =
          is_writing_float24_depth && !shader_writes_depth
              ? D3D10_SB_INTERPOLATION_LINEAR_NOPERSPECTIVE_SAMPLE
              : D3D10_SB_INTERPOLATION_LINEAR_NOPERSPECTIVE;
      shader_object_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INPUT_PS_SIV) |
          ENCODE_D3D10_SB_INPUT_INTERPOLATION_MODE(
              position_interpolation_mode) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4));
      shader_object_.push_back(EncodeVectorMaskedOperand(
          D3D10_SB_OPERAND_TYPE_INPUT, in_position_used_, 1));
      shader_object_.push_back(uint32_t(InOutRegister::kPSInPosition));
      shader_object_.push_back(ENCODE_D3D10_SB_NAME(D3D10_SB_NAME_POSITION));
      ++stat_.dcl_count;
    }
    if (in_front_face_used_) {
      // Is front face.
      shader_object_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INPUT_PS_SGV) |
          // This needs to be set according to FXC output, despite the
          // description in d3d12TokenizedProgramFormat.hpp saying bits 11:23
          // are ignored.
          ENCODE_D3D10_SB_INPUT_INTERPOLATION_MODE(
              D3D10_SB_INTERPOLATION_CONSTANT) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4));
      shader_object_.push_back(
          EncodeVectorMaskedOperand(D3D10_SB_OPERAND_TYPE_INPUT, 0b0001, 1));
      shader_object_.push_back(uint32_t(InOutRegister::kPSInFrontFace));
      shader_object_.push_back(
          ENCODE_D3D10_SB_NAME(D3D10_SB_NAME_IS_FRONT_FACE));
      ++stat_.dcl_count;
    }
    if (edram_rov_used_) {
      // Sample coverage input.
      shader_object_.push_back(
          ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INPUT) |
          ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(2));
      shader_object_.push_back(
          EncodeScalarOperand(D3D11_SB_OPERAND_TYPE_INPUT_COVERAGE_MASK, 0));
      ++stat_.dcl_count;
    } else {
      if (current_shader().writes_color_targets()) {
        // Color output.
        for (uint32_t i = 0; i < 4; ++i) {
          shader_object_.push_back(
              ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_OUTPUT) |
              ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3));
          shader_object_.push_back(EncodeVectorMaskedOperand(
              D3D10_SB_OPERAND_TYPE_OUTPUT, 0b1111, 1));
          shader_object_.push_back(i);
          ++stat_.dcl_count;
        }
      }
      // Depth output.
      if (is_writing_float24_depth || shader_writes_depth) {
        D3D10_SB_OPERAND_TYPE depth_operand_type;
        if (!shader_writes_depth &&
            GetDxbcShaderModification().depth_stencil_mode ==
                Modification::DepthStencilMode::kFloat24Truncating) {
          depth_operand_type = D3D11_SB_OPERAND_TYPE_OUTPUT_DEPTH_LESS_EQUAL;
        } else {
          depth_operand_type = D3D10_SB_OPERAND_TYPE_OUTPUT_DEPTH;
        }
        shader_object_.push_back(
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_OUTPUT) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(2));
        shader_object_.push_back(EncodeScalarOperand(depth_operand_type, 0));
        ++stat_.dcl_count;
      }
    }
  }

  // Temporary registers - guest general-purpose registers if not using dynamic
  // indexing and Xenia internal registers.
  stat_.temp_register_count = system_temp_count_max_;
  if (!is_depth_only_pixel_shader_ &&
      !current_shader().uses_register_dynamic_addressing()) {
    stat_.temp_register_count += register_count();
  }
  if (stat_.temp_register_count != 0) {
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_TEMPS) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(2));
    shader_object_.push_back(stat_.temp_register_count);
  }

  // General-purpose registers if using dynamic indexing (x0).
  if (!is_depth_only_pixel_shader_ &&
      current_shader().uses_register_dynamic_addressing()) {
    assert_true(register_count() != 0);
    shader_object_.push_back(
        ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INDEXABLE_TEMP) |
        ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(4));
    // x0.
    shader_object_.push_back(0);
    shader_object_.push_back(register_count());
    // 4 components in each.
    shader_object_.push_back(4);
    stat_.temp_array_count += register_count();
  }

  // Write the translated shader code.
  size_t code_size_dwords = shader_code_.size();
  // So [] won't crash in case the size is zero somehow.
  if (code_size_dwords != 0) {
    shader_object_.resize(shader_object_.size() + code_size_dwords);
    std::memcpy(&shader_object_[shader_object_.size() - code_size_dwords],
                shader_code_.data(), code_size_dwords * sizeof(uint32_t));
  }

  // Write the length.
  shader_object_[chunk_position_dwords + 1] =
      uint32_t(shader_object_.size()) - chunk_position_dwords;
}

}  // namespace gpu
}  // namespace xe
