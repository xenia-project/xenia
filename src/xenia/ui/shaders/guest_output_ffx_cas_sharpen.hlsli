#if XE_GUEST_OUTPUT_DITHER
  #include "dither_8bpc.xesli"
#endif  // XE_GUEST_OUTPUT_DITHER

cbuffer XeCasSharpenConstants : register(b0) {
  int2 xe_cas_output_offset;
  // CasSetup const1.x.
  float xe_cas_sharpness_post_setup;
};

Texture2D<float3> xe_texture : register(t0);

#define A_GPU 1
#define A_HLSL 1
#include "../../../../third_party/FidelityFX-CAS/ffx-cas/ffx_a.h"
float3 CasLoad(int2 p) {
  return xe_texture.Load(int3(p, 0)).rgb;
}
void CasInput(inout float r, inout float g, inout float b) {
  // Linear conversion approximation as recommended in the CAS presentation.
  r *= r;
  g *= g;
  b *= b;
}
#include "../../../../third_party/FidelityFX-CAS/ffx-cas/ffx_cas.h"

float4 main(float4 xe_position : SV_Position) : SV_Target {
  uint2 pixel_coord = uint2(int2(xe_position.xy) - xe_cas_output_offset);
  // CasSetup with smaller root signature usage.
  uint4 cas_const_0 = asuint(float4(1.0f, 1.0f, 0.0f, 0.0f));
  uint4 cas_const_1 =
      uint4(asuint(xe_cas_sharpness_post_setup),
            f32tof16(xe_cas_sharpness_post_setup), asuint(8.0f), 0u);
  float3 cas_color;
  CasFilter(cas_color.r, cas_color.g, cas_color.b, pixel_coord, cas_const_0,
            cas_const_1, true);
  // Linear conversion approximation as recommended in the CAS presentation.
  cas_color = sqrt(cas_color);
  #if XE_GUEST_OUTPUT_DITHER
    // Not clamping because a normalized format is explicitly requested on DXGI.
    cas_color += XeDitherOffset8bpc(pixel_coord);
  #endif  // XE_GUEST_OUTPUT_DITHER
  return float4(cas_color, 1.0f);
}
