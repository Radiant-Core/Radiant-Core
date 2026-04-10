# Building the Radiant Node Web GUI DMG for macOS

This guide explains how to build the macOS DMG for the Radiant Node Web GUI.

## Prerequisites

- macOS (ARM64 or Intel)
- Python 3.8 or later
- Xcode Command Line Tools (`xcode-select --install`)
- Fresh build of Radiant Core binaries

## Build Steps

### 1. Update Binaries

First, ensure you have the latest macOS ARM64 binaries in the correct location:

```bash
cd /Users/main/Downloads/Radiant-Core-main

# Copy fresh binaries to gui/binaries/radiant-core-macos-arm64/
mkdir -p gui/binaries/radiant-core-macos-arm64
cp build/src/radiantd gui/binaries/radiant-core-macos-arm64/
cp build/src/radiant-cli gui/binaries/radiant-core-macos-arm64/
cp build/src/radiant-tx gui/binaries/radiant-core-macos-arm64/

# Make them executable
chmod +x gui/binaries/radiant-core-macos-arm64/*
```

### 2. Set Up Python Virtual Environment

```bash
cd gui

# Create virtual environment
python3 -m venv venv

# Activate it
source venv/bin/activate

# Install dependencies
pip install --upgrade pip setuptools py2app
```

### 3. Bundle Dynamic Libraries (CRITICAL)

The macOS binaries need their dylibs bundled to work on other machines:

```bash
# Fix dylib dependencies
cd /Users/main/Downloads/Radiant-Core-main
./scripts/fix-macos-dylibs.sh gui/binaries/radiant-core-macos-arm64

# This creates gui/binaries/radiant-core-macos-arm64/libs/ with all required dylibs
```

### 4. Build the App Bundle

```bash
cd gui

# Clean previous builds
rm -rf dist build

# Build the app
source venv/bin/activate
python3 setup.py py2app

# Copy bundled dylibs to app bundle (py2app doesn't handle wildcards)
# Note: The binaries expect dylibs in ../Frameworks/ (not libs/)
cp -R binaries/radiant-core-macos-arm64/libs dist/"Radiant Core.app"/Contents/Resources/binaries/radiant-core-macos-arm64/

# CRITICAL: Also copy dylibs to Frameworks/ directory where binaries expect them
mkdir -p dist/"Radiant Core.app"/Contents/Resources/binaries/Frameworks
cp binaries/radiant-core-macos-arm64/libs/* dist/"Radiant Core.app"/Contents/Resources/binaries/Frameworks/

# Verify the app was built
ls -la dist/"Radiant Core.app"

# Test the bundled binary (must show version without dyld errors)
dist/"Radiant Core.app"/Contents/Resources/binaries/radiant-core-macos-arm64/radiantd --version
```

### 5. Verify Binary Paths

The app bundle should have this structure:

```
Radiant Core.app/
├── Contents/
    ├── MacOS/
    │   └── Radiant Core (Python executable)
    ├── Resources/
    │   ├── binaries/
    │   │   ├── radiant-core-macos-arm64/
    │   │   │   ├── radiantd
    │   │   │   ├── radiant-cli
    │   │   │   ├── radiant-tx
    │   │   │   └── libs/ (58 bundled dylibs - copied for reference)
    │   │   │       ├── libcrypto.3.dylib
    │   │   │       ├── libevent_pthreads-2.1.7.dylib
    │   │   │       ├── libzmq.5.dylib
    │   │   │       └── ... (55 more dylibs)
    │   │   └── Frameworks/ (CRITICAL: binaries load from here)
    │   │       ├── libevent-2.1.7.dylib
    │   │       ├── libcrypto.3.dylib
    │   │       ├── libzmq.5.dylib
    │   │       └── ... (all 58 dylibs)
    │   ├── bip39.py
    │   ├── radiant_node_web.py
    │   └── ... (other Python resources)
    └── Info.plist
```

Verify the binaries are in the correct location:

```bash
ls -la dist/"Radiant Core.app"/Contents/Resources/binaries/radiant-core-macos-arm64/
```

### 6. Test the App Locally

Before creating the DMG, test the app:

```bash
open dist/"Radiant Core.app"
```

The app should:
- Launch without errors
- Find the binaries automatically
- Start the node successfully

### 7. Create the DMG

```bash
cd ..  # Back to Radiant-Core-main root

# Create DMG
hdiutil create \
  -volname "Radiant Node Web GUI 2.3.0" \
  -srcfolder gui/dist/"Radiant Core.app" \
  -ov \
  -format UDZO \
  releases/v2.3.0/Radiant-Node-Web-GUI-2.3.0.dmg

# Verify DMG was created
ls -lh releases/v2.3.0/Radiant-Node-Web-GUI-2.3.0.dmg
```

### 8. Test the DMG

Mount and test the DMG:

