#!/bin/bash
#
# Build Radiant Core GUI macOS Application
# Creates a standalone .app bundle and .dmg installer
#
# Requirements:
#   - macOS with Xcode Command Line Tools
#   - Python 3.9+
#   - Build deps are pinned/installed by this script (see scripts/
#     requirements-macos-app.txt). Versions: py2app==0.28.8, pywebview==5.1.
#
# Usage: ./scripts/build-macos-app.sh [version]
# Example: ./scripts/build-macos-app.sh 3.1.1
#

set -e

VERSION="${1:-3.1.1}"
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

# Check for Python 3 — prefer 3.12 or 3.13 over 3.14+ for py2app compatibility.
# py2app 0.28.8 requires pkg_resources (setuptools); Python 3.14 venvs no
# longer pre-install setuptools, and py2app may have other 3.14 edge-cases.
PYTHON_BIN=""
for candidate in python3.12 python3.13 python3; do
    if command -v "$candidate" &> /dev/null; then
        PYTHON_BIN="$candidate"
        break
    fi
done
if [[ -z "$PYTHON_BIN" ]]; then
    echo -e "${RED}Error: Python 3 is required${NC}"
    exit 1
fi
PYTHON_VER=$("$PYTHON_BIN" --version 2>&1)
echo "  Using $PYTHON_BIN ($PYTHON_VER)"

echo "Step 1: Setting up build environment..."
echo "----------------------------------------"

# Create virtual environment for clean build
VENV_DIR="$BUILD_DIR/venv"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

echo "  Creating virtual environment..."
"$PYTHON_BIN" -m venv "$VENV_DIR"
source "$VENV_DIR/bin/activate"

echo "  Installing build dependencies..."
# SECURITY (audit N5): pin build dependencies to exact versions instead of
# pulling whatever PyPI serves at build time. Unpinned `pip install py2app
# pywebview` lets a compromised or yanked-and-replaced release flow into the
# signed .app we ship.
#
# Strongest form: a hash-locked requirements file consumed with
# --require-hashes (pip then refuses any package whose artifact hash differs).
# A scaffold lives at scripts/requirements-macos-app.txt. Until its --hash
# lines are filled in (PIN-REQUIRED, see that file), we fall back to exact
# version pins, which still removes the "latest floating version" risk.
REQ_FILE="$SCRIPT_DIR/requirements-macos-app.txt"
# Upgrade pip first; do NOT pin to 24.0 — a downgrade can interfere with
# setuptools installation in the venv.
pip install --upgrade pip > /dev/null
if [[ -f "$REQ_FILE" ]] && grep -v '^[[:space:]]*#' "$REQ_FILE" | grep -q -- '--hash='; then
    echo "  Using hash-locked $REQ_FILE (--require-hashes)..."
    pip install --require-hashes -r "$REQ_FILE" > /dev/null
else
    echo -e "  ${YELLOW}Note: no hash-locked requirements found; using exact" \
            "version pins (fill in $REQ_FILE with --hash lines to harden).${NC}"
    # py2app 0.28.8 imports pkg_resources (part of setuptools) at load time.
    # setuptools>=70 restructured pkg_resources in a way that breaks py2app's
    # entry-point bootstrap; install setuptools<70 alongside py2app so pip
    # resolves all constraints together to a known-compatible set.
    pip install 'setuptools<70' 'py2app==0.28.8' 'pywebview==5.1' > /dev/null
fi
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

# Pre-populate macos-app-build/binaries/ so that py2app's DATA_FILES can
# resolve the binary paths listed in setup.py.  py2app evaluates DATA_FILES
# relative to BUILD_DIR, so the files must exist there before the build runs.
# The full dylib-fixed copies are inserted into the bundle AFTER py2app in
# Step 3 below; these pre-staged copies just satisfy the DATA_FILES check.
BIN_STAGE="$BUILD_DIR/binaries/radiant-core-macos-arm64"
if [[ -f "$ROOT_DIR/gui/binaries/radiant-core-macos-arm64/radiantd" ]]; then
    mkdir -p "$BIN_STAGE"
    cp "$ROOT_DIR/gui/binaries/radiant-core-macos-arm64/radiantd" \
       "$ROOT_DIR/gui/binaries/radiant-core-macos-arm64/radiant-cli" \
       "$ROOT_DIR/gui/binaries/radiant-core-macos-arm64/radiant-tx" \
       "$BIN_STAGE/" 2>/dev/null || true
    echo -e "  ${GREEN}✓${NC} Binaries pre-staged for py2app DATA_FILES (gui/binaries)"
elif [[ -f "$ROOT_DIR/build-release/src/radiantd" ]]; then
    mkdir -p "$BIN_STAGE"
    cp "$ROOT_DIR/build-release/src/radiantd" \
       "$ROOT_DIR/build-release/src/radiant-cli" \
       "$ROOT_DIR/build-release/src/radiant-tx" \
       "$BIN_STAGE/" 2>/dev/null || true
    echo -e "  ${GREEN}✓${NC} Binaries pre-staged for py2app DATA_FILES (build-release)"
else
    echo -e "  ${YELLOW}Note: No binaries found to pre-stage; py2app DATA_FILES may warn.${NC}"
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

    # py2app DATA_FILES already placed the binaries at:
    #   Resources/binaries/radiant-core-macos-arm64/{radiantd,radiant-cli,radiant-tx}
    # The binaries embed @loader_path/libs/<name>.dylib.  @loader_path resolves
    # to the directory that CONTAINS the binary, so libs/ must live alongside
    # the binaries in radiant-core-macos-arm64/ — NOT one level up at binaries/.
    APP_BIN_SUBDIR="$APP_PATH/Contents/Resources/binaries/radiant-core-macos-arm64"
    if [[ -d "$APP_BIN_SUBDIR" ]]; then
        cp -R "$LOCAL_BINARIES/libs" "$APP_BIN_SUBDIR/" 2>/dev/null || true
        chmod +x "$APP_BIN_SUBDIR/radiantd" "$APP_BIN_SUBDIR/radiant-cli" "$APP_BIN_SUBDIR/radiant-tx" 2>/dev/null || true
        echo -e "  ${GREEN}✓${NC} libs/ copied into radiant-core-macos-arm64/ (fixes @loader_path resolution)"
    else
        # Fallback: py2app did not create the subdir (e.g. DATA_FILES changed)
        # — copy binaries + libs to the flat binaries/ directory instead.
        cp "$LOCAL_BINARIES"/*.dylib "$BINARIES_DIR/" 2>/dev/null || true
        cp "$LOCAL_BINARIES"/radiantd "$LOCAL_BINARIES"/radiant-cli "$LOCAL_BINARIES"/radiant-tx "$BINARIES_DIR/" 2>/dev/null || true
        cp -R "$LOCAL_BINARIES/libs" "$BINARIES_DIR/" 2>/dev/null || true
        chmod +x "$BINARIES_DIR/radiantd" "$BINARIES_DIR/radiant-cli" "$BINARIES_DIR/radiant-tx" 2>/dev/null || true
        echo -e "  ${GREEN}✓${NC} Local binaries bundled (fallback flat layout)"
    fi
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
  If macOS blocks the app, right-click "Radiant Core.app" and select "Open",
  then confirm. This keeps Gatekeeper's signature/notarization checks in place.
  Do NOT run "xattr -rd com.apple.quarantine" — that disables those checks.

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
