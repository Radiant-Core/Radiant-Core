#!/bin/bash
# Radiant Core Release Build Script
# Builds Docker images and creates release artifacts
#
# Usage:
#   ./contrib/release/build-release.sh [version]
#
# Examples:
#   ./contrib/release/build-release.sh 2.0.0
#   ./contrib/release/build-release.sh          # Uses version from configure.ac

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Get version from argument or extract from CMakeLists.txt
get_version() {
    if [ -n "$1" ]; then
        echo "$1"
    else
        # Extract version from CMakeLists.txt
        local major=$(grep "CLIENT_VERSION_MAJOR" "$PROJECT_ROOT/CMakeLists.txt" | head -1 | grep -o '[0-9]\+')
        local minor=$(grep "CLIENT_VERSION_MINOR" "$PROJECT_ROOT/CMakeLists.txt" | head -1 | grep -o '[0-9]\+')
        local revision=$(grep "CLIENT_VERSION_REVISION" "$PROJECT_ROOT/CMakeLists.txt" | head -1 | grep -o '[0-9]\+')
        echo "${major}.${minor}.${revision}"
    fi
}

VERSION=$(get_version "$1")
IMAGE_NAME="radiant-core"
REGISTRY="${DOCKER_REGISTRY:-}"  # Optional: ghcr.io/radiantblockchain or docker.io/radiantblockchain

log_info "Building Radiant Core v${VERSION}"
log_info "Project root: ${PROJECT_ROOT}"

# Verify we're in the right directory
if [ ! -f "$PROJECT_ROOT/CMakeLists.txt" ]; then
    log_error "CMakeLists.txt not found. Run this script from the radiant-core repository."
    exit 1
fi

# Check for uncommitted changes
if [ -n "$(git -C "$PROJECT_ROOT" status --porcelain)" ]; then
    log_warn "Working directory has uncommitted changes!"
    read -p "Continue anyway? [y/N] " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# Build the CI Docker image (contains build tools)
log_info "Building CI Docker image..."
docker build -f "$PROJECT_ROOT/Dockerfile.ci" -t radiant-node-ci "$PROJECT_ROOT"

# Build the release Docker image (contains radiantd binary)
log_info "Building release Docker image..."
docker build -f "$PROJECT_ROOT/Dockerfile.core" \
    -t "${IMAGE_NAME}:${VERSION}" \
    -t "${IMAGE_NAME}:latest" \
    "$PROJECT_ROOT"

log_info "Docker images built successfully:"
echo "  - ${IMAGE_NAME}:${VERSION}"
echo "  - ${IMAGE_NAME}:latest"

# Tag for registry if specified
if [ -n "$REGISTRY" ]; then
    log_info "Tagging for registry: ${REGISTRY}"
    docker tag "${IMAGE_NAME}:${VERSION}" "${REGISTRY}/${IMAGE_NAME}:${VERSION}"
    docker tag "${IMAGE_NAME}:latest" "${REGISTRY}/${IMAGE_NAME}:latest"
    echo "  - ${REGISTRY}/${IMAGE_NAME}:${VERSION}"
    echo "  - ${REGISTRY}/${IMAGE_NAME}:latest"
fi

# Create release directory
RELEASE_DIR="$PROJECT_ROOT/release-${VERSION}"
mkdir -p "$RELEASE_DIR"

# Extract binaries from Docker image
log_info "Extracting binaries from Docker image..."
CONTAINER_ID=$(docker create "${IMAGE_NAME}:${VERSION}")
docker cp "$CONTAINER_ID:/usr/local/bin/radiantd" "$RELEASE_DIR/" 2>/dev/null || true
docker cp "$CONTAINER_ID:/usr/local/bin/radiant-cli" "$RELEASE_DIR/" 2>/dev/null || true
docker cp "$CONTAINER_ID:/usr/local/bin/radiant-wallet" "$RELEASE_DIR/" 2>/dev/null || true
docker cp "$CONTAINER_ID:/usr/local/bin/radiant-tx" "$RELEASE_DIR/" 2>/dev/null || true
docker rm "$CONTAINER_ID" > /dev/null

# Create tarball
log_info "Creating release tarball..."
TARBALL="radiant-core-${VERSION}-linux-x86_64.tar.gz"
tar -czvf "$RELEASE_DIR/$TARBALL" -C "$RELEASE_DIR" \
    radiantd radiant-cli radiant-wallet radiant-tx 2>/dev/null || \
tar -czvf "$RELEASE_DIR/$TARBALL" -C "$RELEASE_DIR" radiantd radiant-cli 2>/dev/null

# Generate checksums
log_info "Generating checksums..."
cd "$RELEASE_DIR"
sha256sum *.tar.gz > SHA256SUMS 2>/dev/null || shasum -a 256 *.tar.gz > SHA256SUMS

# Sign checksums if GPG key is available
if gpg --list-secret-keys --keyid-format LONG 2>/dev/null | grep -q "sec"; then
    log_info "Signing checksums with GPG..."
    gpg --armor --detach-sign SHA256SUMS
    log_info "Signature created: SHA256SUMS.asc"
else
    log_warn "No GPG key found. Skipping signature."
fi

cd "$PROJECT_ROOT"

log_info "Release artifacts created in: ${RELEASE_DIR}"
ls -la "$RELEASE_DIR"

echo ""
log_info "=== Release Checklist ==="
echo "  [ ] Verify binaries work correctly"
echo "  [ ] Create signed git tag: git tag -s v${VERSION} -m 'Radiant Core ${VERSION}'"
echo "  [ ] Push tag: git push origin v${VERSION}"
if [ -n "$REGISTRY" ]; then
    echo "  [ ] Push Docker images: docker push ${REGISTRY}/${IMAGE_NAME}:${VERSION}"
fi
echo "  [ ] Upload release artifacts to GitHub/GitLab"
echo "  [ ] Update release notes"

log_info "Done!"
