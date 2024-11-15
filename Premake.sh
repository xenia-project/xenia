#!/usr/bin/env zsh

# ANSI color codes
PURPLE='\033[0;35m'
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# This gets the location that the script is being run from and moves there.
SCRIPT_DIR=${0:a:h}
cd "$SCRIPT_DIR"

# Detect CPU architecture
ARCH_NAME="$(uname -m)"

# build a premake5 binary
git clone https://github.com/premake/premake-core
cd premake-core

echo "${PURPLE}Building Premake for ${GREEN}${ARCH_NAME}${NC}...${NC}"
if [[ "${ARCH_NAME}" == "arm64" ]]; then 
	make -f Bootstrap.mak osx PLATFORM=ARM
	else 
	make -f Bootstrap.mak osx
fi

# Check for failure. Exit if there were any problems  
if [ $? -ne 0 ]; then
	echo -e "${RED}Error:${PURPLE} Could not build Premake${NC}"
	exit 1
fi

cd build/bootstrap
make config=release

cd $SCRIPT_DIR
# The premake5 binary will be in premake-core/bin/release
echo "${PURPLE}Moving Premake to script directory...${NC}"
mv premake-core/bin/release/premake5 ./premake5
rm -rf premake-core
