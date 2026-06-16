#!/usr/bin/env bash
# scripts/build-macos-release.sh — reproducible macOS ARM64 release build.
#
# Uses depends/ + cmake/platforms/OSXArm64.cmake on an Apple Silicon macOS
# host. The depends/ tree carries pinned-source builds for every C/C++
# dependency and an extracted Apple SDK, so two clean builds at the same
# commit on the same host produce a byte-identical *pre-codesign* artifact.
#
# IMPORTANT — reproducibility ceiling on macOS:
#
#   Apple codesigning is non-deterministic by design: every `codesign` run
#   embeds a fresh per-signature CMS structure (timestamp, signer-info,
#   resource-bundle hash salts). Notarization rewrites bytes again.
#
#   This script therefore produces TWO artifacts:
#
#     1. radiant-core-macos-arm64-<version>-unsigned.tar.gz
#        — the reproducible one. Two clean builds from the same SHA produce
#          the same sha256. Attest this hash.
#
#     2. radiant-core-macos-arm64-<version>.tar.gz
#        — codesigned + (optionally) notarized. NOT byte-reproducible.
#          Verify by `codesign --verify` and the Apple notary log, not by
#          sha256 equality.
#
# Requires (on the Apple Silicon macOS build host):
#   - Xcode Command Line Tools (clang, ld, strip, codesign)
#   - cmake, ninja, make (Homebrew: `brew install cmake ninja make`)
#   - python3 (system or Homebrew)
#   - the macOS SDK extracted into depends/SDKs/MacOSX14.0.sdk — see
#     doc/build-reproducibility.md for the extraction procedure
#
# Codesign + notarize step requires (only if SIGN_IDENTITY env var is set):
#   - A Developer ID Application certificate in the build host's keychain
#   - $APPLE_ID, $APPLE_TEAM_ID, $APPLE_NOTARY_PASSWORD env vars for notarytool
#
# Usage:
#   scripts/build-macos-release.sh [version]
#     version  Tag string (default: v3.1.2).

set -euo pipefail

VERSION="${1:-v3.1.2}"
HOST_TRIPLE="aarch64-apple-darwin"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
cd "$ROOT_DIR"

if [[ "$OSTYPE" != "darwin"* ]]; then
    echo "ERROR: this script must run on macOS." >&2
    exit 1
fi

if [[ "$(uname -m)" != "arm64" ]]; then
    echo "ERROR: this script targets Apple Silicon (arm64). Detected $(uname -m)." >&2
    exit 1
fi

# --- determinism baseline -----------------------------------------------------
export SOURCE_DATE_EPOCH="$(git log -1 --pretty=%ct HEAD)"
export LC_ALL=C
export TZ=UTC
umask 0022

echo "================================================================"
echo "Radiant Core macOS release build (Apple Silicon, depends/-based)"
echo "================================================================"
echo "Version           : ${VERSION}"
echo "Host triple       : ${HOST_TRIPLE}"
echo "SOURCE_DATE_EPOCH : ${SOURCE_DATE_EPOCH}"
echo "SIGN_IDENTITY     : ${SIGN_IDENTITY:-(unset — will produce unsigned only)}"

# --- host toolchain sanity ----------------------------------------------------
for cmd in cmake ninja make clang clang++ strip tar gzip sha256sum git python3 xcode-select; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "ERROR: '$cmd' is required on the build host." >&2
        exit 1
    fi
done

# Verify the macOS SDK is extracted. depends/ doesn't auto-download it because
# Apple's SDK redistribution terms require manual extraction.
if [ ! -d "${ROOT_DIR}/depends/SDKs/MacOSX14.0.sdk" ]; then
    cat <<EOF >&2
ERROR: macOS SDK not found at depends/SDKs/MacOSX14.0.sdk.

