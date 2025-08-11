#!/bin/bash
# Comprehensive Metal Debug Script for Xenia GPU
# Combines all debugging functionality: GPU capture, texture dumping, shader analysis, buffer inspection

set -e

# Configuration
TRACE_FILE="${1:-testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace}"
BUILD_CONFIG="${2:-Checked}"
CAPTURE_FRAME="${3:-0}"  # Which frame to capture (0 = first frame)
DEBUG_DIR="metal_debug"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
OUTPUT_DIR="$DEBUG_DIR/session_$TIMESTAMP"

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Create comprehensive output directory structure
mkdir -p "$OUTPUT_DIR"/{logs,shaders,textures,captures,analysis,buffers,traces}
mkdir -p "$OUTPUT_DIR"/shaders/{dxbc,dxil,metal,air,reflection}

echo -e "${BLUE}================================================${NC}"
echo -e "${BLUE}    Xenia Metal Comprehensive Debug Session${NC}"
echo -e "${BLUE}================================================${NC}"
echo "Trace File: $TRACE_FILE"
echo "Build Config: $BUILD_CONFIG"
echo "Output Directory: $OUTPUT_DIR"
echo "Capture Frame: $CAPTURE_FRAME"
echo "Timestamp: $TIMESTAMP"
echo ""

# Function to print section headers
print_section() {
    echo -e "\n${GREEN}>>> $1${NC}"
}

# Function to print warnings
print_warning() {
    echo -e "${YELLOW}⚠️  $1${NC}"
}

# Function to print errors
print_error() {
    echo -e "${RED}❌ $1${NC}"
}

# Function to print success
print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

# System Information Collection
print_section "System Information"
{
    echo "=== System Information ==="
    echo "Date: $(date)"
    echo "User: $(whoami)"
    echo "Hostname: $(hostname)"
    echo ""
    echo "=== macOS Information ==="
    sw_vers
    echo ""
    echo "=== GPU Information ==="
    system_profiler SPDisplaysDataType | grep -A 10 "Chipset Model" || echo "GPU info not available"
    echo ""
    echo "=== Metal Information ==="
    xcrun metal --version || echo "Metal compiler not found"
    echo ""
    echo "=== Metal Shader Converter ==="
    if command -v metal-shaderconverter &> /dev/null; then
        metal-shaderconverter --version
    else
        echo "Metal Shader Converter not found"
    fi
    echo ""
    echo "=== Available Metal Tools ==="
    which xcrun && echo "  xcrun: $(xcrun --version | head -1)"
    which metal-objdump && echo "  metal-objdump: available"
    which metal-nm && echo "  metal-nm: available"
    which metal-source && echo "  metal-source: available"
} > "$OUTPUT_DIR/analysis/system_info.txt"
print_success "System information collected"

# Set comprehensive Metal debugging environment
print_section "Configuring Metal Debug Environment"
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

# Additional Xenia-specific debug flags
export XENIA_GPU_METAL_DEBUG=1
export XENIA_GPU_METAL_DUMP_SHADERS=1
export XENIA_GPU_METAL_DUMP_TEXTURES=1
export XENIA_GPU_METAL_TRACE_COMMANDS=1
export XENIA_GPU_METAL_CAPTURE_FRAME=$CAPTURE_FRAME

print_success "Metal validation and debug environment configured"

# Build if needed
if [ ! -f "./build/bin/Mac/$BUILD_CONFIG/xenia-gpu-metal-trace-dump" ]; then
    print_section "Building xenia-gpu-metal-trace-dump"
    ./xb build --config=$(echo $BUILD_CONFIG | tr '[:upper:]' '[:lower:]') --target=xenia-gpu-metal-trace-dump
fi

# Clean up old temporary files before running
print_section "Cleaning up old temporary files"
rm -f temp_shader_*.dxbc temp_shader_*.dxil 2>/dev/null || true
rm -f /tmp/xenia_gpu_capture_*.gputrace 2>/dev/null || true
rm -f /tmp/xenia_tex_*.{raw,png} 2>/dev/null || true

