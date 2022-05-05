// float XeApplyGammaGetAlpha(float3 color) needs to be specified externally.

cbuffer XeApplyGammaRampConstants : register(b0) {
  uint2 xe_apply_gamma_size;
};
RWTexture2D<unorm float4> xe_apply_gamma_dest : register(u0);
Texture2D<float3> xe_apply_gamma_source : register(t0);
Buffer<float3> xe_apply_gamma_ramp : register(t1);

[numthreads(8, 8, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  [branch] if (any(xe_thread_id.xy >= xe_apply_gamma_size)) {
    return;
  }
  // UNORM conversion according to the Direct3D 10+ rules.
  uint3 input = uint3(xe_apply_gamma_source[xe_thread_id.xy] * 255.0f + 0.5f);
  // The ramp has blue in bits 0:9, green in 10:19, red in 20:29 - BGR passed as
  // an R10G10B10A2 buffer.
  float3 output = float3(xe_apply_gamma_ramp[input.r].b,
                         xe_apply_gamma_ramp[input.g].g,
                         xe_apply_gamma_ramp[input.b].r);
  xe_apply_gamma_dest[xe_thread_id.xy] =
      float4(output, XeApplyGammaGetAlpha(output));
}
