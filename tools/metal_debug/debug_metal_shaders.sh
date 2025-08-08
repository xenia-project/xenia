#!/bin/bash
# Metal Shader Pipeline Debug Script for Xenia GPU
# Captures and analyzes all shader conversion artifacts: DXBC → DXIL → Metal

set -e

# Configuration
TRACE_FILE="${1:-testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace}"
BUILD_CONFIG="${2:-Checked}"
DEBUG_DIR="metal_debug"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
OUTPUT_DIR="$DEBUG_DIR/shaders_$TIMESTAMP"

# Create output directory structure
mkdir -p "$OUTPUT_DIR"/{dxbc,dxil,metal,air,decompiled,json,analysis}

echo "================================================"
echo "Xenia Metal Shader Pipeline Analysis"
echo "================================================"
echo "Trace: $TRACE_FILE"
echo "Output: $OUTPUT_DIR"
echo "Build: $BUILD_CONFIG"
echo ""

# Function to analyze a metallib file
analyze_metallib() {
    local metallib="$1"
    local base=$(basename "$metallib" .metallib)
    
    echo "  Analyzing $base..."
    
    # Get function list
    xcrun metal-objdump -list "$metallib" > "$OUTPUT_DIR/analysis/${base}_functions.txt" 2>/dev/null || true
    
    # Disassemble to AIR
    xcrun metal-objdump -disassemble "$metallib" > "$OUTPUT_DIR/air/${base}_disasm.air" 2>/dev/null || true
    
    # Try to decompile to Metal source (this creates more readable output)
    xcrun metal-source -o "$OUTPUT_DIR/decompiled/${base}.metal" "$metallib" 2>/dev/null || {
        echo "    Note: metal-source decompilation failed (expected for IR-converted shaders)"
    }
    
    # Extract archive contents to see the bitcode
    xcrun metal-objdump -arch-name air64 -s "$metallib" > "$OUTPUT_DIR/analysis/${base}_sections.txt" 2>/dev/null || true
    
    # Try to extract the reflection data
    xcrun metal-objdump -reflection-dump "$metallib" > "$OUTPUT_DIR/json/${base}_reflection.json" 2>/dev/null || true
    
    # Get size info
    local size=$(stat -f%z "$metallib" 2>/dev/null || echo "0")
    echo "    Size: $size bytes"
}

# Function to analyze DXBC file
analyze_dxbc() {
    local dxbc="$1"
    local base=$(basename "$dxbc" .dxbc)
    
    echo "  Analyzing DXBC $base..."
    
    # Hexdump first 256 bytes to see header
    xxd -l 256 "$dxbc" > "$OUTPUT_DIR/analysis/${base}_header.hex" 2>/dev/null || true
    
    # Try to disassemble with Wine if dxbc tools are available
    if command -v wine &> /dev/null && [ -f "./third_party/dxbc2dxil/bin/dxbc-disasm.exe" ]; then
        WINEPATH="./third_party/dxbc2dxil/bin" wine ./third_party/dxbc2dxil/bin/dxbc-disasm.exe "$dxbc" > "$OUTPUT_DIR/analysis/${base}_disasm.txt" 2>/dev/null || true
    fi
}

# Function to analyze DXIL file
analyze_dxil() {
    local dxil="$1"
    local base=$(basename "$dxil" .dxil)
    
    echo "  Analyzing DXIL $base..."
    
    # DXIL is LLVM bitcode, try to disassemble if we have llvm-dis
    if command -v llvm-dis &> /dev/null; then
        llvm-dis "$dxil" -o "$OUTPUT_DIR/analysis/${base}.ll" 2>/dev/null || {
            echo "    Note: LLVM disassembly failed (DXIL may use different format)"
        }
    fi
    
    # Hexdump to see structure
    xxd -l 512 "$dxil" > "$OUTPUT_DIR/analysis/${base}_header.hex" 2>/dev/null || true
}

# Set environment for shader dumping
export METAL_DEVICE_WRAPPER_TYPE=1
export METAL_DEBUG_ERROR_MODE=5
export METAL_SHADER_VALIDATION=1
export XENIA_GPU_METAL_DUMP_SHADERS=1

# Clean up old temp files
rm -f temp_shader_*.dxbc temp_shader_*.dxil shader_*.metallib 2>/dev/null || true

