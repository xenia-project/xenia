#if XE_GUEST_OUTPUT_DITHER
  #include "dither_8bpc.xesli"
#endif  // XE_GUEST_OUTPUT_DITHER

cbuffer XeFsrRcasConstants : register(b0) {
  int2 xe_fsr_rcas_output_offset;
  // FsrRcasCon const0.x.
  float xe_fsr_rcas_sharpness_post_setup;
};

Texture2D<float3> xe_texture : register(t0);

#define A_GPU 1
#define A_HLSL 1
#include "../../../../third_party/FidelityFX-FSR/ffx-fsr/ffx_a.h"
#define FSR_RCAS_F 1
float4 FsrRcasLoadF(int2 p) {
  return float4(xe_texture.Load(int3(p, 0)).rgb, 1.0f);
}
void FsrRcasInputF(inout float r, inout float g, inout float b) {}
#include "../../../../third_party/FidelityFX-FSR/ffx-fsr/ffx_fsr1.h"

float4 main(float4 xe_position : SV_Position) : SV_Target {
  uint2 pixel_coord = uint2(int2(xe_position.xy) - xe_fsr_rcas_output_offset);
  // FsrRcasCon with smaller root signature usage.
  uint4 rcas_const =
      uint4(asuint(xe_fsr_rcas_sharpness_post_setup),
            f32tof16(xe_fsr_rcas_sharpness_post_setup) * 0x00010001u, 0u, 0u);
  float3 rcas_color;
  FsrRcasF(rcas_color.r, rcas_color.g, rcas_color.b, pixel_coord, rcas_const);
  #if XE_GUEST_OUTPUT_DITHER
    // Not clamping because a normalized format is explicitly requested on DXGI.
    rcas_color += XeDitherOffset8bpc(pixel_coord);
  #endif  // XE_GUEST_OUTPUT_DITHER
  return float4(rcas_color, 1.0f);
}
