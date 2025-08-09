// Simple pass-through vertex shader for testing NDC transformation pipeline
// This shader outputs vertices in screen space (0 to viewport dimensions)
// to match Xbox 360 behavior

cbuffer Constants : register(b0) {
    // Xbox 360 system constants at specific offsets
    float4 padding[32];        // Padding to reach offset 128
    float4 ndc_scale;          // Offset 128 (floats 32-35)
    float4 ndc_offset;         // Offset 144 (floats 36-39)
};

struct VS_INPUT {
    float3 position : POSITION;
    uint vertex_id : SV_VertexID;
};

struct VS_OUTPUT {
    float4 position : SV_Position;
    float4 debug_color : COLOR0;
};

VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    
    // Output position in screen space (like Xbox 360 shaders do)
    // Expecting coordinates like (0,0) to (1280,720)
    output.position = float4(input.position.xyz, 1.0);
    
    // Debug color to verify vertex processing
    // Red channel: normalized X position
    // Green channel: normalized Y position  
    // Blue channel: vertex ID indicator
    output.debug_color = float4(
        input.position.x / 1280.0,
        input.position.y / 720.0,
        float(input.vertex_id) / 3.0,
        1.0
    );
    
    return output;
}