// Ultra-simple vertex shader for testing
struct VS_OUTPUT {
    float4 position : SV_Position;
    float4 color : COLOR0;
};

VS_OUTPUT main(uint vid : SV_VertexID) {
    VS_OUTPUT output;
    
    // Create a triangle in screen space
    float2 positions[3] = {
        float2(640.0, 100.0),   // Top center
        float2(100.0, 620.0),   // Bottom left
        float2(1180.0, 620.0)   // Bottom right
    };
    
    output.position = float4(positions[vid], 0.5, 1.0);
    output.color = float4(vid == 0 ? 1.0 : 0.0, 
                          vid == 1 ? 1.0 : 0.0,
                          vid == 2 ? 1.0 : 0.0, 
                          1.0);
    return output;
}
