float2 xe_viewport_inv_size : register(b0);

struct XeVertexShaderInput {
  float2 position : POSITION;
  float2 texcoord : TEXCOORD;
  float4 color : COLOR;
};

struct XeVertexShaderOutput {
  float2 texcoord : TEXCOORD0;
  float4 color : TEXCOORD1;
  float4 position : SV_Position;
};

XeVertexShaderOutput main(XeVertexShaderInput input) {
  XeVertexShaderOutput output;
  output.position = float4(
      input.position * xe_viewport_inv_size * float2(2.0, -2.0) +
      float2(-1.0, 1.0), 0.0, 1.0);
  output.texcoord = input.texcoord;
  output.color = input.color;
  return output;
}