# Set environment variables for debug output paths
export XENIA_DEBUG_OUTPUT_DIR="$OUTPUT_DIR"
export XENIA_SHADER_DUMP_DIR="$OUTPUT_DIR/shaders"
export XENIA_DXBC_OUTPUT_DIR="$OUTPUT_DIR/shaders/dxbc"
export XENIA_DXIL_OUTPUT_DIR="$OUTPUT_DIR/shaders/dxil"
export XENIA_GPU_CAPTURE_DIR="$OUTPUT_DIR/captures"
export XENIA_TEXTURE_DUMP_DIR="$OUTPUT_DIR/textures"

# Run trace dump with full logging
print_section "Running Trace Dump"
echo "This will execute the GPU trace and collect all debug information..."
echo "Debug output directory: $OUTPUT_DIR"
echo "  • DXBC files: $XENIA_DXBC_OUTPUT_DIR"
echo "  • DXIL files: $XENIA_DXIL_OUTPUT_DIR"
echo "  • GPU captures: $XENIA_GPU_CAPTURE_DIR"
echo "  • Textures: $XENIA_TEXTURE_DUMP_DIR"
echo ""

# Run with all debug features enabled
./build/bin/Mac/$BUILD_CONFIG/xenia-gpu-metal-trace-dump \
    "$TRACE_FILE" 2>&1 | tee "$OUTPUT_DIR/logs/full_output.log"

TRACE_EXIT_CODE=${PIPESTATUS[0]}
echo ""
if [ $TRACE_EXIT_CODE -eq 0 ]; then
    print_success "Trace dump completed successfully"
else
    print_warning "Trace dump exited with code $TRACE_EXIT_CODE"
fi

# Collect GPU Captures
print_section "Collecting GPU Captures"
GPU_CAPTURE_COUNT=0

# First check if programmatic capture was attempted
if grep -q "BeginProgrammaticCapture\|Started programmatic GPU capture" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null; then
    echo "  Programmatic capture was initiated"
else
    print_warning "No programmatic capture attempts found in log"
fi

