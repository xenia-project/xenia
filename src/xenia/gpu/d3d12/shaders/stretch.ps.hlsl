Texture2D<float4> xe_texture : register(t0);
SamplerState xe_sampler : register(s0);
float4 main(float2 xe_texcoord : TEXCOORD) : SV_Target {
  return xe_texture.SampleLevel(xe_sampler, xe_texcoord, 0.0f);
}
