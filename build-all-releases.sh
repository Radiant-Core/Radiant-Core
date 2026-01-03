#!/bin/bash
# Radiant Core Multi-Platform Release Build Script

set -e

echo "========================================"
echo "Radiant Core Multi-Platform Release"
echo "========================================"

# Get current version
VERSION=$(git describe --tags --always 2>/dev/null || echo "2.0.0")
echo "Building version: $VERSION"

# Create release directory
RELEASE_DIR="radiant-core-$VERSION-release"
mkdir -p "$RELEASE_DIR"
cd "$RELEASE_DIR"

echo ""
echo "Available build targets:"
echo "1. Linux x86_64"
echo "2. Docker (Linux containers)"
echo "3. macOS (Universal Binary)"
echo "4. Windows x64 (cross-compile)"
echo "5. All platforms"
echo ""
read -p "Select build target (1-5): " CHOICE

case $CHOICE in
    1)
        echo "Building Linux release..."
        bash ../build-linux-release.sh
        ;;
    2)
        echo "Building Docker release..."
        bash ../build-docker-release.sh
        ;;
    3)
        echo "Building macOS release..."
        bash ../build-macos-release.sh
        ;;
    4)
        echo "Building Windows release (cross-compile)..."
        bash ../build-windows-cross-release.sh
        ;;
    5)
        echo "Building all platforms..."
        
        echo ""
        echo "========================================"
        echo "Building Linux x86_64..."
        echo "========================================"
        if [[ "$OSTYPE" == "linux-gnu"* ]]; then
            bash ../build-linux-release.sh
        else
            echo "Skipping Linux build (not on Linux)"
        fi
        
        echo ""
        echo "========================================"
        echo "Building Docker release..."
        echo "========================================"
        bash ../build-docker-release.sh
        
        echo ""
        echo "========================================"
        echo "Building macOS release..."
        echo "========================================"
        if [[ "$OSTYPE" == "darwin"* ]]; then
            bash ../build-macos-release.sh
        else
            echo "Skipping macOS build (not on macOS)"
        fi
        
        echo ""
        echo "========================================"
        echo "Building Windows release..."
        echo "========================================"
        if [[ "$OSTYPE" == "linux-gnu"* ]]; then
            bash ../build-windows-cross-release.sh
        else
            echo "Skipping Windows cross-compile (not on Linux)"
        fi
        ;;
    *)
        echo "Invalid selection"
        exit 1
        ;;
esac

echo ""
echo "========================================"
echo "Creating release packages..."
echo "========================================"

# Create tarballs for Linux
if [ -d "release-linux" ]; then
    echo "Creating Linux tarball..."
    cd release-linux
    tar -czf "../radiant-core-linux-x86_64-$VERSION.tar.gz" *
    cd ..
fi

# Create Docker package
if [ -d "docker-release" ]; then
    echo "Creating Docker package..."
    cd docker-release
    tar -czf "../radiant-core-docker-$VERSION.tar.gz" *
    cd ..
fi

# Create macOS package
if [ -d "release-macos" ]; then
    echo "Creating macOS package..."
    cd release-macos
    tar -czf "../radiant-core-macos-universal-$VERSION.tar.gz" *
    
    # Try to create DMG if on macOS
    if [[ "$OSTYPE" == "darwin"* ]] && [ -f "../create-dmg.sh" ]; then
        echo "Creating DMG..."
        bash ../create-dmg.sh
    fi
    cd ..
fi

# Create Windows package
if [ -d "release-windows" ]; then
    echo "Creating Windows package..."
    cd release-windows
    zip -r "../radiant-core-windows-x64-$VERSION.zip" *
    cd ..
fi

# Create checksums
echo "Creating checksums..."
cd ..
for file in "$RELEASE_DIR"/*.{tar.gz,zip,dmg}; do
    if [ -f "$file" ]; then
        sha256sum "$file" > "$file.sha256"
    fi
done

echo ""
echo "========================================"
echo "Release build completed!"
echo "========================================"
echo "Version: $VERSION"
echo "Location: $RELEASE_DIR/"
echo ""
echo "Created packages:"
ls -la "$RELEASE_DIR"/*.{tar.gz,zip,dmg,sha256} 2>/dev/null || true
echo ""
echo "Checksums:"
for file in "$RELEASE_DIR"/*.sha256; do
    if [ -f "$file" ]; then
        echo "$(basename "$file"):"
        cat "$file"
        echo ""
    fi
done
echo ""
echo "To upload to GitHub Releases:"
echo "1. Create a new release on GitHub"
echo "2. Upload the files from $RELEASE_DIR/"
echo "3. Copy the checksums to the release description"
echo "========================================"
