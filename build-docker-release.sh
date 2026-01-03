#!/bin/bash
# Radiant Core Docker Release Build Script

set -e

echo "========================================"
echo "Radiant Core Docker Release Build"
echo "========================================"

# Check if Docker is available
if ! command -v docker >/dev/null 2>&1; then
    echo "ERROR: Docker is required but not installed"
    exit 1
fi

# Create Dockerfile for release
cat > Dockerfile.release << 'EOF'
# Multi-stage Docker build for Radiant Core releases
FROM ubuntu:24.04 AS builder

LABEL maintainer="info@radiantfoundation.org"
LABEL version="2.0.0"
LABEL description="Radiant Core Node - Release Build"

# Install build dependencies
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    git \
    libboost-all-dev \
    libevent-dev \
    libssl-dev \
    libdb++-dev \
    libminiupnpc-dev \
    libzmq3-dev \
    curl \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && apt-get clean

# Set working directory
WORKDIR /root

# Clone and build Radiant Core
ARG GIT_TAG=main
ARG BUILD_TYPE=Release

RUN git clone --depth 1 --branch ${GIT_TAG} https://github.com/Radiant-Core/Radiant-Core.git

WORKDIR /root/Radiant-Core
RUN mkdir -p build
WORKDIR /root/Radiant-Core/build

RUN cmake -GNinja .. \
    -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
    -DBUILD_RADIANT_WALLET=OFF \
    -DBUILD_RADIANT_ZMQ=OFF \
    -DBUILD_RADIANT_QT=OFF \
    -DENABLE_UPNP=OFF

RUN ninja

# Runtime image
FROM ubuntu:24.04 AS runtime

LABEL maintainer="info@radiantfoundation.org"
LABEL version="2.0.0"
LABEL description="Radiant Core Node - Runtime"

# Install runtime dependencies
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    libboost-chrono1.83.0 \
    libboost-filesystem1.83.0 \
    libboost-thread1.83.0 \
    libboost-atomic1.83.0 \
    libevent-2.1-7 \
    libssl3 \
    libdb5.3++ \
    libminiupnpc17 \
    libzmq5 \
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/* \
    && apt-get clean

# Create radiant user
RUN useradd -m -s /bin/bash radiant

# Copy binaries from builder
COPY --from=builder /root/Radiant-Core/build/src/radiantd /usr/local/bin/
COPY --from=builder /root/Radiant-Core/build/src/radiant-cli /usr/local/bin/
COPY --from=builder /root/Radiant-Core/build/src/radiant-tx /usr/local/bin/

# Create data directory
RUN mkdir -p /home/radiant/.radiant && \
    chown -R radiant:radiant /home/radiant/.radiant

USER radiant
WORKDIR /home/radiant

# Expose ports
EXPOSE 7332 7333

# Default command
ENTRYPOINT ["radiantd"]
CMD ["-server", "-rest", "-txindex=1"]

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=60s --retries=3 \
    CMD radiant-cli getblockchaininfo > /dev/null || exit 1
EOF

# Build arguments
GIT_TAG=${1:-main}
BUILD_TYPE=${2:-Release}
IMAGE_NAME="radiant-core:${GIT_TAG}-${BUILD_TYPE}"

echo "Building Docker image..."
echo "Git Tag: $GIT_TAG"
echo "Build Type: $BUILD_TYPE"
echo "Image Name: $IMAGE_NAME"

# Build the Docker image
docker build \
    --build-arg GIT_TAG=$GIT_TAG \
    --build-arg BUILD_TYPE=$BUILD_TYPE \
    -f Dockerfile.release \
    -t $IMAGE_NAME \
    .

# Create a container to extract binaries
echo "Extracting binaries..."
docker create --name radiant-temp $IMAGE_NAME
docker cp radiant-temp:/usr/local/bin/radiantd ./docker-release/
docker cp radiant-temp:/usr/local/bin/radiant-cli ./docker-release/
docker cp radiant-temp:/usr/local/bin/radiant-tx ./docker-release/
docker rm radiant-temp

# Create release package
echo "Creating Docker release package..."
mkdir -p docker-release
cp Dockerfile.release docker-release/

cat > docker-release/README.txt << EOF
Radiant Core Docker Release v${GIT_TAG}
=====================================

Docker Image: ${IMAGE_NAME}

Quick Start:
-----------
1. Pull the image:
   docker pull ${IMAGE_NAME}

2. Run the daemon:
   docker run -d --name radiant-node \
     -p 7332:7332 -p 7333:7333 \
     -v radiant-data:/home/radiant/.radiant \
     ${IMAGE_NAME}

3. Check status:
   docker exec radiant-node radiant-cli getblockchaininfo

4. Stop the node:
   docker stop radiant-node

Configuration:
-------------
You can customize the daemon by adding command-line arguments:

docker run -d --name radiant-node \
  -p 7332:7332 -p 7333:7333 \
  -v radiant-data:/home/radiant/.radiant \
  ${IMAGE_NAME} \
  -rpcuser=youruser -rpcpassword=yourpass

Data Persistence:
-----------------
The container uses /home/radiant/.radiant for blockchain data.
Mount a volume to persist data across container restarts.

Build Information:
-----------------
- Base Image: ubuntu:24.04
- Git Tag: ${GIT_TAG}
- Build Type: ${BUILD_TYPE}
- Build Date: $(date)

For more information, visit: https://radiantblockchain.org
EOF

echo "========================================"
echo "Docker release build completed!"
echo "Location: docker-release/"
echo ""
echo "Docker image created: $IMAGE_NAME"
echo ""
echo "To push to registry:"
echo "docker tag $IMAGE_NAME your-registry/$IMAGE_NAME"
echo "docker push your-registry/$IMAGE_NAME"
echo "========================================"