echo "Starting trace dump to capture shader conversions..."
echo "================================================"

# Run trace dump and capture all shader-related output
timeout 60 ./build/bin/Mac/$BUILD_CONFIG/xenia-gpu-metal-trace-dump \
    "$TRACE_FILE" 2>&1 | tee "$OUTPUT_DIR/shader_conversion.log" | \
    grep -E "shader|Shader|DXBC|DXIL|Metal|metallib|Converting|hash|Reflection" &

# Give it time to generate some shaders
sleep 5

# Monitor for shader files being created
echo ""
echo "Monitoring for shader artifacts..."

# Wait for completion or timeout
wait

echo ""
echo "Collecting shader artifacts..."
echo "================================================"

# Collect DXBC files
echo "Collecting DXBC files..."
for file in temp_shader_*.dxbc shader_*.dxbc; do
    if [ -f "$file" ]; then
        cp "$file" "$OUTPUT_DIR/dxbc/"
        echo "  Found: $file"
        analyze_dxbc "$file"
    fi
done

# Collect DXIL files
echo ""
echo "Collecting DXIL files..."
for file in temp_shader_*.dxil shader_*.dxil; do
    if [ -f "$file" ]; then
        cp "$file" "$OUTPUT_DIR/dxil/"
        echo "  Found: $file"
        analyze_dxil "$file"
    fi
done

# Collect Metal libraries
echo ""
echo "Collecting Metal libraries..."
for file in shader_*.metallib *.metallib; do
    if [ -f "$file" ]; then
        cp "$file" "$OUTPUT_DIR/metal/"
        echo "  Found: $file"
        analyze_metallib "$file"
    fi
done

# Extract shader info from log
echo ""
echo "Extracting shader conversion data from log..."
grep "shader.*hash" "$OUTPUT_DIR/shader_conversion.log" > "$OUTPUT_DIR/analysis/shader_hashes.txt" 2>/dev/null || true
grep "Reflection" "$OUTPUT_DIR/shader_conversion.log" > "$OUTPUT_DIR/analysis/reflection_data.txt" 2>/dev/null || true
grep "TopLevelAB" "$OUTPUT_DIR/shader_conversion.log" > "$OUTPUT_DIR/analysis/argument_buffers.txt" 2>/dev/null || true
grep "Resource\[" "$OUTPUT_DIR/shader_conversion.log" > "$OUTPUT_DIR/analysis/resources.txt" 2>/dev/null || true

# Analyze shader bindings
echo ""
echo "Analyzing shader bindings..."
{
    echo "=== Shader Resource Bindings ==="
    echo ""
    echo "Vertex Shaders:"
    grep -E "Vertex shader.*hash|VS.*texture|T[0-9].*slot" "$OUTPUT_DIR/shader_conversion.log" 2>/dev/null || echo "  No vertex shader bindings found"
    echo ""
    echo "Pixel Shaders:"
    grep -E "Pixel shader.*hash|PS.*texture|fragment.*texture" "$OUTPUT_DIR/shader_conversion.log" 2>/dev/null || echo "  No pixel shader bindings found"
    echo ""
    echo "Texture Slots:"
    grep -E "texture slot|heap slot|textureViewID" "$OUTPUT_DIR/shader_conversion.log" 2>/dev/null || echo "  No texture slots found"
    echo ""
    echo "Samplers:"
    grep -E "sampler|Sampler|S[0-9]" "$OUTPUT_DIR/shader_conversion.log" 2>/dev/null || echo "  No samplers found"
} > "$OUTPUT_DIR/analysis/binding_analysis.txt"

