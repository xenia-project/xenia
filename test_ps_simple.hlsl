// Simple pixel shader for testing
// Outputs the interpolated color from vertex shader

struct PS_INPUT {
    float4 position : SV_Position;
    float4 debug_color : COLOR0;
    float4 ndc_info : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_Target0 {
    // Output the debug color directly
    // This will help us verify if NDC constants are being passed correctly
    return input.debug_color;
}