#!/bin/bash
# Radiant Core Windows Cross-Compilation Script (Linux to Windows)

set -e

echo "========================================"
echo "Radiant Core Windows Cross-Compilation"
echo "========================================"

# Check if we're on Linux
if [[ "$OSTYPE" != "linux-gnu"* ]]; then
    echo "ERROR: This script requires Linux for cross-compilation"
    exit 1
fi

# Install cross-compilation dependencies
echo "Installing cross-compilation dependencies..."
if command -v apt-get >/dev/null 2>&1; then
    sudo apt-get update
    sudo apt-get install -y \
        mingw-w64 \
        g++-mingw-w64-x86-64 \
        gcc-mingw-w64-x86-64 \
        binutils-mingw-w64-x86-64 \
        mingw-w64-tools \
        cmake \
        ninja-build \
        pkg-config \
        git \
        libboost-all-dev \
        libevent-dev \
        libssl-dev \
        libdb++-dev \
        libminiupnpc-dev \
        libzmq3-dev
elif command -v yum >/dev/null 2>&1; then
    sudo yum groupinstall -y "Development Tools"
    sudo yum install -y \
        mingw64-gcc \
        mingw64-gcc-c++ \
        mingw64-binutils \
        mingw64-boost-static \
        mingw64-libevent-static \
        mingw64-openssl-static \
        cmake \
        ninja-build
fi

# Create build directory
echo "Creating build directory..."
mkdir -p windows-release
cd windows-release

# Set up cross-compilation environment
export CC=x86_64-w64-mingw32-gcc
export CXX=x86_64-w64-mingw32-g++
export AR=x86_64-w64-mingw32-ar
export RANLIB=x86_64-w64-mingw32-ranlib
export STRIP=x86_64-w64-mingw32-strip

# Configure cross-compilation
echo "Configuring cross-compilation..."
cmake -GNinja .. \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_SYSTEM_PROCESSOR=x86_64 \
    -DCMAKE_C_COMPILER=$CC \
    -DCMAKE_CXX_COMPILER=$CXX \
    -DCMAKE_AR=$AR \
    -DCMAKE_RANLIB=$RANLIB \
    -DCMAKE_STRIP=$STRIP \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_RADIANT_WALLET=OFF \
    -DBUILD_RADIANT_ZMQ=OFF \
    -DBUILD_RADIANT_QT=OFF \
    -DENABLE_UPNP=OFF \
    -DCMAKE_FIND_ROOT_PATH=/usr/x86_64-w64-mingw32

# Build
echo "Building..."
ninja

# Create release package
echo "Creating release package..."
mkdir -p ../release-windows
cp src/radiantd.exe ../release-windows/
cp src/radiant-cli.exe ../release-windows/
cp src/radiant-tx.exe ../release-windows/

# Copy required DLLs
echo "Copying required DLLs..."
DLL_DIR="/usr/x86_64-w64-mingw32/lib"
if [ -f "$DLL_DIR/libwinpthread-1.dll" ]; then
    cp "$DLL_DIR/libwinpthread-1.dll" ../release-windows/
fi
if [ -f "$DLL_DIR/libgcc_s_seh-1.dll" ]; then
    cp "$DLL_DIR/libgcc_s_seh-1.dll" ../release-windows/
fi
if [ -f "$DLL_DIR/libstdc++-6.dll" ]; then
    cp "$DLL_DIR/libstdc++-6.dll" ../release-windows/
fi

# Create README
cat > ../release-windows/README.txt << EOF
Radiant Core Windows Binaries v$(git describe --tags --always)
===============================================

Files Included:
--------------
- radiantd.exe     - Main Radiant Core daemon
- radiant-cli.exe  - RPC client utility
- radiant-tx.exe   - Transaction utility
- *.dll           - Required runtime libraries

System Requirements:
-------------------
- Windows 10/11 (64-bit)
- No additional dependencies required

Quick Start:
-----------
1. Extract all files to a folder
2. Run the daemon:
   radiantd.exe

3. Check daemon status:
   radiant-cli.exe getblockchaininfo

4. Create transactions:
   radiant-tx.exe --help

Build Information:
-----------------
- Cross-compiled on Linux with MinGW-w64
- Target: Windows x86_64
- GCC: $($CC --version | head -1)
- CMake: $(cmake --version | head -1)
- Build Type: Release
- Git Commit: $(git rev-parse --short HEAD)

Security Notes:
--------------
These binaries were cross-compiled from Linux source code.
For maximum security, consider building from source on Windows
using the build-portable-windows-v2.bat script.

For more information, visit: https://radiantblockchain.org
EOF

echo "========================================"
echo "Windows cross-compilation completed!"
echo "Location: release-windows/"
echo ""
echo "Binaries created:"
ls -la ../release-windows/
echo ""
echo "To create a ZIP archive:"
echo "cd release-windows && zip -r radiant-core-windows-x86_64.zip *"
echo "========================================"
