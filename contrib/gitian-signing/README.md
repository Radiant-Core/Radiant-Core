# Release Signing Guide

This document explains how Radiant Core releases are signed and how users can verify signatures.

## Overview

All official Radiant Core releases are signed by trusted maintainers. This allows users to verify that binaries haven't been tampered with.

### Signing Methods

| Method | Use Case | Files |
|--------|----------|-------|
| **GPG** | Binary releases, tarballs | `.asc` signature files |
| **SHA256** | Quick integrity check | `SHA256SUMS` file |
| **Git Tags** | Source releases | Signed git tags |

## Maintainer Keys

### Current Signing Keys

| Maintainer | Key Type | Fingerprint | Email |
|------------|----------|-------------|-------|
| Art | Ed25519 | `SHA256:V1HnaMF9599Bu6qy9iSXdhmjSg74XYi8VFmD6xJm4m4` | art@radiantfoundation.org |

### Key Files

- `pubkeys/` - Contains maintainer public keys
- `SHA256SUMS.asc` - Signed checksums for release binaries

## For Users: Verifying Releases

### 1. Import Maintainer Keys

```bash
# Import all maintainer keys
gpg --import contrib/gitian-signing/pubkeys/*.asc

# Or import from keyserver
gpg --keyserver keyserver.ubuntu.com --recv-keys <KEY_ID>
```

### 2. Verify Binary Checksums

```bash
# Download the release and signature files
wget https://github.com/radiantblockchain/radiant-node/releases/download/v2.0.0/radiant-core-2.0.0-linux-x86_64.tar.gz
wget https://github.com/radiantblockchain/radiant-node/releases/download/v2.0.0/SHA256SUMS
wget https://github.com/radiantblockchain/radiant-node/releases/download/v2.0.0/SHA256SUMS.asc

# Verify the signature on SHA256SUMS
gpg --verify SHA256SUMS.asc SHA256SUMS

# Verify the binary checksum
sha256sum -c SHA256SUMS --ignore-missing
```

Expected output:
```
gpg: Good signature from "Art <art@radiantfoundation.org>"
radiant-core-2.0.0-linux-x86_64.tar.gz: OK
```

### 3. Verify Git Tags

```bash
# Fetch tags
git fetch --tags

# Verify a signed tag
git tag -v v2.0.0
```

## For Maintainers: Signing Releases

### Setup GPG Key (One-time)

```bash
# Generate a new GPG key (if needed)
gpg --full-generate-key

# Export public key for the repo
gpg --armor --export art@radiantfoundation.org > contrib/gitian-signing/pubkeys/art.asc

# Configure git to use your key
git config --global user.signingkey <KEY_ID>
git config --global commit.gpgsign true
git config --global tag.gpgsign true
```

### Sign a Release

```bash
# 1. Create checksums for all release binaries
cd release-binaries/
sha256sum radiant-core-*.tar.gz radiant-core-*.zip > SHA256SUMS

# 2. Sign the checksums file
gpg --armor --detach-sign SHA256SUMS

# 3. Create a signed git tag
git tag -s v2.0.0 -m "Radiant Core 2.0.0"

# 4. Push the signed tag
git push origin v2.0.0
```

### Release Checklist

- [ ] All binaries built deterministically (Gitian)
- [ ] SHA256SUMS generated
- [ ] SHA256SUMS.asc signature created
- [ ] Git tag signed
- [ ] Public key in `contrib/gitian-signing/pubkeys/`
- [ ] Release notes updated

## Gitian Deterministic Builds

For maximum security, releases should be built using Gitian to ensure reproducible builds:

```bash
# See contrib/gitian-descriptors/ for build descriptors
./contrib/gitian-build.py --setup
./contrib/gitian-build.py --build v2.0.0
```

Multiple maintainers can independently build and compare checksums to verify no tampering occurred.

## Security Contact

Report security issues to: security@radiantfoundation.org

See [DISCLOSURE_POLICY.md](/DISCLOSURE_POLICY.md) for responsible disclosure guidelines.
