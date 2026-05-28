# Reproducible Builds

Starting with Radiant Core v3.0.0, the release-build scripts produce
byte-reproducible artifacts on three of the four supported platforms.
This document explains the workflow, the per-platform state, and how to
independently verify a release.

## What "reproducible" means here

A release tarball is reproducible if any maintainer, starting from a clean
checkout of the source tree at the release commit, can run the same build
script on the same host and get a tarball whose SHA-256 matches the
published one byte-for-byte. Two properties combine to make this work:

1. **All linked C/C++ dependencies come from `depends/`**, not from system
   package managers. `depends/packages/*.mk` pins the upstream tarball
   URL, version, and SHA-256 for every dependency. The build won't run
   against a package whose hash doesn't match.

2. **Every embedded timestamp is pinned to the commit's author time** via
   `SOURCE_DATE_EPOCH`. This covers object file metadata, `ar` indices,
   tar headers, and the gzip stream. Combined with stable file ordering
   (`tar --sort=name`, `find … | sort` for zip), this eliminates the
   common sources of byte-level drift between rebuilds.

## Per-platform status

| Platform               | Script                                | depends/-based | Reproducible | Notes |
|------------------------|---------------------------------------|----------------|--------------|-------|
| Linux x86_64           | `scripts/build-linux-release.sh`      | yes            | yes          | Tested on Ubuntu 24.04 + GCC 13. |
| Linux x86_64 (Docker)  | `scripts/build-docker-release.sh`     | yes            | yes          | Base image pinned by SHA digest in `docker/Dockerfile.linux`. |
| Windows x64 (mingw)    | `scripts/build-windows-release.sh`    | yes            | yes          | Cross-compiled from Linux. Requires mingw-w64 + posix-threads. |
| macOS arm64            | `scripts/build-macos-release.sh`      | yes            | partial      | Pre-codesign tarball is reproducible; codesigning + notarization break byte-equality (Apple constraint). |

## Build environment baseline

The reference build host for each platform is the configuration we run CI on:

- **Linux x86_64**: Ubuntu 24.04 LTS, GCC 13, GNU tar ≥ 1.30, GNU gzip
- **Docker**: pinned `ubuntu:24.04@sha256:…` base; rotated explicitly when
  upstream Ubuntu publishes a new image. The digest lives in
  `docker/Dockerfile.linux` — search for `PINNED_DIGEST_PLACEHOLDER` to
  find the rotation point.
- **Windows cross**: Ubuntu 24.04 LTS as the build host, mingw-w64
  (`g++-mingw-w64-x86-64`), posix threads
- **macOS arm64**: macOS 14 (Sonoma) or later on Apple Silicon, Xcode
  Command Line Tools current with the SDK version pinned in
  `cmake/platforms/OSXArm64.cmake`

If your host configuration drifts from this baseline, two clean builds may
still produce different artifacts. We attest the SHA-256 against the
reference baseline, not against arbitrary build hosts.

## Producing a release

From a clean checkout at the release commit:

```bash
# Linux x86_64 (native)
scripts/build-linux-release.sh v3.0.0

# Linux x86_64 (via Docker) — produces the same tarball plus a portable image
scripts/build-docker-release.sh v3.0.0

# Windows x64 (cross-compiled from Linux)
scripts/build-windows-release.sh v3.0.0

# macOS arm64 (must be run on Apple Silicon hardware)
scripts/build-macos-release.sh v3.0.0
# or with codesigning + notarization:
SIGN_IDENTITY='Developer ID Application: <Org> (<TEAMID>)' \
  APPLE_ID=… APPLE_TEAM_ID=… APPLE_NOTARY_PASSWORD=… \
  scripts/build-macos-release.sh v3.0.0
```

All artifacts land in `releases/<version>/`:

