#!/bin/bash

# Script to debug autorelease pool crash with lldb

echo "Starting LLDB debugging session for autorelease pool crash..."

lldb ./build/bin/Mac/Checked/xenia-gpu-metal-trace-dump <<EOF
# Set breakpoints on key functions
breakpoint set --name objc_autoreleasePoolPop
breakpoint set --name objc_autoreleasePoolPush
breakpoint set --file metal_render_target_cache.cc --line 68
breakpoint set --file metal_render_target_cache.cc --line 142

# Run with arguments
run testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace

# When it breaks, continue until we hit the crash
continue
continue
continue

# Get thread info when we crash
thread backtrace all
frame variable
register read

# Exit
quit
EOF