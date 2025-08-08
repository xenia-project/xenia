#!/bin/bash
# Comprehensive Metal Debug Script for Xenia GPU
# Captures all possible debug information for troubleshooting rendering issues

set -e

# Configuration
TRACE_FILE="${1:-testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace}"
BUILD_CONFIG="${2:-Checked}"
DEBUG_DIR="metal_debug"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
OUTPUT_DIR="$DEBUG_DIR/session_$TIMESTAMP"

# Create output directory structure
mkdir -p "$OUTPUT_DIR"/{logs,textures,shaders,captures,analysis,traces}

echo "==============================================="
echo "Xenia Metal GPU Comprehensive Debug Session"
echo "==============================================="
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
    echo "Build: $(sw_vers -buildVersion)"
    echo ""
    echo "=== GPU Information ==="
    system_profiler SPDisplaysDataType | grep -A 10 "Chipset Model"
    echo ""
    echo "=== Metal Info ==="
    xcrun metal --version
    echo ""
    echo "=== Metal Shader Converter Info ==="
    if command -v metal-shaderconverter &> /dev/null; then
        metal-shaderconverter --version
    else
        echo "Metal Shader Converter not found in PATH"
    fi
    echo ""
} > "$OUTPUT_DIR/logs/system_info.txt"

# Set comprehensive Metal debugging environment
export METAL_DEVICE_WRAPPER_TYPE=1
export METAL_DEBUG_ERROR_MODE=5
export METAL_SHADER_VALIDATION=1
export MTL_SHADER_VALIDATION=1
export METAL_CAPTURE_ENABLED=1
export METAL_GPU_CAPTURE_UNTRACK_RESOURCES=1
export METAL_GPU_EVENTS_ENABLED=1
export OBJC_DEBUG_MISSING_POOLS=YES
export METAL_DEBUG_SHADER_VALIDATION_GLOBAL_MEMORY=1
export METAL_DEBUG_SHADER_VALIDATION_THREAD_GROUP_MEMORY=1
export METAL_DEBUG_SHADER_VALIDATION_TEXTURE_USAGE=1
export METAL_SHADER_VALIDATION_COMPILE_TIME_VALIDATION=1
export METAL_SHADER_VALIDATION_API_VALIDATION=1

# Additional debug flags for verbose output
export XENIA_GPU_METAL_DEBUG=1
export XENIA_GPU_METAL_DUMP_SHADERS=1
export XENIA_GPU_METAL_DUMP_TEXTURES=1
export XENIA_GPU_METAL_TRACE_COMMANDS=1

echo "Metal validation environment configured"
echo ""

# Build if needed
if [ ! -f "./build/bin/Mac/$BUILD_CONFIG/xenia-gpu-metal-trace-dump" ]; then
    echo "Building xenia-gpu-metal-trace-dump..."
    ./xb build --config=$(echo $BUILD_CONFIG | tr '[:upper:]' '[:lower:]') --target=xenia-gpu-metal-trace-dump
fi

# Run with full logging
echo "Starting trace dump with comprehensive debugging..."
echo "================================================"

# Main execution with verbose output
./build/bin/Mac/$BUILD_CONFIG/xenia-gpu-metal-trace-dump \
    "$TRACE_FILE" \
    2>&1 | tee "$OUTPUT_DIR/logs/full_output.log"

echo ""
echo "================================================"
echo "Main execution completed"
echo ""

# Extract specific log sections
echo "Extracting debug information..."

# Shader information
grep -E "shader|Shader|SHADER|MSC|metallib|dxbc|dxil" "$OUTPUT_DIR/logs/full_output.log" > "$OUTPUT_DIR/logs/shader_activity.log" 2>/dev/null || true

# Texture information
grep -E "texture|Texture|TEXTURE|Upload|untiled|heap slot|setTexture|textureViewID" "$OUTPUT_DIR/logs/full_output.log" > "$OUTPUT_DIR/logs/texture_activity.log" 2>/dev/null || true

