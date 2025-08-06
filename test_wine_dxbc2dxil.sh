#!/bin/bash

# Test Wine and dxbc2dxil.exe setup

echo "Testing Wine setup for dxbc2dxil.exe..."
echo "Working directory: $(pwd)"
echo ""

# Check if Wine is available
if ! command -v wine &> /dev/null; then
    echo "ERROR: Wine not found in PATH"
    echo "Please install Wine or ensure it's in your PATH"
    exit 1
fi

echo "Wine version: $(wine --version)"
echo ""

# Check if dxbc2dxil.exe exists
DXBC2DXIL_PATH="./third_party/dxbc2dxil/bin/dxbc2dxil.exe"
if [ ! -f "$DXBC2DXIL_PATH" ]; then
    echo "ERROR: dxbc2dxil.exe not found at: $DXBC2DXIL_PATH"
    echo "Please ensure the file exists"
    exit 1
fi

echo "Found dxbc2dxil.exe at: $DXBC2DXIL_PATH"
echo ""

# Set up Wine environment
BIN_DIR=$(dirname "$DXBC2DXIL_PATH")
export WINEPATH="$BIN_DIR"
export WINEDEBUG="-all"  # Suppress Wine debug messages

echo "Testing dxbc2dxil.exe with Wine..."
echo "Command: WINEPATH=\"$BIN_DIR\" wine \"$DXBC2DXIL_PATH\" /?"
echo ""

# Test the executable
WINEPATH="$BIN_DIR" wine "$DXBC2DXIL_PATH" /? 2>&1

if [ $? -eq 0 ]; then
    echo ""
    echo "SUCCESS: Wine can run dxbc2dxil.exe"
else
    echo ""
    echo "ERROR: Wine failed to run dxbc2dxil.exe"
    echo "Possible issues:"
    echo "1. Wine is not properly installed or configured"
    echo "2. Missing Wine dependencies (try: brew install wine)"
    echo "3. dxbc2dxil.exe dependencies are not found"
fi