# Check for GPU captures created during this session
echo "  Searching for GPU captures from this session..."
# Since we set XENIA_GPU_CAPTURE_DIR, captures should be directly in our output dir
for capture in "$OUTPUT_DIR/captures"/*.gputrace; do
    if [ -e "$capture" ]; then  # Use -e to check for either file or directory
        ((GPU_CAPTURE_COUNT++))
        if [ -d "$capture" ]; then
            # Get directory size for directories
            SIZE=$(du -sh "$capture" | cut -f1)
            echo "  Found: $(basename $capture) ($SIZE)"
        else
            echo "  Found: $(basename $capture) ($(stat -f%z "$capture") bytes)"
        fi
    fi
done

# Also check if trace dump created one in /tmp (only if not already found)
if [ $GPU_CAPTURE_COUNT -eq 0 ]; then
    echo "  No captures in output dir, checking /tmp for this session..."
    # Only look for captures created after we started (using timestamp)
    for capture in /tmp/xenia_gpu_capture_*.gputrace /tmp/gpu_capture_*.gputrace; do
        if [ -e "$capture" ]; then
            # Check if this capture is newer than our session start
            if [ "$capture" -nt "$OUTPUT_DIR" ]; then
                cp -r "$capture" "$OUTPUT_DIR/captures/" 2>/dev/null && {
                    ((GPU_CAPTURE_COUNT++))
                    echo "  Found and copied: $(basename $capture) from /tmp"
                }
            fi
        fi
    done
fi

if [ $GPU_CAPTURE_COUNT -gt 0 ]; then
    print_success "Collected $GPU_CAPTURE_COUNT GPU capture(s)"
else
    print_warning "No GPU captures found - EndProgrammaticCapture may not be called"
    echo "  Issue: BeginProgrammaticCapture is called but EndProgrammaticCapture is missing"
    echo "  Fix needed in: src/xenia/gpu/metal/metal_command_processor.mm"
fi

# Collect Shader Artifacts
print_section "Collecting Shader Pipeline Artifacts"

# Check for any remaining temp files that weren't moved
for file in temp_shader_*.dxbc; do
    if [ -f "$file" ]; then
        mv "$file" "$OUTPUT_DIR/shaders/dxbc/" 2>/dev/null
    fi
done
for file in temp_shader_*.dxil; do
    if [ -f "$file" ]; then
        mv "$file" "$OUTPUT_DIR/shaders/dxil/" 2>/dev/null
    fi
done

# Count collected files
DXBC_COUNT=$(ls -1 "$OUTPUT_DIR/shaders/dxbc"/*.dxbc 2>/dev/null | wc -l | tr -d ' ')
DXIL_COUNT=$(ls -1 "$OUTPUT_DIR/shaders/dxil"/*.dxil 2>/dev/null | wc -l | tr -d ' ')

echo "  DXBC files: $DXBC_COUNT"
echo "  DXIL files: $DXIL_COUNT"

if [ $DXBC_COUNT -eq 0 ] && [ $DXIL_COUNT -eq 0 ]; then
    print_warning "No DXBC/DXIL files captured - they may be deleted too quickly"
    echo "  Note: temp_shader_*.dxbc and *.dxil files are created during conversion"
    echo "  They are usually deleted after Metal shader creation"
fi

# Metal libraries
METAL_COUNT=0
for file in shader_*.metallib; do
    if [ -f "$file" ]; then
        cp "$file" "$OUTPUT_DIR/shaders/metal/"
        ((METAL_COUNT++))
    fi
done
echo "  Metal libraries: $METAL_COUNT"

print_success "Shader artifacts collected"

# Analyze Metal Shaders
print_section "Analyzing Metal Shaders"
for metallib in "$OUTPUT_DIR/shaders/metal"/*.metallib; do
    if [ -f "$metallib" ]; then
        base=$(basename "$metallib" .metallib)
        echo "  Processing $base..."
        
        # Extract symbols
        xcrun metal-objdump --metallib --syms "$metallib" \
            > "$OUTPUT_DIR/shaders/reflection/${base}_symbols.txt" 2>/dev/null || true
        
        # Get AIR disassembly
        xcrun metal-objdump --metallib --disassemble "$metallib" \
            > "$OUTPUT_DIR/shaders/air/${base}_disasm.air" 2>/dev/null || true
        
        # Extract reflection data
        xcrun metal-objdump --metallib --reflection "$metallib" \
            > "$OUTPUT_DIR/shaders/reflection/${base}_reflection.json" 2>/dev/null || true
        
        # Get AIR version
        xcrun metal-objdump --metallib --air-version "$metallib" \
            > "$OUTPUT_DIR/shaders/reflection/${base}_version.txt" 2>/dev/null || true
        
        # Try to decompile (usually fails for IR-converted shaders)
        xcrun metal-source "$metallib" -o "$OUTPUT_DIR/shaders/metal/${base}.metal" 2>/dev/null || true
    fi
done

# Collect Texture Dumps
print_section "Collecting Texture Dumps"
TEXTURE_COUNT=0

# Collect raw texture dumps
for file in /tmp/xenia_tex_*.raw texture_dump/*.raw; do
    if [ -f "$file" ]; then
        cp "$file" "$OUTPUT_DIR/textures/"
        ((TEXTURE_COUNT++))
    fi
done

# Collect PNG outputs
for file in title_*.png /tmp/xenia_tex_*.png texture_dump/*.png; do
    if [ -f "$file" ]; then
        cp "$file" "$OUTPUT_DIR/textures/"
        ((TEXTURE_COUNT++))
    fi
done

if [ $TEXTURE_COUNT -gt 0 ]; then
    print_success "Collected $TEXTURE_COUNT texture file(s)"
else
    print_warning "No texture dumps found"
fi

# Extract Critical Information from Logs
print_section "Extracting Debug Information"

# Shader compilation info
grep -E "shader.*hash|Converting.*shader|Shader data hash|Reflection JSON" \
    "$OUTPUT_DIR/logs/full_output.log" \
    > "$OUTPUT_DIR/analysis/shader_compilation.txt" 2>/dev/null || true

# Texture binding info
grep -E "texture|Texture|Found valid texture|heap slot|textureViewID|MSC res-heap|setTexture" \
    "$OUTPUT_DIR/logs/full_output.log" \
    > "$OUTPUT_DIR/analysis/texture_binding.txt" 2>/dev/null || true

# Resource binding and descriptors
grep -E "descriptor|Descriptor|heap|IRDescriptor|argument buffer|TopLevelAB" \
    "$OUTPUT_DIR/logs/full_output.log" \
    > "$OUTPUT_DIR/analysis/resource_binding.txt" 2>/dev/null || true

# Buffer contents and memory
grep -E "buffer contents|Buffer\[|Memory:|Uniforms|CB0|CB1" \
    "$OUTPUT_DIR/logs/full_output.log" \
    > "$OUTPUT_DIR/analysis/buffer_contents.txt" 2>/dev/null || true

# Draw calls
grep -E "Metal IssueDraw: Starting draw call|Drew.*primitives|Draw.*state|viewport|scissor" \
    "$OUTPUT_DIR/logs/full_output.log" \
    > "$OUTPUT_DIR/analysis/draw_calls.txt" 2>/dev/null || true

# Render targets
grep -E "RB_COLOR|render target|EDRAM|framebuffer|color attachment" \
    "$OUTPUT_DIR/logs/full_output.log" \
    > "$OUTPUT_DIR/analysis/render_targets.txt" 2>/dev/null || true

# GPU capture events
grep -E "GPU capture|BeginProgrammaticCapture|EndProgrammaticCapture|CaptureManager" \
    "$OUTPUT_DIR/logs/full_output.log" \
    > "$OUTPUT_DIR/analysis/gpu_capture_events.txt" 2>/dev/null || true

# Errors and warnings
grep -E "ERROR|WARN|Failed|invalid|empty.*heap" \
    "$OUTPUT_DIR/logs/full_output.log" \
    > "$OUTPUT_DIR/analysis/errors_warnings.txt" 2>/dev/null || true

print_success "Debug information extracted"

# Analyze Buffer Contents
print_section "Analyzing Buffer Contents"
{
    echo "=== Buffer Content Analysis ==="
    echo ""
    
    echo "== Uniform Buffers =="
    grep "Uniforms buffer" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null | tail -10 || echo "No uniform buffer dumps found"
    
    echo ""
    echo "== Vertex Buffers =="
    grep -A 2 "First vertex" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null | tail -20 || echo "No vertex buffer dumps found"
    
    echo ""
    echo "== Descriptor Heaps =="
    grep -E "heap\[[0-9]+\]:|gpuVA=|tex=0x" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null | tail -30 || echo "No heap dumps found"
    
} > "$OUTPUT_DIR/buffers/buffer_analysis.txt"

# Create Comprehensive Analysis Report
print_section "Creating Analysis Report"
{
    echo "=== Xenia Metal Debug Analysis Report ==="
    echo "Generated: $(date)"
    echo "Trace: $TRACE_FILE"
    echo "Session: $TIMESTAMP"
    echo ""
    
    echo "=== Execution Statistics ==="
    TOTAL_LINES=$(wc -l < "$OUTPUT_DIR/logs/full_output.log")
    DRAW_CALLS=$(grep -c "Metal IssueDraw: Starting draw call" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null || echo "0")
    SHADER_COMPILES=$(grep -c "Converting.*shader" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null || echo "0")
    TEXTURE_UPLOADS=$(grep -c "Uploaded.*texture" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null || echo "0")
    ERRORS=$(grep -c "ERROR" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null || echo "0")
    WARNINGS=$(grep -c "WARN" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null || echo "0")
    
    echo "  Total log lines: $TOTAL_LINES"
    echo "  Draw calls: $DRAW_CALLS"
    echo "  Shader compilations: $SHADER_COMPILES"
    echo "  Texture uploads: $TEXTURE_UPLOADS"
    echo "  Errors: $ERRORS"
    echo "  Warnings: $WARNINGS"
    echo ""
    
    echo "=== Collected Artifacts ==="
    echo "  DXBC files: $DXBC_COUNT"
    echo "  DXIL files: $DXIL_COUNT"
    echo "  Metal libraries: $METAL_COUNT"
    echo "  GPU captures: $GPU_CAPTURE_COUNT"
    echo "  Texture dumps: $TEXTURE_COUNT"
    echo ""
    
    echo "=== Critical Issues Detected ==="
    
    # Check for empty descriptor heaps
    if grep -q "descriptor heap is empty" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null; then
        echo "❌ Empty descriptor heaps detected:"
        grep "descriptor heap is empty" "$OUTPUT_DIR/logs/full_output.log" | head -3
        echo ""
    fi
    
    # Check for texture binding issues
    if grep -q "Found valid texture" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null; then
        echo "✓ Textures ARE being found:"
        grep "Found valid texture" "$OUTPUT_DIR/logs/full_output.log" | head -3
    else
        echo "❌ No textures found"
    fi
    
    if grep -q "Prepared 0 textures" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null; then
        echo ""
        echo "❌ Textures found but NOT uploaded:"
        grep "Prepared.*textures" "$OUTPUT_DIR/logs/full_output.log" | head -3
    fi
    
    # Check render target configuration
    echo ""
    echo "=== Render Target Configuration ==="
    grep "RB_COLOR_INFO" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null | head -5 || echo "No RB_COLOR_INFO found"
    
    # Check for GPU capture issues
    echo ""
    echo "=== GPU Capture Status ==="
    if [ $GPU_CAPTURE_COUNT -eq 0 ]; then
        echo "❌ No GPU captures were generated"
        echo "   Check if MetalDebugUtils::BeginProgrammaticCapture() is being called"
        grep "capture" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null | tail -5 || echo "   No capture-related log entries found"
    else
        echo "✓ $GPU_CAPTURE_COUNT GPU capture(s) generated"
    fi
    
    # List shader pipeline
    echo ""
    echo "=== Shader Pipeline ==="
    echo "Xbox 360 Microcode → DXBC → DXIL → Metal AIR → Metal Library"
    echo ""
    if [ -f "$OUTPUT_DIR/analysis/shader_compilation.txt" ]; then
        echo "Compiled shaders:"
        grep "Shader data hash" "$OUTPUT_DIR/analysis/shader_compilation.txt" | head -10
    fi
    
} > "$OUTPUT_DIR/analysis/report.txt"

# Check Output Image
print_section "Analyzing Output Image"
OUTPUT_PNG="$OUTPUT_DIR/textures/title_414B07D1_frame_589.png"
if [ -f "$OUTPUT_PNG" ]; then
    # Check if ImageMagick is available for analysis
    if command -v identify &> /dev/null; then
        MEAN=$(identify -format "%[mean]" "$OUTPUT_PNG" 2>/dev/null || echo "unknown")
        COLORS=$(identify -format "%k" "$OUTPUT_PNG" 2>/dev/null || echo "unknown")
        SIZE=$(identify -format "%wx%h" "$OUTPUT_PNG" 2>/dev/null || echo "unknown")
        
        echo "  Output PNG Analysis:"
        echo "    Size: $SIZE"
        echo "    Unique colors: $COLORS"
        echo "    Mean brightness: $MEAN"
        
        if [ "$MEAN" = "0" ] || [ "$COLORS" = "1" ]; then
            print_error "Output image appears to be completely BLACK!"
        else
            print_success "Output image has content (not black)"
        fi
    else
        echo "  Output PNG saved (install ImageMagick for analysis)"
    fi
else
    print_warning "No output PNG found"
fi

# Create HTML Report
print_section "Creating HTML Report"
cat > "$OUTPUT_DIR/analysis/report.html" << EOF
<!DOCTYPE html>
<html>
<head>
    <title>Xenia Metal Debug Report - $TIMESTAMP</title>
    <style>
        body { 
            font-family: 'SF Mono', Monaco, 'Courier New', monospace; 
            margin: 20px; 
            background: #1e1e1e; 
            color: #d4d4d4; 
        }
        h1 { color: #569cd6; border-bottom: 2px solid #569cd6; padding-bottom: 10px; }
        h2 { color: #4ec9b0; margin-top: 30px; }
        h3 { color: #dcdcaa; }
        .section { 
            margin: 20px 0; 
            padding: 15px; 
            background: #2d2d30; 
            border-radius: 5px; 
            border-left: 3px solid #569cd6;
        }
        .error { color: #f48771; font-weight: bold; }
        .warning { color: #dcdcaa; font-weight: bold; }
        .success { color: #6a9955; font-weight: bold; }
        .code { 
            background: #1e1e1e; 
            padding: 10px; 
            border-radius: 3px;
            overflow-x: auto;
        }
        pre { white-space: pre-wrap; word-wrap: break-word; }
        table { border-collapse: collapse; width: 100%; margin: 10px 0; }
        th { background: #383838; padding: 8px; text-align: left; }
        td { padding: 8px; border-bottom: 1px solid #383838; }
        .metric { display: inline-block; margin: 10px 20px 10px 0; }
        .metric-value { font-size: 24px; font-weight: bold; color: #569cd6; }
        .metric-label { font-size: 12px; color: #808080; }
    </style>
</head>
<body>
    <h1>Xenia Metal GPU Debug Report</h1>
    
    <div class="section">
        <h2>Session Information</h2>
        <table>
            <tr><td>Date:</td><td>$(date)</td></tr>
            <tr><td>Trace File:</td><td>$TRACE_FILE</td></tr>
            <tr><td>Build Config:</td><td>$BUILD_CONFIG</td></tr>
            <tr><td>Session ID:</td><td>$TIMESTAMP</td></tr>
        </table>
    </div>
    
    <div class="section">
        <h2>Execution Metrics</h2>
        <div class="metric">
            <div class="metric-value">$DRAW_CALLS</div>
            <div class="metric-label">Draw Calls</div>
        </div>
        <div class="metric">
            <div class="metric-value">$SHADER_COMPILES</div>
            <div class="metric-label">Shaders Compiled</div>
        </div>
        <div class="metric">
            <div class="metric-value">$TEXTURE_UPLOADS</div>
            <div class="metric-label">Textures Uploaded</div>
        </div>
        <div class="metric">
            <div class="metric-value">$GPU_CAPTURE_COUNT</div>
            <div class="metric-label">GPU Captures</div>
        </div>
    </div>
    
    <div class="section">
        <h2>Issues Detected</h2>
        <pre class="code">
$(grep -E "ERROR|WARN|empty.*heap|Found valid texture|Prepared.*textures" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null | head -20)
        </pre>
    </div>
    
    <div class="section">
        <h2>File Artifacts</h2>
        <table>
            <tr><th>Type</th><th>Count</th><th>Location</th></tr>
            <tr><td>DXBC Files</td><td>$DXBC_COUNT</td><td>shaders/dxbc/</td></tr>
            <tr><td>DXIL Files</td><td>$DXIL_COUNT</td><td>shaders/dxil/</td></tr>
            <tr><td>Metal Libraries</td><td>$METAL_COUNT</td><td>shaders/metal/</td></tr>
            <tr><td>GPU Captures</td><td>$GPU_CAPTURE_COUNT</td><td>captures/</td></tr>
            <tr><td>Texture Dumps</td><td>$TEXTURE_COUNT</td><td>textures/</td></tr>
        </table>
    </div>
</body>
</html>
EOF

print_success "HTML report created"

# Create Quick Access Script
cat > "$OUTPUT_DIR/analyze.sh" << 'ANALYZER'
#!/bin/bash
# Quick analysis tool for this debug session

DIR="$(dirname "$0")"

echo "=== Xenia Metal Debug Session Analyzer ==="
echo ""
echo "1) View summary report"
echo "2) Search for texture binding issues"
echo "3) View shader compilations"
echo "4) Check GPU capture status"
echo "5) View errors and warnings"
echo "6) Analyze buffer contents"
echo "7) View render target config"
echo "8) Open HTML report"
echo "9) Open GPU capture in Xcode (if available)"
echo "10) Search logs"
echo ""
read -p "Select option (1-10): " choice

case $choice in
    1) cat "$DIR/analysis/report.txt" | less ;;
    2) grep -E "texture|Texture|Found valid|heap slot" "$DIR/logs/full_output.log" | less ;;
    3) cat "$DIR/analysis/shader_compilation.txt" | less ;;
    4) cat "$DIR/analysis/gpu_capture_events.txt" ;;
    5) cat "$DIR/analysis/errors_warnings.txt" | less ;;
    6) cat "$DIR/buffers/buffer_analysis.txt" | less ;;
    7) cat "$DIR/analysis/render_targets.txt" | less ;;
    8) open "$DIR/analysis/report.html" ;;
    9) 
        CAPTURE=$(ls "$DIR/captures"/*.gputrace 2>/dev/null | head -1)
        if [ -f "$CAPTURE" ]; then
            open "$CAPTURE"
        else
            echo "No GPU capture found"
        fi
        ;;
    10) 
        read -p "Enter search term: " term
        grep -i "$term" "$DIR/logs/full_output.log" | less
        ;;
    *) echo "Invalid option" ;;
esac
ANALYZER
chmod +x "$OUTPUT_DIR/analyze.sh"

# Final Summary
echo ""
echo -e "${BLUE}================================================${NC}"
echo -e "${BLUE}           Debug Session Complete${NC}"
echo -e "${BLUE}================================================${NC}"
echo ""
echo "📁 Output Directory: $OUTPUT_DIR"
echo ""
echo "📊 Summary:"
echo "  • Draw Calls: $DRAW_CALLS"
echo "  • Shaders Compiled: $SHADER_COMPILES"
echo "  • GPU Captures: $GPU_CAPTURE_COUNT"
echo "  • Errors: $ERRORS"
echo "  • Warnings: $WARNINGS"
echo ""

if [ $GPU_CAPTURE_COUNT -eq 0 ]; then
    print_warning "No GPU captures found - check programmatic capture implementation"
fi

if [ $ERRORS -gt 0 ]; then
    print_warning "$ERRORS errors detected - review logs"
fi

echo ""
echo "🔧 Quick Access:"
echo "  • Interactive analyzer: $OUTPUT_DIR/analyze.sh"
echo "  • Summary report: cat $OUTPUT_DIR/analysis/report.txt"
echo "  • HTML report: open $OUTPUT_DIR/analysis/report.html"
echo "  • Full log: less $OUTPUT_DIR/logs/full_output.log"
echo ""

if [ $GPU_CAPTURE_COUNT -gt 0 ]; then
    echo "🎮 GPU Capture:"
    for capture in "$OUTPUT_DIR/captures"/*.gputrace; do
        if [ -f "$capture" ]; then
            echo "  • open $capture"
        fi
    done
    echo ""
fi

echo "🔍 Key Commands:"
echo "  • Find textures: grep 'Found valid texture' $OUTPUT_DIR/logs/full_output.log"
echo "  • Check heaps: grep 'heap\\[' $OUTPUT_DIR/logs/full_output.log"
echo "  • View draws: grep 'Metal IssueDraw: Starting draw call' $OUTPUT_DIR/logs/full_output.log | head"
echo ""
echo -e "${BLUE}================================================${NC}"

# Check for critical issues and provide recommendations
UNSUPPORTED_FORMATS=$(grep -c "Unsupported.*format" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null || echo "0")
TEXTURES_BOUND=$(grep -c "Bound.*texture at heap slot" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null || echo "0")
TEXTURES_SET=$(grep -c "MSC res-heap: setTexture" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null || echo "0")

if [ $UNSUPPORTED_FORMATS -gt 0 ]; then
    echo ""
    echo -e "${YELLOW}⚠️  UNSUPPORTED TEXTURE FORMATS:${NC}"
    echo "Found $UNSUPPORTED_FORMATS unsupported texture format(s):"
    grep "Unsupported.*format" "$OUTPUT_DIR/logs/full_output.log" | head -3
    echo ""
    if [ $TEXTURES_BOUND -gt 0 ]; then
        echo "However, $TEXTURES_BOUND other texture(s) were successfully bound."
    fi
fi

if [ $TEXTURES_BOUND -eq 0 ] && grep -q "Found valid texture" "$OUTPUT_DIR/logs/full_output.log" 2>/dev/null; then
    echo ""
    echo -e "${RED}⚠️  CRITICAL ISSUE DETECTED:${NC}"
    echo "Textures are being found but NONE are being bound to descriptor heaps."
    echo ""
    echo "Recommended fix location:"
    echo "  src/xenia/gpu/metal/metal_command_processor.mm"
    echo "  Look for texture upload and binding code"
elif [ $TEXTURES_SET -gt 0 ]; then
    echo ""
    echo -e "${GREEN}✓ Texture binding is working:${NC}"
    echo "  • $TEXTURES_BOUND texture(s) bound to heap slots"
    echo "  • $TEXTURES_SET texture(s) set in descriptor heaps"
fi

exit 0