#!/bin/bash
# Radiant Core Release Build Script
# Usage: ./contrib/build-release.sh [version]
# Example: ./contrib/build-release.sh v1.0.0

set -e

VERSION=${1:-"v0.0.0-dev"}
BUILD_TYPE="Release"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
RELEASE_DIR="$PROJECT_DIR/release"

echo "========================================="
echo "Radiant Core Release Build"
echo "Version: $VERSION"
echo "========================================="

# Detect OS
case "$(uname -s)" in
    Linux*)     OS="linux";;
    Darwin*)    OS="macos";;
    MINGW*|MSYS*|CYGWIN*) OS="windows";;
    *)          OS="unknown";;
esac

ARCH=$(uname -m)
case "$ARCH" in
    x86_64)  ARCH="x64";;
    aarch64) ARCH="arm64";;
    arm64)   ARCH="arm64";;
esac

RELEASE_NAME="radiant-${VERSION}-${OS}-${ARCH}"

echo "Detected OS: $OS"
echo "Detected Architecture: $ARCH"
echo "Release Name: $RELEASE_NAME"
echo ""

# Clean and create build directory
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure based on OS
echo "Configuring build..."
if [ "$OS" = "macos" ]; then
    cmake .. -G Ninja \
        -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
        -DBUILD_RADIANT_QT=ON \
        -DBUILD_RADIANT_WALLET=ON \
        -DOPENSSL_ROOT_DIR=$(brew --prefix openssl@3 2>/dev/null || echo "/usr/local/opt/openssl@3") \
        -DQt5_DIR=$(brew --prefix qt@5 2>/dev/null || echo "/usr/local/opt/qt@5")/lib/cmake/Qt5
elif [ "$OS" = "linux" ]; then
    cmake .. -G Ninja \
        -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
        -DBUILD_RADIANT_QT=ON \
        -DBUILD_RADIANT_WALLET=ON
else
    cmake .. \
        -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
        -DBUILD_RADIANT_QT=OFF \
        -DBUILD_RADIANT_WALLET=ON
fi

# Build
echo ""
echo "Building..."
if command -v ninja &> /dev/null; then
    ninja -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
else
    cmake --build . --config $BUILD_TYPE -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
fi

# Create release directory
echo ""
echo "Creating release package..."
rm -rf "$RELEASE_DIR"
mkdir -p "$RELEASE_DIR/$RELEASE_NAME"

# Copy binaries
if [ "$OS" = "windows" ]; then
    cp "$BUILD_DIR/src/$BUILD_TYPE/"*.exe "$RELEASE_DIR/$RELEASE_NAME/" 2>/dev/null || true
else
    for binary in radiantd radiant-cli radiant-tx radiant-wallet; do
        if [ -f "$BUILD_DIR/src/$binary" ]; then
            cp "$BUILD_DIR/src/$binary" "$RELEASE_DIR/$RELEASE_NAME/"
        fi
    done
    
    # Copy Qt app on macOS
    if [ "$OS" = "macos" ] && [ -d "$BUILD_DIR/src/qt/Radiant-Qt.app" ]; then
        cp -R "$BUILD_DIR/src/qt/Radiant-Qt.app" "$RELEASE_DIR/$RELEASE_NAME/"
    elif [ -f "$BUILD_DIR/src/qt/radiant-qt" ]; then
        cp "$BUILD_DIR/src/qt/radiant-qt" "$RELEASE_DIR/$RELEASE_NAME/"
    fi
fi

# Copy documentation
cp "$PROJECT_DIR/README.md" "$RELEASE_DIR/$RELEASE_NAME/" 2>/dev/null || true
cp "$PROJECT_DIR/COPYING" "$RELEASE_DIR/$RELEASE_NAME/" 2>/dev/null || true

# Create archives
cd "$RELEASE_DIR"

# Create zip (works on all platforms)
if command -v zip &> /dev/null; then
    zip -r "${RELEASE_NAME}.zip" "$RELEASE_NAME"
    echo "Created: $RELEASE_DIR/${RELEASE_NAME}.zip"
fi

# Create tar.gz on Unix
if [ "$OS" != "windows" ]; then
    tar -czvf "${RELEASE_NAME}.tar.gz" "$RELEASE_NAME"
    echo "Created: $RELEASE_DIR/${RELEASE_NAME}.tar.gz"
fi

# Create DMG on macOS
if [ "$OS" = "macos" ]; then
    if command -v hdiutil &> /dev/null; then
        hdiutil create -volname "Radiant-${VERSION}" \
            -srcfolder "$RELEASE_NAME" \
            -ov -format UDZO \
            "${RELEASE_NAME}.dmg" 2>/dev/null || echo "Note: DMG creation skipped"
        if [ -f "${RELEASE_NAME}.dmg" ]; then
            echo "Created: $RELEASE_DIR/${RELEASE_NAME}.dmg"
        fi
    fi
fi

echo ""
echo "========================================="
echo "Build Complete!"
echo "Release files in: $RELEASE_DIR"
echo "========================================="
ls -la "$RELEASE_DIR"
