#!/bin/bash
# Build Booster out of tree, flash it, and confirm the RP runs it.
#
#   tools/dev/flash.sh <release|debug> [--src DIR] [--build-only] [--probe]
#                      [--timeout S]
#
# Builds into tools/dev/builds/<type> (incremental; never touches booster/build
# or the submodules), keeps booster.elf as tools/dev/builds/elf/<type>-<build
# id>.elf, and flashes it with the Debug Probe (picotool cannot see Booster:
# it has no USB stdio). Only Booster's own region, from 0x10120000, is
# written; the microfirmware slot, the app settings and the global settings are
# left alone. Then checks over SWD, without the firmware's help, that the RP
# booted a firmware, that its flash matches the ELF and that it carries the new
# build ID (tools/dev/swd.py). A change to upgrader/ or term/atarist/ needs a
# root ./build.sh first: it regenerates the headers Booster embeds.
#
# The flash ends with a watchdog-style chip reset, which does not reset the
# Pico W's CYW43 radio: power-cycle before checking anything over WiFi.
#
# Environment: PICO_TOOLCHAIN_PATH (the Arm toolchain's bin folder; default:
# arm-none-eabi-gcc on PATH), OPENOCD and PICO_OPENOCD_PATH (see swd.py),
# RELEASE_DATE
# (default: the date of the HEAD commit, so builds of one commit are
# byte-identical), BOOSTER_CMAKE_BUILD_TYPE for a CMake type other than
# MinSizeRel (Booster ships MinSizeRel; Release does not fit its slot).
#
# Adapted from md-microfirmware-template's tools/dev/flash.sh (v1.3.0).
set -Eeo pipefail
trap 'echo "ERROR: ${BASH_SOURCE[0]}: failed at line ${LINENO}" >&2' ERR

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"

usage() { sed -n '2,27p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 2; }

TYPE="${1:-}"
case "$TYPE" in
  release) DEBUG_MODE=0 ;;
  debug) DEBUG_MODE=1 ;;
  *) usage ;;
esac
shift
SRC="$REPO/booster/src"
BUILD_ONLY=0
TIMEOUT=30
while [ $# -gt 0 ]; do
  case "$1" in
    --src) SRC="$(cd "$2" && pwd)"; shift 2 ;;
    --build-only) BUILD_ONLY=1; shift ;;
    --probe) shift ;;  # always the probe; kept for the template's command lines
    --timeout) TIMEOUT="$2"; shift 2 ;;
    *) usage ;;
  esac
done

NAME="$TYPE"
CMAKE_TYPE="${BOOSTER_CMAKE_BUILD_TYPE:-MinSizeRel}"
# Another CMake type builds in its own folder.
if [ "$CMAKE_TYPE" != MinSizeRel ]; then
  NAME="$NAME-$(printf '%s' "$CMAKE_TYPE" | tr '[:upper:]' '[:lower:]')"
  echo "WARNING: BOOSTER_CMAKE_BUILD_TYPE=$CMAKE_TYPE: not a shipping build"
fi
# A source outside the repo gets its own build folder, keyed by its path: two
# checkouts are both called booster/src, and sharing a folder confuses CMake.
if [ "$SRC" != "$REPO/booster/src" ]; then
  NAME="$NAME-$(basename "$(dirname "$(dirname "$SRC")")")-$(printf '%s' "$SRC" | shasum | cut -c1-6)"
fi
OUT="$HERE/builds/$NAME"
mkdir -p "$OUT" "$HERE/builds/elf"

# Same environment as booster/build.sh, without its submodule checkout: this
# script is for fast iteration and the pins are only verified, never changed.
for pin in "pico-sdk tags/2.2.0" "pico-extras tags/sdk-2.2.0" \
           "fatfs-sdk tags/v3.6.2"; do
  set -- $pin
  if [ "$(git -C "$REPO/$1" rev-parse HEAD)" != "$(git -C "$REPO/$1" rev-parse "$2^{commit}")" ]; then
    echo "WARNING: $1 is not at $2; run ./build.sh once to pin it" >&2
  fi
done
export PICO_SDK_PATH="$REPO/pico-sdk" PICO_EXTRAS_PATH="$REPO/pico-extras" \
       FATFS_SDK_PATH="$REPO/fatfs-sdk"
export BOARD_TYPE=pico_w PICO_BOARD=pico_w
export DISPLAY_ATARIST=1 PICO_FLASH_ASSUME_CORE0_SAFE=1
RELEASE_VERSION="$(tr -d '\r\n ' < "$REPO/version.txt")"
export RELEASE_VERSION RELEASE_TYPE="${RELEASE_TYPE:-}" DEBUG_MODE
if [ -z "${RELEASE_DATE:-}" ]; then
  RELEASE_DATE="$(git -C "$REPO" log -1 --format=%cd --date=format:'%Y-%m-%d %H:%M:%S')"
fi
export RELEASE_DATE

echo "Compiler: $("${PICO_TOOLCHAIN_PATH:+$PICO_TOOLCHAIN_PATH/}arm-none-eabi-gcc" --version | head -1)"
echo "Building $NAME from $SRC"
cmake -S "$SRC" -B "$OUT" -DCMAKE_BUILD_TYPE="$CMAKE_TYPE" \
  > "$OUT/cmake.log" 2>&1 \
  || { tail -20 "$OUT/cmake.log"; exit 1; }
make -C "$OUT" -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)" > "$OUT/make.log" 2>&1 \
  || { grep -n 'error' "$OUT/make.log" | head -30; exit 1; }

BUILD_ID="$(sed -n 's/^#define RELEASE_BUILD_ID "\(.*\)"$/\1/p' \
  "$OUT/generated/build_id/build_id.h" 2> /dev/null || true)"
BUILD_ID="${BUILD_ID:-no-build-id}"
cp "$OUT/booster.elf" "$HERE/builds/elf/$TYPE-$BUILD_ID.elf"
grep -h 'Booster flash usage' "$OUT/make.log" || arm-none-eabi-size "$OUT/booster.elf" | tail -1
echo "Built $TYPE $BUILD_ID: $OUT/booster.uf2"
[ "$BUILD_ONLY" = 1 ] && exit 0

python3 "$HERE/swd.py" program "$OUT/booster.elf"

# Verified over SWD, without the firmware's help: the RP has left the bootrom
# for a firmware with this layout, its flash matches the ELF byte for byte,
# and the build ID is the one just built.
if python3 "$HERE/swd.py" running "$OUT/booster.elf" --timeout "$TIMEOUT" \
   && python3 "$HERE/swd.py" verify "$OUT/booster.elf" \
   && { [ "$BUILD_ID" = no-build-id ] \
        || [ "$(python3 "$HERE/swd.py" build-id "$OUT/booster.elf")" = "$BUILD_ID" ]; }; then
  echo "Running $TYPE $BUILD_ID"
  exit 0
fi
echo "ERROR: the RP is not running $TYPE $BUILD_ID" >&2
if [ -f "$HERE/logs/console.log" ]; then
  echo "--- console since the last boot (last 30 lines)" >&2
  python3 "$HERE/console.py" since-boot 2>/dev/null | tail -30 >&2 || true
fi
exit 1
