#!/bin/bash
#
# Build Radiant Core GUI macOS Application
# Creates a standalone .app bundle and .dmg installer
#
# Requirements:
#   - macOS with Xcode Command Line Tools
#   - Python 3.9+
#   - pip install py2app pywebview
#
# Usage: ./scripts/build-macos-app.sh [version]
# Example: ./scripts/build-macos-app.sh 2.3.0
#

set -e

VERSION="${1:-2.3.0}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
GUI_DIR="$ROOT_DIR/gui"
BUILD_DIR="$ROOT_DIR/macos-app-build"
DMG_NAME="Radiant-Core-GUI-${VERSION}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "=============================================="
echo "  Radiant Core macOS App Builder v${VERSION}"
echo "=============================================="
echo ""

# Check for macOS
if [[ "$(uname)" != "Darwin" ]]; then
    echo -e "${RED}Error: This script must be run on macOS${NC}"
    exit 1
fi

# Check for Python 3
if ! command -v python3 &> /dev/null; then
    echo -e "${RED}Error: Python 3 is required${NC}"
    exit 1
fi

echo "Step 1: Setting up build environment..."
echo "----------------------------------------"

# Create virtual environment for clean build
VENV_DIR="$BUILD_DIR/venv"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

echo "  Creating virtual environment..."
python3 -m venv "$VENV_DIR"
source "$VENV_DIR/bin/activate"

echo "  Installing build dependencies..."
pip install --upgrade pip > /dev/null
pip install py2app pywebview > /dev/null
echo -e "  ${GREEN}✓${NC} Dependencies installed"

echo ""
echo "Step 2: Building application bundle..."
echo "----------------------------------------"

# Copy GUI files to build directory
cp "$GUI_DIR/radiant_node_web.py" "$BUILD_DIR/"
cp "$GUI_DIR/bip39.py" "$BUILD_DIR/"
cp "$GUI_DIR/setup.py" "$BUILD_DIR/"

# Copy doc/images for logos and icon
mkdir -p "$BUILD_DIR/doc/images"
cp "$ROOT_DIR/doc/images/RXDCore.icns" "$BUILD_DIR/doc/images/" 2>/dev/null || true
cp "$ROOT_DIR/doc/images/RXD_light_logo.svg" "$BUILD_DIR/doc/images/" 2>/dev/null || true
cp "$ROOT_DIR/doc/images/RXD_dark_logo.svg" "$BUILD_DIR/doc/images/" 2>/dev/null || true

if [[ -f "$BUILD_DIR/doc/images/RXDCore.icns" ]]; then
    echo -e "  ${GREEN}✓${NC} Icon file found"
else
    echo "  Note: No icon file found, using default"
fi

cd "$BUILD_DIR"

# Build the app bundle
echo "  Running py2app..."
python setup.py py2app --dist-dir dist > build.log 2>&1 || {
    echo -e "${RED}Error: py2app build failed. Check $BUILD_DIR/build.log${NC}"
    cat build.log
    exit 1
}

APP_PATH="$BUILD_DIR/dist/Radiant Core.app"
if [[ ! -d "$APP_PATH" ]]; then
    echo -e "${RED}Error: App bundle not created${NC}"
    exit 1
fi

echo -e "  ${GREEN}✓${NC} App bundle created"

# Copy logo images to Resources folder
RESOURCES_DIR="$APP_PATH/Contents/Resources"
mkdir -p "$RESOURCES_DIR/images"
cp "$BUILD_DIR/doc/images/RXD_light_logo.svg" "$RESOURCES_DIR/images/" 2>/dev/null || true
cp "$BUILD_DIR/doc/images/RXD_dark_logo.svg" "$RESOURCES_DIR/images/" 2>/dev/null || true
echo -e "  ${GREEN}✓${NC} Logo images copied"

# Copy Radiant Core binaries into the app bundle
echo ""
echo "Step 3: Bundling Radiant Core binaries..."
echo "----------------------------------------"

RESOURCES_DIR="$APP_PATH/Contents/Resources"
BINARIES_DIR="$RESOURCES_DIR/binaries"
mkdir -p "$BINARIES_DIR"

# Check for local binaries first (from build-release or gui/binaries)
LOCAL_BINARIES="$ROOT_DIR/gui/binaries/radiant-core-macos-arm64"
BUILD_BINARIES="$ROOT_DIR/build-release/src"
BINARIES_FOUND=false