The depends/ system needs an extracted SDK to produce reproducible builds.
See doc/build-reproducibility.md → "macOS SDK extraction" for the one-time
setup procedure (requires Xcode installed locally; depends/ does NOT
download the SDK because of Apple's redistribution restrictions).
EOF
    exit 1
fi

# --- step 1: depends/ ---------------------------------------------------------
echo
echo ">>> Building depends/ prefix for ${HOST_TRIPLE} ..."
make -C depends HOST="${HOST_TRIPLE}" NO_QT=1 -j"$(sysctl -n hw.ncpu)"

DEPENDS_PREFIX="${ROOT_DIR}/depends/${HOST_TRIPLE}"
if [ ! -d "${DEPENDS_PREFIX}" ]; then
    echo "ERROR: depends/ build did not produce ${DEPENDS_PREFIX}" >&2
    exit 1
fi

# --- step 2: cmake configure + build ------------------------------------------
BUILD_DIR="${ROOT_DIR}/build-${HOST_TRIPLE}-release"
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"

echo
echo ">>> Configuring cmake (OSXArm64 toolchain, depends prefix) ..."
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="${ROOT_DIR}/cmake/platforms/OSXArm64.cmake" \
    -DCMAKE_TOOLCHAIN_PREFIX="${HOST_TRIPLE}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_RADIANT_DAEMON=ON \
    -DBUILD_RADIANT_CLI=ON \
    -DBUILD_RADIANT_TX=ON \
    -DBUILD_RADIANT_WALLET=ON \
    -DBUILD_RADIANT_QT=OFF \
    -DBUILD_RADIANT_ZMQ=ON \
    -DENABLE_HARDENING=ON \
    -DENABLE_UPNP=OFF

echo
echo ">>> Building radiantd / radiant-cli / radiant-tx ..."
cmake --build "${BUILD_DIR}" --target radiantd radiant-cli radiant-tx -j"$(sysctl -n hw.ncpu)"

# --- step 3: stage ------------------------------------------------------------
STAGE_DIR="${ROOT_DIR}/build-${HOST_TRIPLE}-stage/radiant-core-macos-arm64"
rm -rf "${STAGE_DIR}"
mkdir -p "${STAGE_DIR}"

cp "${BUILD_DIR}/src/radiantd"    "${STAGE_DIR}/"
cp "${BUILD_DIR}/src/radiant-cli" "${STAGE_DIR}/"
cp "${BUILD_DIR}/src/radiant-tx"  "${STAGE_DIR}/"

# Strip with deterministic flags. macOS `strip` doesn't accept --strip-all
# or --remove-section; use -S -x for the equivalent (strip debug + non-global
# symbols).
for bin in "${STAGE_DIR}"/*; do
    "${ROOT_DIR}/depends/${HOST_TRIPLE}/native/bin/${HOST_TRIPLE}-strip" -S -x "${bin}" \
        2>/dev/null \
        || strip -S -x "${bin}"
done

GIT_SHA="$(git rev-parse --short HEAD)"
cat > "${STAGE_DIR}/README.txt" <<EOF
Radiant Core ${VERSION} — macOS ARM64 (Apple Silicon)
==========================================================

Files:
  radiantd      — node daemon (wallet RPC enabled)
  radiant-cli   — RPC client
  radiant-tx    — transaction tool

System requirements:
  macOS 11 (Big Sur) or later, Apple Silicon (M1+) only.

Quick start:
  # If macOS Gatekeeper blocks these binaries, right-click each in Finder and
  # choose "Open" the first time (this keeps signature/notarization checks
  # engaged). Do NOT run "xattr -rd com.apple.quarantine" — that disables
  # Gatekeeper's verification for these files, a security downgrade.
  ./radiantd -server -txindex=1
  ./radiant-cli getblockchaininfo

Build provenance:
  Built reproducibly from depends/ at commit ${GIT_SHA}.

  This artifact's pre-codesign sibling tarball (radiant-core-macos-arm64-
  ${VERSION}-unsigned.tar.gz) is byte-reproducible from the same commit.
  The codesigned + notarized binary is NOT byte-reproducible — Apple's
  signature format is intentionally non-deterministic. Verify via
  \`codesign --verify --deep --strict --verbose=2 radiantd\` and the
  Apple notary log, not by sha256 equality.

  See doc/build-reproducibility.md for details.

For more information: https://radiantblockchain.org
EOF

# --- step 4: unsigned tarball (reproducible!) ---------------------------------
RELEASE_DIR="${ROOT_DIR}/releases/${VERSION}"
mkdir -p "${RELEASE_DIR}"
UNSIGNED_ARTIFACT="${RELEASE_DIR}/radiant-core-macos-arm64-${VERSION}-unsigned.tar.gz"

echo
echo ">>> Packing unsigned tarball (reproducible) ..."
# BSD tar on macOS supports the same determinism flags as GNU tar 1.28+.
tar --uid 0 --gid 0 --numeric-owner \
    -C "$(dirname "${STAGE_DIR}")" \
    -cf - "$(basename "${STAGE_DIR}")" \
    | gzip -9n > "${UNSIGNED_ARTIFACT}"

( cd "${RELEASE_DIR}" && shasum -a 256 "$(basename "${UNSIGNED_ARTIFACT}")" \
    > "$(basename "${UNSIGNED_ARTIFACT}").sha256" )

echo
echo "Reproducible (unsigned) artifact:"
echo "  ${UNSIGNED_ARTIFACT}"
echo "  $(cat "${UNSIGNED_ARTIFACT}.sha256")"

# --- step 5: codesign + notarize (NOT reproducible) ---------------------------
if [ -z "${SIGN_IDENTITY:-}" ]; then
    echo
    echo "SIGN_IDENTITY env var not set — skipping codesign / notarize step."
    echo "To produce a Gatekeeper-friendly artifact, re-run with e.g.:"
    echo "  SIGN_IDENTITY='Developer ID Application: Acme (TEAMID)' \\"
    echo "  APPLE_ID=... APPLE_TEAM_ID=... APPLE_NOTARY_PASSWORD=... \\"
    echo "  scripts/build-macos-release.sh ${VERSION}"
    echo
    echo "================================================================"
    echo "macOS release build complete (unsigned only)."
    echo "================================================================"
    exit 0
fi

SIGNED_STAGE_DIR="${ROOT_DIR}/build-${HOST_TRIPLE}-stage/radiant-core-macos-arm64-signed"
rm -rf "${SIGNED_STAGE_DIR}"
cp -R "${STAGE_DIR}" "${SIGNED_STAGE_DIR}"

echo
echo ">>> Codesigning with ${SIGN_IDENTITY} ..."
for bin in "${SIGNED_STAGE_DIR}/radiantd" "${SIGNED_STAGE_DIR}/radiant-cli" "${SIGNED_STAGE_DIR}/radiant-tx"; do
    codesign --force --options runtime --timestamp \
        --sign "${SIGN_IDENTITY}" "${bin}"
    codesign --verify --deep --strict --verbose=2 "${bin}"
done

SIGNED_ARTIFACT="${RELEASE_DIR}/radiant-core-macos-arm64-${VERSION}.tar.gz"
tar --uid 0 --gid 0 --numeric-owner \
    -C "$(dirname "${SIGNED_STAGE_DIR}")" \
    -cf - "$(basename "${SIGNED_STAGE_DIR}")" \
    | gzip -9n > "${SIGNED_ARTIFACT}"

( cd "${RELEASE_DIR}" && shasum -a 256 "$(basename "${SIGNED_ARTIFACT}")" \
    > "$(basename "${SIGNED_ARTIFACT}").sha256" )

# Notarization is optional; gated on the env vars being set.
if [ -n "${APPLE_ID:-}" ] && [ -n "${APPLE_TEAM_ID:-}" ] && [ -n "${APPLE_NOTARY_PASSWORD:-}" ]; then
    echo
    echo ">>> Submitting to Apple notary ..."
    xcrun notarytool submit "${SIGNED_ARTIFACT}" \
        --apple-id "${APPLE_ID}" \
        --team-id "${APPLE_TEAM_ID}" \
        --password "${APPLE_NOTARY_PASSWORD}" \
        --wait
fi

echo
echo "================================================================"
echo "macOS release build complete."
echo "Unsigned (reproducible) : ${UNSIGNED_ARTIFACT}"
echo "Signed (non-reproducible): ${SIGNED_ARTIFACT}"
echo "================================================================"
