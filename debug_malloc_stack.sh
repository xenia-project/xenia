#!/bin/bash

echo "Running with MallocStackLogging to track object allocations..."

# Enable malloc stack logging
export MallocStackLogging=1
export MallocStackLoggingNoCompact=1

# Run with guard malloc to catch over-releases
export DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib
export MALLOC_GUARD_EDGES=1

echo "Running trace dump with memory debugging enabled..."
./build/bin/Mac/Checked/xenia-gpu-metal-trace-dump testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace 2>&1 | head -100

echo "If it crashed, you can use:"
echo "lldb ./build/bin/Mac/Checked/xenia-gpu-metal-trace-dump"
echo "(lldb) run testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace"
echo "After crash:"
echo "(lldb) thread backtrace all"
echo "(lldb) malloc_history <pid> <address>"