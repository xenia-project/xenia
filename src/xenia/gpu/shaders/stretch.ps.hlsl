Texture2D<float3> xe_texture : register(t0);
SamplerState xe_sampler_linear_clamp : register(s0);

float4 main(float2 xe_texcoord : TEXCOORD) : SV_Target {
  // Force alpha to 1 to make sure the surface won't be translucent.
  return float4(
      xe_texture.SampleLevel(xe_sampler_linear_clamp, xe_texcoord, 0.0f), 1.0f);
}
