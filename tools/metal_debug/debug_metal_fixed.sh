#!/bin/bash
# Fixed Metal Debug Script for Xenia GPU
# Properly collects all artifacts and analyzes shaders

set -e

# Configuration
TRACE_FILE="${1:-testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace}"
BUILD_CONFIG="${2:-Checked}"
DEBUG_DIR="metal_debug"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
OUTPUT_DIR="$DEBUG_DIR/analysis_$TIMESTAMP"

# Create output directory structure
mkdir -p "$OUTPUT_DIR"/{logs,shaders,textures,analysis,air,metal}

echo "================================================"
echo "Xenia Metal Debug Analysis (Fixed)"
echo "================================================"
echo "Trace: $TRACE_FILE"
echo "Output: $OUTPUT_DIR"
echo "Build: $BUILD_CONFIG"
echo "Timestamp: $TIMESTAMP"
echo ""

# System Information
echo "Collecting system information..."
{
    echo "=== System Information ==="
    echo "Date: $(date)"
    echo "macOS Version: $(sw_vers -productVersion)"
    echo "GPU: $(system_profiler SPDisplaysDataType | grep 'Chipset Model' | head -1)"
    echo "Metal Version: $(xcrun metal --version | head -1)"
} > "$OUTPUT_DIR/analysis/system_info.txt"

# Set Metal debug environment
export METAL_DEVICE_WRAPPER_TYPE=1
export METAL_DEBUG_ERROR_MODE=5
export METAL_SHADER_VALIDATION=1
export MTL_SHADER_VALIDATION=1

echo "Running trace dump..."
echo "================================================"

# Run trace dump and wait for it to complete
./build/bin/Mac/$BUILD_CONFIG/xenia-gpu-metal-trace-dump \
    "$TRACE_FILE" 2>&1 | tee "$OUTPUT_DIR/logs/full_output.log"

echo ""
echo "Trace dump completed"
echo "================================================"

# Now collect artifacts AFTER the trace dump has completed
echo "Collecting shader artifacts..."

# Collect DXBC files
echo "  Collecting DXBC files..."
DXBC_COUNT=0
for file in temp_shader_*.dxbc; do
    if [ -f "$file" ]; then
        cp "$file" "$OUTPUT_DIR/shaders/"
        ((DXBC_COUNT++))
    fi
done
echo "    Found $DXBC_COUNT DXBC files"

# Collect DXIL files
echo "  Collecting DXIL files..."
DXIL_COUNT=0
for file in temp_shader_*.dxil; do
    if [ -f "$file" ]; then
        cp "$file" "$OUTPUT_DIR/shaders/"
        ((DXIL_COUNT++))
    fi
done
echo "    Found $DXIL_COUNT DXIL files"

# Collect Metal libraries
echo "  Collecting Metal libraries..."
METAL_COUNT=0
for file in shader_*.metallib; do
    if [ -f "$file" ]; then
        cp "$file" "$OUTPUT_DIR/metal/"
        ((METAL_COUNT++))
    fi
done
echo "    Found $METAL_COUNT Metal libraries"

# Collect textures
echo "  Collecting texture outputs..."
if [ -f "title_414B07D1_frame_589.png" ]; then
    cp "title_414B07D1_frame_589.png" "$OUTPUT_DIR/textures/output.png"
    echo "    Captured output PNG"
fi

# Extract key information from logs
echo ""
echo "Extracting debug information..."

# Shader information
grep -E "shader.*hash|Converting.*shader|Shader data hash" "$OUTPUT_DIR/logs/full_output.log" \
    > "$OUTPUT_DIR/analysis/shader_hashes.txt" 2>/dev/null || true

# Texture information
grep -E "texture|Texture|Found valid texture|heap slot|textureViewID" "$OUTPUT_DIR/logs/full_output.log" \
    > "$OUTPUT_DIR/analysis/texture_info.txt" 2>/dev/null || true

# Draw calls
grep -E "IssueDraw|Drew.*primitives|Draw.*state" "$OUTPUT_DIR/logs/full_output.log" \
    > "$OUTPUT_DIR/analysis/draw_calls.txt" 2>/dev/null || true

# Render targets
grep -E "RB_COLOR|render target|EDRAM" "$OUTPUT_DIR/logs/full_output.log" \
    > "$OUTPUT_DIR/analysis/render_targets.txt" 2>/dev/null || true

# Errors and warnings
grep -E "ERROR|WARN|Failed|invalid" "$OUTPUT_DIR/logs/full_output.log" \
    > "$OUTPUT_DIR/analysis/errors.txt" 2>/dev/null || true

# Analyze Metal libraries with correct flags
echo ""
echo "Analyzing Metal shaders..."

