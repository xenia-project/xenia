#!/bin/bash

# Metal Debug Session Analyzer
# This script prepares a debug session for analysis by the Metal Debug Analyzer Agent

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Default to latest session if not specified
if [ -z "$1" ]; then
    SESSION_DIR=$(ls -td metal_debug/session_* 2>/dev/null | head -1)
    if [ -z "$SESSION_DIR" ]; then
        echo -e "${RED}Error: No debug sessions found${NC}"
        echo "Usage: $0 [session_directory]"
        echo "Example: $0 metal_debug/session_20250808_131546"
        exit 1
    fi
    echo -e "${YELLOW}Using latest session: $SESSION_DIR${NC}"
else
    SESSION_DIR="$1"
fi

# Verify session directory exists
if [ ! -d "$SESSION_DIR" ]; then
    echo -e "${RED}Error: Session directory not found: $SESSION_DIR${NC}"
    exit 1
fi

echo -e "${BLUE}================================================${NC}"
echo -e "${BLUE}    Metal Debug Session Analyzer${NC}"
echo -e "${BLUE}================================================${NC}"
echo ""
echo -e "${GREEN}Session: ${NC}$SESSION_DIR"
echo ""

# Create analysis package
ANALYSIS_PACKAGE="$SESSION_DIR/analysis_package.md"

cat > "$ANALYSIS_PACKAGE" << 'EOF'
# Metal Debug Session Analysis Request

Please analyze this Metal graphics debug session using the Metal Debug Analyzer Agent guidelines.

## Session Information
EOF

echo "- **Session Path**: $SESSION_DIR" >> "$ANALYSIS_PACKAGE"
echo "- **Analysis Date**: $(date '+%Y-%m-%d %H:%M:%S')" >> "$ANALYSIS_PACKAGE"
echo "" >> "$ANALYSIS_PACKAGE"

# Add quick statistics
echo "## Quick Statistics" >> "$ANALYSIS_PACKAGE"
echo '```' >> "$ANALYSIS_PACKAGE"

# Extract key metrics
if [ -f "$SESSION_DIR/analysis/report.txt" ]; then
    grep -E "Draw Calls:|Shaders Compiled:|GPU Captures:|Errors:|Warnings:" "$SESSION_DIR/analysis/report.txt" >> "$ANALYSIS_PACKAGE" 2>/dev/null || true
fi
echo '```' >> "$ANALYSIS_PACKAGE"
echo "" >> "$ANALYSIS_PACKAGE"

# Add error summary
echo "## Errors and Warnings Summary" >> "$ANALYSIS_PACKAGE"
echo '```' >> "$ANALYSIS_PACKAGE"
if [ -f "$SESSION_DIR/analysis/errors_warnings.txt" ]; then
    head -20 "$SESSION_DIR/analysis/errors_warnings.txt" >> "$ANALYSIS_PACKAGE"
    LINE_COUNT=$(wc -l < "$SESSION_DIR/analysis/errors_warnings.txt")
    if [ "$LINE_COUNT" -gt 20 ]; then
        echo "... ($(($LINE_COUNT - 20)) more lines)" >> "$ANALYSIS_PACKAGE"
    fi
else
    echo "No errors/warnings file found" >> "$ANALYSIS_PACKAGE"
fi
echo '```' >> "$ANALYSIS_PACKAGE"
echo "" >> "$ANALYSIS_PACKAGE"

# Add render target info
echo "## Render Target Configuration" >> "$ANALYSIS_PACKAGE"
echo '```' >> "$ANALYSIS_PACKAGE"
if [ -f "$SESSION_DIR/analysis/render_targets.txt" ]; then
    grep -E "RB_COLOR_INFO|CREATED AND BOUND|EDRAM:" "$SESSION_DIR/analysis/render_targets.txt" | head -10 >> "$ANALYSIS_PACKAGE" 2>/dev/null || true
else
    echo "No render targets file found" >> "$ANALYSIS_PACKAGE"
fi
echo '```' >> "$ANALYSIS_PACKAGE"
echo "" >> "$ANALYSIS_PACKAGE"

# Add texture binding info
echo "## Texture Binding Status" >> "$ANALYSIS_PACKAGE"
echo '```' >> "$ANALYSIS_PACKAGE"
if [ -f "$SESSION_DIR/analysis/texture_binding.txt" ]; then
    grep -E "Found valid texture|Prepared.*textures|No texture binding" "$SESSION_DIR/analysis/texture_binding.txt" | head -10 >> "$ANALYSIS_PACKAGE" 2>/dev/null || true
else
    echo "No texture binding file found" >> "$ANALYSIS_PACKAGE"
fi
echo '```' >> "$ANALYSIS_PACKAGE"
echo "" >> "$ANALYSIS_PACKAGE"

