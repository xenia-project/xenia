#!/bin/bash

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}Building Xenia with JIT entitlements for macOS ARM64...${NC}"

# Build using premake
echo -e "${YELLOW}Step 1: Generating build files with premake5...${NC}"
./premake5 --os=macosx xcode4
if [ $? -ne 0 ]; then
    echo -e "${RED}Error: Failed to generate build files${NC}"
    exit 1
fi

echo -e "${YELLOW}Step 2: Building with xcodebuild...${NC}"
cd build

# Build the CPU PPC tests project with our entitlements
echo -e "${YELLOW}Building xenia-cpu-ppc-tests with JIT entitlements...${NC}"
xcodebuild -project xenia-cpu-ppc-tests.xcodeproj -configuration Checked -destination "platform=macOS,arch=arm64" build
if [ $? -ne 0 ]; then
    echo -e "${RED}Error: Failed to build xenia-cpu-ppc-tests${NC}"
    exit 1
fi

# Apply entitlements to the built binary
echo -e "${YELLOW}Step 3: Applying JIT entitlements to built binary...${NC}"
cd ..

# Find the built binary
XENIA_BINARY="build/bin/Mac/Checked/xenia-cpu-ppc-tests"
if [ ! -f "$XENIA_BINARY" ]; then
    echo -e "${RED}Error: Could not find built binary at $XENIA_BINARY${NC}"
    echo -e "${YELLOW}Checking what was actually built...${NC}"
    find build/bin -name "*ppc*" -type f 2>/dev/null || echo "No PPC test binaries found"
    find build/bin -type f 2>/dev/null | head -10
    exit 1
fi

echo -e "${YELLOW}Codesigning with entitlements...${NC}"
codesign --force --options runtime --entitlements xenia.entitlements --sign - "$XENIA_BINARY"
if [ $? -ne 0 ]; then
    echo -e "${RED}Error: Failed to apply entitlements${NC}"
    exit 1
fi

echo -e "${GREEN}Build completed successfully!${NC}"
echo -e "${BLUE}Binary location: $XENIA_BINARY${NC}"
echo -e "${BLUE}Entitlements applied: xenia.entitlements${NC}"

# Verify entitlements
echo -e "${YELLOW}Verifying applied entitlements...${NC}"
codesign -d --entitlements :- "$XENIA_BINARY"

echo -e "${GREEN}JIT-enabled Xenia build complete!${NC}"
