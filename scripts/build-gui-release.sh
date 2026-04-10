#!/bin/bash
#
# Build All-in-One GUI Release Packages
# Creates platform-specific packages containing GUI + pre-built binaries
#
# Usage: ./scripts/build-gui-release.sh [version]
# Example: ./scripts/build-gui-release.sh 2.1.1
#

set -e

VERSION="${1:-2.3.0}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$ROOT_DIR/release-builds"
GITHUB_RELEASE_URL="https://github.com/Radiant-Core/Radiant-Core/releases/download/v${VERSION}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "=============================================="
echo "  Radiant Core GUI Release Builder v${VERSION}"
echo "=============================================="
echo ""

# Platform configurations (bash 3.x compatible - no associative arrays)
PLATFORMS="macos-arm64 linux-x64"

get_binary_filename() {
    case "$1" in
        macos-arm64) echo "radiant-core-macos-arm64.tar.gz" ;;
        linux-x64)   echo "radiant-core-linux-x64.tar.gz" ;;
    esac
}

# Create build directory
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR/downloads"

# Function to download binaries if not cached
download_binaries() {
    local platform=$1
    local filename=$(get_binary_filename "$platform")
    local download_path="$BUILD_DIR/downloads/$filename"
    
    if [ -f "$download_path" ]; then
        echo -e "${GREEN}✓${NC} Using cached: $filename"
        return 0
    fi
    
    echo -e "${YELLOW}⬇${NC} Downloading: $filename"
    curl -L -o "$download_path" "$GITHUB_RELEASE_URL/$filename" || {
        echo -e "${RED}✗${NC} Failed to download $filename"
        return 1
    }
    echo -e "${GREEN}✓${NC} Downloaded: $filename"
}

