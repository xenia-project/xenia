#!/bin/bash

# Debug script to find zombie threads and hanging shutdown

echo "=== Testing Metal trace dump with zombie detection ==="

# Enable zombie objects to catch over-releases
export NSZombieEnabled=YES
export MallocStackLogging=1
export OBJC_DEBUG_MISSING_POOLS=YES

# Run with timeout and capture output
timeout -s KILL 5 ./build/bin/Mac/Checked/xenia-gpu-metal-trace-dump \
    testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace 2>&1 | tail -100

echo ""
echo "=== Process terminated after 5 seconds ==="

# Check for any remaining processes
ps aux | grep xenia-gpu-metal | grep -v grep