# Draw calls
grep -E "IssueDraw|Draw|draw|DRAW|indexed primitives|vertices" "$OUTPUT_DIR/logs/full_output.log" > "$OUTPUT_DIR/logs/draw_calls.log" 2>/dev/null || true

# Render targets
grep -E "render target|RenderTarget|EDRAM|RB_COLOR|framebuffer|color attachment" "$OUTPUT_DIR/logs/full_output.log" > "$OUTPUT_DIR/logs/render_targets.log" 2>/dev/null || true

# Descriptor heaps and binding
grep -E "descriptor|Descriptor|heap|Heap|IRDescriptor|resource binding|argument buffer" "$OUTPUT_DIR/logs/full_output.log" > "$OUTPUT_DIR/logs/descriptor_heaps.log" 2>/dev/null || true

# Command buffers
grep -E "command buffer|CommandBuffer|commit|encode|submission" "$OUTPUT_DIR/logs/full_output.log" > "$OUTPUT_DIR/logs/command_buffers.log" 2>/dev/null || true

# Validation errors and warnings
grep -E "ERROR|WARN|Warning|Error|Failed|failed|invalid|Invalid" "$OUTPUT_DIR/logs/full_output.log" > "$OUTPUT_DIR/logs/errors_warnings.log" 2>/dev/null || true

# Pipeline states
grep -E "pipeline|Pipeline|PSO|vertex shader|pixel shader|fragment" "$OUTPUT_DIR/logs/full_output.log" > "$OUTPUT_DIR/logs/pipeline_states.log" 2>/dev/null || true

# Memory and resources
grep -E "memory|Memory|allocation|buffer|Buffer|resource" "$OUTPUT_DIR/logs/full_output.log" > "$OUTPUT_DIR/logs/memory_resources.log" 2>/dev/null || true

# Viewport and scissor
grep -E "viewport|Viewport|scissor|Scissor|clip|screen space" "$OUTPUT_DIR/logs/full_output.log" > "$OUTPUT_DIR/logs/viewport_scissor.log" 2>/dev/null || true

# Collect shader files
echo "Collecting shader files..."
# Copy all shader metallib files
cp shader_*.metallib "$OUTPUT_DIR/shaders/" 2>/dev/null || true
# Copy any temp shader files
cp temp_shader_*.dxbc "$OUTPUT_DIR/shaders/" 2>/dev/null || true
cp temp_shader_*.dxil "$OUTPUT_DIR/shaders/" 2>/dev/null || true
# List what we found
echo "  Found $(ls -1 "$OUTPUT_DIR/shaders"/*.metallib 2>/dev/null | wc -l) metallib files"
echo "  Found $(ls -1 "$OUTPUT_DIR/shaders"/*.dxbc 2>/dev/null | wc -l) DXBC files"
echo "  Found $(ls -1 "$OUTPUT_DIR/shaders"/*.dxil 2>/dev/null | wc -l) DXIL files"

