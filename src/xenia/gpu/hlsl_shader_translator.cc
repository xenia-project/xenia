/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/hlsl_shader_translator.h"

#include <algorithm>

#include "xenia/base/assert.h"

namespace xe {
namespace gpu {
using namespace ucode;

constexpr uint32_t kMaxInterpolators = 16;

#define EmitSource(...) source_inner_.AppendFormat(__VA_ARGS__)
#define EmitSourceDepth(...) \
  source_inner_.Append(depth_prefix_); \
  source_inner_.AppendFormat(__VA_ARGS__)

HlslShaderTranslator::HlslShaderTranslator() {}
HlslShaderTranslator::~HlslShaderTranslator() = default;

void HlslShaderTranslator::Reset() {
  ShaderTranslator::Reset();

  source_inner_.Reset();
  depth_ = 0;
  depth_prefix_[0] = 0;

  cf_exec_pred_ = false;
  cf_exec_pred_cond_ = false;

  writes_depth_ = false;

  texture_srv_count_ = 0;
  sampler_count_ = 0;

  cube_used_ = false;
}

void HlslShaderTranslator::EmitTranslationError(const char* message) {
  ShaderTranslator::EmitTranslationError(message);
  EmitSourceDepth("// TRANSLATION ERROR: %s\n", message);
}

void HlslShaderTranslator::EmitUnimplementedTranslationError() {
  ShaderTranslator::EmitUnimplementedTranslationError();
  EmitSourceDepth("// UNIMPLEMENTED TRANSLATION\n");
}

void HlslShaderTranslator::Indent() {
  depth_prefix_[depth_] = ' ';
  depth_prefix_[depth_ + 1] = ' ';
  depth_prefix_[depth_ + 2] = 0;
  depth_ += 2;
}

void HlslShaderTranslator::Unindent() {
  depth_ -= 2;
  depth_prefix_[depth_] = 0;
}

void HlslShaderTranslator::StartTranslation() {
  // Main function level (1).
  Indent();
  // While level (2).
  Indent();
  EmitSourceDepth("[branch] if (xe_pc == 0u) {\n");
  // If level (3).
  Indent();
}

std::vector<uint8_t> HlslShaderTranslator::CompleteTranslation() {
  // Add the declarations, the prologue and the epilogue knowing what is needed.
  StringBuffer source;

  // Common preprocessor statements.
  // 3557 is the "loop only executes for 1 iteration" warning caused by the
  // control flow loop design.
  // 3595 is for texture fetches in loops/conditionals, which may have undefined
  // gradients.
  source.Append(
      "#pragma warning(disable : 3557)\n"
      "#pragma warning(disable : 3595)\n"
      "\n"
      "#define XE_FLT_MAX 3.402823466e+38\n"
      "\n");

  // Cubemap sampling. XeCubeTo2D emulates the cube vector ALU instruction that
  // gives (t, s, 2 * major axis, face index), XeCubeTo3D reverts its effects
  // in tfetchCube because sampling a cubemap as an array doesn't work properly
  // with mipmaps (at the edges, there's a jump of S and T between 0 and 1, and
  // the entire texture becomes 1x1 in size).
  //
  // If X is the major axis:
  //   T is -Y
  //   S is -Z for positive X, +Z for negative X
  //   Face is 0 for positive X, 1 for negative X
  // If Y is the major axis:
  //   T is +Z for positive Y, -Z for negative Y
  //   S is +X
  //   Face is 2 for positive Y, 3 for negative Y
  // If Z is the major axis:
  //   T is -Y
  //   S is +X for positive Z, -X for negative Z
  //   Face is 4 for positive Z, 5 for negative Z
  // From T and S, abs(2 * MA) needs to be subtracted also.
  //
  // Undo accepts (s, t, face index).
  if (cube_used_) {
    source.Append(
        "float4 XeCubeTo2D(float3 xe_cube_3d) {\n"
        "  float3 xe_cube_3d_abs = abs(xe_cube_3d);\n"
        "  float4 xe_cube_2d;\n"
        "  if (xe_cube_3d_abs.x >= xe_cube_3d_abs.y &&\n"
        "      xe_cube_3d_abs.x >= xe_cube_3d_abs.z) {\n"
        "    xe_cube_2d.xy = -xe_cube_3d.yz;\n"
        "    xe_cube_2d.y *= sign(xe_cube_3d.x);\n"
        "    xe_cube_2d.z = xe_cube_3d.x;\n"
        "    xe_cube_2d.w = 0.0;\n"
        "  } else if (xe_cube_3d_abs.y >= xe_cube_3d_abs.z) {\n"
        "    xe_cube_2d.xyz = xe_cube_3d.zxy;\n"
        "    xe_cube_2d.x *= sign(xe_cube_3d.y);\n"
        "    xe_cube_2d.w = 2.0;\n"
        "  } else {\n"
        "    xe_cube_2d.x = -xe_cube_3d.y;\n"
        "    xe_cube_2d.yz = xe_cube_3d.xz;\n"
        "    xe_cube_2d.y *= sign(xe_cube_3d.z);\n"
        "    xe_cube_2d.w = 4.0;\n"
        "  }\n"
        "  xe_cube_2d.z *= 2.0;\n"
        "  xe_cube_2d.xy -= abs(xe_cube_2d.zz);\n"
        "  xe_cube_2d.w += saturate(-sign(xe_cube_2d.z));\n"
        "  return xe_cube_2d;\n"
        "}\n"
        "\n"
        "float3 XeCubeTo3D(float3 xe_cube_2d) {\n"
        "  xe_cube_2d.xy = xe_cube_2d.xy * 2.0 - 1.0;\n"
        "  float3 xe_cube_3d;\n"
        "  uint xe_cube_face_index = uint(xe_cube_2d.z);\n"
        "  uint xe_cube_ma_index = xe_cube_face_index >> 1u;\n"
        "  xe_cube_2d.z =\n"
        "      -(float(xe_cube_face_index & 1u) * 2.0 - 1.0);\n"
        "  if (xe_cube_ma_index == 0u) {\n"
        "    xe_cube_3d.x = xe_cube_2d.z;\n"
        "    xe_cube_3d.yz = -xe_cube_2d.yx;\n"
        "    xe_cube_3d.z *= xe_cube_2d.z;\n"
        "  } else if (xe_cube_ma_index == 1u) {\n"
        "    xe_cube_3d = xe_cube_2d.xzy;\n"
        "    xe_cube_3d.z *= xe_cube_2d.z;\n"
        "  } else {\n"
        "    xe_cube_3d.xz = xe_cube_2d.xz;\n"
        "    xe_cube_3d.x *= xe_cube_2d.z;\n"
        "    xe_cube_3d.y = -xe_cube_2d.y;\n"
        "  }\n"
        "  return xe_cube_3d;\n"
        "}\n"
        "\n");
  }

  // Common declarations.
  // Only up to 14 constant buffers can be used on binding tiers 1 and 2.
  // Bool and loop constants are quadrupled to allow dynamic indexing (constant
  // registers are vectors).
  source.Append(
      "cbuffer XeSystemConstants : register(b0) {\n"
      // vec4 0
      "  float3 xe_mul_rcp_w;\n"
      "  uint xe_vertex_base_index;\n"
      // vec4 1
      "  float3 xe_ndc_scale;\n"
      "  uint xe_vertex_index_endian;\n"
      // vec4 2
      "  float3 xe_ndc_offset;\n"
      "  float xe_pixel_half_pixel_offset;\n"
      // vec4 3
      "  float2 xe_point_size;\n"
      "  float2 xe_ssaa_inv_scale;\n"
      // vec4 4
      "  uint xe_pixel_pos_reg;\n"
      "  int xe_alpha_test;\n"
      "  float2 xe_alpha_test_range;\n"
      // vec4 5
      "  float4 xe_color_exp_bias;\n"
      // vec4 6
      "  uint4 xe_color_output_map;\n"
      "};\n"
      "\n"
      "cbuffer XeLoopBoolConstants : register(b1) {\n"
      "  uint4 xe_bool_constants[8];\n"
      "  uint4 xe_loop_constants[32];\n"
      "};\n"
      "\n"
      "cbuffer XeFetchConstants : register(b2) {\n"
      "  uint4 xe_fetch[48];\n"
      "};\n"
      "\n"
      "struct XeFloatConstantPage {\n"
      "  float4 c[32];\n"
      "};\n"
      "ConstantBuffer<XeFloatConstantPage> "
      "xe_float_constants[8] : register(b3);\n"
      "\n");

  // Textures and samplers.
  for (uint32_t i = 0; i < texture_srv_count_; ++i) {
    const TextureSRV& srv = texture_srvs_[i];
    const char* srv_type_dimension;
    const char* srv_name_suffix;
    switch (srv.dimension) {
      case TextureDimension::k3D:
        srv_type_dimension = "3D";
        srv_name_suffix = "3d";
        break;
      case TextureDimension::kCube:
        srv_type_dimension = "Cube";
        srv_name_suffix = "cube";
        break;
      default:
        srv_type_dimension = "2DArray";
        srv_name_suffix = "2d";
        break;
    }
    // t0 is shared memory for vfetch, so textures start from t1.
    source.AppendFormat("Texture%s<float4> xe_texture%u_%s : register(t%u);\n",
                        srv_type_dimension, srv.fetch_constant, srv_name_suffix,
                        i + 1);
  }
  for (uint32_t i = 0; i < sampler_count_; ++i) {
    source.AppendFormat("SamplerState xe_sampler%u : register(s%u);\n",
                        sampler_fetch_constants_[i], i);
  }
  if (texture_srv_count_ != 0 || sampler_count_ != 0) {
    source.Append("\n");
  }

  if (is_vertex_shader()) {
    // Vertex fetching, output and prologue.
    // Endian register (2nd word of the fetch constant) is 00 for no swap, 01
    // for 8-in-16, 10 for 8-in-32 (a combination of 8-in-16 and 16-in-32), and
    // 11 for 16-in-32. This means we can check bits 0 ^ 1 to see if we need to
    // do a 8-in-16 swap, and bit 1 to see if a 16-in-32 swap is needed.
    // Vertex element is a temporary integer value for fetches.
    // -1 point size means the geometry shader will use the global setting by
    // default.
    source.AppendFormat(
        "ByteAddressBuffer xe_shared_memory : register(t0);\n"
        "\n"
        "#define XE_BYTE_SWAP_OVERLOAD(XeByteSwapType) \\\n"
        "XeByteSwapType XeByteSwap(XeByteSwapType v, uint endian) { \\\n"
        "  [flatten] if (((endian ^ (endian >> 1u)) & 1u) != 0u) { \\\n"
        "    v = ((v & 0x00FF00FFu) << 8u) | ((v & 0xFF00FF00u) >> 8u); \\\n"
        "  } \\\n"
        "  [flatten] if ((endian & 2u) != 0u) { \\\n"
        "    v = (v << 16u) | (v >> 16u); \\\n"
        "  } \\\n"
        "  return v; \\\n"
        "}\n"
        "XE_BYTE_SWAP_OVERLOAD(uint)\n"
        "XE_BYTE_SWAP_OVERLOAD(uint2)\n"
        "XE_BYTE_SWAP_OVERLOAD(uint3)\n"
        "XE_BYTE_SWAP_OVERLOAD(uint4)\n"
        "\n"
        "struct XeVertexShaderOutput {\n"
        "  float4 interpolators[%u] : TEXCOORD0;\n"
        "  float2 point_coord : TEXCOORD16;\n"
        "  float4 position : SV_Position;\n"
        "  float point_size : PSIZE;\n"
        "};\n"
        "\n"
        "XeVertexShaderOutput main(uint xe_vertex_index_be : SV_VertexID) {\n"
        "  float4 xe_r[%u];\n"
        "  uint xe_vertex_index =\n"
        "      XeByteSwap(xe_vertex_index_be, xe_vertex_index_endian) +\n"
        "      xe_vertex_base_index;\n"
        "  uint4 xe_vertex_element;\n"
        "  xe_r[0].r = float(xe_vertex_index);\n"
        "  XeVertexShaderOutput xe_output;\n"
        // point_coord is written by the geometry shader.
        "  xe_output.point_coord = float2(0.0, 0.0);\n"
        "  xe_output.position = float4(0.0, 0.0, 0.0, 1.0);\n"
        "  xe_output.point_size = -1.0;\n",
        kMaxInterpolators, register_count());
    for (uint32_t i = 0; i < kMaxInterpolators; ++i) {
      source.AppendFormat("  xe_output.interpolators[%u] = (0.0).xxxx;\n", i);
    }
  } else if (is_pixel_shader()) {
    // Pixel shader inputs, outputs and prologue.
    // If the shader writes to depth, it needs to define
    // XE_PIXEL_SHADER_WRITES_DEPTH in the beginning of the final output.
    source.AppendFormat(
        "struct XePixelShaderInput {\n"
        "  float4 interpolators[%u] : TEXCOORD0;\n"
        "  float2 point_coord : TEXCOORD16;\n"
        "  float4 position : SV_Position;\n"
        "};\n"
        "\n"
        "struct XePixelShaderOutput {\n"
        "  float4 colors[4] : SV_Target;\n"
        "%s"
        "};\n"
        "\n"
        "XePixelShaderOutput main(XePixelShaderInput xe_input) {\n"
        "  float4 xe_r[%u];\n"
        "  XePixelShaderOutput xe_output;\n"
        "  float4 xe_color_output[4];\n"
        "  xe_color_output[0] = (0.0).xxxx;\n"
        "  xe_color_output[1] = (0.0).xxxx;\n"
        "  xe_color_output[2] = (0.0).xxxx;\n"
        "  xe_color_output[3] = (0.0).xxxx;\n",
        kMaxInterpolators, writes_depth_ ? "  float depth : SV_Depth;\n" : "",
        register_count());
    // Initialize SV_Depth if using it.
    if (writes_depth_) {
      source.Append("  xe_output.depth = xe_input.position.z;\n");
    }
    // Copy interpolants to the first registers.
    uint32_t interpolator_register_count =
        std::min(register_count(), kMaxInterpolators);
    for (uint32_t i = 0; i < interpolator_register_count; ++i) {
      source.AppendFormat("  xe_r[%u] = xe_input.interpolators[%u];\n", i, i);
    }
    // Write pixel position and point coordinate to the register specified by
    // ps_param_gen.
    source.AppendFormat(
        "  [branch] if (xe_pixel_pos_reg < %uu) {\n"
        "    xe_r[xe_pixel_pos_reg] =\n"
        "        float4(xe_input.position.xy * xe_ssaa_inv_scale +\n"
        "               xe_pixel_half_pixel_offset, xe_input.point_coord);\n"
        "  }\n",
        register_count());
  }

  // Common main function variables and prologue.
  source.Append(
      // Dynamic index for source operands (mainly for float and bool constants
      // since they are indexed in two parts).
      "  uint xe_src_index;\n"
      // Sources for instructions.
      "  float4 xe_src0, xe_src1, xe_src2;\n"
      // Previous vector result (used as a scratch).
      "  float4 xe_pv = float4(0.0, 0.0, 0.0, 0.0);\n"
      // Previous scalar result (used for RETAIN_PREV).
      "  float xe_ps = 0.0;\n"
      // Predicate temp, clause-local. Initially false like cf_exec_pred_cond_.
      "  bool xe_p0 = false;\n"
      // Address register when using absolute addressing.
      "  int xe_a0 = 0;\n"
      // Loop index stack - .x is the active loop, shifted right to yzw on push.
      "  int4 xe_aL = int4(0, 0, 0, 0);\n"
      // Loop counter stack, .x is the active loop.
      // Represents number of times remaining to loop.
      "  uint4 xe_loop_count = uint4(0u, 0u, 0u, 0u);\n"
      // Coordinates for texture fetches.
      "  float3 xe_texture_coords;\n"
      // In 3D texture fetch instructions, whether the current texture is 3D as
      // opposed to a 2D array.
      "  bool xe_texture_is_3d;\n"
      // Size shift and offset for 3D or array textures.
      "  uint4 xe_tfetch3d_size_bits;\n"
      // Explicit LOD for texture fetches.
      "  float xe_texture_lod = 0.0;\n"
      // Explicit gradients for texture fetches.
      "  float3 xe_texture_grad_h = float3(0.0, 0.0, 0.0);\n"
      "  float3 xe_texture_grad_v = float3(0.0, 0.0, 0.0);\n"
      // Master loop and instruction pointer for flow control.
      // It's very easy to cause internal compiler errors with anything related
      // to flow control. Switch, for example, used to cause "l-value expected",
      // so it was replaced with ifs (and also it's easy to fall through).
      "  uint xe_pc = 0u;\n"
      "\n"
      "  [loop] while (xe_pc < 0xFFFFu) {\n");

  // Translated code.
  source.Append(source_inner_.GetString());

  // Epilogue.
  source.Append(
      "    }\n"
      "    xe_pc = 0xFFFFu;\n"
      "  }\n");
  if (is_vertex_shader()) {
    // Restore the original W if the shader has already taken its reciprocal,
    // and restore the original XYZ if the shader has divided them by W. Also
    // normalize the coordinates to the viewport if the shader has returned
    // unnormalized ones (for rectangle lists, for instance) and apply the half-
    // pixel offset.
    source.Append(
        "  [flatten] if (xe_mul_rcp_w.z == 0.0) {\n"
        "    xe_output.position.w = rcp(xe_output.position.w);\n"
        "  }\n"
        "  xe_output.position.xyz *=\n"
        "      lerp((1.0).xxx, xe_output.position.www, xe_mul_rcp_w.xxy);\n"
        "  xe_output.position.xyz =\n"
        "      xe_output.position.xyz * xe_ndc_scale +\n"
        "      xe_ndc_offset * xe_output.position.www;\n");
  } else if (is_pixel_shader()) {
    source.Append(
        // Apply the exponent bias.
        "  xe_color_output[0u] *= xe_color_exp_bias.x;\n"
        "  xe_color_output[1u] *= xe_color_exp_bias.y;\n"
        "  xe_color_output[2u] *= xe_color_exp_bias.z;\n"
        "  xe_color_output[3u] *= xe_color_exp_bias.w;\n"
        // Perform alpha test - check if the alpha is within the specified
        // bounds (inclusively), fail or pass depending on comparison mode and
        // on the results of the bound test.
        "  [branch] if (xe_alpha_test != 0) {\n"
        "    bool xe_alpha_test_failed =\n"
        "        xe_color_output[0u].a >= xe_alpha_test_range.x &&\n"
        "        xe_color_output[0u].a <= xe_alpha_test_range.y;\n"
        "    if (xe_alpha_test > 0) {\n"
        "      xe_alpha_test_failed = !xe_alpha_test_failed;\n"
        "    }\n"
        "    if (xe_alpha_test_failed) {\n"
        "      discard;\n"
        "    }\n"
        "  }\n"
        // Remap guest color outputs to host render targets because null render
        // target descriptors are broken.
        "  xe_output.colors[0u] = xe_color_output[xe_color_output_map.r];\n"
        "  xe_output.colors[1u] = xe_color_output[xe_color_output_map.g];\n"
        "  xe_output.colors[2u] = xe_color_output[xe_color_output_map.b];\n"
        "  xe_output.colors[3u] = xe_color_output[xe_color_output_map.a];\n");
  }
  // TODO(Triang3l): Gamma if needed.
  source.Append(
      "  return xe_output;\n"
      "}\n");

  return source.ToBytes();
}

void HlslShaderTranslator::ProcessLabel(uint32_t cf_index) {
  // 0 is always added in the beginning.
  if (cf_index == 0) {
    return;
  }
  Unindent();
  EmitSourceDepth("}\n");
  EmitSourceDepth("[branch] if (xe_pc <= %uu) {\n", cf_index);
  Indent();
}

void HlslShaderTranslator::ProcessControlFlowNopInstruction(uint32_t cf_index) {
  EmitSourceDepth("// cnop\n");
}

void HlslShaderTranslator::ProcessExecInstructionBegin(
    const ParsedExecInstruction& instr) {
  EmitSourceDepth("// ");
  instr.Disassemble(&source_inner_);

  cf_exec_pred_ = false;
  switch (instr.type) {
    case ParsedExecInstruction::Type::kUnconditional:
      EmitSourceDepth("{\n");
      break;
    case ParsedExecInstruction::Type::kConditional:
      EmitSourceDepth(
          "[branch] if ((xe_bool_constants[%u].x & (1u << %uu)) %c= 0u) {\n",
          instr.bool_constant_index >> 5, instr.bool_constant_index & 31,
          instr.condition ? '!' : '=');
      break;
    case ParsedExecInstruction::Type::kPredicated:
      cf_exec_pred_ = true;
      cf_exec_pred_cond_ = instr.condition;
      EmitSourceDepth("[branch] if (%cxe_p0) {\n", instr.condition ? ' ' : '!');
      break;
  }
  Indent();
}

void HlslShaderTranslator::ProcessExecInstructionEnd(
    const ParsedExecInstruction& instr) {
  if (instr.is_end) {
    // Break causes internal errors, set PC to 0xFFFF and go to the next
    // iteration.
    EmitSourceDepth("xe_pc = 0xFFFFu;\n");
    EmitSourceDepth("continue;\n");
  }
  Unindent();
  EmitSourceDepth("}\n");
}

void HlslShaderTranslator::ProcessLoopStartInstruction(
    const ParsedLoopStartInstruction& instr) {
  EmitSourceDepth("// ");
  instr.Disassemble(&source_inner_);

  // Setup counter.
  EmitSourceDepth("xe_loop_count.yzw = xe_loop_count.xyz;\n");
  EmitSourceDepth("xe_loop_count.x = xe_loop_constants[%u].x & 0xFFu;\n",
                  instr.loop_constant_index);

  // Setup relative indexing.
  EmitSourceDepth("xe_aL = xe_aL.xxyz;\n");
  if (!instr.is_repeat) {
    // Push new loop starting index if not reusing the current one.
    EmitSourceDepth("xe_aL.x = int((xe_loop_constants[%u].x >> 8u) & 0xFFu);\n",
                    instr.loop_constant_index);
  }

  // Quick skip loop if zero count.
  EmitSourceDepth("[branch] if (xe_loop_count.x == 0u) {\n");
  EmitSourceDepth("  xe_pc = %uu;  // Skip loop to L%u\n",
                  instr.loop_skip_address, instr.loop_skip_address);
  EmitSourceDepth("  continue;\n");
  EmitSourceDepth("}\n");
}

void HlslShaderTranslator::ProcessLoopEndInstruction(
    const ParsedLoopEndInstruction& instr) {
  EmitSourceDepth("// ");
  instr.Disassemble(&source_inner_);

  // Decrement loop counter, and if we are done break out.
  EmitSourceDepth("[branch] if (--xe_loop_count.x == 0u");
  if (instr.is_predicated_break) {
    // If the predicate condition is met we break out of the loop.
    // Need to restore stack and fall through to the next cf.
    EmitSource(" || %cxe_p0) {\n", instr.predicate_condition ? ' ' : '!');
  } else {
    EmitSource(") {\n");
  }
  Indent();
  // Loop completed - pop and fall through to next cf.
  EmitSourceDepth("xe_loop_count.xyz = xe_loop_count.yzw;\n");
  EmitSourceDepth("xe_loop_count.w = 0u;\n");
  EmitSourceDepth("xe_aL.xyz = xe_aL.yzw;\n");
  EmitSourceDepth("xe_aL.w = 0;\n");
  Unindent();
  EmitSourceDepth("} else {\n");
  Indent();
  // Still looping. Adjust index and jump back to body.
  EmitSourceDepth("xe_aL.x += int(xe_loop_constants[%u].x << 8u) >> 24;\n",
                  instr.loop_constant_index);
  EmitSourceDepth("xe_pc = %uu;  // Loop back to body L%u\n",
                  instr.loop_body_address, instr.loop_body_address);
  EmitSourceDepth("continue;\n");
  Unindent();
  EmitSourceDepth("}\n");
}

void HlslShaderTranslator::ProcessCallInstruction(
    const ParsedCallInstruction& instr) {
  EmitSourceDepth("// ");
  instr.Disassemble(&source_inner_);

  EmitUnimplementedTranslationError();
}

void HlslShaderTranslator::ProcessReturnInstruction(
    const ParsedReturnInstruction& instr) {
  EmitSourceDepth("// ");
  instr.Disassemble(&source_inner_);

  EmitUnimplementedTranslationError();
}

void HlslShaderTranslator::ProcessJumpInstruction(
    const ParsedJumpInstruction& instr) {
  EmitSourceDepth("// ");
  instr.Disassemble(&source_inner_);

  bool conditional = false;
  switch (instr.type) {
    case ParsedJumpInstruction::Type::kUnconditional:
      break;
    case ParsedJumpInstruction::Type::kConditional:
      EmitSourceDepth(
          "[branch] if ((xe_bool_constants[%u].x & (1u << %uu)) %c= 0u) {\n",
          instr.bool_constant_index >> 5, instr.bool_constant_index & 31,
          instr.condition ? '!' : '=');
      conditional = true;
      break;
    case ParsedJumpInstruction::Type::kPredicated:
      EmitSourceDepth("[branch] if (%cxe_p0) {\n", instr.condition ? ' ' : '!');
      conditional = true;
      break;
  }
  if (conditional) {
    Indent();
  }
  EmitSourceDepth("xe_pc = %uu;  // L%u\n", instr.target_address,
                  instr.target_address);
  EmitSourceDepth("continue;\n");
  if (conditional) {
    Unindent();
    EmitSourceDepth("}\n");
  }
}

void HlslShaderTranslator::ProcessAllocInstruction(
    const ParsedAllocInstruction& instr) {
  EmitSourceDepth("// ");
  instr.Disassemble(&source_inner_);
}

bool HlslShaderTranslator::BeginPredicatedInstruction(
    bool is_predicated, bool predicate_condition) {
  if (is_predicated &&
      (!cf_exec_pred_ || cf_exec_pred_cond_ != predicate_condition)) {
    EmitSourceDepth("[branch] if (%cxe_p0) {\n",
                    predicate_condition ? ' ' : '!');
    Indent();
    return true;
  }
  return false;
}

void HlslShaderTranslator::EndPredicatedInstruction(bool conditional_emitted) {
  if (conditional_emitted) {
    Unindent();
    EmitSourceDepth("}\n");
  }
}

void HlslShaderTranslator::EmitLoadOperand(size_t src_index,
                                           const InstructionOperand& op) {
  // If indexing dynamically, emit the index because float and bool constants
  // need to be indexed in two parts.
  // Also verify we are not using vertex/texture fetch constants here.
  uint32_t storage_index_max;
  switch (op.storage_source) {
    case InstructionStorageSource::kRegister:
      storage_index_max = 127;
      break;
    case InstructionStorageSource::kConstantFloat:
    case InstructionStorageSource::kConstantBool:
      storage_index_max = 255;
      break;
    case InstructionStorageSource::kConstantInt:
      storage_index_max = 31;
      break;
    default:
      assert_always();
      return;
  }
  if (op.storage_addressing_mode ==
      InstructionStorageAddressingMode::kAddressAbsolute) {
    EmitSourceDepth("xe_src_index = uint(%u + xe_a0) & %uu;\n",
                    op.storage_index, storage_index_max);
  } else if (op.storage_addressing_mode ==
             InstructionStorageAddressingMode::kAddressRelative) {
    EmitSourceDepth("xe_src_index = uint(%u + xe_aL.x) & %uu;\n",
                    op.storage_index, storage_index_max);
  }

  // Negation and abs are store modifiers, so they're applied after swizzling.
  EmitSourceDepth("xe_src%u = ", uint32_t(src_index));
  if (op.is_negated) {
    EmitSource("-");
  }
  if (op.is_absolute_value) {
    EmitSource("abs");
  }
  EmitSource("(");

  if (op.storage_addressing_mode == InstructionStorageAddressingMode::kStatic) {
    switch (op.storage_source) {
      case InstructionStorageSource::kRegister:
        EmitSource("xe_r[%u]", op.storage_index);
        break;
      case InstructionStorageSource::kConstantFloat:
        EmitSource("xe_float_constants[%u].c[%u]", op.storage_index >> 5,
                   op.storage_index & 31);
        break;
      case InstructionStorageSource::kConstantInt:
        EmitSource("xe_loop_constants[%u].x", op.storage_index);
        break;
      case InstructionStorageSource::kConstantBool:
        EmitSource("float((xe_bool_constants[%u].x >> %uu) & 1u)",
                   op.storage_index >> 5, op.storage_index & 31);
        break;
      default:
        assert_always();
        break;
    }
  } else {
    switch (op.storage_source) {
      case InstructionStorageSource::kRegister:
        EmitSource("xe_r[xe_src_index]");
        break;
      case InstructionStorageSource::kConstantFloat:
        EmitSource(
            "xe_float_constants[xe_src_index >> 5u].c[xe_src_index & 31u]");
        break;
      case InstructionStorageSource::kConstantInt:
        EmitSource("xe_loop_constants[xe_src_index].x");
        break;
      case InstructionStorageSource::kConstantBool:
        EmitSource(
            "float((xe_bool_constants[xe_src_index >> 5u].x >> "
            "(xe_src_index & 31u)) & 1u)");
        break;
      default:
        assert_always();
        break;
    }
  }

  EmitSource(")");
  // Integer and bool constants are scalar, can't swizzle them.
  if (op.storage_source == InstructionStorageSource::kConstantInt ||
      op.storage_source == InstructionStorageSource::kConstantBool) {
    EmitSource(".xxxx");
  } else {
    if (!op.is_standard_swizzle()) {
      EmitSource(".");
      // For 1 component stores it will be .aaaa, for 2 components it's .abbb.
      for (int i = 0; i < op.component_count; ++i) {
        EmitSource("%c", GetCharForSwizzle(op.components[i]));
      }
      for (int i = op.component_count; i < 4; ++i) {
        EmitSource("%c",
                   GetCharForSwizzle(op.components[op.component_count - 1]));
      }
    }
  }
  EmitSource(";\n");
}

void HlslShaderTranslator::EmitStoreResult(const InstructionResult& result,
                                           bool source_is_scalar) {
  bool storage_is_scalar =
      result.storage_target == InstructionStorageTarget::kPointSize ||
      result.storage_target == InstructionStorageTarget::kDepth;
  if (storage_is_scalar) {
    if (!result.write_mask[0]) {
      return;
    }
  } else {
    if (!result.has_any_writes()) {
      return;
    }
  }

  bool storage_is_array = false;
  switch (result.storage_target) {
    case InstructionStorageTarget::kRegister:
      EmitSourceDepth("xe_r");
      storage_is_array = true;
      break;
    case InstructionStorageTarget::kInterpolant:
      EmitSourceDepth("xe_output.interpolators");
      storage_is_array = true;
      break;
    case InstructionStorageTarget::kPosition:
      EmitSourceDepth("xe_output.position");
      break;
    case InstructionStorageTarget::kPointSize:
      EmitSourceDepth("xe_output.point_size");
      break;
    case InstructionStorageTarget::kColorTarget:
      EmitSourceDepth("xe_color_output");
      storage_is_array = true;
      break;
    case InstructionStorageTarget::kDepth:
      EmitSourceDepth("xe_output.depth");
      writes_depth_ = true;
      break;
    default:
    case InstructionStorageTarget::kNone:
      return;
  }
  if (storage_is_array) {
    switch (result.storage_addressing_mode) {
      case InstructionStorageAddressingMode::kStatic:
        EmitSource("[%u]", result.storage_index);
        break;
      case InstructionStorageAddressingMode::kAddressAbsolute:
        EmitSource("[%u + xe_a0]", result.storage_index);
        break;
      case InstructionStorageAddressingMode::kAddressRelative:
        EmitSource("[%u + xe_aL.x]", result.storage_index);
        break;
    }
  }
  if (storage_is_scalar) {
    EmitSource(" = ");
    switch (result.components[0]) {
      case SwizzleSource::k0:
        EmitSource("0.0");
        break;
      case SwizzleSource::k1:
        EmitSource("1.0");
        break;
      default:
        if (result.is_clamped) {
          EmitSource("saturate(");
        }
        if (source_is_scalar) {
          EmitSource("xe_ps");
        } else {
          EmitSource("xe_pv.%c", GetCharForSwizzle(result.components[0]));
        }
        if (result.is_clamped) {
          EmitSource(")");
        }
        break;
    }
  } else {
    bool has_const_writes = false;
    uint32_t component_write_count;
    if (result.is_standard_swizzle()) {
      component_write_count = 4;
    } else {
      component_write_count = 0;
      EmitSource(".");
      for (uint32_t i = 0; i < 4; ++i) {
        if (result.write_mask[i]) {
          if (result.components[i] == SwizzleSource::k0 ||
              result.components[i] == SwizzleSource::k1) {
            has_const_writes = true;
          }
          ++component_write_count;
          EmitSource("%c", GetCharForSwizzle(GetSwizzleFromComponentIndex(i)));
        }
      }
    }
    EmitSource(" = ");
    if (result.is_clamped) {
      EmitSource("saturate(");
    }
    if (has_const_writes) {
      if (component_write_count > 1) {
        EmitSource("float%u(", component_write_count);
      }
      bool has_written = false;
      for (uint32_t i = 0; i < 4; ++i) {
        if (result.write_mask[i]) {
          if (has_written) {
            EmitSource(", ");
          }
          has_written = true;
          switch (result.components[i]) {
            case SwizzleSource::k0:
              EmitSource("0.0");
              break;
            case SwizzleSource::k1:
              EmitSource("1.0");
              break;
            default:
              if (source_is_scalar) {
                EmitSource("xe_ps");
              } else {
                EmitSource("xe_pv.%c", GetCharForSwizzle(result.components[i]));
              }
              break;
          }
        }
      }
      if (component_write_count > 1) {
        EmitSource(")");
      }
    } else {
      if (source_is_scalar) {
        EmitSource("xe_ps");
        if (component_write_count > 1) {
          EmitSource(".x");
          if (component_write_count > 2) {
            EmitSource("x");
            if (component_write_count > 3) {
              EmitSource("x");
            }
          }
        }
      } else {
        EmitSource("xe_pv");
        if (!result.is_standard_swizzle()) {
          EmitSource(".");
          for (uint32_t i = 0; i < 4; ++i) {
            if (result.write_mask[i]) {
              EmitSource("%c", GetCharForSwizzle(result.components[i]));
            }
          }
        }
      }
    }
    if (result.is_clamped) {
      EmitSource(")");
    }
  }
  EmitSource(";\n");
}

void HlslShaderTranslator::ProcessVertexFetchInstruction(
      const ParsedVertexFetchInstruction& instr) {
  EmitSourceDepth("// ");
  instr.Disassemble(&source_inner_);

  if (instr.operand_count < 2 ||
      instr.operands[1].storage_source !=
      InstructionStorageSource::kVertexFetchConstant) {
    assert_always();
    return;
  }

  bool conditional_emitted = BeginPredicatedInstruction(
      instr.is_predicated, instr.predicate_condition);

  // Load the element from the shared memory as uints and swap.
  EmitLoadOperand(0, instr.operands[0]);
  uint32_t vfetch_index = instr.operands[1].storage_index;
  const char* load_swizzle;
  const char* load_function_suffix;
  switch (instr.attributes.data_format) {
    case VertexFormat::k_16_16_16_16:
    case VertexFormat::k_16_16_16_16_FLOAT:
    case VertexFormat::k_32_32:
    case VertexFormat::k_32_32_FLOAT:
      load_swizzle = ".xy";
      load_function_suffix = "2";
      break;
    case VertexFormat::k_32_32_32_FLOAT:
      load_swizzle = ".xyz";
      load_function_suffix = "3";
      break;
    case VertexFormat::k_32_32_32_32:
    case VertexFormat::k_32_32_32_32_FLOAT:
      load_swizzle = "";
      load_function_suffix = "4";
      break;
    default:
      load_swizzle = ".x";
      load_function_suffix = "";
      break;
  }
  EmitSourceDepth("xe_vertex_element%s = XeByteSwap(xe_shared_memory.Load%s(\n",
                  load_swizzle, load_function_suffix);
  EmitSourceDepth("    (xe_fetch[%uu].%c & 0x1FFFFFFCu)", vfetch_index >> 1,
                  (vfetch_index & 1) ? 'z' : 'x');
  if (instr.attributes.stride != 0) {
    EmitSource(" + uint(xe_src0.x) * %uu", instr.attributes.stride * 4);
  }
  if (instr.attributes.offset != 0) {
    EmitSource(" + %uu", instr.attributes.offset * 4);
  }
  EmitSource("),\n");
  EmitSourceDepth("    xe_fetch[%uu].%c);\n", vfetch_index >> 1,
                  (vfetch_index & 1) ? 'w' : 'y');

  // Convert to the target format.
  uint32_t component_count = 4;
  switch (instr.attributes.data_format) {
    case VertexFormat::k_8_8_8_8:
      EmitSourceDepth("xe_vertex_element = (xe_vertex_element.xxxx >>\n");
      EmitSourceDepth("    uint4(0u, 8u, 16u, 24u)) & 255u;\n");
      if (instr.attributes.is_signed) {
        EmitSourceDepth(
            "xe_pv = float4(int4(xe_vertex_element << 24u) >> 24);\n");
      } else {
        EmitSourceDepth("xe_pv = float4(xe_vertex_element);\n");
      }
      if (!instr.attributes.is_integer) {
        if (instr.attributes.is_signed) {
          EmitSourceDepth("xe_pv = max(xe_pv * (1.0 / 127.0), (-1.0).xxxx);\n");
        } else {
          EmitSourceDepth("xe_pv *= 1.0 / 255.0;\n");
        }
      }
      break;
    case VertexFormat::k_2_10_10_10:
      EmitSourceDepth("xe_vertex_element = (xe_vertex_element.xxxx >>\n");
      EmitSourceDepth(
          "    uint4(0u, 10u, 20u, 30u)) & uint4((1023u).xxx, 3u);\n");
      if (instr.attributes.is_signed) {
        EmitSourceDepth("xe_pv = float4(int4(xe_vertex_element <<\n");
        EmitSourceDepth("    uint4((22u).xxx, 30u)) >> int4((22).xxx, 30));\n");
      } else {
        EmitSourceDepth("xe_pv = float4(xe_vertex_element);\n");
      }
      if (!instr.attributes.is_integer) {
        if (instr.attributes.is_signed) {
          EmitSourceDepth("xe_pv = max(xe_pv * float4((1.0 / 511.0).xxx, 1.0), "
                          "(-1.0).xxxx);\n");
        } else {
          EmitSourceDepth("xe_pv *= float4((1.0 / 1023.0).xxx, 1.0 / 3.0);\n");
        }
      }
      break;
    case VertexFormat::k_10_11_11:
      component_count = 3;
      EmitSourceDepth("xe_vertex_element.xyz = (xe_vertex_element.xxx >>\n");
      EmitSourceDepth(
          "    uint3(0u, 11u, 22u)) & uint3(2047u, 2047u, 1023u);\n");
      if (instr.attributes.is_signed) {
        EmitSourceDepth("xe_pv.xyz = float3(int3(xe_vertex_element.xyz <<\n");
        EmitSourceDepth("    uint3(21u, 21u, 22u)) >> int3(21, 21, 22));\n");
      } else {
        EmitSourceDepth("xe_pv.xyz = float3(xe_vertex_element.xyz);\n");
      }
      if (!instr.attributes.is_integer) {
        if (instr.attributes.is_signed) {
          EmitSourceDepth("xe_pv.xyz = max(xe_pv.xyz *\n");
          EmitSourceDepth(
              "    float3((1.0 / 1023.0).xx, 1.0 / 511.0), (-1.0).xxx);\n");
        } else {
          EmitSourceDepth(
              "xe_pv.xyz *= float3((1.0 / 2047.0).xx, 1.0 / 1023.0);\n");
        }
      }
      EmitSourceDepth("xe_pv.w = 1.0;\n");
      break;
    case VertexFormat::k_11_11_10:
      component_count = 3;
      EmitSourceDepth("xe_vertex_element.xyz = (xe_vertex_element.xxx >>\n");
      EmitSourceDepth(
          "    uint3(0u, 10u, 21u)) & uint3(1023u, 2047u, 2047u);\n");
      if (instr.attributes.is_signed) {
        EmitSourceDepth("xe_pv.xyz = float3(int3(xe_vertex_element.xyz <<\n");
        EmitSourceDepth("    uint3(22u, 21u, 21u)) >> int3(22, 21, 21));\n");
      } else {
        EmitSourceDepth("xe_pv.xyz = float3(xe_vertex_element.xyz);\n");
      }
      if (!instr.attributes.is_integer) {
        if (instr.attributes.is_signed) {
          EmitSourceDepth("xe_pv.xyz = max(xe_pv.xyz *\n");
          EmitSourceDepth(
              "    float3(1.0 / 511.0, (1.0 / 1023.0).xx), (-1.0).xxx);\n");
        } else {
          EmitSourceDepth(
              "xe_pv.xyz *= float3(1.0 / 1023.0, (1.0 / 2047.0).xx);\n");
        }
      }
      EmitSourceDepth("xe_pv.w = 1.0;\n");
      break;
    case VertexFormat::k_16_16:
      component_count = 2;
      EmitSourceDepth("xe_vertex_element.xy = (xe_vertex_element.xx >>\n");
      EmitSourceDepth("    uint2(0u, 16u)) & 65535u;\n");
      if (instr.attributes.is_signed) {
        EmitSourceDepth(
            "xe_pv.xy = float2(int2(xe_vertex_element.xy << 16u) >> 16);\n");
      } else {
        EmitSourceDepth("xe_pv.xy = float2(xe_vertex_element.xy);\n");
      }
      if (!instr.attributes.is_integer) {
        if (instr.attributes.is_signed) {
          EmitSourceDepth(
              "xe_pv.xy = max(xe_pv.xy * (1.0 / 32767.0), (-1.0).xx);\n");
        } else {
          EmitSourceDepth("xe_pv.xy *= 1.0 / 65535.0;\n");
        }
      }
      EmitSourceDepth("xe_pv.zw = float2(0.0, 1.0);\n");
      break;
    case VertexFormat::k_16_16_16_16:
      EmitSourceDepth("xe_vertex_element = (xe_vertex_element.xxyy >>\n");
      EmitSourceDepth("    uint4(0u, 16u, 0u, 16u)) & 65535u;\n");
      if (instr.attributes.is_signed) {
        EmitSourceDepth(
            "xe_pv = float4(int4(xe_vertex_element << 16u) >> 16);\n");
      } else {
        EmitSourceDepth("xe_pv = float4(xe_vertex_element);\n");
      }
      if (!instr.attributes.is_integer) {
        if (instr.attributes.is_signed) {
          EmitSourceDepth(
              "xe_pv = max(xe_pv * (1.0 / 32767.0), (-1.0).xxxx);\n");
        } else {
          EmitSourceDepth("xe_pv *= 1.0 / 65535.0;\n");
        }
      }
      break;
    case VertexFormat::k_16_16_FLOAT:
      component_count = 2;
      EmitSourceDepth("xe_vertex_element.xy = (xe_vertex_element.xx >>\n");
      EmitSourceDepth("    uint2(0u, 16u)) & 65535u;\n");
      EmitSourceDepth("xe_pv.xy = f16tof32(xe_vertex_element.xy);\n");
      EmitSourceDepth("xe_pv.zw = float2(0.0, 1.0);\n");
      break;
    case VertexFormat::k_16_16_16_16_FLOAT:
      EmitSourceDepth("xe_vertex_element = (xe_vertex_element.xxyy >>\n");
      EmitSourceDepth("    uint4(0u, 16u, 0u, 16u)) & 65535u;\n");
      EmitSourceDepth("xe_pv = f16tof32(xe_vertex_element);\n");
      break;
    case VertexFormat::k_32:
      component_count = 1;
      if (instr.attributes.is_signed) {
        EmitSourceDepth("xe_pv.x = float(int(xe_vertex_element.x));\n");
      } else {
        EmitSourceDepth("xe_pv.x = float(xe_vertex_element.x);\n");
      }
      if (!instr.attributes.is_integer) {
        EmitSourceDepth("xe_pv.x *= asfloat(0x%Xu);\n",
                        instr.attributes.is_signed ? 0x30000000 : 0x2F800000);
      }
      EmitSourceDepth("xe_pv.yzw = float3(0.0, 0.0, 1.0);\n");
      break;
    case VertexFormat::k_32_32:
      component_count = 2;
      if (instr.attributes.is_signed) {
        EmitSourceDepth("xe_pv.xy = float2(int2(xe_vertex_element.xy));\n");
      } else {
        EmitSourceDepth("xe_pv.xy = float2(xe_vertex_element.xy);\n");
      }
      if (!instr.attributes.is_integer) {
        EmitSourceDepth("xe_pv.xy *= asfloat(0x%Xu);\n",
                        instr.attributes.is_signed ? 0x30000000 : 0x2F800000);
      }
      EmitSourceDepth("xe_pv.zw = float2(0.0, 1.0);\n");
      break;
    case VertexFormat::k_32_32_32_32:
      if (instr.attributes.is_signed) {
        EmitSourceDepth("xe_pv = float4(int4(xe_vertex_element));\n");
      } else {
        EmitSourceDepth("xe_pv = float4(xe_vertex_element);\n");
      }
      if (!instr.attributes.is_integer) {
        EmitSourceDepth("xe_pv *= asfloat(0x%Xu);\n",
                        instr.attributes.is_signed ? 0x30000000 : 0x2F800000);
      }
      break;
    case VertexFormat::k_32_FLOAT:
      component_count = 1;
      EmitSourceDepth("xe_pv.x = asfloat(xe_vertex_element.x);\n");
      EmitSourceDepth("xe_pv.yzw = float3(0.0, 0.0, 1.0);\n");
      break;
    case VertexFormat::k_32_32_FLOAT:
      component_count = 2;
      EmitSourceDepth("xe_pv.xy = asfloat(xe_vertex_element.xy);\n");
      EmitSourceDepth("xe_pv.zw = float2(0.0, 1.0);\n");
      break;
    case VertexFormat::k_32_32_32_32_FLOAT:
      EmitSourceDepth("xe_pv = asfloat(xe_vertex_element);\n");
      break;
    case VertexFormat::k_32_32_32_FLOAT:
      component_count = 3;
      EmitSourceDepth("xe_pv.xyz = asfloat(xe_vertex_element.xyz);\n");
      EmitSourceDepth("xe_pv.w = 1.0;\n");
      break;
  }
  // TODO(Triang3l): Check if the exponent bias needs to be added to unfetched
  // components as well (and check if W should actually be 1 in this case).
  if (instr.attributes.exp_adjust != 0) {
    EmitSourceDepth("xe_pv");
    if (component_count != 4) {
      EmitSource(".x");
      if (component_count >= 2) {
        EmitSource("y");
        if (component_count >= 3) {
          EmitSource("z");
        }
      }
    }
    EmitSource(" *= asfloat(0x%Xu);\n",
               0x3F800000 + (instr.attributes.exp_adjust << 23));
  }

  EmitStoreResult(instr.result, false);

  EndPredicatedInstruction(conditional_emitted);
}

uint32_t HlslShaderTranslator::AddTextureSRV(uint32_t fetch_constant,
                                             TextureDimension dimension) {
  if (dimension == TextureDimension::k1D) {
    // 1D textures are treated as 2D.
    dimension = TextureDimension::k2D;
  }
  for (uint32_t i = 0; i < texture_srv_count_; ++i) {
    const TextureSRV& srv = texture_srvs_[i];
    if (srv.fetch_constant == fetch_constant && srv.dimension == dimension) {
      return i;
    }
  }
  TextureSRV& new_srv = texture_srvs_[texture_srv_count_];
  new_srv.fetch_constant = fetch_constant;
  new_srv.dimension = dimension;
  return texture_srv_count_++;
}

uint32_t HlslShaderTranslator::AddSampler(uint32_t fetch_constant) {
  for (uint32_t i = 0; i < sampler_count_; ++i) {
    if (sampler_fetch_constants_[i] == fetch_constant) {
      return i;
    }
  }
  sampler_fetch_constants_[sampler_count_] = fetch_constant;
  return sampler_count_++;
}

void HlslShaderTranslator::ProcessTextureFetchInstruction(
      const ParsedTextureFetchInstruction& instr) {
  EmitSourceDepth("// ");
  instr.Disassemble(&source_inner_);

  bool conditional_emitted = BeginPredicatedInstruction(
      instr.is_predicated, instr.predicate_condition);

  for (size_t i = 0; i < instr.operand_count; ++i) {
    if (instr.operands[i].storage_source !=
        InstructionStorageSource::kTextureFetchConstant) {
      EmitLoadOperand(i, instr.operands[i]);
    }
  }
  bool store_result = true;

  uint32_t tfetch_index = instr.operands[1].storage_index;
  // Fetch constants are laid out like:
  // tf0[0] tf0[1] tf0[2] tf0[3]
  // tf0[4] tf0[5] tf1[0] tf1[1]
  // tf1[2] tf1[3] tf1[4] tf1[5]
  uint32_t tfetch_pair_offset = (tfetch_index >> 1) * 3;

  if (instr.opcode == FetchOpcode::kGetTextureGradients) {
    EmitSourceDepth("xe_pv = float4(ddx(xe_src0.xy), ddy(xe_src0.xy)).xzyw;\n");
    // Exponent bias is in dword 4 bits 22:26 and 27:31.
    char tfetch_grad_exp_component = (tfetch_index & 1) ? 'z' : 'x';
    EmitSourceDepth("xe_pv *= exp2(float(int(xe_fetch[%uu].%c%c <<\n",
                    tfetch_pair_offset + 1 + (tfetch_index & 1),
                    tfetch_grad_exp_component, tfetch_grad_exp_component);
    EmitSourceDepth("                        uint2(5u, 0u)) >> 27)).xyxy;\n");
  } else if (instr.opcode == FetchOpcode::kSetTextureLod) {
    EmitSourceDepth("xe_texture_lod = xe_src0.x;\n");
    store_result = false;
  } else if (instr.opcode == FetchOpcode::kSetTextureGradientsHorz) {
    EmitSourceDepth("xe_texture_grad_h = xe_src0.xyz;\n");
    store_result = false;
  } else if (instr.opcode == FetchOpcode::kSetTextureGradientsVert) {
    EmitSourceDepth("xe_texture_grad_v = xe_src0.xyz;\n");
    store_result = false;
  } else if (instr.opcode == FetchOpcode::kGetTextureBorderColorFrac) {
    EmitUnimplementedTranslationError();
    EmitSourceDepth("xe_pv = (0.0).xxxx;\n");
  } else {
    AddTextureSRV(tfetch_index, instr.dimension);
    if (instr.dimension == TextureDimension::k3D) {
      // 3D or 2D array is selected dynamically.
      AddTextureSRV(tfetch_index, TextureDimension::k2D);
    }
    // TODO(Triang3l): Filter and LOD bias overrides.
    AddSampler(tfetch_index);

    // Treat 1D and 2D textures as 2D arrays, also make unnormalized and, if
    // needed, apply the offset. Size is in dword 2.
    uint32_t tfetch_size_index = tfetch_pair_offset + (tfetch_index & 1) * 2;
    char tfetch_size_component = (tfetch_index & 1) ? 'x' : 'z';
    switch (instr.dimension) {
      case TextureDimension::k1D:
        EmitSourceDepth("xe_texture_coords = float3(xe_src0.x, 0.0, 0.0);\n");
        if (instr.attributes.unnormalized_coordinates) {
          if (instr.attributes.offset_x != 0.0f) {
            EmitSourceDepth("xe_texture_coords.x += %.1f;\n",
                            instr.attributes.offset_x);
          }
          EmitSourceDepth("xe_texture_coords.x /=\n");
          EmitSourceDepth("    float((xe_fetch[%uu].%c & 8191u) + 1u);\n",
                          tfetch_size_index, tfetch_size_component);
        } else if (instr.attributes.offset_x != 0.0f) {
          EmitSourceDepth("xe_texture_coords.x += %.1f /\n",
                          instr.attributes.offset_x);
          EmitSourceDepth("    float((xe_fetch[%uu].%c & 8191u) + 1u);\n",
                          tfetch_size_index, tfetch_size_component);
        }
        break;
      case TextureDimension::k2D:
      case TextureDimension::kCube:
        // Cubemap coordinates are similar to array texture coordinates on the
        // Xbox 360, not a 3D direction, so offset is applied on a plane.
        if (instr.dimension == TextureDimension::kCube) {
          EmitSourceDepth("xe_texture_coords = xe_src0.xyz;\n");
        } else {
          EmitSourceDepth("xe_texture_coords = float3(xe_src0.xy, 0.0);\n");
        }
        if (instr.attributes.unnormalized_coordinates) {
          if (instr.attributes.offset_x != 0.0f ||
              instr.attributes.offset_y != 0.0f) {
            EmitSourceDepth("xe_texture_coords.xy += float2(%.1f, %.1f);\n",
                            instr.attributes.offset_x,
                            instr.attributes.offset_y);
          }
          EmitSourceDepth("xe_texture_coords.xy /=\n");
          EmitSourceDepth("    float2(((xe_fetch[%uu].%c%c >>\n",
                          tfetch_size_index, tfetch_size_component,
                          tfetch_size_component);
          EmitSourceDepth("             uint2(0u, 13u)) & 8191u) + 1u);\n");
        } else if (instr.attributes.offset_x != 0.0f ||
                   instr.attributes.offset_y != 0.0f) {
          EmitSourceDepth("xe_texture_coords.xy += float2(%.1f, %.1f) /\n",
                          instr.attributes.offset_x, instr.attributes.offset_y);
          EmitSourceDepth("    float2(((xe_fetch[%uu].%c%c >>\n",
                          tfetch_size_index, tfetch_size_component,
                          tfetch_size_component);
          EmitSourceDepth("             uint2(0u, 13u)) & 8191u) + 1u);\n");
        }
        if (instr.dimension == TextureDimension::kCube) {
          // Convert to 3D direction.
          EmitSourceDepth(
              "xe_texture_coords = XeCubeTo3D(xe_texture_coords);\n");
          cube_used_ = true;
        }
        break;
      case TextureDimension::k3D:
        // Both 3D textures and 2D arrays have their Z coordinate normalized
        // according to the "Next-Generation Graphics Programming on Xbox 360"
        // presentation, however, on PC, array elements have unnormalized
        // indices. We first take the normalized coordinates, but then multiply
        // Z by the number of array slices.
        EmitSourceDepth(
            "xe_texture_is_3d = (xe_fetch[%uu].%c & 0x600u) == 0x400u;\n",
            tfetch_pair_offset + 1 + (tfetch_index & 1),
            (tfetch_index & 1) ? 'w' : 'y');
        EmitSourceDepth("xe_texture_coords = xe_src0.xyz;\n");
        if (instr.attributes.unnormalized_coordinates) {
          if (instr.attributes.offset_x != 0.0f ||
              instr.attributes.offset_y != 0.0f ||
              instr.attributes.offset_z != 0.0f) {
            EmitSourceDepth("xe_texture_coords += float3(%.1f, %.1f, %.1f);\n",
                            instr.attributes.offset_x,
                            instr.attributes.offset_y,
                            instr.attributes.offset_z);
          }
          EmitSourceDepth("xe_tfetch3d_size_bits = xe_texture_is_3d ?\n");
          EmitSourceDepth(
              "    uint4(0u, 11u, 22u, 2047u) : uint4(0u, 13u, 26u, 8191u);\n");
          EmitSourceDepth("xe_texture_coords /=\n");
          EmitSourceDepth("    float3(((xe_fetch[%uu].%c%c%c >>\n",
                          tfetch_size_index, tfetch_size_component,
                          tfetch_size_component, tfetch_size_component);
          EmitSourceDepth("             xe_tfetch3d_size_bits.xyz) &\n");
          EmitSourceDepth("            xe_tfetch3d_size_bits.w) + 1u);\n");
        } else if (instr.attributes.offset_x != 0.0f ||
                   instr.attributes.offset_y != 0.0f ||
                   instr.attributes.offset_z != 0.0f) {
          EmitSourceDepth("xe_tfetch3d_size_bits = xe_texture_is_3d ?\n");
          EmitSourceDepth(
              "    uint4(0u, 11u, 22u, 2047u) : uint4(0u, 13u, 26u, 8191u);\n");
          EmitSourceDepth("xe_texture_coords += float3(%.1f, %.1f, %.1f) /\n",
                          instr.attributes.offset_x, instr.attributes.offset_y,
                          instr.attributes.offset_z);
          EmitSourceDepth("    float3(((xe_fetch[%uu].%c%c%c >>\n",
                          tfetch_size_index, tfetch_size_component,
                          tfetch_size_component, tfetch_size_component);
          EmitSourceDepth("             xe_tfetch3d_size_bits.xyz) &\n");
          EmitSourceDepth("            xe_tfetch3d_size_bits.w) + 1u);\n");
        }
        // Unnormalize Z if sampling an array.
        EmitSourceDepth("if (!xe_texture_is_3d) {\n");
        EmitSourceDepth(
            "  xe_texture_coords.z *= float((xe_fetch[%uu].%c >> 26u) + 1u);\n",
            tfetch_size_index, tfetch_size_component);
        EmitSourceDepth("};\n");
        break;
      default:
        assert_unhandled_case(instr.dimension);
    }

    if (instr.opcode == FetchOpcode::kTextureFetch) {
      switch (instr.dimension) {
        case TextureDimension::k3D:
          EmitSourceDepth("[branch] if (xe_texture_is_3d) {\n");
          if (instr.attributes.use_register_lod) {
            EmitSourceDepth(
                "  xe_pv = xe_texture%u_3d.SampleLevel(xe_sampler%u,\n",
                tfetch_index, tfetch_index);
            EmitSourceDepth("      xe_texture_coords, xe_texture_lod);\n");
          } else if (instr.attributes.use_register_gradients) {
            EmitSourceDepth(
                "  xe_pv = xe_texture%u_3d.SampleGrad(xe_sampler%u,\n",
                tfetch_index, tfetch_index);
            EmitSourceDepth("      xe_texture_coords, xe_texture_grad_h,\n");
            EmitSourceDepth("      xe_texture_grad_v);\n");
          } else {
            EmitSourceDepth("  xe_pv = xe_texture%u_3d.Sample(xe_sampler%u,\n",
                            tfetch_index, tfetch_index);
            EmitSourceDepth("      xe_texture_coords);\n");
          }
          EmitSourceDepth("} else {\n");
          if (instr.attributes.use_register_lod) {
            EmitSourceDepth(
                "  xe_pv = xe_texture%u_2d.SampleLevel(xe_sampler%u,\n",
                tfetch_index, tfetch_index);
            EmitSourceDepth("      xe_texture_coords, xe_texture_lod);\n");
          } else if (instr.attributes.use_register_gradients) {
            EmitSourceDepth(
                "  xe_pv = xe_texture%u_2d.SampleGrad(xe_sampler%u,\n",
                tfetch_index, tfetch_index);
            EmitSourceDepth("      xe_texture_coords, xe_texture_grad_h.xy,\n");
            EmitSourceDepth("      xe_texture_grad_v.xy);\n");
          } else {
            EmitSourceDepth("  xe_pv = xe_texture%u_2d.Sample(xe_sampler%u,\n",
                            tfetch_index, tfetch_index);
            EmitSourceDepth("      xe_texture_coords);\n");
          }
          EmitSourceDepth("}\n");
          break;
        case TextureDimension::kCube:
          // TODO(Triang3l): Investigate how explicit gradients should work with
          // cubemaps (if they should work at all) because due to 2D->3D
          // coordinate conversion, you probably can't just sent the gradients
          // to SampleGrad.
          if (instr.attributes.use_register_lod) {
            EmitSourceDepth(
                "xe_pv = xe_texture%u_cube.SampleLevel(xe_sampler%u,\n",
                tfetch_index, tfetch_index);
            EmitSourceDepth("    xe_texture_coords, xe_texture_lod);\n");
          } else {
            EmitSourceDepth("xe_pv = xe_texture%u_cube.Sample(xe_sampler%u,\n",
                            tfetch_index, tfetch_index);
            EmitSourceDepth("    xe_texture_coords);\n");
          }
          break;
        default:
          if (instr.attributes.use_register_lod) {
            EmitSourceDepth(
                "xe_pv = xe_texture%u_2d.SampleLevel(xe_sampler%u,\n",
                tfetch_index, tfetch_index);
            EmitSourceDepth("    xe_texture_coords, xe_texture_lod);\n");
          } else if (instr.attributes.use_register_gradients) {
            EmitSourceDepth(
                "xe_pv = xe_texture%u_2d.SampleGrad(xe_sampler%u,\n",
                tfetch_index, tfetch_index);
            EmitSourceDepth("    xe_texture_coords, xe_texture_grad_h.xy,\n");
            EmitSourceDepth("    xe_texture_grad_v.xy);\n");
          } else {
            EmitSourceDepth("xe_pv = xe_texture%u_2d.Sample(xe_sampler%u,\n",
                            tfetch_index, tfetch_index);
            EmitSourceDepth("    xe_texture_coords);\n");
          }
          break;
      }
      // Apply exponent bias from dword 3.
      EmitSourceDepth(
          "xe_pv *= exp2(float(int(xe_fetch[%uu].%c << 13u) >> 26));\n",
          tfetch_pair_offset + (tfetch_index & 1) * 2,
          (tfetch_index & 1) ? 'y' : 'w');
    } else if (instr.opcode == FetchOpcode::kGetTextureComputedLod) {
      // TODO(Triang3l): Add FetchOpcode::kGetTextureComputedLod via
      // CalculateLevelOfDetail.
      EmitUnimplementedTranslationError();
      EmitSourceDepth("xe_pv = (0.0).xxxx;\n");
    } else if (instr.opcode == FetchOpcode::kGetTextureWeights) {
      // TODO(Triang3l): Add FetchOpcode::kGetTextureWeights.
      EmitUnimplementedTranslationError();
      EmitSourceDepth("xe_pv = (0.0).xxxx;\n");
    }
  }

  if (store_result) {
    EmitStoreResult(instr.result, false);
  }

  EndPredicatedInstruction(conditional_emitted);
}

void HlslShaderTranslator::ProcessVectorAluInstruction(
    const ParsedAluInstruction& instr) {
  bool conditional_emitted = BeginPredicatedInstruction(
      instr.is_predicated, instr.predicate_condition);

  for (size_t i = 0; i < instr.operand_count; ++i) {
    EmitLoadOperand(i, instr.operands[i]);
  }

  switch (instr.vector_opcode) {
    case AluVectorOpcode::kAdd:
      EmitSourceDepth("xe_pv = xe_src0 + xe_src1;\n");
      break;
    case AluVectorOpcode::kMul:
      EmitSourceDepth("xe_pv = xe_src0 * xe_src1;\n");
      break;
    case AluVectorOpcode::kMax:
      EmitSourceDepth("xe_pv = max(xe_src0, xe_src1);\n");
      break;
    case AluVectorOpcode::kSeq:
      EmitSourceDepth("xe_pv = float4(xe_src0 == xe_src1);\n");
      break;
    case AluVectorOpcode::kSgt:
      EmitSourceDepth("xe_pv = float4(xe_src0 > xe_src1);\n");
      break;
    case AluVectorOpcode::kSge:
      EmitSourceDepth("xe_pv = float4(xe_src0 >= xe_src1);\n");
      break;
    case AluVectorOpcode::kSne:
      EmitSourceDepth("xe_pv = float4(xe_src0 != xe_src1);\n");
      break;
    case AluVectorOpcode::kFrc:
      EmitSourceDepth("xe_pv = frac(xe_src0);\n");
      break;
    case AluVectorOpcode::kTrunc:
      EmitSourceDepth("xe_pv = trunc(xe_src0);\n");
      break;
    case AluVectorOpcode::kFloor:
      EmitSourceDepth("xe_pv = floor(xe_src0);\n");
      break;
    case AluVectorOpcode::kMad:
      EmitSourceDepth("xe_pv = xe_src0 * xe_src1 + xe_src2;\n");
      break;
    case AluVectorOpcode::kCndEq:
      EmitSourceDepth(
          "xe_pv = lerp(xe_src2, xe_src1, float4(xe_src0 == (0.0).xxxx));\n");
      break;
    case AluVectorOpcode::kCndGe:
      EmitSourceDepth(
          "xe_pv = lerp(xe_src2, xe_src1, float4(xe_src0 >= (0.0).xxxx));\n");
      break;
    case AluVectorOpcode::kCndGt:
      EmitSourceDepth(
          "xe_pv = lerp(xe_src2, xe_src1, float4(xe_src0 > (0.0).xxxx));\n");
      break;
    case AluVectorOpcode::kDp4:
      EmitSourceDepth("xe_pv = dot(xe_src0, xe_src1).xxxx;\n");
      break;
    case AluVectorOpcode::kDp3:
      EmitSourceDepth("xe_pv = dot(xe_src0.xyz, xe_src1.xyz).xxxx;\n");
      break;
    case AluVectorOpcode::kDp2Add:
      EmitSourceDepth(
          "xe_pv = (dot(xe_src0.xy, xe_src1.xy) + xe_src2.x).xxxx;\n");
      break;
    case AluVectorOpcode::kCube:
      // The operand index and the swizzle are correct!
      EmitSourceDepth("xe_pv = XeCubeTo2D(xe_src1.yxz);\n");
      cube_used_ = true;
      break;
    case AluVectorOpcode::kMax4:
      EmitSourceDepth("xe_pv.xy = max(xe_src0.xy, xe_src0.zw);\n");
      EmitSourceDepth("xe_pv.xxxx = max(xe_pv.x, xe_pv.y);\n");
      break;
    case AluVectorOpcode::kSetpEqPush:
      cf_exec_pred_ = false;
      EmitSourceDepth("xe_p0 = xe_src0.w == 0.0 && xe_src1.w == 0.0;\n");
      EmitSourceDepth("xe_pv = (xe_src0.x == 0.0 && xe_src1.x == 0.0 ? "
                      "0.0 : xe_src0.x + 1.0).xxxx;\n");
      break;
    case AluVectorOpcode::kSetpNePush:
      cf_exec_pred_ = false;
      EmitSourceDepth("xe_p0 = xe_src0.w == 0.0 && xe_src1.w != 0.0;\n");
      EmitSourceDepth("xe_pv = (xe_src0.x == 0.0 && xe_src1.x != 0.0 ? "
                      "0.0 : xe_src0.x + 1.0).xxxx;\n");
      break;
    case AluVectorOpcode::kSetpGtPush:
      cf_exec_pred_ = false;
      EmitSourceDepth("xe_p0 = xe_src0.w == 0.0 && xe_src1.w > 0.0;\n");
      EmitSourceDepth("xe_pv = (xe_src0.x == 0.0 && xe_src1.x > 0.0 ? "
                      "0.0 : xe_src0.x + 1.0).xxxx;\n");
      break;
    case AluVectorOpcode::kSetpGePush:
      cf_exec_pred_ = false;
      EmitSourceDepth("xe_p0 = xe_src0.w == 0.0 && xe_src1.w >= 0.0;\n");
      EmitSourceDepth("xe_pv = (xe_src0.x == 0.0 && xe_src1.x >= 0.0 ? "
                      "0.0 : xe_src0.x + 1.0).xxxx;\n");
      break;
    case AluVectorOpcode::kKillEq:
      EmitSourceDepth("xe_pv.xxxx = float(any(xe_src0 == xe_src1));\n");
      EmitSourceDepth("clip(-xe_pv.x);\n");
      break;
    case AluVectorOpcode::kKillGt:
      EmitSourceDepth("xe_pv.xxxx = float(any(xe_src0 > xe_src1));\n");
      EmitSourceDepth("clip(-xe_pv.x);\n");
      break;
    case AluVectorOpcode::kKillGe:
      EmitSourceDepth("xe_pv.xxxx = float(any(xe_src0 >= xe_src1));\n");
      EmitSourceDepth("clip(-xe_pv.x);\n");
      break;
    case AluVectorOpcode::kKillNe:
      EmitSourceDepth("xe_pv.xxxx = float(any(xe_src0 != xe_src1));\n");
      EmitSourceDepth("clip(-xe_pv.x);\n");
      break;
    case AluVectorOpcode::kDst:
      EmitSourceDepth("xe_pv.x = 1.0;\n");
      EmitSourceDepth("xe_pv.y = xe_src0.y * xe_src1.y;\n");
      EmitSourceDepth("xe_pv.z = xe_src0.z;\n");
      EmitSourceDepth("xe_pv.w = xe_src1.w;\n");
      break;
    case AluVectorOpcode::kMaxA:
      EmitSourceDepth("xe_a0 = clamp(int(round(xe_src0.w)), -256, 255);\n");
      EmitSourceDepth("xe_pv = max(xe_src0, xe_src1);\n");
      break;
  }

  EmitStoreResult(instr.result, false);

  EndPredicatedInstruction(conditional_emitted);
}

void HlslShaderTranslator::ProcessScalarAluInstruction(
    const ParsedAluInstruction& instr) {
  bool conditional_emitted = BeginPredicatedInstruction(
      instr.is_predicated, instr.predicate_condition);

  for (size_t i = 0; i < instr.operand_count; ++i) {
    EmitLoadOperand(i, instr.operands[i]);
  }

  switch (instr.scalar_opcode) {
    case AluScalarOpcode::kAdds:
      EmitSourceDepth("xe_ps = xe_src0.x + xe_src0.y;\n");
      break;
    case AluScalarOpcode::kAddsPrev:
      EmitSourceDepth("xe_ps += xe_src0.x;\n");
      break;
    case AluScalarOpcode::kMuls:
      EmitSourceDepth("xe_ps = xe_src0.x * xe_src0.y;\n");
      break;
    case AluScalarOpcode::kMulsPrev:
      EmitSourceDepth("xe_ps *= xe_src0.x;\n");
      break;
    case AluScalarOpcode::kMulsPrev2:
      EmitSourceDepth(
          "xe_ps = (xe_ps == -XE_FLT_MAX || (isinf(xe_ps) && xe_ps < 0.0)\n");
      EmitSourceDepth(
          "    || isnan(xe_ps) || xe_src0.y <= 0.0 || isnan(xe_src0.y)) ?\n");
      EmitSourceDepth("    -XE_FLT_MAX : xe_src0.x * xe_ps;\n");
      break;
    case AluScalarOpcode::kMaxs:
      EmitSourceDepth("xe_ps = max(xe_src0.x, xe_src0.y);\n");
      break;
    case AluScalarOpcode::kMins:
      EmitSourceDepth("xe_ps = max(xe_src0.x, xe_src0.y);\n");
      break;
    case AluScalarOpcode::kSeqs:
      EmitSourceDepth("xe_ps = float(xe_src0.x == 0.0);\n");
      break;
    case AluScalarOpcode::kSgts:
      EmitSourceDepth("xe_ps = float(xe_src0.x > 0.0);\n");
      break;
    case AluScalarOpcode::kSges:
      EmitSourceDepth("xe_ps = float(xe_src0.x >= 0.0);\n");
      break;
    case AluScalarOpcode::kSnes:
      EmitSourceDepth("xe_ps = float(xe_src0.x != 0.0);\n");
      break;
    case AluScalarOpcode::kFrcs:
      EmitSourceDepth("xe_ps = frac(xe_src0.x);\n");
      break;
    case AluScalarOpcode::kTruncs:
      EmitSourceDepth("xe_ps = trunc(xe_src0.x);\n");
      break;
    case AluScalarOpcode::kFloors:
      EmitSourceDepth("xe_ps = floor(xe_src0.x);\n");
      break;
    case AluScalarOpcode::kExp:
      EmitSourceDepth("xe_ps = exp2(xe_src0.x);\n");
      break;
    case AluScalarOpcode::kLogc:
      EmitSourceDepth("xe_ps = log2(xe_src0.x);\n");
      EmitSourceDepth(
          "xe_ps = (isinf(xe_ps) && xe_ps < 0.0) ? -XE_FLT_MAX : ps;\n");
      break;
    case AluScalarOpcode::kLog:
      EmitSourceDepth("xe_ps = log2(xe_src0.x);\n");
      break;
    case AluScalarOpcode::kRcpc:
      EmitSourceDepth(
          "xe_ps = clamp(rcp(xe_src0.x), -XE_FLT_MAX, XE_FLT_MAX);\n");
      break;
    case AluScalarOpcode::kRcpf:
      EmitSourceDepth("xe_ps = rcp(xe_src0.x);\n");
      EmitSourceDepth("xe_ps *= float(!isinf(xe_ps));\n");
      break;
    case AluScalarOpcode::kRcp:
      EmitSourceDepth("xe_ps = rcp(xe_src0.x);\n");
      break;
    case AluScalarOpcode::kRsqc:
      EmitSourceDepth(
          "xe_ps = clamp(rsqrt(xe_src0.x), -XE_FLT_MAX, XE_FLT_MAX);\n");
      break;
    case AluScalarOpcode::kRsqf:
      EmitSourceDepth("xe_ps = rsqrt(xe_src0.x);\n");
      EmitSourceDepth("xe_ps *= float(!isinf(xe_ps));\n");
      break;
    case AluScalarOpcode::kRsq:
      EmitSourceDepth("xe_ps = rsqrt(xe_src0.x);\n");
      break;
    case AluScalarOpcode::kMaxAs:
      EmitSourceDepth("xe_a0 = clamp(int(round(xe_src0.x)), -256, 255);\n");
      EmitSourceDepth("xe_ps = max(xe_src0.x, xe_src0.y);\n");
      break;
    case AluScalarOpcode::kMaxAsf:
      EmitSourceDepth("xe_a0 = clamp(int(floor(xe_src0.x)), -256, 255);\n");
      EmitSourceDepth("xe_ps = max(xe_src0.x, xe_src0.y);\n");
      break;
    case AluScalarOpcode::kSubs:
      EmitSourceDepth("xe_ps = xe_src0.x - xe_src0.y;\n");
      break;
    case AluScalarOpcode::kSubsPrev:
      EmitSourceDepth("xe_ps = xe_src0.x - xe_ps;\n");
      break;
    case AluScalarOpcode::kSetpEq:
      cf_exec_pred_ = false;
      EmitSourceDepth("xe_p0 = xe_src0.x == 0.0;\n");
      EmitSourceDepth("xe_ps = float(!xe_p0);\n");
      break;
    case AluScalarOpcode::kSetpNe:
      cf_exec_pred_ = false;
      EmitSourceDepth("xe_p0 = xe_src0.x != 0.0;\n");
      EmitSourceDepth("xe_ps = float(!xe_p0);\n");
      break;
    case AluScalarOpcode::kSetpGt:
      cf_exec_pred_ = false;
      EmitSourceDepth("xe_p0 = xe_src0.x > 0.0;\n");
      EmitSourceDepth("xe_ps = float(!xe_p0);\n");
      break;
    case AluScalarOpcode::kSetpGe:
      cf_exec_pred_ = false;
      EmitSourceDepth("xe_p0 = xe_src0.x >= 0.0;\n");
      EmitSourceDepth("xe_ps = float(!xe_p0);\n");
      break;
    case AluScalarOpcode::kSetpInv:
      cf_exec_pred_ = false;
      EmitSourceDepth("xe_p0 = xe_src0.x == 1.0;\n");
      EmitSourceDepth(
          "xe_ps = float(!xe_p0) * (xe_src0.x == 0.0 ? 1.0 : xe_src0.x);\n");
      break;
    case AluScalarOpcode::kSetpPop:
      cf_exec_pred_ = false;
      EmitSourceDepth("xe_ps = max(xe_src0.x - 1.0, 0.0);\n");
      EmitSourceDepth("xe_p0 = xe_ps == 0.0;\n");
      break;
    case AluScalarOpcode::kSetpClr:
      cf_exec_pred_ = false;
      EmitSourceDepth("xe_ps = false;\n");
      EmitSourceDepth("xe_p0 = XE_FLT_MAX;\n");
      break;
    case AluScalarOpcode::kSetpRstr:
      cf_exec_pred_ = false;
      EmitSourceDepth("xe_p0 = src0.x == 0.0;\n");
      EmitSourceDepth("xe_ps = src0.x;\n");
      break;
    case AluScalarOpcode::kKillsEq:
      EmitSourceDepth("xe_ps = float(xe_src0.x == 0.0);\n");
      EmitSourceDepth("clip(-xe_ps);\n");
      break;
    case AluScalarOpcode::kKillsGt:
      EmitSourceDepth("xe_ps = float(xe_src0.x > 0.0);\n");
      EmitSourceDepth("clip(-xe_ps);\n");
      break;
    case AluScalarOpcode::kKillsGe:
      EmitSourceDepth("xe_ps = float(xe_src0.x >= 0.0);\n");
      EmitSourceDepth("clip(-xe_ps);\n");
      break;
    case AluScalarOpcode::kKillsNe:
      EmitSourceDepth("xe_ps = float(xe_src0.x != 0.0);\n");
      EmitSourceDepth("clip(-xe_ps);\n");
      break;
    case AluScalarOpcode::kKillsOne:
      EmitSourceDepth("xe_ps = float(xe_src0.x == 1.0);\n");
      EmitSourceDepth("clip(-xe_ps);\n");
      break;
    case AluScalarOpcode::kSqrt:
      EmitSourceDepth("xe_ps = sqrt(xe_src0.x);\n");
      break;
    case AluScalarOpcode::kMulsc0:
    case AluScalarOpcode::kMulsc1:
      EmitSourceDepth("xe_ps = xe_src0.x * xe_src1.x;\n");
      break;
    case AluScalarOpcode::kAddsc0:
    case AluScalarOpcode::kAddsc1:
      EmitSourceDepth("xe_ps = xe_src0.x + xe_src1.x;\n");
      break;
    case AluScalarOpcode::kSubsc0:
    case AluScalarOpcode::kSubsc1:
      EmitSourceDepth("xe_ps = xe_src0.x - xe_src1.x;\n");
      break;
    case AluScalarOpcode::kSin:
      EmitSourceDepth("xe_ps = sin(xe_src0.x);\n");
      break;
    case AluScalarOpcode::kCos:
      EmitSourceDepth("xe_ps = cos(xe_src0.x);\n");
      break;
    case AluScalarOpcode::kRetainPrev:
      break;
  }

  EmitStoreResult(instr.result, true);

  EndPredicatedInstruction(conditional_emitted);
}

void HlslShaderTranslator::ProcessAluInstruction(
    const ParsedAluInstruction& instr) {
  EmitSourceDepth("// ");
  instr.Disassemble(&source_inner_);

  switch (instr.type) {
    case ParsedAluInstruction::Type::kNop:
      break;
    case ParsedAluInstruction::Type::kVector:
      ProcessVectorAluInstruction(instr);
      break;
    case ParsedAluInstruction::Type::kScalar:
      ProcessScalarAluInstruction(instr);
      break;
  }
}

}  // namespace gpu
}  // namespace xe
