Texture2D<float4> immediate_texture : register(t0);
SamplerState immediate_sampler : register(s0);
bool restrict_texture_samples : register(b0);

struct ps_input {
  float2 uv : TEXCOORD0;
  float4 color : TEXCOORD1;
};

float4 main(ps_input input) : SV_Target {
  float4 output = input.color;
  if (!restrict_texture_samples || input.uv.x <= 1.0) {
    output *= immediate_texture.Sample(immediate_sampler, input.uv);
  }
  return output;
}
