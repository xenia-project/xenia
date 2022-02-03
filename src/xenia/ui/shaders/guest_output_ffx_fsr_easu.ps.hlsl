cbuffer XeFsrEasuConstants : register(b0) {
  float2 xe_fsr_easu_input_output_size_ratio;
  float2 xe_fsr_easu_input_size_inv;
};

Texture2D<float3> xe_texture : register(t0);
SamplerState xe_sampler_linear_clamp : register(s0);

#define A_GPU 1
#define A_HLSL 1
#include "../../../../third_party/FidelityFX-FSR/ffx-fsr/ffx_a.h"
#define FSR_EASU_F 1
float4 FsrEasuRF(float2 p) {
  return xe_texture.GatherRed(xe_sampler_linear_clamp, p);
}
float4 FsrEasuGF(float2 p) {
  return xe_texture.GatherGreen(xe_sampler_linear_clamp, p);
}
float4 FsrEasuBF(float2 p) {
  return xe_texture.GatherBlue(xe_sampler_linear_clamp, p);
}
#include "../../../../third_party/FidelityFX-FSR/ffx-fsr/ffx_fsr1.h"

float4 main(float4 xe_position : SV_Position) : SV_Target {
  // FsrEasuCon with smaller root signature usage.
  uint4 easu_const_0 =
      uint4(asuint(xe_fsr_easu_input_output_size_ratio),
            asuint(0.5f * xe_fsr_easu_input_output_size_ratio - 0.5f));
  uint4 easu_const_1 =
      asuint(float4(1.0f, 1.0f, 1.0f, -1.0f) * xe_fsr_easu_input_size_inv.xyxy);
  uint4 easu_const_2 =
      asuint(float4(-1.0f, 2.0f, 1.0f, 2.0f) * xe_fsr_easu_input_size_inv.xyxy);
  uint4 easu_const_3 =
      uint4(asuint(0.0f), asuint(4.0f * xe_fsr_easu_input_size_inv.y), 0u, 0u);
  float3 easu_color;
  FsrEasuF(easu_color, uint2(xe_position.xy), easu_const_0, easu_const_1,
           easu_const_2, easu_const_3);
  return float4(easu_color, 1.0f);
}
