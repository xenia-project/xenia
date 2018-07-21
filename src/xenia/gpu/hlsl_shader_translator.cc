/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/hlsl_shader_translator.h"

namespace xe {
namespace gpu {
using namespace ucode;

constexpr uint32_t kMaxInterpolators = 16;

#define EmitSource(...) source_.AppendFormat(__VA_ARGS__)
#define EmitSourceDepth(...)     \
  source_.Append("  ");          \
  source_.Append(depth_prefix_); \
  source_.AppendFormat(__VA_ARGS__)

HlslShaderTranslator::HlslShaderTranslator() {}
HlslShaderTranslator::~HlslShaderTranslator() = default;

void HlslShaderTranslator::Reset() {
  ShaderTranslator::Reset();
  depth_ = 0;
  depth_prefix[0] = 0;
  source_.Reset();
  srv_bindings_.clear();
  sampler_count_ = 0;
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
  // Common things.
  EmitSource(
     "cbuffer xe_system_constants : register(b0) {\n"
     "  float2 xe_viewport_inv_scale;\n"
     "}\n"
     "\n"
     "struct XeFloatConstantPage {\n"
     "  float4 c[16];\n"
     "}\n"
     "ConstantBuffer<XeFloatConstantPage> "
     "xe_float_constants[16] : register(b1);\n"
     "\n"
     "cbuffer xe_loop_bool_constants : register(b17) {\n"
     "  uint xe_bool_constants[8];\n"
     "  uint xe_loop_constants[32];\n"
     "}\n"
     "\n");

  if (is_vertex_shader()) {
    // Vertex fetching, output and prologue.
    // Endian register (2nd word of the fetch constant) is 00 for no swap, 01
    // for 8-in-16, 10 for 8-in-32 (a combination of 8-in-16 and 16-in-32), and
    // 11 for 16-in-32. This means we can check bits 0 ^ 1 to see if we need to
    // do a 8-in-16 swap, and bit 1 to see if a 16-in-32 swap is needed.
    EmitSource(
        "cbuffer xe_vertex_fetch_constants : register(b18) {\n"
        "  uint2 xe_vertex_fetch[96];\n"
        "}\n"
        "\n"
        "ByteAddressBuffer xe_virtual_memory : register(t0, space1);\n"
        "\n"
        "#define XE_SWAP_OVERLOAD(XeSwapType) \\\n"
        "XeSwapType XeSwap(XeSwapType v, uint endian) { \\\n"
        "  [flatten] if (((endian ^ (endian >> 1u)) & 1u) != 0u) { \\\n"
        "    v = ((v & 0x00FF00FFu) << 8u) | ((v & 0xFF00FF00u) >> 8u); \\\n"
        "  } \\\n"
        "  [flatten] if ((endian & 2u) != 0u) { \\\n"
        "    v = (v << 16u) | (v >> 16u); \\\n"
        "  } \\\n"
        "  return v; \\\n"
        "}\n"
        "XE_SWAP_OVERLOAD(uint)\n"
        "XE_SWAP_OVERLOAD(uint2)\n"
        "XE_SWAP_OVERLOAD(uint3)\n"
        "XE_SWAP_OVERLOAD(uint4)\n"
        "\n"
        "struct XeVertexShaderOutput {\n"
        "  float4 position : SV_Position;\n"
        "  float4 interpolators[%u] : TEXCOORD;\n"
        "  float4 point_size : PSIZE;\n"
        "}\n"
        "\n"
        "XeVertexShaderOutput main(uint xe_vertex_id : SV_VertexID) {\n"
        "  float4 xe_r[%u];\n"
        "  xe_r[0].r = float(xe_vertex_id);\n"
        "  XeVertexShaderOutput xe_output;\n",
        kMaxInterpolators, register_count());
    // TODO(Triang3l): Reset interpolators to zero if really needed.
  } else if (is_pixel_shader()) {
    // Pixel shader inputs, outputs and prologue.
    // If the shader writes to depth, it needs to define
    // XE_PIXEL_SHADER_WRITES_DEPTH in the beginning of the final output.
    EmitSource(
        "struct XePixelShaderInput {\n"
        "  float4 position : SV_Position;\n"
        "  float4 interpolators[%u] : TEXCOORD;\n"
        "}\n"
        "\n"
        "struct XePixelShaderOutput {\n"
        "  float4 colors[4] : SV_Target;\n"
        "  #ifdef XE_PIXEL_SHADER_WRITES_DEPTH\n"
        "  float depth : SV_Depth;\n"
        "  #endif\n"
        "}\n"
        "\n"
        "XePixelShaderOutput main(XePixelShaderInput xe_input) {\n"
        "  float4 xe_r[%u];\n"
        "  XePixelShaderOutput xe_output;\n",
        kMaxInterpolators, std::max(register_count(), kMaxInterpolators));
    // No need to write zero to every output because in case an output is
    // completely unused, writing to that render target will be disabled in the
    // blending state (in Halo 3, one important render target is destroyed by a
    // shader not writing to one of the outputs otherwise).
    // TODO(Triang3l): ps_param_gen.
  }

  EmitSource(
      // Predicate temp, clause-local.
      "  bool xe_p0 = false;\n"
      // Address register when using absolute addressing.
      "  int xe_a0 = 0;\n"
      // Loop index stack - .x is the active loop, shifted right to yzw on push.
      "  int4 xe_aL = int4(0, 0, 0, 0);\n"
      // Loop counter stack, .x is the active loop.
      // Represents number of times remaining to loop.
      "  int4 xe_loop_count = int4(0, 0, 0, 0);\n"
      // Previous vector result (used as a scratch).
      "  float4 xe_pv;\n"
      // Previous scalar result (used for RETAIN_PREV).
      "  float xe_ps;\n"
      // Master loop and switch for flow control.
      "  uint xe_pc = 0u;\n"
      "\n"
      "  do {\n";
      "    switch (xe_pc) {\n"
      "    case 0u:\n");

  // Main function level (1).
  Indent();
  // Do level (2)
  Indent();
}

uint32_t HlslShaderTranslator::AddSRVBinding(SRVType type,
                                             uint32_t fetch_constant) {
  for (uint32_t i = 0; i < srv_bindings_.size(); ++i) {
    const SRVBinding& binding = srv_bindings_[i];
    if (binding.type == type && binding.fetch_constant == fetch_constant) {
      return i;
    }
  }
  SRVBinding new_binding;
  new_binding.type = type;
  new_binding.fetch_constant = fetch_constant;
  srv_bindings_.push_back(new_binding);
  return uint32_t(srv_bindings_.size() - 1);
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

}  // namespace gpu
}  // namespace xe
