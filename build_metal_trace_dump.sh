#!/bin/bash
# Build Metal trace dump target

echo "Building xenia-gpu-metal-trace-dump..."
./xb build --target xenia-gpu-metal-trace-dump

if [ $? -eq 0 ]; then
    echo "Build successful"
else
    echo "Build failed"
    exit 1
fi