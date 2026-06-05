#!/bin/bash
#
# Build All-in-One GUI Release Packages
# Creates platform-specific packages containing GUI + pre-built binaries
#
# Usage: ./scripts/build-gui-release.sh [version]
# Example: ./scripts/build-gui-release.sh 2.1.1
#

set -e

VERSION="${1:-3.1.0}"
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

# SECURITY (audit N3): pin the expected SHA-256 of every binary we download and
# verify it before repackaging (fail-closed). The previous version fetched
# binaries over `curl -L` with NO integrity check, so a tampered or
# MITM'd download would be silently repackaged and shipped to users.
#
# RELEASE ENGINEER: update these to the SHA-256 of the v${VERSION} release
# assets (read them from the GPG-signed SHA256SUMS for that release — see
# RELEASE_SECURITY_PROCESS.md). Leaving a placeholder makes the build refuse
# to run, by design.
PLACEHOLDER_SHA="UPDATE_AT_RELEASE_WITH_SHA256_FROM_SIGNED_SHA256SUMS"
get_binary_sha256() {
    case "$1" in
        macos-arm64) echo "$PLACEHOLDER_SHA" ;;
        linux-x64)   echo "$PLACEHOLDER_SHA" ;;
    esac
}

# Compute the SHA-256 of a file (portable: GNU sha256sum or BSD shasum).
compute_sha256() {
    local f="$1"
    if command -v sha256sum &> /dev/null; then
        sha256sum "$f" | awk '{print $1}'
    else
        shasum -a 256 "$f" | awk '{print $1}'
    fi
}

# Verify a downloaded asset against its pinned SHA-256. Fail-closed.
verify_binary_sha256() {
    local platform="$1"
    local path="$2"
    local expected
    expected="$(get_binary_sha256 "$platform")"

    if [ -z "$expected" ] || [ "$expected" = "$PLACEHOLDER_SHA" ]; then
        echo -e "${RED}✗${NC} No pinned SHA-256 for $platform."
        echo -e "${RED}  Refusing to repackage an unverified binary.${NC}"
        echo -e "${YELLOW}  Update get_binary_sha256() with the value from the" \
                "signed SHA256SUMS for v${VERSION}.${NC}"
        return 1
    fi

    local actual
    actual="$(compute_sha256 "$path")"
    if [ "$actual" != "$expected" ]; then
        echo -e "${RED}✗${NC} SHA-256 mismatch for $(basename "$path")"
        echo -e "${RED}  expected: $expected${NC}"
        echo -e "${RED}  actual:   $actual${NC}"
        echo -e "${RED}  Aborting — possible tampering or corrupted download.${NC}"
        rm -f "$path"
        return 1
    fi
    echo -e "${GREEN}✓${NC} SHA-256 verified: $(basename "$path")"
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
        # SECURITY (audit N3): verify even cached files — the cache could have
        # been populated by an earlier untrusted run.
        verify_binary_sha256 "$platform" "$download_path" || return 1
        return 0
    fi

    echo -e "${YELLOW}⬇${NC} Downloading: $filename"
    curl --fail -L -o "$download_path" "$GITHUB_RELEASE_URL/$filename" || {
        echo -e "${RED}✗${NC} Failed to download $filename"
        return 1
    }
    echo -e "${GREEN}✓${NC} Downloaded: $filename"
    # SECURITY (audit N3): verify the SHA-256 before this file is ever extracted
    # and repackaged. verify_binary_sha256 deletes the file and fails on
    # mismatch or missing pin.
    verify_binary_sha256 "$platform" "$download_path" || return 1
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
If macOS blocks the app, right-click "start-gui.command" in Finder and choose
"Open", then confirm. This keeps Gatekeeper's signature and notarization
checks in place. Do NOT run "xattr -rd com.apple.quarantine" — that disables
those checks for the whole package and is a security downgrade.

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

# SECURITY (audit N3): do NOT strip the Gatekeeper quarantine attribute.
# Removing com.apple.quarantine disables macOS signature/notarization checks
# for the whole package. Users who trust this build should open it via Finder
# right-click > Open the first time, which keeps Gatekeeper engaged.

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
