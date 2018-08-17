Texture2D<float4> xe_immediate_texture : register(t0);
SamplerState xe_immediate_sampler : register(s0);
bool xe_restrict_texture_samples : register(b0);

struct XePixelShaderInput {
  float2 texcoord : TEXCOORD0;
  float4 color : TEXCOORD1;
};

float4 main(XePixelShaderInput input) : SV_Target {
  float4 output = input.color;
  if (!xe_restrict_texture_samples || input.texcoord.x <= 1.0) {
    output *= xe_immediate_texture.Sample(xe_immediate_sampler, input.texcoord);
  }
  return output;
}
