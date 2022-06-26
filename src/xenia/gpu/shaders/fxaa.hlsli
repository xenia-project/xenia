// Inputs:
// - FXAA_QUALITY__PRESET
// - FXAA_QUALITY__SUBPIX
// - FXAA_QUALITY__EDGE_THRESHOLD
// - FXAA_QUALITY__EDGE_THRESHOLD_MIN

// "use of potentially uninitialized variable" in FxaaPixelShader due to the
// early exit with a `return` in the beginning of the function, caused by a bug
// in FXC (the warning is common for early exiting in HLSL in general).
#pragma warning(disable: 4000)

cbuffer XeApplyGammaRampConstants : register(b0) {
  uint2 xe_fxaa_size;
  float2 xe_fxaa_size_inv;
};

RWTexture2D<unorm float4> xe_fxaa_dest : register(u0);
Texture2D<float4> xe_fxaa_source : register(t0);
SamplerState xe_sampler_linear_clamp : register(s0);

#define FXAA_PC 1
#define FXAA_HLSL_5 1
#include "../../../../third_party/fxaa/FXAA3_11.h"

[numthreads(16, 8, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  [branch] if (any(xe_thread_id.xy >= xe_fxaa_size)) {
    return;
  }
  FxaaTex fxaa_texture;
  fxaa_texture.smpl = xe_sampler_linear_clamp;
  fxaa_texture.tex = xe_fxaa_source;
  // Force alpha to 1 to simplify calculations, won't need it anymore anyway.
  xe_fxaa_dest[xe_thread_id.xy] =
      float4(
          FxaaPixelShader(
              (float2(xe_thread_id.xy) + 0.5) * xe_fxaa_size_inv,
              (float2(xe_thread_id.xy).xyxy + float2(0.0, 1.0).xxyy) *
                  xe_fxaa_size_inv.xyxy,
              fxaa_texture, fxaa_texture, fxaa_texture, xe_fxaa_size_inv,
              float2(-0.5, 0.5).xxyy * xe_fxaa_size_inv.xyxy,
              float2(-2.0, 2.0).xxyy * xe_fxaa_size_inv.xyxy,
              float2(8.0, -4.0).xxyy * xe_fxaa_size_inv.xyxy,
              FXAA_QUALITY__SUBPIX, FXAA_QUALITY__EDGE_THRESHOLD,
              FXAA_QUALITY__EDGE_THRESHOLD_MIN, 8.0, 0.125, 0.05,
              float4(1.0, -1.0, 0.25, -0.25)).rgb,
          1.0);
}
