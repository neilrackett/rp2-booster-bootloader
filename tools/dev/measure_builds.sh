#!/bin/bash
# Out-of-tree measurement builds of Booster: MinSizeRel, with and without
# DEBUG_MODE. Booster ships MinSizeRel; a Release (-O3) build does not fit its
# 768K slot.
#
# Does not touch booster/build, booster/dist or the submodule pins (it never
# runs build.sh, which re-pins submodules and wipes booster/build). Adds
# -fstack-usage and -fcallgraph-info=su so tools/dev/stackdepth.py can compute
# worst-case stack depth.
#
# Usage: tools/dev/measure_builds.sh [OUT_DIR]
#
# Flags go in through CFLAGS on purpose: passing -DCMAKE_C_FLAGS replaces
# the Pico toolchain's -mcpu=cortex-m0plus -mthumb and every file fails to
# assemble.
#
# Adapted from md-microfirmware-template's tools/dev/measure_builds.sh (v1.3.0).
set -Eeo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${1:-$(mktemp -d)}"
mkdir -p "$OUT"

export PICO_SDK_PATH="$REPO/pico-sdk"
export PICO_EXTRAS_PATH="$REPO/pico-extras"
export FATFS_SDK_PATH="$REPO/fatfs-sdk"
export BOARD_TYPE=pico_w
export PICO_BOARD=pico_w
export DISPLAY_ATARIST=1
export PICO_FLASH_ASSUME_CORE0_SAFE=1
export RELEASE_VERSION="$(tr -d '\r\n ' < "$REPO/version.txt")"
export RELEASE_DATE="measurement"
export RELEASE_TYPE=final

echo "Compiler: $("${PICO_TOOLCHAIN_PATH:+$PICO_TOOLCHAIN_PATH/}arm-none-eabi-gcc" --version | head -1)"
build() {
  local name=$1 cmtype=$2 dbg=$3
  local bdir="$OUT/build-$name"
  rm -rf "$bdir"; mkdir -p "$bdir"
  echo "=== [$name] CMAKE_BUILD_TYPE=$cmtype DEBUG_MODE=$dbg ==="
  ( cd "$bdir" \
    && DEBUG_MODE=$dbg CFLAGS="-fstack-usage -fcallgraph-info=su" \
       cmake "$REPO/booster/src" -DCMAKE_BUILD_TYPE="$cmtype" > cmake.log 2>&1 \
    && DEBUG_MODE=$dbg make -j8 > make.log 2>&1 ) || { echo "[$name] FAILED, see $bdir/make.log"; return 1; }
  arm-none-eabi-size -A "$bdir/booster.elf" | grep -E '^\.(text|rodata|data|bss)\s'
  grep 'Booster flash usage' "$bdir/make.log" || true
  # The heap runs from the end of .bss to core 0's stack (__StackLimit).
  local bss_end stack_limit
  bss_end=$(arm-none-eabi-nm "$bdir/booster.elf" | awk '$3 == "end" {print $1}')
  stack_limit=$(arm-none-eabi-nm "$bdir/booster.elf" | awk '$3 == "__StackLimit" {print $1}')
  printf "heap (end of .bss to __StackLimit): %d bytes\n" $((0x$stack_limit - 0x$bss_end))
}

build minsizerel-rel MinSizeRel 0
build minsizerel-dbg MinSizeRel 1
echo "Outputs in $OUT"
