// Vertex shader that explicitly applies NDC transformation
// This tests if the transformation is preserved through the pipeline

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
    float4 ndc_info : TEXCOORD0;
};

VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    
    // Start with screen space position
    float4 pos = float4(input.position.xyz, 1.0);
    
    // Apply Xbox 360 to NDC transformation
    // This is what Xenia's D3D12/Vulkan backends inject
    pos.xyz = pos.xyz * ndc_scale.xyz + ndc_offset.xyz * pos.w;
    
    output.position = pos;
    
    // Output NDC constants as color for debugging
    output.debug_color = float4(
        ndc_scale.x,   // Should be 2.0/1280.0 for 1280 width
        ndc_scale.y,   // Should be -2.0/720.0 for 720 height (Y-flip)
        ndc_offset.x,  // Should be -1.0
        ndc_offset.y   // Should be 1.0
    );
    
    // Also output the transformed position for analysis
    output.ndc_info = pos;
    
    return output;
}