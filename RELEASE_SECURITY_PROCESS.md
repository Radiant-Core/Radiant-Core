# Release Security Process

This document outlines the security-hardened release engineering process for Radiant Core, addressing the findings from the security audit (C5).

## Security Requirements

All releases **must** meet the following security standards. These are
**mandatory and CI-enforced** — the release workflow
(`.github/workflows/release.yml`) refuses to publish (the GitHub Release is
left as a `draft`) until every requirement below is satisfied:

- The pushed tag is an **annotated, GPG-signed** git tag (`git tag -s`); the
  release job verifies `git tag -v` and fails the pipeline on an unsigned or
  unverifiable tag.
- A **GPG-signed `SHA256SUMS`** manifest covering **every** release artifact is
  present and verifies; the release job fails if it is missing or the signature
  does not verify.
- The GUI auto-downloader hashes have been updated from their placeholder
  values (see §3); the GUI itself **fails closed** at runtime if they have not.

> **Why signatures, not just hashes?** A `SHA256SUMS` file alone provides *no*
> security if it travels down the same channel as the binaries (the GitHub
> Release). An attacker who can swap a binary can equally swap the hash file.
> Integrity therefore comes from the **detached GPG signature** over
> `SHA256SUMS`, anchored to a maintainer key that users obtain out-of-band.
> Hashes are a convenience for `sha256sum -c`; the signature is the trust root.

### 1. Signed Git Tags

**Requirement (MANDATORY / CI-ENFORCED):** Every release must have an
annotated, GPG-signed git tag. Lightweight or unsigned tags are rejected by the
release workflow.

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

**Requirement (MANDATORY / CI-ENFORCED):** Every release artifact must be
covered by a `SHA256SUMS` manifest, and that manifest must carry a **detached
GPG signature**. A hash without a signature is **not** acceptable: the hash
rides the same distribution channel as the binary and provides no protection
against a tampered release. The release workflow blocks publication
(keeps the release a `draft`) until a verifiable `SHA256SUMS.asc` is present.

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

**Requirement:** Update SHA-256 hashes in `gui/radiant_node_web.py` for each
release. Because the GUI ships *without* vendored node binaries (see §5 and
audit findings N1/C4), the auto-downloader is now the only path by which the
GUI obtains `radiantd`/`radiant-cli`/`radiant-tx`, so correct, pinned hashes
are a hard requirement for every release.

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

**Fail-closed behaviour (audit N6 — now TRUE in code):** The GUI auto-downloader
**fails closed**. It refuses to download or execute any binary when the pinned
`sha256` is still a placeholder (`PLACEHOLDER_SHA256_UPDATE_AT_RELEASE`),
empty, or missing, and it aborts if a downloaded asset's computed SHA-256 does
not match the pinned value. There is no "download anyway" fallback. This makes
the previous documentation claim accurate rather than aspirational.

> **Limitation — hashes are not a trust root.** These pinned hashes are
> fetched/shipped over the same channel as the GUI and the release. They
> protect against accidental corruption and detect a mismatched download, but
> they are **not** a substitute for the GPG-signed `SHA256SUMS` manifest (§2).
> Wherever feasible, operators and packagers should additionally verify the
> detached GPG signature before trusting an artifact.

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
- Never commit `releases/` directory binary contents (`.exe`, `.zip`,
  `.tar.gz`, `.dmg`, `.dylib`, bare executables, `._*` AppleDouble sidecars)
- Never vendor node binaries under `gui/binaries/**` (see below)

**Alternative:**
- Use GitHub Releases for binary distribution
- Store release artifacts in separate storage (not in repo)

**`.gitignore` enforcement (audit C5):** The earlier `.gitignore` *re-included*
release binaries with `!releases/**/*.{exe,zip,tar.gz}`, causing ~800 MB of
compiled artifacts to be tracked. That allow-list has been removed and the
binary patterns are now ignored. Text metadata kept in `releases/`
(`RELEASE_NOTES.md`, `SHA256SUMS.txt`, `*.sha256`) is still tracked.

**GUI no longer ships node binaries (depends on N1/C4):** The macOS node
binaries and dylibs that were tracked under
`gui/binaries/radiant-core-macos-arm64/` are removed from the repo and ignored.
Consequently the GUI **must download and verify** `radiantd`, `radiant-cli`,
and `radiant-tx` at runtime via the auto-downloader, which fails closed on
placeholder/missing/mismatched hashes (§3, audit N6/C4). Release engineers
**must** keep the GUI's pinned `RELEASE_ASSETS` hashes current for every
release or the GUI will be unable to obtain its binaries.

## Pre-Release Checklist

Before tagging a release:

- [ ] All Critical and High severity issues are addressed
- [ ] Regression tests pass (including OP_K12/BLAKE3 boundary tests)
- [ ] GUI security features tested:
  - [ ] Per-launch token generation
  - [ ] Host header validation
  - [ ] Security headers present
  - [ ] SHA-256 verification on auto-download
- [ ] SHA-256 hashes updated in `gui/radiant_node_web.py` (GUI fails closed on
      placeholders — required because the GUI no longer vendors node binaries)
- [ ] GitHub Actions workflows use pinned commit SHAs (not floating tags), and
      every workflow scopes `permissions:` to least privilege
- [ ] GPG key available and listed in `contrib/gitian-signing/keys.txt`
- [ ] Annotated, GPG-signed tag pushed; `git tag -v` verifies (CI re-checks)
- [ ] GPG-signed `SHA256SUMS` for ALL artifacts present; `gpg --verify` passes
      (CI re-checks and keeps the GitHub Release a draft until satisfied)

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