if [[ -f "$LOCAL_BINARIES/radiantd" ]] && [[ -d "$LOCAL_BINARIES/libs" ]]; then
    # Use pre-bundled local binaries (already has libs fixed)
    echo "  Using local bundled binaries from gui/binaries..."
    # Copy binaries directly (not in subdirectory)
    cp "$LOCAL_BINARIES"/*.dylib "$BINARIES_DIR/" 2>/dev/null || true
    cp "$LOCAL_BINARIES"/radiantd "$LOCAL_BINARIES"/radiant-cli "$LOCAL_BINARIES"/radiant-tx "$BINARIES_DIR/" 2>/dev/null || true
    # Copy libs directory
    cp -R "$LOCAL_BINARIES/libs" "$BINARIES_DIR/" 2>/dev/null || true
    chmod +x "$BINARIES_DIR/radiantd" "$BINARIES_DIR/radiant-cli" "$BINARIES_DIR/radiant-tx" 2>/dev/null || true
    BINARIES_FOUND=true
    echo -e "  ${GREEN}✓${NC} Local binaries bundled (with libs)"
    
elif [[ -f "$BUILD_BINARIES/radiantd" ]]; then
    # Use freshly built binaries - need to fix dylibs
    echo "  Using freshly built binaries from build-release..."
    cp "$BUILD_BINARIES/radiantd" "$BUILD_BINARIES/radiant-cli" "$BUILD_BINARIES/radiant-tx" "$BINARIES_DIR/" 2>/dev/null || true
    chmod +x "$BINARIES_DIR"/* 2>/dev/null || true
    
    # Fix dynamic library paths for portable distribution
    # This copies Homebrew dylibs, rewrites paths to @loader_path/libs/, and code signs
    if [[ -f "$ROOT_DIR/scripts/fix-macos-dylibs.sh" ]]; then
        echo "  Running fix-macos-dylibs.sh to bundle libraries..."
        "$ROOT_DIR/scripts/fix-macos-dylibs.sh" "$BINARIES_DIR" || {
            echo -e "  ${YELLOW}Warning: Could not fix dylib paths. Binaries may require Homebrew.${NC}"
        }
    fi
    BINARIES_FOUND=true
    echo -e "  ${GREEN}✓${NC} Built binaries bundled"
    
else
    # Try downloading from GitHub releases
    BINARY_URL="https://github.com/Radiant-Core/Radiant-Core/releases/download/v${VERSION}/radiant-core-macos-arm64.zip"
    echo "  Downloading binaries from GitHub..."
    curl -sL -o "$BUILD_DIR/binaries.zip" "$BINARY_URL" || {
        echo -e "${YELLOW}Warning: Could not download binaries.${NC}"
    }
    
    if [[ -f "$BUILD_DIR/binaries.zip" ]]; then
        unzip -q "$BUILD_DIR/binaries.zip" -d "$BUILD_DIR/binary-extract"
        cp -R "$BUILD_DIR/binary-extract/radiant-core-macos-arm64"/* "$BINARIES_DIR/" 2>/dev/null || true
        chmod +x "$BINARIES_DIR"/* 2>/dev/null || true
        
        # Fix dynamic library paths if libs not already bundled
        if [[ ! -d "$BINARIES_DIR/libs" ]] && [[ -f "$ROOT_DIR/scripts/fix-macos-dylibs.sh" ]]; then
            echo "  Running fix-macos-dylibs.sh to bundle libraries..."
            "$ROOT_DIR/scripts/fix-macos-dylibs.sh" "$BINARIES_DIR" || {
                echo -e "  ${YELLOW}Warning: Could not fix dylib paths.${NC}"
            }
        fi
        BINARIES_FOUND=true
        echo -e "  ${GREEN}✓${NC} Downloaded binaries bundled"
    fi
fi

if ! $BINARIES_FOUND; then
    echo -e "  ${YELLOW}No binaries found. App will prompt user to download.${NC}"
fi

# Code sign the entire app bundle (required for macOS)
echo ""
echo "Step 3b: Code signing app bundle..."
echo "----------------------------------------"
codesign --force --deep -s - "$APP_PATH" 2>/dev/null && {
    echo -e "  ${GREEN}✓${NC} App bundle signed"
} || {
    echo -e "  ${YELLOW}Warning: Code signing failed${NC}"
}

echo ""
echo "Step 4: Creating DMG installer..."
echo "----------------------------------------"

DMG_PATH="$BUILD_DIR/$DMG_NAME.dmg"
DMG_TEMP="$BUILD_DIR/dmg-temp"

# Create DMG staging area
rm -rf "$DMG_TEMP"
mkdir -p "$DMG_TEMP"
cp -R "$APP_PATH" "$DMG_TEMP/"

# Create Applications symlink for drag-to-install
ln -s /Applications "$DMG_TEMP/Applications"

# Create README in DMG
cat > "$DMG_TEMP/README.txt" << 'EOF'
Radiant Core GUI
================

Installation:
  Drag "Radiant Core.app" to the Applications folder.

First Launch:
  If macOS blocks the app, right-click and select "Open",
  or run: xattr -rd com.apple.quarantine /Applications/Radiant\ Core.app

Support: https://radiantblockchain.org
EOF

# Create the DMG
echo "  Creating DMG..."
hdiutil create -volname "$DMG_NAME" \
    -srcfolder "$DMG_TEMP" \
    -ov -format UDZO \
    "$DMG_PATH" > /dev/null 2>&1

if [[ -f "$DMG_PATH" ]]; then
    echo -e "  ${GREEN}✓${NC} DMG created"
else
    echo -e "${RED}Error: DMG creation failed${NC}"
    exit 1
fi

# Generate checksum
cd "$BUILD_DIR"
shasum -a 256 "$DMG_NAME.dmg" > "$DMG_NAME.dmg.sha256"

# Get file size
DMG_SIZE=$(ls -lh "$DMG_PATH" | awk '{print $5}')

# Cleanup
deactivate
rm -rf "$DMG_TEMP" "$VENV_DIR" "$BUILD_DIR/build" "$BUILD_DIR/binary-extract"

echo ""
echo "=============================================="
echo -e "${GREEN}Build Complete!${NC}"
echo "=============================================="
echo ""
echo "Output files:"
echo "  $DMG_PATH ($DMG_SIZE)"
echo "  $BUILD_DIR/$DMG_NAME.dmg.sha256"
echo ""
echo "SHA256:"
cat "$BUILD_DIR/$DMG_NAME.dmg.sha256"
echo ""
echo "To upload to GitHub Release:"
echo "  gh release upload v${VERSION} $DMG_PATH $BUILD_DIR/$DMG_NAME.dmg.sha256"
echo ""
