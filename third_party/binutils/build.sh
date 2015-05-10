#!/usr/bin/env bash
set -e

THIS_SCRIPT_DIR=$( cd "$( dirname "$0" )" && pwd )

SNAPSHOT_VERSION=2.24
echo ""
echo "Building binutils from snapshot version $SNAPSHOT_VERSION..."

echo ""
echo "Ensure you have the following packages installed:"
echo "  build-essential texinfo flex bison"

echo ""
echo "Removing old output..."
rm -rf ./bin
rm -rf ./powerpc-none-elf
rm -rf ./share

SNAPSHOT_FILE=binutils-$SNAPSHOT_VERSION.tar.gz
if [ ! -f $SNAPSHOT_FILE ]; then
  echo ""
  echo "Grabbing binutils snapshot $SNAPSHOT_FILE..."
  curl -nv http://mirrors.kernel.org/sourceware/binutils/releases/$SNAPSHOT_FILE > $SNAPSHOT_FILE
else
  echo ""
  echo "Reusing existing binutils snapshot file $SNAPSHOT_FILE..."
fi

echo ""
echo "Extracting binutils snapshot $SNAPSHOT_FILE..."
tar zxf $SNAPSHOT_FILE

cd binutils-$SNAPSHOT_VERSION/

echo ""
echo "Patching binutils with vmx128 support..."
patch -p0 < $THIS_SCRIPT_DIR/binutils-$SNAPSHOT_VERSION-vmx128.patch

echo ""
echo "Running ./configure..."
./configure \
    --disable-debug \
    --disable-dependency-tracking \
    --disable-werror \
    --enable-interwork \
    --enable-multilib \
    --target=powerpc-none-elf \
    --with-gnu-ld \
    --with-gnu-as \
    --prefix=$THIS_SCRIPT_DIR

echo ""
echo "Running make..."
make

echo ""
echo "Running make install..."
make install

cd ..

echo ""
echo "Cleaning up binutils build temp"
#rm -rf ./binutils-$SNAPSHOT_VERSION
