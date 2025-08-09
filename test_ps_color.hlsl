// Ultra-simple pixel shader
struct PS_INPUT {
    float4 position : SV_Position;
    float4 color : COLOR0;
};

float4 main(PS_INPUT input) : SV_Target0 {
    return input.color;
}
