// Test shader to output a hardcoded triangle in NDC space
// This bypasses any vertex transformation issues

struct VS_OUTPUT {
    float4 position : SV_POSITION;
    float4 color : COLOR0;
};

VS_OUTPUT main(uint vid : SV_VertexID) {
    VS_OUTPUT output;
    
    // Hardcode a triangle in NDC space (-1 to 1)
    // This should produce a visible triangle on screen
    float2 positions[3] = {
        float2(-0.5, -0.5),  // Bottom left
        float2( 0.5, -0.5),  // Bottom right  
        float2( 0.0,  0.5)   // Top center
    };
    
    float4 colors[3] = {
        float4(1.0, 0.0, 0.0, 1.0),  // Red
        float4(0.0, 1.0, 0.0, 1.0),  // Green
        float4(0.0, 0.0, 1.0, 1.0)   // Blue
    };
    
    // Output in NDC space
    output.position = float4(positions[vid % 3], 0.5, 1.0);
    output.color = colors[vid % 3];
    
    return output;
}