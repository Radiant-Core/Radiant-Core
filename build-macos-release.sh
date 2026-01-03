#!/bin/bash
# Radiant Core macOS Release Build Script

set -e

echo "========================================"
echo "Radiant Core macOS Release Build"
echo "========================================"

# Check if we're on macOS
if [[ "$OSTYPE" != "darwin"* ]]; then
    echo "ERROR: This script is for macOS builds only"
    exit 1
fi

# Check dependencies
echo "Checking dependencies..."
command -v cmake >/dev/null 2>&1 || { echo "ERROR: cmake is required but not installed"; exit 1; }
command -v ninja >/dev/null 2>&1 || { echo "ERROR: ninja is required but not installed"; exit 1; }
command -v clang >/dev/null 2>&1 || { echo "ERROR: clang is required but not installed"; exit 1; }

# Install dependencies via Homebrew if needed
if command -v brew >/dev/null 2>&1; then
    echo "Installing dependencies via Homebrew..."
    brew install cmake ninja boost libevent openssl berkeley-db miniupnpc zeromq qt5
else
    echo "ERROR: Homebrew is required for dependency management"
    echo "Please install Homebrew: https://brew.sh/"
    exit 1
fi

# Create build directory
echo "Creating build directory..."
mkdir -p macos-release
cd macos-release

# Set environment variables for OpenSSL
export OPENSSL_ROOT_DIR=$(brew --prefix openssl)
export CMAKE_PREFIX_PATH="/opt/homebrew/opt/qt@5:$CMAKE_PREFIX_PATH"

# Configure build for both architectures (Universal Binary)
echo "Configuring build for Universal Binary..."
cmake -GNinja .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_RADIANT_WALLET=OFF \
    -DBUILD_RADIANT_ZMQ=OFF \
    -DBUILD_RADIANT_QT=OFF \
    -DENABLE_UPNP=OFF \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="10.15" \
    -DOPENSSL_ROOT_DIR="$OPENSSL_ROOT_DIR"

# Build
echo "Building..."
ninja

# Create release package
echo "Creating release package..."
mkdir -p ../release-macos
cp src/radiantd ../release-macos/
cp src/radiant-cli ../release-macos/
cp src/radiant-tx ../release-macos/

# Create DMG creation script
cat > ../create-dmg.sh << 'EOF'
#!/bin/bash
# Create DMG for macOS distribution

set -e

DMG_NAME="Radiant-Core"
VERSION=$(git describe --tags --always 2>/dev/null || echo "2.0.0")
SOURCE_DIR="release-macos"
DMG_FILE="${DMG_NAME}-${VERSION}.dmg"

echo "Creating DMG: $DMG_FILE"

# Create temporary DMG directory
TEMP_DIR=$(mktemp -d)
DMG_TEMP_DIR="$TEMP_DIR/$DMG_NAME"

# Copy files to DMG directory
mkdir -p "$DMG_TEMP_DIR"
cp -R "$SOURCE_DIR"/* "$DMG_TEMP_DIR/"

# Create symbolic link to Applications
ln -s /Applications "$DMG_TEMP_DIR/Applications"

# Create DMG
hdiutil create -volname "$DMG_NAME" \
    -srcfolder "$DMG_TEMP_DIR" \
    -ov -format UDZO \
    "$DMG_FILE"

# Clean up
rm -rf "$TEMP_DIR"

echo "DMG created: $DMG_FILE"
EOF

chmod +x ../create-dmg.sh

# Create README
cat > ../release-macos/README.txt << EOF
Radiant Core macOS Binaries v$(git describe --tags --always)
==============================================

Files Included:
--------------
- radiantd     - Main Radiant Core daemon
- radiant-cli  - RPC client utility
- radiant-tx   - Transaction utility

System Requirements:
-------------------
- macOS 10.15 (Catalina) or later
- Apple Silicon (M1/M2) or Intel x86_64
- Universal Binary (supports both architectures)

Quick Start:
-----------
1. Open Terminal and navigate to this folder
2. Make the executables executable:
   chmod +x radiantd radiant-cli radiant-tx

3. Run the daemon:
   ./radiantd

4. Check daemon status:
   ./radiant-cli getblockchaininfo

5. Create transactions:
   ./radiant-tx --help

Installation:
-------------
Option 1: Run from current folder
   ./radiantd

Option 2: Copy to /usr/local/bin
   sudo cp radiantd radiant-cli radiant-tx /usr/local/bin/
   radiantd

Option 3: Create DMG installer
   ./create-dmg.sh

Build Information:
-----------------
- Built with: $(clang --version | head -1)
- Target Architectures: arm64, x86_64
- Deployment Target: macOS 10.15+
- CMake: $(cmake --version | head -1)
- Build Type: Release
- Git Commit: $(git rev-parse --short HEAD)

For more information, visit: https://radiantblockchain.org
EOF

echo "========================================"
echo "macOS release build completed!"
echo "Location: release-macos/"
echo ""
echo "Binaries created:"
ls -la ../release-macos/
echo ""
echo "To create DMG installer:"
echo "./create-dmg.sh"
echo "========================================"
