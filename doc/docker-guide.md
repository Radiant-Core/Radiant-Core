# Docker Build Guide

This document explains the different Dockerfiles available for Radiant Core and their intended use cases.

## Quick Start with Docker Compose (Recommended)

The easiest way to run Radiant Core via Docker:

```bash
# Start the node using docker-compose
docker-compose up -d

# Check status
docker-compose exec radiant-node radiant-cli getblockchaininfo

# View logs
docker-compose logs -f radiant-node

# Stop the node
docker-compose down
```

## Dockerfiles Overview

All production Dockerfiles now build from the **GitHub repository** for reproducible, verifiable builds.

### Production Dockerfiles (Build from GitHub)

#### `docker/Dockerfile.release` (⭐ Recommended for Production)
**Multi-stage build optimized for production deployments - Builds from GitHub**

- **Source**: Clones from https://github.com/Radiant-Core/Radiant-Core.git
- **Base**: Ubuntu 24.04 (multi-stage: builder + runtime)
- **Size**: Optimized minimal runtime image (~200MB)
- **Features**: 
  - Multi-stage build (smaller final image)
  - Non-root user execution (security)
  - Health checks included
  - Configurable via build args (GIT_TAG, BUILD_TYPE)
  - Production-ready configuration
- **Use Case**: Production deployments, Docker Hub releases
- **Build Args**:
  - `GIT_TAG`: Git branch/tag to build (default: `main`)
  - `BUILD_TYPE`: CMake build type (default: `Release`)
- **Build**: 
  ```bash
  docker build -f docker/Dockerfile.release -t radiant-core:latest .
  # Or with specific version:
  docker build -f docker/Dockerfile.release --build-arg GIT_TAG=v2.3.0 -t radiant-core:v2.3.0 .
  ```

#### `docker/Dockerfile.seeder` (DNS Seeder)
**DNS seed node for network discovery - Builds from GitHub**

- **Source**: Clones from GitHub repository
- **Base**: Ubuntu 24.04
- **Features**:
  - Builds only the seeder component
  - Exposes DNS port (53/udp)
  - Minimal footprint
  - Configurable via build args (GIT_TAG)
- **Use Case**: Network infrastructure, DNS seeding
- **Build**: 
  ```bash
  docker build -f docker/Dockerfile.seeder -t radiant-seeder .
  ```

#### `Dockerfile.linux-release` (Linux Build Container)
**Linux x64 release build container - Builds from GitHub**

- **Source**: Clones from GitHub repository
- **Base**: Ubuntu 22.04
- **Features**:
  - Builds with wallet support
  - Creates release tarball
  - Configurable via build args
- **Use Case**: Creating Linux release packages
- **Build**: 
  ```bash
  docker build -f Dockerfile.linux-release -t radiant-linux-builder .
  ```

#### `releases/Dockerfile` (Full-Featured Release)
**Multi-stage production build with wallet and ZMQ - Builds from GitHub**

- **Source**: Clones from GitHub repository
- **Base**: Ubuntu 22.04 (multi-stage)
- **Features**:
  - Wallet support enabled
  - ZMQ support enabled
  - Multi-stage for minimal runtime
  - Configurable build options
- **Use Case**: Production deployments requiring wallet functionality
- **Build**: 
  ```bash
  docker build -f releases/Dockerfile -t radiant-core-wallet .
  ```

### Development Dockerfiles (Build from Local Source)

⚠️ **These Dockerfiles are for development/testing only and build from local source code.**

#### `Dockerfile.test` (Local Testing)
**Development/testing build from local source**

- **Source**: ⚠️ Copies local source tree (COPY . .)
- **Base**: Ubuntu 22.04
- **Features**:
  - Quick local builds for testing
  - No wallet or QT
  - Suitable for uncommitted changes
- **Use Case**: **Development and testing only**
- **Build**: `docker build -f Dockerfile.test -t radiant-test .`

#### `docker/Dockerfile.testnet` (Local Testnet)
**Testnet deployment from pre-built local binaries**

- **Source**: ⚠️ Expects pre-built binaries in `build/src/`
- **Base**: Ubuntu 24.04
- **Features**:
  - Testnet configuration pre-loaded
  - Testnet ports exposed (27332, 27333)
  - Runtime-only (requires local build first)
- **Use Case**: **Development/testing testnet deployments**
- **Requires**: Build locally first: `cmake -B build && cmake --build build`
- **Build**: `docker build -f docker/Dockerfile.testnet -t radiant-testnet .`

## Usage Examples

### Docker Compose (Recommended)

The easiest way to deploy Radiant Core:

```bash
# Start the node (builds from GitHub automatically)
docker-compose up -d

# Check blockchain status
docker-compose exec radiant-node radiant-cli getblockchaininfo

# View logs
docker-compose logs -f radiant-node

# Stop the node
docker-compose down

# Stop and remove volumes
docker-compose down -v
```

**Optional: Run with Seeder**
```bash
# Start both node and seeder
docker-compose --profile seeder up -d

# Check seeder status
docker-compose logs radiant-seeder
```

**Environment Configuration**

Create a `.env` file for custom settings:
```bash
RPC_USER=myuser
RPC_PASSWORD=mypassword
GIT_TAG=v2.3.0
```

