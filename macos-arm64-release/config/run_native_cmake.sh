#!/bin/sh

"/opt/homebrew/bin/cmake" -G"Ninja" \
    -S "/Users/main/Downloads/Radiant-Core-main" \
    -B "/Users/main/Downloads/Radiant-Core-main/macos-arm64-release/native" \
    -D__IS_NATIVE_BUILD=1 \
    -DCMAKE_MAKE_PROGRAM=/opt/homebrew/bin/ninja \
    -DBUILD_RADIANT_QT=OFF -DBUILD_RADIANT_WALLET=OFF -DBUILD_RADIANT_ZMQ=OFF -DENABLE_CLANG_TIDY=OFF -DENABLE_QRCODE=OFF -DENABLE_UPNP=OFF -DSECP256K1_ECMULT_GEN_PRECISION=4 -DSECP256K1_ECMULT_WINDOW_SIZE=15 -DSECP256K1_USE_ASM=OFF

# Ok let's generate a depfile if we can.
if test "xNinja" = "xNinja"; then
    "/Users/main/Downloads/Radiant-Core-main/cmake/utils/gen-ninja-deps.py" \
        --build-dir "/Users/main/Downloads/Radiant-Core-main/macos-arm64-release/native" \
        --base-dir "/Users/main/Downloads/Radiant-Core-main/macos-arm64-release" \
        --ninja "/opt/homebrew/bin/ninja" \
        native/CMakeCache.txt build.ninja \
        --extra-deps build.ninja \
            > "/Users/main/Downloads/Radiant-Core-main/macos-arm64-release/native/CMakeFiles/CMakeCache.txt.d"
fi
