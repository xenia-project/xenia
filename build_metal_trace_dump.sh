#!/bin/bash
# Build Metal trace dump target

echo "Building xenia-gpu-metal-trace-dump..."
xcodebuild -project xenia-gpu-metal-trace-dump.xcodeproj \
    -configuration Release \
    -parallelizeTargets \
    -jobs 8 \
    build

if [ $? -eq 0 ]; then
    echo "Build successful"
else
    echo "Build failed"
    exit 1
fi