for metallib in "$OUTPUT_DIR/metal"/*.metallib; do
    if [ -f "$metallib" ]; then
        base=$(basename "$metallib" .metallib)
        echo "  Analyzing $base..."
        
        # Get function list using --syms
        xcrun metal-objdump --metallib --syms "$metallib" \
            > "$OUTPUT_DIR/analysis/${base}_symbols.txt" 2>/dev/null || true
        
        # Get disassembly using --disassemble
        xcrun metal-objdump --metallib --disassemble "$metallib" \
            > "$OUTPUT_DIR/air/${base}_disasm.txt" 2>/dev/null || true
        
        # Get reflection data
        xcrun metal-objdump --metallib --reflection "$metallib" \
            > "$OUTPUT_DIR/analysis/${base}_reflection.txt" 2>/dev/null || true
        
        # Get AIR version info
        xcrun metal-objdump --metallib --air-version "$metallib" \
            > "$OUTPUT_DIR/analysis/${base}_air_version.txt" 2>/dev/null || true
    fi
done

# Analyze DXBC/DXIL files
echo "  Analyzing shader bytecode..."
for dxbc in "$OUTPUT_DIR/shaders"/*.dxbc; do
    if [ -f "$dxbc" ]; then
        base=$(basename "$dxbc" .dxbc)
        xxd -l 256 "$dxbc" > "$OUTPUT_DIR/analysis/${base}_hex.txt" 2>/dev/null || true
    fi
done

# Create analysis report
echo ""
echo "Creating analysis report..."

{
    echo "=== Xenia Metal Debug Analysis Report ==="
    echo "Generated: $(date)"
    echo "Trace: $TRACE_FILE"
    echo ""
    
    echo "=== Statistics ==="
    echo "Total log lines: $(wc -l < "$OUTPUT_DIR/logs/full_output.log")"
    echo "Draw calls: $(grep -c "IssueDraw" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null || echo "0")"
    echo "Shader compilations: $(grep -c "Converting.*shader" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null || echo "0")"
    echo "Errors: $(grep -c "ERROR" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null || echo "0")"
    echo "Warnings: $(grep -c "WARN" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null || echo "0")"
    echo ""
    
    echo "=== Collected Artifacts ==="
    echo "DXBC files: $DXBC_COUNT"
    echo "DXIL files: $DXIL_COUNT"
    echo "Metal libraries: $METAL_COUNT"
    echo ""
    
    echo "=== Critical Issues ==="
    
    # Check for empty descriptor heaps
    if grep -q "descriptor heap is empty" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null; then
        echo "⚠️  ISSUE: Empty descriptor heaps detected"
        grep "descriptor heap is empty" "$OUTPUT_DIR/logs/full_output.log" | head -3
    fi
    
    # Check for texture binding
    echo ""
    echo "=== Texture Binding Status ==="
    if grep -q "Found valid texture" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null; then
        echo "✓ Textures ARE being found:"
        grep "Found valid texture" "$OUTPUT_DIR/logs/full_output.log" | head -5
    else
        echo "✗ No textures found"
    fi
    
    if grep -q "Prepared 0 textures" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null; then
        echo ""
        echo "⚠️  ISSUE: Textures found but NOT uploaded:"
        grep "Prepared.*textures" "$OUTPUT_DIR/logs/full_output.log" | head -3
    fi
    
    # Check render targets
    echo ""
    echo "=== Render Target Configuration ==="
    grep "RB_COLOR_INFO" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null | head -3 || echo "No RB_COLOR_INFO found"
    
    # List shader hashes
    echo ""
    echo "=== Shader Hashes Compiled ==="
    if [ -f "$OUTPUT_DIR/analysis/shader_hashes.txt" ]; then
        cat "$OUTPUT_DIR/analysis/shader_hashes.txt" | head -10
    fi
    
} > "$OUTPUT_DIR/analysis/report.txt"

# Check if output is black
echo ""
echo "Checking output image..."
if [ -f "$OUTPUT_DIR/textures/output.png" ]; then
    # Use ImageMagick if available to check if image is all black
    if command -v identify &> /dev/null; then
        MEAN=$(identify -format "%[mean]" "$OUTPUT_DIR/textures/output.png" 2>/dev/null || echo "unknown")
        if [ "$MEAN" = "0" ]; then
            echo "  ⚠️  Output image is completely BLACK"
        else
            echo "  Output image mean value: $MEAN"
        fi
    else
        echo "  Output PNG saved (install ImageMagick to analyze)"
    fi
fi

# Final summary
echo ""
echo "================================================"
echo "Analysis Complete"
echo "================================================"
echo ""
echo "📁 Output Directory: $OUTPUT_DIR"
echo ""
echo "📊 Key Files:"
echo "  • Full log: $OUTPUT_DIR/logs/full_output.log"
echo "  • Report: $OUTPUT_DIR/analysis/report.txt"
echo "  • Textures: $OUTPUT_DIR/analysis/texture_info.txt"
echo "  • Errors: $OUTPUT_DIR/analysis/errors.txt"
echo ""
echo "📂 Artifacts:"
echo "  • DXBC: $OUTPUT_DIR/shaders/*.dxbc"
echo "  • DXIL: $OUTPUT_DIR/shaders/*.dxil"
echo "  • Metal: $OUTPUT_DIR/metal/*.metallib"
echo "  • AIR: $OUTPUT_DIR/air/*_disasm.txt"
echo ""
echo "🔍 Quick Commands:"
echo "  cat $OUTPUT_DIR/analysis/report.txt"
echo "  grep 'Found valid texture' $OUTPUT_DIR/logs/full_output.log"
echo "  grep 'RB_COLOR' $OUTPUT_DIR/logs/full_output.log"
echo ""

# Create quick viewer
cat > "$OUTPUT_DIR/view.sh" << 'EOF'
#!/bin/bash
DIR="$(dirname "$0")"
echo "1) View report"
echo "2) View texture info"
echo "3) View errors"
echo "4) View shader symbols"
echo "5) Search log"
read -p "Choice: " choice
case $choice in
    1) cat "$DIR/analysis/report.txt" ;;
    2) cat "$DIR/analysis/texture_info.txt" ;;
    3) cat "$DIR/analysis/errors.txt" ;;
    4) cat "$DIR/analysis/"*_symbols.txt 2>/dev/null ;;
    5) read -p "Search term: " term; grep -i "$term" "$DIR/logs/full_output.log" ;;
esac
EOF
chmod +x "$OUTPUT_DIR/view.sh"

echo "Quick viewer: $OUTPUT_DIR/view.sh"
echo "================================================"