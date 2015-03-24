#!/bin/sh
set -e

THIS_SCRIPT_DIR=$( cd "$( dirname "$0" )" && pwd )

BINUTILS=$THIS_SCRIPT_DIR/../../../../../../third_party/binutils/bin/
PPC_AS=$BINUTILS/powerpc-none-elf-as
PPC_LD=$BINUTILS/powerpc-none-elf-ld
PPC_OBJDUMP=$BINUTILS/powerpc-none-elf-objdump
PPC_NM=$BINUTILS/powerpc-none-elf-nm

BIN=$THIS_SCRIPT_DIR/bin/

if [ -d "$BIN" ]; then
  rm -f $BIN/*.o
  rm -f $BIN/*.dis
  rm -f $BIN/*.bin
  rm -f $BIN/*.map
else
  mkdir -p $BIN
fi

SRCS=$THIS_SCRIPT_DIR/*.s
for SRC in $SRCS
do
  SRC_NAME=$(basename $SRC)
  OBJ_FILE=$BIN/${SRC_NAME%.*}.o

  $PPC_AS \
    -a64 \
    -be \
    -mregnames \
    -mpower7 \
    -maltivec \
    -mvsx \
    -mvmx128 \
    -R \
    -o $OBJ_FILE \
    $SRC

  $PPC_OBJDUMP \
    --adjust-vma=0x100000 \
    -Mpower7 \
    -Mvmx128 \
    -D \
    -EB \
    $OBJ_FILE \
    > $BIN/${SRC_NAME%.*}.dis

  $PPC_LD \
    -A powerpc:common64 \
    -melf64ppc \
    -EB \
    -nostdlib \
    --oformat binary \
    -Ttext 0x100000 \
    -e 0x100000 \
    -o $BIN/${SRC_NAME%.*}.bin \
    $OBJ_FILE

  $PPC_NM \
    --numeric-sort \
    $OBJ_FILE \
    > $BIN/${SRC_NAME%.*}.map

done
