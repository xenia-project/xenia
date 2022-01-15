Texture2D<float4> xe_immediate_texture : register(t0);
SamplerState xe_immediate_sampler : register(s0);

struct XePixelShaderInput {
  float2 texcoord : TEXCOORD0;
  float4 color : TEXCOORD1;
};

float4 main(XePixelShaderInput input) : SV_Target {
  return input.color * xe_immediate_texture.SampleLevel(xe_immediate_sampler,
                                                        input.texcoord, 0.0f);
}