# Dump shader JSON metadata if metallibs exist
echo "Extracting shader metadata..."
for metallib in "$OUTPUT_DIR/shaders"/*.metallib; do
    if [ -f "$metallib" ]; then
        base=$(basename "$metallib" .metallib)
        # Try to get function list
        xcrun metal-objdump -list "$metallib" > "$OUTPUT_DIR/shaders/${base}_functions.txt" 2>/dev/null || true
        # Try to disassemble
        xcrun metal-objdump -disassemble "$metallib" > "$OUTPUT_DIR/shaders/${base}_disasm.txt" 2>/dev/null || true
    fi
done

# Collect textures
echo "Collecting texture dumps..."
# Copy PNG output (don't move the reference image)
cp title_414B07D1_frame_*.png "$OUTPUT_DIR/textures/" 2>/dev/null || true
# Move any texture dumps
mv /tmp/xenia_tex_*.raw "$OUTPUT_DIR/textures/" 2>/dev/null || true
mv /tmp/xenia_tex_*.png "$OUTPUT_DIR/textures/" 2>/dev/null || true
cp texture_dump/*.png "$OUTPUT_DIR/textures/" 2>/dev/null || true
echo "  Found $(ls -1 "$OUTPUT_DIR/textures"/*.png 2>/dev/null | wc -l) PNG files"

# Collect GPU captures
echo "Collecting GPU captures..."
mv ~/Documents/*.gputrace "$OUTPUT_DIR/captures/" 2>/dev/null || true
mv /tmp/xenia_gpu_capture_*.gputrace "$OUTPUT_DIR/captures/" 2>/dev/null || true
mv *.gputrace "$OUTPUT_DIR/captures/" 2>/dev/null || true

# Analyze results
echo ""
echo "Performing analysis..."
{
    echo "=== Debug Session Analysis ==="
    echo "Timestamp: $TIMESTAMP"
    echo "Trace File: $TRACE_FILE"
    echo ""
    
    echo "=== Statistics ==="
    echo "Total lines in log: $(wc -l < "$OUTPUT_DIR/logs/full_output.log")"
    echo "Shader compilations: $(grep -c "Compiling shader" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null || echo "0")"
    echo "Texture uploads: $(grep -c "Uploaded.*texture" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null || echo "0")"
    echo "Draw calls: $(grep -c "IssueDraw" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null || echo "0")"
    echo "Errors: $(grep -c "ERROR" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null || echo "0")"
    echo "Warnings: $(grep -c "WARN" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null || echo "0")"
    echo ""
    
    echo "=== Key Issues Found ==="
    
    # Check for empty heaps
    if grep -q "descriptor heap is empty" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null; then
        echo "⚠️  Empty descriptor heaps detected"
        grep "descriptor heap is empty" "$OUTPUT_DIR/logs/full_output.log" | head -5
    fi
    
    # Check for zero scissors
    if grep -q "zero scissor" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null; then
        echo "⚠️  Zero scissor rectangles detected"
        grep "zero scissor" "$OUTPUT_DIR/logs/full_output.log" | head -5
    fi
    
    # Check for validation errors
    if grep -q "validation" "$OUTPUT_DIR/logs/errors_warnings.log" 2>/dev/null; then
        echo "❌ Metal validation errors found"
        grep "validation" "$OUTPUT_DIR/logs/errors_warnings.log" | head -5
    fi
    
    # Check for texture binding
    echo ""
    echo "=== Texture Binding Analysis ==="
    echo "Textures found in heap slots:"
    grep "heap slot" "$OUTPUT_DIR/logs/texture_activity.log" 2>/dev/null | tail -10 || echo "No heap slot entries found"
    
    echo ""
    echo "Texture view IDs:"
    grep "textureViewID" "$OUTPUT_DIR/logs/texture_activity.log" 2>/dev/null | tail -10 || echo "No texture view IDs found"
    
    # Check for render targets
    echo ""
    echo "=== Render Target Analysis ==="
    echo "Color attachments:"
    grep "RB_COLOR" "$OUTPUT_DIR/logs/render_targets.log" 2>/dev/null | tail -10 || echo "No RB_COLOR entries found"
    
    echo ""
    echo "EDRAM operations:"
    grep "EDRAM" "$OUTPUT_DIR/logs/render_targets.log" 2>/dev/null | tail -10 || echo "No EDRAM entries found"
    
    # Shader analysis
    echo ""
    echo "=== Shader Analysis ==="
    echo "Vertex shaders compiled: $(grep -c "vertex.*shader\\|vs_" "$OUTPUT_DIR/logs/shader_activity.log" 2>/dev/null || echo "0")"
    echo "Pixel shaders compiled: $(grep -c "pixel.*shader\\|ps_\\|fragment" "$OUTPUT_DIR/logs/shader_activity.log" 2>/dev/null || echo "0")"
    
    echo ""
    echo "Shader files collected:"
    ls -la "$OUTPUT_DIR/shaders/" 2>/dev/null | head -20 || echo "No shader files found"
    
    # Draw call analysis
    echo ""
    echo "=== Draw Call Analysis ==="
    echo "First 10 draw calls:"
    grep "IssueDraw" "$OUTPUT_DIR/logs/draw_calls.log" 2>/dev/null | head -10 || echo "No draw calls found"
    
    echo ""
    echo "Draw indexed primitives:"
    grep "Drew indexed" "$OUTPUT_DIR/logs/draw_calls.log" 2>/dev/null | tail -10 || echo "No indexed draws found"
    
    # Command buffer analysis
    echo ""
    echo "=== Command Buffer Analysis ==="
    echo "Commits:"
    grep "commit" "$OUTPUT_DIR/logs/command_buffers.log" 2>/dev/null | tail -10 || echo "No commits found"
    
} > "$OUTPUT_DIR/analysis/summary.txt"

# Create a focused debug report
{
    echo "=== CRITICAL DEBUG INFORMATION ==="
    echo ""
    echo "1. TEXTURE BINDING STATUS:"
    echo "-------------------------"
    grep -E "MSC res-heap.*setTexture|textureViewID|heap slot [0-9]+" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null | tail -20
    
    echo ""
    echo "2. VERTEX SHADER INPUTS:"
    echo "-----------------------"
    grep -E "vertex_inputs|vfetch|vertex shader.*texture" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null | tail -20
    
    echo ""
    echo "3. RENDER TARGET CONFIGURATION:"
    echo "------------------------------"
    grep -E "RB_COLOR_INFO|render target|EDRAM.*0x[0-9a-f]+" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null | tail -20
    
    echo ""
    echo "4. DESCRIPTOR HEAP CONTENTS:"
    echo "---------------------------"
    grep -E "heap\[[0-9]+\]:|gpuVA=|tex=0x|meta=" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null | tail -30
    
    echo ""
    echo "5. ACTUAL DRAWS EXECUTED:"
    echo "------------------------"
    grep -E "Drew indexed primitives|Encoded draw" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null | tail -20
    
    echo ""
    echo "6. FINAL COMMAND BUFFER STATE:"
    echo "-----------------------------"
    grep -E "endEncoding|commit|waitUntilCompleted" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null | tail -20
    
} > "$OUTPUT_DIR/analysis/critical_debug.txt"

# Generate HTML report
cat > "$OUTPUT_DIR/analysis/report.html" << 'EOF'
<!DOCTYPE html>
<html>
<head>
    <title>Xenia Metal Debug Report</title>
    <style>
        body { font-family: monospace; margin: 20px; background: #1e1e1e; color: #d4d4d4; }
        h1 { color: #569cd6; }
        h2 { color: #4ec9b0; margin-top: 30px; }
        .section { margin: 20px 0; padding: 10px; background: #2d2d30; border-radius: 5px; }
        .error { color: #f48771; }
        .warning { color: #dcdcaa; }
        .success { color: #6a9955; }
        .code { background: #1e1e1e; padding: 10px; border-left: 3px solid #569cd6; }
        pre { white-space: pre-wrap; word-wrap: break-word; }
    </style>
</head>
<body>
    <h1>Xenia Metal GPU Debug Report</h1>
EOF

# Add timestamp and basic info to HTML
echo "<div class='section'>" >> "$OUTPUT_DIR/analysis/report.html"
echo "<h2>Session Information</h2>" >> "$OUTPUT_DIR/analysis/report.html"
echo "<pre>" >> "$OUTPUT_DIR/analysis/report.html"
echo "Date: $(date)" >> "$OUTPUT_DIR/analysis/report.html"
echo "Trace: $TRACE_FILE" >> "$OUTPUT_DIR/analysis/report.html"
echo "Output Directory: $OUTPUT_DIR" >> "$OUTPUT_DIR/analysis/report.html"
echo "</pre></div>" >> "$OUTPUT_DIR/analysis/report.html"

# Add summary to HTML
echo "<div class='section'>" >> "$OUTPUT_DIR/analysis/report.html"
echo "<h2>Analysis Summary</h2>" >> "$OUTPUT_DIR/analysis/report.html"
echo "<pre class='code'>" >> "$OUTPUT_DIR/analysis/report.html"
cat "$OUTPUT_DIR/analysis/summary.txt" >> "$OUTPUT_DIR/analysis/report.html"
echo "</pre></div>" >> "$OUTPUT_DIR/analysis/report.html"

# Add critical debug info to HTML
echo "<div class='section'>" >> "$OUTPUT_DIR/analysis/report.html"
echo "<h2>Critical Debug Information</h2>" >> "$OUTPUT_DIR/analysis/report.html"
echo "<pre class='code'>" >> "$OUTPUT_DIR/analysis/report.html"
cat "$OUTPUT_DIR/analysis/critical_debug.txt" >> "$OUTPUT_DIR/analysis/report.html"
echo "</pre></div>" >> "$OUTPUT_DIR/analysis/report.html"

echo "</body></html>" >> "$OUTPUT_DIR/analysis/report.html"

# Final summary
echo ""
echo "==============================================="
echo "Debug Session Complete"
echo "==============================================="
echo ""
echo "📁 Output Directory: $OUTPUT_DIR"
echo ""
echo "📊 Key Files:"
echo "  • Full log: $OUTPUT_DIR/logs/full_output.log"
echo "  • Summary: $OUTPUT_DIR/analysis/summary.txt"
echo "  • Critical: $OUTPUT_DIR/analysis/critical_debug.txt"
echo "  • HTML Report: $OUTPUT_DIR/analysis/report.html"
echo ""
echo "📂 Directory Structure:"
tree -L 2 "$OUTPUT_DIR" 2>/dev/null || ls -la "$OUTPUT_DIR/"
echo ""
echo "🔍 Quick Analysis Commands:"
echo "  • View summary: cat $OUTPUT_DIR/analysis/summary.txt"
echo "  • View critical issues: cat $OUTPUT_DIR/analysis/critical_debug.txt"
echo "  • Open HTML report: open $OUTPUT_DIR/analysis/report.html"
echo "  • Search for textures: grep -n 'texture' $OUTPUT_DIR/logs/texture_activity.log"
echo "  • View errors: cat $OUTPUT_DIR/logs/errors_warnings.log"
echo ""
if ls "$OUTPUT_DIR/captures"/*.gputrace 2>/dev/null; then
    echo "🎮 GPU Capture Available:"
    echo "  open $OUTPUT_DIR/captures/*.gputrace"
    echo ""
fi
echo "==============================================="

# Create a quick access script
cat > "$OUTPUT_DIR/view_logs.sh" << 'VIEWER'
#!/bin/bash
# Quick log viewer for this debug session

DIR="$(dirname "$0")"

echo "Select log to view:"
echo "1) Full output"
echo "2) Summary analysis"
echo "3) Critical debug info"
echo "4) Shader activity"
echo "5) Texture activity"
echo "6) Draw calls"
echo "7) Errors and warnings"
echo "8) Render targets"
echo "9) Open HTML report"

read -p "Choice: " choice

case $choice in
    1) less "$DIR/logs/full_output.log" ;;
    2) cat "$DIR/analysis/summary.txt" ;;
    3) cat "$DIR/analysis/critical_debug.txt" ;;
    4) less "$DIR/logs/shader_activity.log" ;;
    5) less "$DIR/logs/texture_activity.log" ;;
    6) less "$DIR/logs/draw_calls.log" ;;
    7) less "$DIR/logs/errors_warnings.log" ;;
    8) less "$DIR/logs/render_targets.log" ;;
    9) open "$DIR/analysis/report.html" ;;
    *) echo "Invalid choice" ;;
esac
VIEWER

chmod +x "$OUTPUT_DIR/view_logs.sh"
echo "Quick viewer script created: $OUTPUT_DIR/view_logs.sh"