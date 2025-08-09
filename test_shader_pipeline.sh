#!/bin/bash

# Test shader pipeline script
# Compiles HLSL shaders and traces them through DXBC->DXIL->Metal pipeline

set -e

echo "=== Shader Pipeline Test ==="
echo

# Check for required tools
if ! command -v wine &> /dev/null; then
    echo "Error: wine not found. Please install wine."
    exit 1
fi

# Paths
FXC_PATH="wine_binaries/fxc.exe"
DXBC2DXIL_PATH="third_party/dxbc2dxil/bin/dxbc2dxil.exe"
MSC_PATH="/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/usr/bin/metal-shaderconverter"

# Create output directory
mkdir -p test_shaders_output

echo "Step 1: Compiling HLSL to DXBC..."
echo "--------------------------------"

# Compile vertex shader (pass-through)
echo "Compiling test_vs_passthrough.hlsl..."
wine "$FXC_PATH" /T vs_5_0 /E main /Fo test_shaders_output/test_vs_passthrough.dxbc test_vs_passthrough.hlsl 2>/dev/null || {
    echo "Failed to compile test_vs_passthrough.hlsl"
    exit 1
}

# Compile vertex shader (NDC transform)
echo "Compiling test_vs_ndc_transform.hlsl..."
wine "$FXC_PATH" /T vs_5_0 /E main /Fo test_shaders_output/test_vs_ndc_transform.dxbc test_vs_ndc_transform.hlsl 2>/dev/null || {
    echo "Failed to compile test_vs_ndc_transform.hlsl"
    exit 1
}

# Compile pixel shader
echo "Compiling test_ps_simple.hlsl..."
wine "$FXC_PATH" /T ps_5_0 /E main /Fo test_shaders_output/test_ps_simple.dxbc test_ps_simple.hlsl 2>/dev/null || {
    echo "Failed to compile test_ps_simple.hlsl"
    exit 1
}

echo
echo "Step 2: Converting DXBC to DXIL..."
echo "--------------------------------"

# Convert to DXIL
echo "Converting test_vs_passthrough.dxbc to DXIL..."
WINEPATH="../third_party/dxbc2dxil/bin" wine "$DXBC2DXIL_PATH" test_shaders_output/test_vs_passthrough.dxbc /o test_shaders_output/test_vs_passthrough.dxil 2>/dev/null

echo "Converting test_vs_ndc_transform.dxbc to DXIL..."
WINEPATH="../third_party/dxbc2dxil/bin" wine "$DXBC2DXIL_PATH" test_shaders_output/test_vs_ndc_transform.dxbc /o test_shaders_output/test_vs_ndc_transform.dxil 2>/dev/null

echo "Converting test_ps_simple.dxbc to DXIL..."
WINEPATH="../third_party/dxbc2dxil/bin" wine "$DXBC2DXIL_PATH" test_shaders_output/test_ps_simple.dxbc /o test_shaders_output/test_ps_simple.dxil 2>/dev/null

echo
echo "Step 3: Converting DXIL to Metal AIR..."
echo "---------------------------------------"

# Convert to Metal
echo "Converting test_vs_passthrough.dxil to Metal..."
"$MSC_PATH" -convert-to-air-from-dxil test_shaders_output/test_vs_passthrough.dxil -o test_shaders_output/test_vs_passthrough.air -suppress-full-uniform-reflection -shader-reflection-dump-json test_shaders_output/test_vs_passthrough_reflection.json

echo "Converting test_vs_ndc_transform.dxil to Metal..."
"$MSC_PATH" -convert-to-air-from-dxil test_shaders_output/test_vs_ndc_transform.dxil -o test_shaders_output/test_vs_ndc_transform.air -suppress-full-uniform-reflection -shader-reflection-dump-json test_shaders_output/test_vs_ndc_transform_reflection.json

echo "Converting test_ps_simple.dxil to Metal..."
"$MSC_PATH" -convert-to-air-from-dxil test_shaders_output/test_ps_simple.dxil -o test_shaders_output/test_ps_simple.air -suppress-full-uniform-reflection -shader-reflection-dump-json test_shaders_output/test_ps_simple_reflection.json

echo
echo "Step 4: Disassembling AIR to check for NDC transformation..."
echo "------------------------------------------------------------"

# Disassemble AIR
echo "Disassembling test_vs_passthrough.air..."
xcrun metal-source -disassemble test_shaders_output/test_vs_passthrough.air > test_shaders_output/test_vs_passthrough_air.txt 2>&1

echo "Disassembling test_vs_ndc_transform.air..."
xcrun metal-source -disassemble test_shaders_output/test_vs_ndc_transform.air > test_shaders_output/test_vs_ndc_transform_air.txt 2>&1

echo
echo "Step 5: Analyzing for NDC transformation..."
echo "-------------------------------------------"

# Check for NDC transformation in passthrough shader
echo "Checking test_vs_passthrough for NDC transformation..."
if grep -q "fma\|CB0\[32\]\|CB0\[36\]" test_shaders_output/test_vs_passthrough_air.txt; then
    echo "  ✓ NDC transformation FOUND in passthrough shader (injected by MSC)"
    echo "  Key lines:"
    grep -n "fma\|CB0\[32\]\|CB0\[36\]" test_shaders_output/test_vs_passthrough_air.txt | head -5
else
    echo "  ✗ No NDC transformation found in passthrough shader"
fi

echo
echo "Checking test_vs_ndc_transform for NDC transformation..."
if grep -q "fma\|CB0\[32\]\|CB0\[36\]" test_shaders_output/test_vs_ndc_transform_air.txt; then
    echo "  ✓ NDC transformation FOUND in NDC transform shader"
    echo "  Key lines:"
    grep -n "fma\|CB0\[32\]\|CB0\[36\]" test_shaders_output/test_vs_ndc_transform_air.txt | head -5
else
    echo "  ✗ No NDC transformation found in NDC transform shader"
fi

echo
echo "Step 6: Checking reflection data..."
echo "-----------------------------------"

# Check reflection for CB0 size
echo "Checking CB0 size in reflections..."
for json in test_shaders_output/*_reflection.json; do
    basename=$(basename "$json" _reflection.json)
    echo "  $basename:"
    if [ -f "$json" ]; then
        grep -A2 "CB0" "$json" 2>/dev/null || echo "    No CB0 found"
    fi
done

echo
echo "=== Analysis Complete ==="
echo "Output files in test_shaders_output/"
echo
echo "Key findings:"
echo "1. Check if MSC automatically injects NDC transformation"
echo "2. Compare with Xenia's actual shader processing"
echo "3. Verify CB0 size includes space for NDC constants (>= 160 bytes)"