# Function to build release package for a platform
build_package() {
    local platform=$1
    local binary_archive=$(get_binary_filename "$platform")
    local package_name="radiant-core-gui-${platform}-v${VERSION}"
    local package_dir="$BUILD_DIR/$package_name"
    
    echo ""
    echo -e "${YELLOW}Building:${NC} $package_name"
    echo "----------------------------------------"
    
    # Create package directory
    rm -rf "$package_dir"
    mkdir -p "$package_dir"
    
    # Extract binaries
    echo "  Extracting binaries..."
    tar -xzf "$BUILD_DIR/downloads/$binary_archive" -C "$package_dir" --strip-components=1 2>/dev/null || \
    tar -xzf "$BUILD_DIR/downloads/$binary_archive" -C "$package_dir" 2>/dev/null
    
    # Copy GUI files
    echo "  Copying GUI files..."
    cp "$ROOT_DIR/gui/radiant_node_web.py" "$package_dir/"
    cp "$ROOT_DIR/gui/bip39.py" "$package_dir/"
    
    # Create README for the package
    cat > "$package_dir/README.txt" << 'README_EOF'
Radiant Core GUI - All-in-One Package
======================================

This package contains everything you need to run a Radiant node with a
graphical interface.

QUICK START
-----------

macOS:
  Double-click "start-gui.command" or run in Terminal:
  ./start-gui.command

Linux:
  Run in terminal:
  ./start-gui.sh

The GUI will open in your default web browser at http://127.0.0.1:8765

FIRST TIME SETUP (macOS)
------------------------
If you see a security warning, run this command first:
  xattr -rd com.apple.quarantine .

CONTENTS
--------
- radiantd        : The Radiant node daemon
- radiant-cli     : Command-line interface for the node
- radiant-tx      : Transaction utility
- radiant_node_web.py : Web-based GUI
- bip39.py        : Seed phrase support module

SUPPORT
-------
Website: https://radiantblockchain.org
GitHub:  https://github.com/Radiant-Core/Radiant-Core

README_EOF

    # Create platform-specific launcher scripts
    if [[ "$platform" == macos* ]]; then
        # Fix dynamic library paths for macOS distribution
        if [[ -f "$SCRIPT_DIR/fix-macos-dylibs.sh" ]]; then
            echo "  Fixing dynamic library paths..."
            "$SCRIPT_DIR/fix-macos-dylibs.sh" "$package_dir" 2>/dev/null || {
                echo -e "  ${YELLOW}Warning: Could not fix dylib paths${NC}"
            }
        fi
        
        # macOS launcher (.command file - double-clickable)
        cat > "$package_dir/start-gui.command" << 'LAUNCHER_EOF'
#!/bin/bash
cd "$(dirname "$0")"

# Remove quarantine attribute if present
if xattr -l . 2>/dev/null | grep -q "com.apple.quarantine"; then
    echo "Removing macOS quarantine..."
    xattr -rd com.apple.quarantine .
fi

# Make binaries executable
chmod +x radiantd radiant-cli radiant-tx 2>/dev/null

# Set library path for bundled dylibs
if [ -d "libs" ]; then
    export DYLD_LIBRARY_PATH="$(pwd)/libs:$DYLD_LIBRARY_PATH"
fi

# Start the GUI
echo "Starting Radiant Core GUI..."
python3 radiant_node_web.py
LAUNCHER_EOF
        chmod +x "$package_dir/start-gui.command"
        
    else
        # Linux launcher
        cat > "$package_dir/start-gui.sh" << 'LAUNCHER_EOF'
#!/bin/bash
cd "$(dirname "$0")"

# Make binaries executable
chmod +x radiantd radiant-cli radiant-tx 2>/dev/null

# Start the GUI
echo "Starting Radiant Core GUI..."
python3 radiant_node_web.py
LAUNCHER_EOF
        chmod +x "$package_dir/start-gui.sh"
    fi
    
    # Create the archives
    echo "  Creating archive..."
    cd "$BUILD_DIR"
    tar -czf "${package_name}.tar.gz" "$package_name"
    
    # Generate checksum for tar.gz
    if command -v sha256sum &> /dev/null; then
        sha256sum "${package_name}.tar.gz" > "${package_name}.tar.gz.sha256"
    else
        shasum -a 256 "${package_name}.tar.gz" > "${package_name}.tar.gz.sha256"
    fi
    
    local size=$(ls -lh "${package_name}.tar.gz" | awk '{print $5}')
    echo -e "  ${GREEN}✓${NC} Created: ${package_name}.tar.gz ($size)"
    
    # Create .zip for macOS (preferred format for Mac users)
    if [[ "$platform" == macos* ]]; then
        zip -rq "${package_name}.zip" "$package_name"
        if command -v sha256sum &> /dev/null; then
            sha256sum "${package_name}.zip" > "${package_name}.zip.sha256"
        else
            shasum -a 256 "${package_name}.zip" > "${package_name}.zip.sha256"
        fi
        local zip_size=$(ls -lh "${package_name}.zip" | awk '{print $5}')
        echo -e "  ${GREEN}✓${NC} Created: ${package_name}.zip ($zip_size)"
    fi
    
    # Cleanup extracted directory
    rm -rf "$package_dir"
    
    cd "$ROOT_DIR"
}

# Main build process
echo "Step 1: Downloading binaries..."
echo "----------------------------------------"
for platform in $PLATFORMS; do
    download_binaries "$platform" || exit 1
done

echo ""
echo "Step 2: Building release packages..."
for platform in $PLATFORMS; do
    build_package "$platform"
done

# Summary
echo ""
echo "=============================================="
echo -e "${GREEN}Build Complete!${NC}"
echo "=============================================="
echo ""
echo "Release packages created in: $BUILD_DIR/"
echo ""
ls -lh "$BUILD_DIR"/*.tar.gz "$BUILD_DIR"/*.zip 2>/dev/null
echo ""
echo "SHA256 checksums:"
cat "$BUILD_DIR"/*.sha256 2>/dev/null
echo ""
echo "To upload to GitHub Release:"
echo "  gh release upload v${VERSION} $BUILD_DIR/*.tar.gz $BUILD_DIR/*.zip $BUILD_DIR/*.sha256"
echo ""