```bash
# Mount the DMG
hdiutil attach releases/v2.3.0/Radiant-Node-Web-GUI-2.3.0.dmg -readonly

# The DMG should contain "Radiant Core.app"
ls -la /Volumes/Radiant\ Node\ Web\ GUI\ 2.3.0/

# Test launching from the DMG
open /Volumes/Radiant\ Node\ Web\ GUI\ 2.3.0/"Radiant Core.app"

# Unmount when done
hdiutil detach /Volumes/Radiant\ Node\ Web\ GUI\ 2.3.0/
```

### 9. Update SHA256 Checksums

```bash
cd releases/v2.3.0

# Regenerate checksums with the new DMG
shasum -a 256 \
  radiant-core-macos-arm64-v2.3.0.tar.gz \
  radiant-core-gui-macos-arm64-v2.3.0.zip \
  Radiant-Node-Web-GUI-2.3.0.dmg \
  radiant-core-docker-v2.3.0.tar.gz \
  radiant-core-linux-x64-v2.3.0.tar.gz \
  > SHA256SUMS.txt
```

## Troubleshooting

### "Exec format error" when launching

This means the app is trying to run Linux binaries instead of macOS binaries. Check:

1. Binaries are in `Contents/Resources/binaries/radiant-core-macos-arm64/`
2. Binaries are macOS ARM64: `file dist/"Radiant Core.app"/Contents/Resources/binaries/radiant-core-macos-arm64/radiantd`
3. Should show: `Mach-O 64-bit executable arm64`

### "Library not loaded" or "dyld" errors

This means dylibs are missing or in the wrong location. The binaries expect dylibs in `../Frameworks/` relative to their location.

Check:

1. Run the dylib fix script: `./scripts/fix-macos-dylibs.sh gui/binaries/radiant-core-macos-arm64`
2. Copy libs to the app bundle's `radiant-core-macos-arm64/libs/` directory
3. **CRITICAL**: Also copy dylibs to `binaries/Frameworks/` where binaries load from:
   ```bash
   mkdir -p dist/"Radiant Core.app"/Contents/Resources/binaries/Frameworks
   cp binaries/radiant-core-macos-arm64/libs/* dist/"Radiant Core.app"/Contents/Resources/binaries/Frameworks/
   ```
4. Test binary: `dist/"Radiant Core.app"/Contents/Resources/binaries/radiant-core-macos-arm64/radiantd --version`
5. Should show version without dyld errors

### App won't open (Gatekeeper)

On first launch, right-click the app and select "Open", or run:

```bash
xattr -rd com.apple.quarantine dist/"Radiant Core.app"
```

### Python import errors

Make sure py2app is properly installed in the virtual environment:

```bash
source venv/bin/activate
pip list | grep py2app
```

## Quick Rebuild Script

For convenience, here's a one-liner to rebuild everything:

```bash
cd /Users/main/Downloads/Radiant-Core-main && \
  cp build/src/{radiantd,radiant-cli,radiant-tx} gui/binaries/radiant-core-macos-arm64/ && \
  chmod +x gui/binaries/radiant-core-macos-arm64/* && \
  ./scripts/fix-macos-dylibs.sh gui/binaries/radiant-core-macos-arm64 && \
  cd gui && \
  rm -rf dist build && \
  source venv/bin/activate && \
  python3 setup.py py2app && \
  cp -R binaries/radiant-core-macos-arm64/libs dist/"Radiant Core.app"/Contents/Resources/binaries/radiant-core-macos-arm64/ && \
  mkdir -p dist/"Radiant Core.app"/Contents/Resources/binaries/Frameworks && \
  cp binaries/radiant-core-macos-arm64/libs/* dist/"Radiant Core.app"/Contents/Resources/binaries/Frameworks/ && \
  cd .. && \
  hdiutil create -volname "Radiant Node Web GUI 2.3.0" -srcfolder gui/dist/"Radiant Core.app" -ov -format UDZO releases/v2.3.0/Radiant-Node-Web-GUI-2.3.0.dmg && \
  echo "✓ DMG created successfully"
```

## Version Updates

When releasing a new version (e.g., 2.3.0):

1. Update `APP_VERSION` in `gui/setup.py`
2. Update `GITHUB_RELEASE_URL` in `gui/radiant_node_web.py`
3. Update DMG volume name in the `hdiutil create` command
4. Update output filename to match version

## Important Notes

- **Binary Search Order**: The app now checks app bundle Resources FIRST before any external paths
- **Platform Detection**: macOS app bundles will always use the bundled macOS ARM64 binaries
- **No Cross-Platform**: The app will NOT accidentally use Linux binaries from Downloads folder
- **Self-Contained**: The DMG includes everything needed - no external downloads required

## Notes

- The app bundle includes all Python dependencies via py2app
- Binaries must be macOS ARM64 (Mach-O) for Apple Silicon
- The app will work on Intel Macs via Rosetta 2
- Total DMG size is ~16 MB (includes Python runtime + binaries)
