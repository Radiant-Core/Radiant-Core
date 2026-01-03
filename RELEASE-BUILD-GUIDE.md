# Radiant Core Release Build Guide

This guide provides comprehensive instructions for building Radiant Core releases across all platforms.

## Quick Start

### Linux (Native Build)
```bash
./build-linux-release.sh
```

### Docker (Any Platform)
```bash
./build-docker-release.sh
```

### macOS (Native Build)
```bash
./build-macos-release.sh
```

### Windows (Native Build)
```cmd
build-portable-windows-v2.bat
```

### Multi-Platform Build
```bash
./build-all-releases.sh
```

## Platform-Specific Instructions

### Linux x86_64

**Requirements:**
- Ubuntu 20.04+ / CentOS 8+ / Fedora 32+
- GCC 10+ or Clang 11+
- CMake 3.16+, Ninja
- Development libraries: Boost, libevent, OpenSSL, etc.

**Build Process:**
1. Installs dependencies via package manager
2. Configures CMake for Release build
3. Compiles with Ninja
4. Creates tarball with binaries

**Output:** `release-linux/radiant-core-linux-x86_64.tar.gz`

### Docker

**Requirements:**
- Docker Engine 20.10+
- Any host OS (Linux, macOS, Windows)

**Build Process:**
1. Creates multi-stage Dockerfile
2. Builds in Ubuntu 24.04 container
3. Creates minimal runtime image
4. Extracts binaries for distribution

**Output:** 
- Docker image: `radiant-core:main-Release`
- Package: `docker-release/radiant-core-docker.tar.gz`

**Usage:**
```bash
docker run -d --name radiant-node \
  -p 7332:7332 -p 7333:7333 \
  -v radiant-data:/home/radiant/.radiant \
  radiant-core:main-Release
```

### macOS Universal Binary

**Requirements:**
- macOS 10.15+ (Catalina)
- Xcode 12+ or Command Line Tools
- Homebrew
- CMake, Ninja, Qt5 (optional)

**Build Process:**
1. Installs dependencies via Homebrew
2. Configures for Universal Binary (arm64 + x86_64)
3. Sets deployment target to macOS 10.15+
4. Creates DMG installer option

**Output:** 
- Package: `release-macos/radiant-core-macos-universal.tar.gz`
- Optional DMG: `Radiant-Core-{version}.dmg`

### Windows x64

**Option 1: Native Build (Recommended)**
```cmd
build-portable-windows-v2.bat
```

**Requirements:**
- Windows 10/11
- Visual Studio 2019/2022 OR MinGW-w64
- CMake 3.22+

**Option 2: Cross-Compilation (Linux)**
```bash
./build-windows-cross-release.sh
```

**Requirements:**
- Linux with MinGW-w64 cross-compiler
- Same dependencies as Linux build

**Output:** `release-windows/radiant-core-windows-x64.zip`

## Release Artifacts

Each platform produces:

| Platform | Artifact | Contents |
|----------|----------|----------|
| Linux | `radiant-core-linux-x86_64.tar.gz` | radiantd, radiant-cli, radiant-tx |
| Docker | `radiant-core-docker.tar.gz` | Docker image, binaries, Dockerfile |
| macOS | `radiant-core-macos-universal.tar.gz` | Universal binaries, README |
| macOS | `Radiant-Core-{version}.dmg` | DMG installer (optional) |
| Windows | `radiant-core-windows-x64.zip` | EXE files, DLLs, README |

## Automated Multi-Platform Build

The `build-all-releases.sh` script orchestrates building for all platforms:

```bash
./build-all-releases.sh
```

Features:
- Interactive platform selection
- Parallel building when possible
- Automatic packaging
- SHA256 checksum generation
- GitHub Release preparation

## Security Considerations

### Reproducible Builds
- Use deterministic compilation flags
- Fixed dependency versions where possible
- Source code verification via Git tags

### Binary Verification
All releases include SHA256 checksums:
```bash
sha256sum radiant-core-linux-x86_64.tar.gz
```

### Code Signing
For production releases, consider:
- Windows: Authenticode code signing
- macOS: Apple Developer ID signing
- Linux: GPG signature verification

## CI/CD Integration

### GitHub Actions Example
```yaml
name: Build Release
on:
  push:
    tags: ['v*']

jobs:
  build-linux:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Build Linux
        run: ./build-linux-release.sh
      
  build-docker:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Build Docker
        run: ./build-docker-release.sh
      
  build-macos:
    runs-on: macos-latest
    steps:
      - uses: actions/checkout@v3
      - name: Build macOS
        run: ./build-macos-release.sh
```

## Troubleshooting

### Common Issues

**Linux: Missing dependencies**
```bash
sudo apt-get install build-essential cmake ninja-build
```

**macOS: Qt5 not found**
```bash
export CMAKE_PREFIX_PATH="/opt/homebrew/opt/qt@5:$CMAKE_PREFIX_PATH"
```

**Windows: CMake generator error**
```cmd
# Use correct generator name
cmake .. -G "Visual Studio 16 2019" -A x64
# or
cmake .. -G "MinGW Makefiles"
```

**Docker: Permission denied**
```bash
sudo usermod -aG docker $USER
# Logout and login again
```

### Build Logs
All scripts create detailed build logs. Check these for specific error messages:
- Linux: `linux-release/build.log`
- Docker: Docker build output
- macOS: `macos-release/build.log`
- Windows: `build/` directory logs

## Support

For build-related issues:
1. Check this guide first
2. Review build logs
3. Open an issue on GitHub
4. Include system information and error logs

## Contributing

To improve the build system:
1. Test changes on multiple platforms
2. Update documentation
3. Submit pull requests
4. Include build test results

For more information, visit: https://radiantblockchain.org
