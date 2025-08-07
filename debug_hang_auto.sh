#!/bin/bash

# Automated crash debugging script that captures the same info as XCode
# This script runs the Metal trace dump under lldb and captures detailed crash info

TRACE_FILE="testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace"
BINARY="./build/bin/Mac/Checked/xenia-gpu-metal-trace-dump"
OUTPUT_LOG="metal_crash_$(date +%Y%m%d_%H%M%S).log"

echo "=== Starting automated crash debugging session ===" | tee "$OUTPUT_LOG"
echo "Binary: $BINARY" | tee -a "$OUTPUT_LOG"
echo "Trace: $TRACE_FILE" | tee -a "$OUTPUT_LOG"
echo "Output: $OUTPUT_LOG" | tee -a "$OUTPUT_LOG"
echo "" | tee -a "$OUTPUT_LOG"

# Create lldb command file
cat > .lldb_commands << 'EOF'
# Set up exception breakpoints
break set -n objc_exception_throw
break set -n __cxa_throw
break set -E objc
break set -n objc_autoreleasePoolPop
break set -n objc_release
break set -n objc_tls_direct_base::dtor_

# Set up signal handlers
process handle SIGABRT -s true -p true -n false
process handle SIGSEGV -s true -p true -n false
process handle SIGBUS -s true -p true -n false
process handle EXC_BAD_ACCESS -s true -p true -n false

# Run the program
run

# When it stops, gather all the info
thread backtrace all
register read
memory read $rdi -c 256
disassemble -f
thread info
frame info
frame variable

# Continue if possible
continue

# If it stops again
thread backtrace all
quit
EOF

# Run under lldb with environment
echo "=== Running under LLDB with full environment ===" | tee -a "$OUTPUT_LOG"
OBJC_DEBUG_MISSING_POOLS=YES \
OBJC_DEBUG_POOL_ALLOCATION=YES \
OBJC_PRINT_POOL_HIGHWATER=YES \
METAL_DEVICE_WRAPPER_TYPE=1 \
METAL_DEBUG_ERROR_MODE=5 \
METAL_SHADER_VALIDATION=1 \
lldb -s .lldb_commands -o quit "$BINARY" -- "$TRACE_FILE" 2>&1 | tee -a "$OUTPUT_LOG"

# Clean up
rm -f .lldb_commands

echo "" | tee -a "$OUTPUT_LOG"
echo "=== Symbolication ===" | tee -a "$OUTPUT_LOG"

# Try to symbolicate any addresses in the log
if command -v atos &> /dev/null; then
    echo "Symbolicating addresses..." | tee -a "$OUTPUT_LOG"
    # Extract hex addresses and symbolicate them
    grep -E '0x[0-9a-fA-F]+' "$OUTPUT_LOG" | while read line; do
        for addr in $(echo "$line" | grep -oE '0x[0-9a-fA-F]+'); do
            symbol=$(atos -o "$BINARY" "$addr" 2>/dev/null)
            if [ "$symbol" != "$addr" ]; then
                echo "$addr -> $symbol" | tee -a "$OUTPUT_LOG"
            fi
        done
    done
fi

echo "" | tee -a "$OUTPUT_LOG"
echo "=== Analysis complete ===" | tee -a "$OUTPUT_LOG"
echo "Full log saved to: $OUTPUT_LOG" | tee -a "$OUTPUT_LOG"

# Extract key crash info
echo "" | tee -a "$OUTPUT_LOG"
echo "=== Key Crash Points ===" | tee -a "$OUTPUT_LOG"
grep -A5 -B5 "EXC_BAD_ACCESS\|SIGSEGV\|objc_autoreleasePoolPop\|objc_release\|objc_tls_direct_base" "$OUTPUT_LOG" | tail -20 | tee -a "$OUTPUT_LOG"