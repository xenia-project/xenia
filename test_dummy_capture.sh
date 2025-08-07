#!/bin/bash
# Test script to check if dummy color target capture is working

# Build in debug mode
echo "Building..."
xcodebuild -project xenia-gpu-metal-trace-dump.xcodeproj -configuration Debug build 2>&1 | tail -5

# Create a minimal test trace if needed
# For now, we'll just check if the binary runs
echo "Running test..."
./bin/Mac/Debug/xenia-gpu-metal-trace-dump --help 2>&1 | head -20

echo "Done"