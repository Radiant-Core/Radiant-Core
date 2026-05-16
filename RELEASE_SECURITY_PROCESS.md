# Release Security Process

This document outlines the security-hardened release engineering process for Radiant Core, addressing the findings from the security audit (C5).

## Security Requirements

All releases must meet the following security standards:

### 1. Signed Git Tags

**Requirement:** Every release must have a GPG-signed git tag.

**Process:**
```bash
# Create and sign a release tag
git tag -s v3.0.1 -m "Release version 3.0.1"

# Verify the tag
git tag -v v3.0.1

# Push the tag to remote
git push origin v3.0.1
```

**Verification:**
- Unsigned tags are considered release-blockers
- Only maintainers with GPG keys listed in `contrib/gitian-signing/keys.txt` may sign releases
- Minimum 2 maintainer signatures required on release tags

### 2. Signed Release Artifacts

**Requirement:** All release artifacts must have SHA-256 checksums and GPG signatures.

**Process:**
```bash
# Generate SHA-256SUMS.txt for release artifacts
cd releases/v3.0.1
sha256sum *.tar.gz *.zip > SHA256SUMS.txt

# Create detached GPG signatures (by multiple maintainers)
gpg --armor --detach-sign SHA256SUMS.txt
cp SHA256SUMS.txt.asc SHA256SUMS.txt.maintainer1.asc
gpg --armor --detach-sign -o SHA256SUMS.txt.maintainer2.asc SHA256SUMS.txt

# Verify signatures
gpg --verify SHA256SUMS.txt.maintainer1.asc SHA256SUMS.txt
```

**Verification:**
- Users must be able to verify: `gpg --verify SHA256SUMS.txt.asc SHA256SUMS.txt`
- Checksums must be verified before installation: `sha256sum -c SHA256SUMS.txt`

### 3. SHA-256 Manifest for GUI Auto-Downloader

**Requirement:** Update SHA-256 hashes in `gui/radiant_node_web.py` for each release.

**Process:**
```bash
# Compute SHA-256 for each release asset
sha256sum radiant-core-macos-arm64.zip
sha256sum radiant-core-linux-x64.tar.gz
sha256sum radiant-core-windows-x64.zip

# Update RELEASE_ASSETS in gui/radiant_node_web.py with new hashes
# Change from: "sha256": "PLACEHOLDER_SHA256_UPDATE_AT_RELEASE"
# Change to:   "sha256": "<computed_hash>"
```

**Important:** The GUI will refuse to auto-download if SHA-256 hashes are not updated from placeholder values.

### 4. Reproducible Builds (Future Goal)

**Current Status:** The `depends/` tree exists but is not used by release scripts.

**Planned Implementation:**
1. Modify `scripts/build-*-release.sh` to use `depends/` instead of system package managers
2. Add CI job that rebuilds release and asserts byte equality
3. Document the deterministic build process

### 5. Binary Commit Policy

**Prohibited:**
- Never commit compiled binaries to the git repository
- Never commit CMake build directories (`macos-arm64-release/`, etc.)
- Never commit `releases/` directory contents

**Alternative:**
- Use GitHub Releases for binary distribution
- Store release artifacts in separate storage (not in repo)

## Pre-Release Checklist

Before tagging a release:

- [ ] All Critical and High severity issues are addressed
- [ ] Regression tests pass (including OP_K12/BLAKE3 boundary tests)
- [ ] GUI security features tested:
  - [ ] Per-launch token generation
  - [ ] Host header validation
  - [ ] Security headers present
  - [ ] SHA-256 verification on auto-download
- [ ] SHA-256 hashes updated in `gui/radiant_node_web.py`
- [ ] GitHub Actions workflows use pinned commit SHAs (not floating tags)
- [ ] GPG key available and listed in `contrib/gitian-signing/keys.txt`

## Release Checklist

During the release:

- [ ] Create signed git tag: `git tag -s vX.Y.Z`
- [ ] Build release artifacts using deterministic process
- [ ] Generate SHA256SUMS.txt and sign it
- [ ] Verify signatures from ≥2 maintainers
- [ ] Upload to GitHub Releases (not git repo)
- [ ] Update GUI hashes for auto-downloader
- [ ] Publish security advisory if fixing Critical/High issues

## Post-Release Verification

After release:

- [ ] Verify tag signature: `git tag -v vX.Y.Z`
- [ ] Verify artifact signatures: `gpg --verify SHA256SUMS.txt.asc`
- [ ] Test GUI auto-downloader with new hashes
- [ ] Update release documentation

## Security Contacts

For security issues related to the release process:
- Report to: security@radiantcore.org
- GPG keys: See `contrib/gitian-signing/keys.txt`

## References

- Audit Finding C5: Release engineering posture
- Audit Finding C4: GUI auto-downloader security
- Audit Finding C3: GUI HTTP server security
