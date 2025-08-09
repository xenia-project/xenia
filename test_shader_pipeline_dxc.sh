#!/bin/bash

# Test shader pipeline script using DXC
# Compiles HLSL shaders and traces them through DXBC->DXIL->Metal pipeline

set -e

echo "=== Shader Pipeline Test (Using DXC) ==="
echo

# Paths
DXC_PATH="./third_party/FidelityFX-FSR/sample/libs/Cauldron/libs/DXC/bin/x64/dxc.exe"
DXBC2DXIL_PATH="./third_party/dxbc2dxil/bin/dxbc2dxil.exe"
MSC_PATH="/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/usr/bin/metal-shaderconverter"

# Create output directory
mkdir -p test_shaders_output

echo "Step 1: Compiling HLSL to DXBC using DXC..."
echo "-------------------------------------------"

# First, let's create simpler test shaders without the cbuffer complexity
cat > test_vs_simple.hlsl << 'EOF'
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
EOF

cat > test_ps_color.hlsl << 'EOF'
// Ultra-simple pixel shader
struct PS_INPUT {
    float4 position : SV_Position;
    float4 color : COLOR0;
};

float4 main(PS_INPUT input) : SV_Target0 {
    return input.color;
}
EOF

# Try to compile with DXC to DXBC format
echo "Compiling test_vs_simple.hlsl to DXBC..."
wine "$DXC_PATH" -T vs_6_0 -E main test_vs_simple.hlsl -Fo test_shaders_output/test_vs_simple.dxbc 2>&1 | grep -v "^[0-9]" || true

echo "Compiling test_ps_color.hlsl to DXBC..."
wine "$DXC_PATH" -T ps_6_0 -E main test_ps_color.hlsl -Fo test_shaders_output/test_ps_color.dxbc 2>&1 | grep -v "^[0-9]" || true

# Check if files were created
if [ ! -f test_shaders_output/test_vs_simple.dxbc ]; then
    echo "Warning: DXBC compilation may have failed, trying DXIL directly..."
    
    echo "Compiling test_vs_simple.hlsl to DXIL..."
    wine "$DXC_PATH" -T vs_6_0 -E main test_vs_simple.hlsl -Fo test_shaders_output/test_vs_simple.dxil 2>&1 | grep -v "^[0-9]" || true
    
    echo "Compiling test_ps_color.hlsl to DXIL..."
    wine "$DXC_PATH" -T ps_6_0 -E main test_ps_color.hlsl -Fo test_shaders_output/test_ps_color.dxil 2>&1 | grep -v "^[0-9]" || true
    
    VS_DXIL="test_shaders_output/test_vs_simple.dxil"
    PS_DXIL="test_shaders_output/test_ps_color.dxil"
else
    echo
    echo "Step 2: Converting DXBC to DXIL..."
    echo "-----------------------------------"
    
    echo "Converting test_vs_simple.dxbc to DXIL..."
    WINEPATH="./third_party/dxbc2dxil/bin" wine "$DXBC2DXIL_PATH" test_shaders_output/test_vs_simple.dxbc /o test_shaders_output/test_vs_simple.dxil 2>&1 | grep -v "^[0-9]" || true
    
    echo "Converting test_ps_color.dxbc to DXIL..."
    WINEPATH="./third_party/dxbc2dxil/bin" wine "$DXBC2DXIL_PATH" test_shaders_output/test_ps_color.dxbc /o test_shaders_output/test_ps_color.dxil 2>&1 | grep -v "^[0-9]" || true
    
    VS_DXIL="test_shaders_output/test_vs_simple.dxil"
    PS_DXIL="test_shaders_output/test_ps_color.dxil"
fi

echo
echo "Step 3: Converting DXIL to Metal AIR..."
echo "---------------------------------------"

if [ -f "$VS_DXIL" ]; then
    echo "Converting vertex shader to Metal..."
    "$MSC_PATH" -convert-to-air-from-dxil "$VS_DXIL" -o test_shaders_output/test_vs_simple.air -suppress-full-uniform-reflection -shader-reflection-dump-json test_shaders_output/test_vs_simple_reflection.json 2>&1 || true
fi

if [ -f "$PS_DXIL" ]; then
    echo "Converting pixel shader to Metal..."
    "$MSC_PATH" -convert-to-air-from-dxil "$PS_DXIL" -o test_shaders_output/test_ps_color.air -suppress-full-uniform-reflection -shader-reflection-dump-json test_shaders_output/test_ps_color_reflection.json 2>&1 || true
fi

echo
echo "Step 4: Disassembling AIR to check for transformations..."
echo "----------------------------------------------------------"

if [ -f test_shaders_output/test_vs_simple.air ]; then
    echo "Disassembling test_vs_simple.air..."
    xcrun metal-source -disassemble test_shaders_output/test_vs_simple.air > test_shaders_output/test_vs_simple_air.txt 2>&1
    
    echo
    echo "Analyzing vertex shader AIR for position transformations..."
    echo "Looking for screen space coordinates (640, 100, etc.)..."
    grep -E "(640|100|1180|620|position)" test_shaders_output/test_vs_simple_air.txt | head -10 || echo "No direct screen coordinates found"
    
    echo
    echo "Looking for NDC transformation (fma operations)..."
    grep -E "(fma|scale|offset)" test_shaders_output/test_vs_simple_air.txt | head -10 || echo "No FMA operations found"
fi

echo
echo "Step 5: Checking what Metal Shader Converter did..."
echo "---------------------------------------------------"

if [ -f test_shaders_output/test_vs_simple_reflection.json ]; then
    echo "Vertex shader reflection data:"
    cat test_shaders_output/test_vs_simple_reflection.json | python3 -m json.tool 2>/dev/null | head -50 || cat test_shaders_output/test_vs_simple_reflection.json | head -50
fi

echo
echo "=== Summary ==="
echo "Check test_shaders_output/ for all generated files"
echo
echo "Key questions to answer:"
echo "1. Does MSC automatically inject NDC transformation?"
echo "2. Are screen space coordinates preserved or transformed?"
echo "3. What's the difference between our test and Xenia's shaders?"

# List output files
echo
echo "Generated files:"
ls -la test_shaders_output/ 2>/dev/null || echo "No output files found"