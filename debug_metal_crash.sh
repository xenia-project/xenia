#!/bin/bash

# Script to debug Metal crashes with detailed output

echo "=== Running Metal trace dump with crash debugging ==="

# Enable all debugging flags
export OBJC_DEBUG_MISSING_POOLS=YES
export OBJC_DEBUG_POOL_ALLOCATION=YES
export OBJC_PRINT_POOL_HIGHWATER=YES
export METAL_DEVICE_WRAPPER_TYPE=1
export METAL_DEBUG_ERROR_MODE=5
export METAL_SHADER_VALIDATION=1

# Run under lldb to catch the crash
echo "=== Starting LLDB session ==="
lldb -o "run" \
     -o "bt" \
     -o "thread info" \
     -o "register read" \
     -o "exit" \
     -- ./build/bin/Mac/Checked/xenia-gpu-metal-trace-dump \
        testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace 2>&1 | tee metal_crash.log

echo "=== Crash log saved to metal_crash.log ==="

# Also try with guard malloc
echo "=== Running with Guard Malloc ==="
export DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib
export MALLOC_GUARD_EDGES=1
export MALLOC_FILL_SPACE=1
export MALLOC_STRICT_SIZE=1

timeout 10 ./build/bin/Mac/Checked/xenia-gpu-metal-trace-dump \
    testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace 2>&1 | tail -50