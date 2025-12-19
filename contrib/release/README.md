# Radiant Core Release Process

This document describes how to create official Radiant Core releases.

## Quick Start

```bash
# Build release for version 2.0.0
./contrib/release/build-release.sh 2.0.0

# Or auto-detect version from CMakeLists.txt
./contrib/release/build-release.sh
```

## Release Workflow

### 1. Pre-Release Checklist

- [ ] All tests pass (`./contrib/run-ci-local.sh`)
- [ ] Release notes updated (`doc/release-notes/release-notes-X.Y.Z.md`)
- [ ] Version bumped in `CMakeLists.txt`
- [ ] UPGRADES.md updated with new features
- [ ] No uncommitted changes in working directory

### 2. Build Release Artifacts

```bash
./contrib/release/build-release.sh 2.0.0
```

This script will:
1. Build the CI Docker image
2. Build the release Docker image with version tags
3. Extract binaries from the container
4. Create a tarball (`radiant-core-X.Y.Z-linux-x86_64.tar.gz`)
5. Generate SHA256 checksums
6. Sign checksums with your GPG key (if available)

### 3. Create Signed Git Tag

```bash
# Create signed tag
git tag -s v2.0.0 -m "Radiant Core 2.0.0"

# Verify the signature
git tag -v v2.0.0

# Push the tag
git push origin v2.0.0
```

### 4. Push Docker Images (Optional)

If using a container registry:

```bash
# Set registry (e.g., Docker Hub or GitHub Container Registry)
export DOCKER_REGISTRY=ghcr.io/radiantblockchain

# Re-run build to tag for registry
./contrib/release/build-release.sh 2.0.0

# Push images
docker push ghcr.io/radiantblockchain/radiant-core:2.0.0
docker push ghcr.io/radiantblockchain/radiant-core:latest
```

### 5. Upload Release to GitHub/GitLab

Upload these files to the release:
- `radiant-core-X.Y.Z-linux-x86_64.tar.gz`
- `SHA256SUMS`
- `SHA256SUMS.asc`

## Directory Structure

```
contrib/release/
├── README.md           # This file
└── build-release.sh    # Main release build script

release-X.Y.Z/          # Created by build script
├── radiantd
├── radiant-cli
├── radiant-wallet
├── radiant-tx
├── radiant-core-X.Y.Z-linux-x86_64.tar.gz
├── SHA256SUMS
└── SHA256SUMS.asc
```

## Docker Images

| Image | Description |
|-------|-------------|
| `radiant-node-ci` | Build environment (Ubuntu 24.04, CMake, Boost, etc.) |
| `radiant-core:X.Y.Z` | Release image with radiantd binary |
| `radiant-core:latest` | Latest release |

## Environment Variables

| Variable | Description | Example |
|----------|-------------|---------|
| `DOCKER_REGISTRY` | Container registry prefix | `ghcr.io/radiantblockchain` |

## Verifying Releases

Users can verify releases using:

```bash
# Import maintainer keys
gpg --import contrib/gitian-signing/pubkeys/*.asc

# Verify signature
gpg --verify SHA256SUMS.asc SHA256SUMS

# Verify checksum
sha256sum -c SHA256SUMS --ignore-missing
```

## See Also

- [contrib/gitian-signing/README.md](../gitian-signing/README.md) - GPG signing guide
- [doc/release-notes/](../../doc/release-notes/) - Release notes