# Add draw call summary
echo "## Draw Call Summary" >> "$ANALYSIS_PACKAGE"
echo '```' >> "$ANALYSIS_PACKAGE"
if [ -f "$SESSION_DIR/analysis/draw_calls.txt" ]; then
    tail -10 "$SESSION_DIR/analysis/draw_calls.txt" >> "$ANALYSIS_PACKAGE"
else
    echo "No draw calls file found" >> "$ANALYSIS_PACKAGE"
fi
echo '```' >> "$ANALYSIS_PACKAGE"
echo "" >> "$ANALYSIS_PACKAGE"

# Check output image
echo "## Output Status" >> "$ANALYSIS_PACKAGE"
if [ -f "$SESSION_DIR/textures/title_414B07D1_frame_589.png" ]; then
    # Check if image is all black (requires ImageMagick)
    if command -v identify &> /dev/null; then
        MEAN_VALUE=$(convert "$SESSION_DIR/textures/title_414B07D1_frame_589.png" -colorspace Gray -format "%[mean]" info: 2>/dev/null || echo "unknown")
        echo "- Output PNG exists" >> "$ANALYSIS_PACKAGE"
        echo "- Mean pixel value: $MEAN_VALUE" >> "$ANALYSIS_PACKAGE"
        if [ "$MEAN_VALUE" = "0" ]; then
            echo "- **Status: Completely BLACK output**" >> "$ANALYSIS_PACKAGE"
        elif (( $(echo "$MEAN_VALUE < 1000" | bc -l 2>/dev/null || echo 0) )); then
            echo "- **Status: Nearly black output**" >> "$ANALYSIS_PACKAGE"
        else
            echo "- **Status: Has visible content**" >> "$ANALYSIS_PACKAGE"
        fi
    else
        echo "- Output PNG exists (install ImageMagick for pixel analysis)" >> "$ANALYSIS_PACKAGE"
    fi
else
    echo "- **No output PNG found**" >> "$ANALYSIS_PACKAGE"
fi
echo "" >> "$ANALYSIS_PACKAGE"

# Add file listing
echo "## Available Files" >> "$ANALYSIS_PACKAGE"
echo '```' >> "$ANALYSIS_PACKAGE"
find "$SESSION_DIR" -type f -name "*.txt" -o -name "*.log" -o -name "*.html" -o -name "*.json" | sed "s|$SESSION_DIR/||" | sort >> "$ANALYSIS_PACKAGE"
echo '```' >> "$ANALYSIS_PACKAGE"
echo "" >> "$ANALYSIS_PACKAGE"

# Add specific analysis requests
cat >> "$ANALYSIS_PACKAGE" << 'EOF'
## Analysis Request

Please provide:
1. **Root cause** of the rendering issue
2. **Specific code locations** that need fixes
3. **Priority-ordered fix list**
4. **Validation steps** to confirm fixes

Focus on getting visible output, even if imperfect.

## Additional Context

This is from the Xenia Xbox 360 emulator's Metal backend, running in trace dump mode (single frame replay, no presenter).

Key systems:
- Metal Shader Converter (MSC) for dynamic resource binding
- EDRAM emulation for Xbox 360's unified memory
- DXBC → DXIL → Metal shader pipeline
- Programmatic vertex fetch (vfetch) for vertex textures
EOF

echo ""
echo -e "${GREEN}✓ Analysis package created${NC}: $ANALYSIS_PACKAGE"
echo ""

# Generate focused grep commands for quick analysis
echo -e "${BLUE}Quick Analysis Commands:${NC}"
echo ""
echo "# Show all errors:"
echo "grep ERROR $SESSION_DIR/logs/full_output.log | head -20"
echo ""
echo "# Check texture binding flow:"
echo "grep -A2 -B2 'Found valid texture' $SESSION_DIR/logs/full_output.log | head -30"
echo ""
echo "# Verify render target creation:"
echo "grep 'CREATED AND BOUND' $SESSION_DIR/logs/full_output.log"
echo ""
echo "# Check shader compilation:"
echo "ls -la $SESSION_DIR/shaders/metal/*.metallib 2>/dev/null | wc -l"
echo ""

# Copy to clipboard if available
if command -v pbcopy &> /dev/null; then
    cat "$ANALYSIS_PACKAGE" | pbcopy
    echo -e "${GREEN}✓ Analysis package copied to clipboard${NC}"
    echo ""
fi

echo -e "${BLUE}================================================${NC}"
echo ""
echo "To analyze with Claude:"
echo "1. Share the Metal Debug Analyzer Agent instructions"
echo "2. Paste the analysis package from: $ANALYSIS_PACKAGE"
echo "3. Ask for specific debugging help"
echo ""
echo "Or use the one-liner:"
echo -e "${YELLOW}cat metal_debug_analyzer_agent.md $ANALYSIS_PACKAGE | pbcopy${NC}"
echo ""