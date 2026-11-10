#!/bin/bash

# Fail fast. Without this a failed cmake, make, link or missing tool was simply
# stepped over: the script carried on, copied whatever binary a previous build
# had left behind, and exited 0. A root build could therefore package stale
# firmware -- or none at all -- with nothing to show anything had gone wrong.
# `-u` is deliberately not set; several scripts test unset positional args.
set -Eeo pipefail
trap 'echo "ERROR: ${BASH_SOURCE[0]}: failed at line ${LINENO}" >&2' ERR

# Copy the version.txt to each project
echo "Copy version.txt to each project"
cp version.txt booster/
cp version.txt placeholder/
cp version.txt upgrader/

# Display the version information
export VERSION=$(cat version.txt)
echo "Version: $VERSION"

# Check if the third parameter is provided
export RELEASE_TYPE=${3:-""}
echo "Release type: $RELEASE_TYPE"

# Set the board type to be used for building
# If nothing passed as first argument, use sidecartos_16mb
export BOARD_TYPE=${1:-pico_w}
echo "Board type: $BOARD_TYPE"

# Set the release or debug build type
# If nothing passed as second argument, use release
export BUILD_TYPE=${2:-release}
if [ "$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')" = "minsizerel" ]; then
    export BUILD_TYPE=MinSizeRel
fi
echo "Build type: $BUILD_TYPE"

# The upgrader is ALWAYS MinSizeRel, debug included. It is not a standalone
# binary: firmware.py turns it into upgrader_firmware.h, which is compiled into
# Booster -- so its size is spent out of Booster's 768K slot. Building it
# -Og for a debug run would inflate that header and push Booster over the limit,
# and nobody debugs the upgrader through Booster anyway.
export UPGRADER_BUILD_TYPE=MinSizeRel
echo "Upgrader build type: $UPGRADER_BUILD_TYPE"

# Booster is built MinSizeRel for release flows. With -O3 and
# HTTPS enabled, Booster overflows its 768K slot by ~48KB and will not link;
# -Os saves ~87KB and brings it to ~90% of the slot. Debug builds are unaffected.
export BOOSTER_BUILD_TYPE=$BUILD_TYPE
case "$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')" in
    release|minsizerel)
        export BOOSTER_BUILD_TYPE=MinSizeRel
        ;;
esac
echo "Booster build type: $BOOSTER_BUILD_TYPE"

# Set the build directory. Delete previous contents if any
echo "Delete previous build directory"
rm -rf build
mkdir build

# Build the upgrader
echo "Building upgrader project"
cd upgrader
./build.sh pico $UPGRADER_BUILD_TYPE
cd ..

# Build the term
echo "Building term project"
cd term/atarist
./build.sh "$PWD" release
cd ../..

# Build the booster project
echo "Building booster project"
cd booster
./build.sh $BOARD_TYPE $BOOSTER_BUILD_TYPE
if [ "$BOOSTER_BUILD_TYPE" = "release" ]; then
    cp  ./dist/booster-$BOARD_TYPE.uf2 ../build/booster.uf2
else
    cp  ./dist/booster-$BOARD_TYPE-$BOOSTER_BUILD_TYPE.uf2 ../build/booster.uf2
fi
cd ..

# Build the placeholder
echo "Building placeholder project"
cd placeholder
./build.sh pico $BUILD_TYPE
if [ "$BUILD_TYPE" = "release" ]; then
    cp  ./dist/placeholder-pico.uf2 ../build/placeholder.uf2
else
    cp  ./dist/placeholder-pico-$BUILD_TYPE.uf2 ../build/placeholder.uf2
fi
cd ..

# Build the UF2 combining the booster and placeholder
mkdir -p dist
echo "Building UF2 file combining booster and placeholder..."
python build_uf2.py ./build/placeholder.uf2 ./build/booster.uf2 ./dist/rp-booster.uf2

# Rename the file to include the version number and the build type
if [ "$BUILD_TYPE" = "release" ]; then
    mv ./dist/rp-booster.uf2 ./dist/rp-booster-$VERSION.uf2
else
    mv ./dist/rp-booster.uf2 ./dist/rp-booster-$VERSION-$BUILD_TYPE.uf2
fi

# If there is no parameter ${3} passed, then exit
if [ -z ${3} ]; then
    echo "Existing now, no image file building requested"
    exit 0
fi

# Build the image file
echo "Building image file..."
python merge_uf2.py ./build/placeholder.uf2 ./build/booster.uf2 ./dist/rp-booster-all.uf2

# Rename the file to include the version number and the build type
if [ "$BUILD_TYPE" = "release" ]; then
    cp ./dist/rp-booster-all.uf2 ./dist/rp-booster-$VERSION-full.uf2
    mv ./dist/rp-booster-all.uf2 ./dist/upgrade.bin
    cp version.txt ./dist/SIDECARTVERSION
    # Checksum of the image, published alongside it. The device fetches this
    # before upgrade.bin and refuses to flash an image that does not match.
    # Bare digest, no filename: the device reads the first 32 characters.
    if command -v md5sum >/dev/null 2>&1; then
        md5sum ./dist/upgrade.bin | cut -d' ' -f1 > ./dist/upgrade.md5
    else
        # BSD/macOS
        md5 -q ./dist/upgrade.bin > ./dist/upgrade.md5
    fi
    echo "upgrade.md5: $(cat ./dist/upgrade.md5)"
else
    mv ./dist/rp-booster-all.uf2 ./dist/rp-booster-$VERSION-$BUILD_TYPE-full.uf2
fi
 
# Done
echo "Done"

exit 0