# Create detailed shader pipeline report
echo ""
echo "Creating shader pipeline report..."
{
    echo "=== Xenia Metal Shader Pipeline Report ==="
    echo "Generated: $(date)"
    echo "Trace: $TRACE_FILE"
    echo ""
    
    echo "=== Pipeline Stages ==="
    echo "1. DXBC (DirectX Bytecode) - Xbox 360 original shaders"
    echo "2. DXIL (DirectX Intermediate Language) - LLVM-based IR"
    echo "3. Metal AIR (Apple Intermediate Representation)"
    echo "4. Metal Library - Final compiled shader"
    echo ""
    
    echo "=== Collected Artifacts ==="
    echo ""
    echo "DXBC Files ($(ls -1 "$OUTPUT_DIR/dxbc" 2>/dev/null | wc -l)):"
    ls -la "$OUTPUT_DIR/dxbc" 2>/dev/null | tail -n +2 | head -10 || echo "  None found"
    echo ""
    
    echo "DXIL Files ($(ls -1 "$OUTPUT_DIR/dxil" 2>/dev/null | wc -l)):"
    ls -la "$OUTPUT_DIR/dxil" 2>/dev/null | tail -n +2 | head -10 || echo "  None found"
    echo ""
    
    echo "Metal Libraries ($(ls -1 "$OUTPUT_DIR/metal" 2>/dev/null | wc -l)):"
    ls -la "$OUTPUT_DIR/metal" 2>/dev/null | tail -n +2 | head -10 || echo "  None found"
    echo ""
    
    echo "=== Shader Hashes ==="
    cat "$OUTPUT_DIR/analysis/shader_hashes.txt" 2>/dev/null | head -20 || echo "No shader hashes captured"
    echo ""
    
    echo "=== Reflection Data ==="
    cat "$OUTPUT_DIR/analysis/reflection_data.txt" 2>/dev/null | head -30 || echo "No reflection data captured"
    echo ""
    
    echo "=== Resource Bindings ==="
    cat "$OUTPUT_DIR/analysis/binding_analysis.txt" 2>/dev/null || echo "No binding analysis available"
    
} > "$OUTPUT_DIR/shader_pipeline_report.txt"

# Try to decompile specific shaders if they exist
echo ""
echo "Attempting to decompile known shaders..."

# Common shader hashes from the trace
KNOWN_SHADERS=(
    "shader_vs_0a6d1dd7767fdf27"
    "shader_ps_2e372ea28cc404b7"
    "shader_vs_72cbcaa6a7984111"
    "shader_ps_29719c33ea979cfa"
)

for shader in "${KNOWN_SHADERS[@]}"; do
    if [ -f "$OUTPUT_DIR/metal/${shader}.metallib" ]; then
        echo "  Decompiling $shader..."
        
        # Try metal-source decompilation
        xcrun metal-source -o "$OUTPUT_DIR/decompiled/${shader}.metal" \
            "$OUTPUT_DIR/metal/${shader}.metallib" 2>/dev/null || {
            echo "    Using metal-objdump instead..."
            xcrun metal-objdump -disassemble "$OUTPUT_DIR/metal/${shader}.metallib" \
                > "$OUTPUT_DIR/decompiled/${shader}_disasm.txt" 2>/dev/null || true
        }
        
        # Extract vertex/fragment function info
        xcrun metal-objdump -list "$OUTPUT_DIR/metal/${shader}.metallib" 2>/dev/null | \
            grep -E "vertex|fragment" > "$OUTPUT_DIR/analysis/${shader}_entry_points.txt" 2>/dev/null || true
    fi
done

