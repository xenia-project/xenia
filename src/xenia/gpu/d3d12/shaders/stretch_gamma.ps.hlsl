Texture2D<float3> xe_texture : register(t0);
Texture1D<float3> xe_gamma_ramp : register(t1);
SamplerState xe_sampler_linear_clamp : register(s0);
cbuffer XeStretchGammaRootConstants : register(b0) {
  float xe_gamma_ramp_inv_size;
};

float4 main(float2 xe_texcoord : TEXCOORD) : SV_Target {
  float3 color =
      xe_texture.SampleLevel(xe_sampler_linear_clamp, xe_texcoord, 0.0f);
  // The center of the first texel of the LUT contains the value for 0, and the
  // center of the last texel contains the value for 1.
  color =
      color * (1.0f - xe_gamma_ramp_inv_size) + (0.5 * xe_gamma_ramp_inv_size);
  color.r = xe_gamma_ramp.SampleLevel(xe_sampler_linear_clamp, color.r, 0.0f).r;
  color.g = xe_gamma_ramp.SampleLevel(xe_sampler_linear_clamp, color.g, 0.0f).g;
  color.b = xe_gamma_ramp.SampleLevel(xe_sampler_linear_clamp, color.b, 0.0f).b;
  // Force alpha to 1 to make sure the surface won't be translucent.
  return float4(color, 1.0f);
}
