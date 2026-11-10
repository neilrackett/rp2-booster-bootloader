#!/bin/bash

# Fail fast. Without this a failed cmake, make, link or missing tool was simply
# stepped over: the script carried on, copied whatever binary a previous build
# had left behind, and exited 0. A root build could therefore package stale
# firmware -- or none at all -- with nothing to show anything had gone wrong.
# `-u` is deliberately not set; several scripts test unset positional args.
set -Eeo pipefail
trap 'echo "ERROR: ${BASH_SOURCE[0]}: failed at line ${LINENO}" >&2' ERR

# Down to main path
cd ..

# Install SDK needed for building
git submodule init
git submodule update --init --recursive

# Pin the building versions
echo "Pinning the SDK versions..."
cd pico-sdk
#git checkout tags/2.1.0
git checkout tags/2.2.0
cd ..

echo "Pinning the Extras SDK versions..."
cd pico-extras
#git checkout tags/sdk-2.1.0
git checkout tags/sdk-2.2.0
cd ..

echo "Pinning the FatFs SDK versions..."
cd fatfs-sdk
#git checkout v3.5.1
#git checkout 6bdb39f96fe8b897aff12bf3416e32515792e318
git checkout tags/v3.6.2
cd ..

# This is a dirty hack to guarantee that I can use the fatfs-sdk submodule
#echo "Patching the fatfs-sdk... to use chmod"
#sed -i.bak 's/#define FF_USE_CHMOD[[:space:]]*0/#define FF_USE_CHMOD 1/' fatfs-sdk/src/include/ffconf.h && mv fatfs-sdk/src/include/ffconf.h.bak .
#sed -i.bak 's/#define FF_FS_TINY[[:space:]]*0/#define FF_FS_TINY 1/' fatfs-sdk/src/include/ffconf.h && mv fatfs-sdk/src/include/ffconf.h.bak2 .

# Set the environment variables of the SDKs
export PICO_SDK_PATH=$PWD/pico-sdk
export FATFS_SDK_PATH=$PWD/fatfs-sdk
export PICO_EXTRAS_PATH=$PWD/pico-extras

# Return to booster path
cd booster

# Check if the third parameter is provided
export RELEASE_TYPE=${3:-""}
echo "Release type: $RELEASE_TYPE"

# Determine the file to use based on RELEASE_TYPE
if [ -z "$RELEASE_TYPE" ] || [ "$RELEASE_TYPE" = "final" ]; then
    VERSION_FILE="version.txt"
else
    VERSION_FILE="version-$RELEASE_TYPE.txt"
fi

# Read the release version from the version.txt file
export RELEASE_VERSION=$(cat "$VERSION_FILE" | tr -d '\r\n ')
echo "Release version: $RELEASE_VERSION"

# Get the release date and time from the current date
export RELEASE_DATE=$(date +"%Y-%m-%d %H:%M:%S")
echo "Release date: $RELEASE_DATE"

# Set the board type to be used for building
# If nothing passed as first argument, use pico_w
export BOARD_TYPE=${1:-pico_w}
export PICO_BOARD=$BOARD_TYPE
echo "Board type: $BOARD_TYPE"

# Set the release or debug build type
# If nothing passed as second argument, use release
export BUILD_TYPE=${2:-release}
if [ "$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')" = "minsizerel" ]; then
    export BUILD_TYPE=MinSizeRel
fi
echo "Build type: $BUILD_TYPE"

# Only explicit debug builds should enable DEBUG_MODE.
if [ "$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')" = "debug" ]; then
    export DEBUG_MODE=1
else
    export DEBUG_MODE=0
fi

# Booster is ALWAYS compiled MinSizeRel, debug included. It has one 768K slot to
# fit and -Og does not fit in it: a Debug build overflows by ~72KB and
# will not link. A debug build therefore differs from a release build by
# _DEBUG=1 -- DPRINTF over UART, no --strip-all -- and not by optimisation
# level. MinSizeRel still carries -g, so the symbols are there to debug with;
# it also carries -DNDEBUG, so assert() is off in debug builds too.
# `release` maps here too: -O3 overflows the slot by ~48KB and will not link,
# which is why the root build.sh already asks for MinSizeRel. Doing it
# here as well means booster/build.sh is safe to run on its own.
CMAKE_BUILD_TYPE_ARG=$BUILD_TYPE
case "$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')" in
    debug|release|minsizerel)
        CMAKE_BUILD_TYPE_ARG=MinSizeRel
        ;;
esac
echo "CMake build type: $CMAKE_BUILD_TYPE_ARG (DEBUG_MODE=$DEBUG_MODE)"

# Set the build directory. Delete previous contents if any
echo "Deleting previous build directory"
rm -rf build
mkdir build

# We assume that the last firmware was built for the same board type
# And previously pushed to the repo version

# Build the project
echo "Building the project"
#export PICO_DEOPTIMIZED_DEBUG=1

# Set more environment variables for the build
export DISPLAY_ATARIST=1
export PICO_FLASH_ASSUME_CORE0_SAFE=1

echo "DEBUG_MODE: $DEBUG_MODE"
echo "DISPLAY_ATARIST: $DISPLAY_ATARIST"
echo "PICO_FLASH_ASSUME_CORE0_SAFE: $PICO_FLASH_ASSUME_CORE0_SAFE"
echo "PICO_DEOPTIMIZED_DEBUG: $PICO_DEOPTIMIZED_DEBUG"

cd build
cmake ../src -DCMAKE_BUILD_TYPE=$CMAKE_BUILD_TYPE_ARG
#cmake ../src -DCMAKE_BUILD_TYPE=CustomBuild
#cmake ../src -DCMAKE_BUILD_TYPE=Debug
make -j4 

# Copy the built firmware to the /dist folder
cd ..
mkdir -p dist
echo "Copying the built firmware to the dist folder"
if [ "$BUILD_TYPE" = "release" ]; then
    cp build/booster.uf2 dist/booster-$BOARD_TYPE.uf2
else
    cp build/booster.uf2 dist/booster-$BOARD_TYPE-$BUILD_TYPE.uf2
fi