# Create conversion flow diagram
echo ""
echo "Creating conversion flow visualization..."
{
    echo "=== Shader Conversion Flow ==="
    echo ""
    echo "Xbox 360 GPU Microcode"
    echo "         ↓"
    echo "    [Translator]"
    echo "         ↓"
    echo "   DXBC (DirectX BC)"
    
    # Check for actual DXBC files
    if ls "$OUTPUT_DIR/dxbc"/*.dxbc >/dev/null 2>&1; then
        echo "    ✓ Found $(ls -1 "$OUTPUT_DIR/dxbc"/*.dxbc | wc -l) files"
    else
        echo "    ✗ No files found"
    fi
    
    echo "         ↓"
    echo "   [dxbc2dxil.exe]"
    echo "         ↓"
    echo "   DXIL (LLVM IR)"
    
    # Check for actual DXIL files
    if ls "$OUTPUT_DIR/dxil"/*.dxil >/dev/null 2>&1; then
        echo "    ✓ Found $(ls -1 "$OUTPUT_DIR/dxil"/*.dxil | wc -l) files"
    else
        echo "    ✗ No files found"
    fi
    
    echo "         ↓"
    echo " [Metal Shader Converter]"
    echo "         ↓"
    echo "    Metal AIR/BC"
    echo "         ↓"
    echo "   Metal Library"
    
    # Check for actual Metal files
    if ls "$OUTPUT_DIR/metal"/*.metallib >/dev/null 2>&1; then
        echo "    ✓ Found $(ls -1 "$OUTPUT_DIR/metal"/*.metallib | wc -l) files"
    else
        echo "    ✗ No files found"
    fi
    
    echo ""
    echo "=== Key Observations ==="
    
    # Check for texture binding issues
    if grep -q "texture slot 0" "$OUTPUT_DIR/shader_conversion.log" 2>/dev/null; then
        echo "• Texture slots ARE being identified in shader reflection"
    else
        echo "• WARNING: No texture slots found in shader reflection"
    fi
    
    if grep -q "textureViewID" "$OUTPUT_DIR/shader_conversion.log" 2>/dev/null; then
        echo "• Texture view IDs ARE being created"
    else
        echo "• WARNING: No texture view IDs found"
    fi
    
    if grep -q "heap slot" "$OUTPUT_DIR/shader_conversion.log" 2>/dev/null; then
        echo "• Heap slots ARE being assigned"
    else
        echo "• WARNING: No heap slot assignments found"
    fi
    
} > "$OUTPUT_DIR/conversion_flow.txt"

# Final summary
echo ""
echo "================================================"
echo "Shader Pipeline Analysis Complete"
echo "================================================"
echo ""
echo "📁 Output Directory: $OUTPUT_DIR"
echo ""
echo "📊 Key Directories:"
echo "  • DXBC files: $OUTPUT_DIR/dxbc/"
echo "  • DXIL files: $OUTPUT_DIR/dxil/"
echo "  • Metal libs: $OUTPUT_DIR/metal/"
echo "  • AIR disasm: $OUTPUT_DIR/air/"
echo "  • Decompiled: $OUTPUT_DIR/decompiled/"
echo "  • Analysis:   $OUTPUT_DIR/analysis/"
echo ""
echo "📄 Reports:"
echo "  • Pipeline:   $OUTPUT_DIR/shader_pipeline_report.txt"
echo "  • Flow:       $OUTPUT_DIR/conversion_flow.txt"
echo "  • Bindings:   $OUTPUT_DIR/analysis/binding_analysis.txt"
echo ""
echo "🔍 Quick Commands:"
echo "  • View report: cat $OUTPUT_DIR/shader_pipeline_report.txt"
echo "  • View flow:   cat $OUTPUT_DIR/conversion_flow.txt"
echo "  • List Metal:  ls -la $OUTPUT_DIR/metal/"
echo ""

# Create viewer script
cat > "$OUTPUT_DIR/view_shader.sh" << 'VIEWER'
#!/bin/bash
# Shader viewer for debugging

DIR="$(dirname "$0")"

echo "Available shaders:"
ls -1 "$DIR/metal"/*.metallib 2>/dev/null | while read f; do
    base=$(basename "$f" .metallib)
    echo "  • $base"
done

echo ""
read -p "Enter shader name (without extension): " shader

if [ -f "$DIR/metal/${shader}.metallib" ]; then
    echo ""
    echo "=== Functions in $shader ==="
    xcrun metal-objdump -list "$DIR/metal/${shader}.metallib" 2>/dev/null || echo "Failed to list functions"
    
    echo ""
    echo "=== Disassembly Preview (first 50 lines) ==="
    xcrun metal-objdump -disassemble "$DIR/metal/${shader}.metallib" 2>/dev/null | head -50 || echo "Failed to disassemble"
    
    if [ -f "$DIR/decompiled/${shader}.metal" ]; then
        echo ""
        echo "=== Decompiled Metal Source ==="
        cat "$DIR/decompiled/${shader}.metal"
    elif [ -f "$DIR/air/${shader}_disasm.air" ]; then
        echo ""
        echo "=== AIR Disassembly (first 50 lines) ==="
        head -50 "$DIR/air/${shader}_disasm.air"
    fi
else
    echo "Shader not found: ${shader}.metallib"
fi
VIEWER

chmod +x "$OUTPUT_DIR/view_shader.sh"
echo "Shader viewer created: $OUTPUT_DIR/view_shader.sh"
echo "================================================"