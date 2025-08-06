#!/bin/bash

echo "Running with NSZombieEnabled to track over-released objects..."

# Enable NSZombie to keep deallocated objects around
export NSZombieEnabled=YES
export NSDebugEnabled=YES
export MallocStackLogging=1

echo "Running trace dump with NSZombie enabled..."
./build/bin/Mac/Checked/xenia-gpu-metal-trace-dump testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace 2>&1 | grep -E "message sent to deallocated|METAL_DEBUG|autorelease|Zombie" | head -50

echo ""
echo "If you see 'message sent to deallocated instance', that's the over-released object!"