### Production Deployment (Standalone)

```bash
# Build from GitHub (specific version)
docker build -f docker/Dockerfile.release \
  --build-arg GIT_TAG=v2.3.0 \
  -t radiant-core:v2.3.0 .

# Run with persistent data
docker run -d --name radiant-node \
  -p 7332:7332 -p 7333:7333 \
  -v radiant-data:/home/radiant/.radiant \
  -e RPC_USER=myuser \
  -e RPC_PASSWORD=mypassword \
  radiant-core:v2.3.0

# Check status
docker exec radiant-node radiant-cli getblockchaininfo
```

### Development with Local Changes

```bash
# Test local changes (builds from current directory)
docker build -f Dockerfile.test -t radiant-test .
docker run --rm -it -p 7332:7332 -p 7333:7333 radiant-test

# For testnet with local binaries
# 1. Build locally first
cmake -B build && cmake --build build

# 2. Build Docker image
docker build -f docker/Dockerfile.testnet -t radiant-testnet .

# 3. Run testnet node
docker run -d --name radiant-testnet \
  -p 27332:27332 -p 27333:27333 \
  -v testnet-data:/home/radiant/.radiant \
  radiant-testnet
```

### DNS Seeder Deployment

```bash
# Build seeder from GitHub
docker build -f docker/Dockerfile.seeder -t radiant-seeder .

# Run seeder
docker run -d --name radiant-seeder \
  -p 5353:53/udp \
  radiant-seeder \
  -host=dnsseed.example.com \
  -ns=vps.example.com \
  -mbox=admin@example.com
```

## Configuration

### Environment Variables
- `DEBIAN_FRONTEND=noninteractive`: Prevents interactive prompts during build
- `CCACHE_DIR`: CCache directory for faster builds (CI Dockerfile)
- `GIT_TAG`: Git tag to build (Release Dockerfile)
- `BUILD_TYPE`: CMake build type (Release Dockerfile)

### Ports
- **Mainnet**: 7332 (RPC), 7333 (P2P)
- **Testnet**: 27332 (RPC), 27333 (P2P)
- **Seeder**: 53/udp (DNS)

### Volumes
- `/home/radiant/.radiant`: Default data directory
- `/ccache`: Build cache (CI Dockerfile)

## Security Considerations

1. **Non-root User**: All production Dockerfiles run as non-root `radiant` user
2. **Minimal Runtime**: `docker/Dockerfile.release` uses multi-stage build for minimal attack surface
3. **RPC Security**: Avoid exposing RPC ports publicly without authentication
4. **Health Checks**: Production images include health checks for monitoring

## Docker Build Sources Summary

### Production Builds (GitHub Source) ✅
All production Dockerfiles now clone from the GitHub repository for **reproducible, verifiable builds**:

| Dockerfile | Source | Use Case |
|------------|--------|----------|
| `docker/Dockerfile.release` | ✅ GitHub | **Recommended for production** |
| `docker/Dockerfile.seeder` | ✅ GitHub | DNS seeder deployments |
| `Dockerfile.linux-release` | ✅ GitHub | Linux release packages |
| `releases/Dockerfile` | ✅ GitHub | Production with wallet support |

### Development Builds (Local Source) ⚠️
These are for development/testing only:

| Dockerfile | Source | Use Case |
|------------|--------|----------|
| `Dockerfile.test` | ⚠️ Local (COPY) | Quick local testing |
| `docker/Dockerfile.testnet` | ⚠️ Local binaries | Testnet with pre-built binaries |

## Migration Guide

### From Local-Source Dockerfiles to GitHub-Based Builds

If you were previously using Dockerfiles that built from local source, migrate to GitHub-based builds:

```bash
# OLD WAY (local source - not recommended for production)
docker build -f Dockerfile.test -t radiant-core .

# NEW WAY (GitHub source - recommended for production)
docker build -f docker/Dockerfile.release -t radiant-core:latest .
# Or use docker-compose:
docker-compose up -d
```

**Benefits of GitHub-based builds:**
- ✅ **Reproducible**: Anyone can verify the build matches the source code
- ✅ **No local dependencies**: Don't need source code locally
- ✅ **Version pinning**: Use specific tags/commits via `GIT_TAG` build arg
- ✅ **Smaller images**: Multi-stage builds reduce final image size
- ✅ **Better security**: Non-root user, minimal runtime dependencies
- ✅ **CI/CD friendly**: Automated deployments without source checkout

## Troubleshooting

### Build Issues
- Ensure Docker has sufficient memory (4GB+ recommended)
- Use `docker/Dockerfile.core` for local development if network issues occur
- Check for sufficient disk space (multi-stage builds require more space)

### Runtime Issues
- Verify port mappings (mainnet vs testnet)
- Check volume permissions for data persistence
- Use `docker logs` to debug startup issues

### Performance Optimization
- Use `docker/Dockerfile.release` for production (smaller size, faster startup)
- Enable build cache with `CCACHE_DIR` volume in CI
- Consider resource limits for mining operations

## Windows Support

Windows users should use WSL2 or Docker Desktop with WSL2 backend. Native Windows builds are deprecated. See [build-windows-portable.md](build-windows-portable.md) for detailed WSL2 setup instructions.
