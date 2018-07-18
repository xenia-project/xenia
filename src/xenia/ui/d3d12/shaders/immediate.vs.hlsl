float2 scale : register(b0);

struct vs_input {
  float2 pos : POSITION;
  float2 uv : TEXCOORD;
  float4 color : COLOR;
};

struct vs_output {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
  float4 color : TEXCOORD1;
};

vs_output main(vs_input input) {
  vs_output output;
  output.pos = float4(scale * input.pos, 0.0, 1.0);
  output.uv = input.uv;
  output.color = input.color;
  return output;
}