```
releases/v3.0.0/
├── radiant-core-linux-x64-v3.0.0.tar.gz
├── radiant-core-linux-x64-v3.0.0.tar.gz.sha256
├── radiant-core-docker-amd64-v3.0.0.tar
├── radiant-core-docker-amd64-v3.0.0.tar.sha256
├── radiant-core-windows-x64-v3.0.0.zip
├── radiant-core-windows-x64-v3.0.0.zip.sha256
├── radiant-core-macos-arm64-v3.0.0-unsigned.tar.gz       ← reproducible
├── radiant-core-macos-arm64-v3.0.0-unsigned.tar.gz.sha256
├── radiant-core-macos-arm64-v3.0.0.tar.gz                ← signed (not reproducible)
└── radiant-core-macos-arm64-v3.0.0.tar.gz.sha256
```

Once the four platform artifacts are present, run the GUI-hash helper to
patch the auto-downloader manifest and tidy the release notes:

```bash
cd releases/v3.0.0
sha256sum *.tar.gz *.zip > SHA256SUMS.txt
../../scripts/update-gui-hashes.sh SHA256SUMS.txt
gpg --armor --detach-sign SHA256SUMS.txt    # per RELEASE_SECURITY_PROCESS.md §2
```

## Independent verification

To independently verify a published release artifact:

```bash
# 1. Clone at the release tag
git clone https://github.com/Radiant-Core/Radiant-Core.git
cd Radiant-Core
git checkout v3.0.0

# 2. Re-run the same build script
scripts/build-linux-release.sh v3.0.0

# 3. Compare your artifact's SHA-256 against the published value
sha256sum releases/v3.0.0/radiant-core-linux-x64-v3.0.0.tar.gz
# Should match the value published at:
#   https://github.com/Radiant-Core/Radiant-Core/releases/tag/v3.0.0
# and in gui/radiant_node_web.py's RELEASE_ASSETS for the same filename.
```

If the hashes don't match, your build environment has drifted from the
reference baseline (see "Build environment baseline" above) — that's the
first place to look. If you're certain the environment matches, file an
issue: that's the kind of finding the release process is designed to
surface.

## macOS reproducibility ceiling

The macOS script produces two artifacts deliberately:

- `radiant-core-macos-arm64-<version>-unsigned.tar.gz` — the reproducible
  one. Attest this hash. Two clean builds from the same SHA produce the
  same byte stream.

- `radiant-core-macos-arm64-<version>.tar.gz` — codesigned and (if env
  vars are set) notarized. **Not byte-reproducible** — Apple's signature
  format embeds a fresh per-signature CMS structure on every `codesign`
  run, and notarization rewrites bytes again. Verify this artifact with
  `codesign --verify --deep --strict` and the Apple notary log instead of
  by SHA-256 equality.

This split lets us claim reproducibility for the build step while still
shipping a Gatekeeper-friendly binary for end users. The end user runs
the signed/notarized one; a maintainer auditing supply-chain integrity
rebuilds and compares against the unsigned one.

## macOS SDK extraction (one-time)

Apple's SDK redistribution terms prevent `depends/` from auto-downloading
it. To set up the SDK once on a build host:

```bash
# 1. Install Xcode (full IDE, not just CLI tools) from the App Store.
# 2. Locate the SDK inside Xcode:
SDK_VERSION=14.0
xcrun --sdk macosx --show-sdk-path
# typical: /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk

# 3. Stage it where depends/ expects it:
mkdir -p depends/SDKs
cp -R "$(xcrun --sdk macosx --show-sdk-path)" depends/SDKs/MacOSX${SDK_VERSION}.sdk
```

The SDK version is pinned in `cmake/platforms/OSXArm64.cmake`
(`OSX_SDK_PATH`). Rotating to a newer SDK is a deliberate change — update
the cmake file, document the version in this file's "Build environment
baseline" table, and rebuild from a clean tree to confirm the new SDK
doesn't break determinism.

## What's still on the roadmap

- **Cross-host reproducibility** (build the same artifact from any modern
  Linux distro, not just the reference baseline). Bitcoin Core uses
  Guix-based builds to achieve this; that's the eventual target.
- **macOS Intel x86_64** — currently the release only ships ARM64. Adding
  Intel would require a second `cmake/platforms/OSXIntel.cmake` and a
  parallel build path.
- **CI verification** — wire `make -C depends && cmake && build && diff`
  into a GitHub Actions workflow so every release commit gets a
  byte-equality check before merge.

These are tracked as follow-ups to v3.0.0, not blockers for it.
