#!/bin/bash
# Radiant Core Linux Release Build Script

set -e

echo "========================================"
echo "Radiant Core Linux Release Build"
echo "========================================"

# Check if we're on Linux
if [[ "$OSTYPE" != "linux-gnu"* ]]; then
    echo "ERROR: This script is for Linux builds only"
    exit 1
fi

# Check dependencies
echo "Checking dependencies..."
command -v cmake >/dev/null 2>&1 || { echo "ERROR: cmake is required but not installed"; exit 1; }
command -v ninja >/dev/null 2>&1 || { echo "ERROR: ninja is required but not installed"; exit 1; }
command -v gcc >/dev/null 2>&1 || { echo "ERROR: gcc is required but not installed"; exit 1; }

# Install dependencies if needed
if command -v apt-get >/dev/null 2>&1; then
    echo "Installing dependencies via apt..."
    sudo apt-get update
    sudo apt-get install -y build-essential cmake ninja-build \
        libboost-all-dev libevent-dev libssl-dev libdb++-dev \
        libminiupnpc-dev libzmq3-dev pkg-config
elif command -v yum >/dev/null 2>&1; then
    echo "Installing dependencies via yum..."
    sudo yum groupinstall -y "Development Tools"
    sudo yum install -y cmake ninja boost-devel libevent-devel \
        openssl-devel db-devel miniupnpc-devel zeromq-devel
fi

# Create build directory
echo "Creating build directory..."
mkdir -p linux-release
cd linux-release

# Configure build
echo "Configuring build..."
cmake -GNinja .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_RADIANT_WALLET=OFF \
    -DBUILD_RADIANT_ZMQ=OFF \
    -DBUILD_RADIANT_QT=OFF \
    -DENABLE_UPNP=OFF

# Build
echo "Building..."
ninja

# Create release package
echo "Creating release package..."
mkdir -p ../release-linux
cp src/radiantd ../release-linux/
cp src/radiant-cli ../release-linux/
cp src/radiant-tx ../release-linux/

# Create README
cat > ../release-linux/README.txt << EOF
Radiant Core Linux Binaries v$(git describe --tags --always)
============================================

Files Included:
--------------
- radiantd     - Main Radiant Core daemon
- radiant-cli  - RPC client utility
- radiant-tx   - Transaction utility

System Requirements:
-------------------
- Linux x86_64 (Ubuntu 20.04+, CentOS 8+, Fedora 32+)
- glibc 2.17+

Quick Start:
-----------
1. Run the daemon:
   ./radiantd

2. Check daemon status:
   ./radiant-cli getblockchaininfo

3. Create transactions:
   ./radiant-tx --help

Build Information:
-----------------
- Built with: $(gcc --version | head -1)
- CMake: $(cmake --version | head -1)
- Build Type: Release
- Git Commit: $(git rev-parse --short HEAD)

For more information, visit: https://radiantblockchain.org
EOF

echo "========================================"
echo "Linux release build completed!"
echo "Location: release-linux/"
echo ""
echo "Binaries created:"
ls -la ../release-linux/
echo ""
echo "To create a tarball:"
echo "cd release-linux && tar -czf radiant-core-linux-x86_64.tar.gz *"
echo "========================================"
