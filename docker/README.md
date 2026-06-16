# Radiant Core Docker Files

This directory contains Docker configurations for various Radiant Core deployment scenarios.

## Quick Start

**Recommended: Use Docker Compose**
```bash
cd ..
docker-compose up -d
```

## Production Dockerfiles (Build from GitHub)

All production Dockerfiles clone from the GitHub repository for reproducible builds.

### `Dockerfile.release` ⭐ **Recommended**
Multi-stage production build optimized for deployment.

```bash
docker build -f docker/Dockerfile.release -t radiant-core:latest .
```

**Features:**
- Clones from GitHub (configurable via `GIT_TAG` build arg)
- Multi-stage build (minimal runtime image ~200MB)
- Non-root user execution
- Health checks included
- Production-ready defaults

**Build Arguments:**
- `GIT_TAG`: Branch/tag to build (default: `main`)
- `BUILD_TYPE`: CMake build type (default: `Release`)

**Example:**
```bash
# Build specific version
docker build -f docker/Dockerfile.release --build-arg GIT_TAG=v3.1.2 -t radiant-core:v3.1.2 .

# Run
docker run -d --name radiant-node \
  -p 7332:7332 -p 7333:7333 \
  -v radiant-data:/home/radiant/.radiant \
  radiant-core:v3.1.2
```

### `Dockerfile.seeder`
DNS seeder for network discovery.

```bash
docker build -f docker/Dockerfile.seeder -t radiant-seeder .
docker run -d -p 5353:53/udp radiant-seeder -host=dnsseed.example.com
```

**Features:**
- Clones from GitHub
- Builds only the seeder component
- Minimal footprint

---

## Development Dockerfiles (Build from Local)

⚠️ **For development/testing only - not for production use**

### `Dockerfile.testnet`
Testnet deployment using pre-built local binaries.

**Requires:** Build locally first
```bash
cd ..
cmake -B build && cmake --build build
docker build -f docker/Dockerfile.testnet -t radiant-testnet .
```

**Features:**
- Uses pre-built binaries from `build/src/`
- Testnet configuration included
- Ports: 27332 (RPC), 27333 (P2P)

---

## Comparison Table

| Dockerfile | Source | Use Case | Image Size |
|------------|--------|----------|------------|
| `Dockerfile.release` | ✅ GitHub | **Production (recommended)** | ~200MB |
| `Dockerfile.seeder` | ✅ GitHub | DNS seeder | ~150MB |
| `Dockerfile.testnet` | ⚠️ Local binaries | Development testnet | ~100MB |

---

## Environment Variables

### Production (`Dockerfile.release`)
- `RPC_USER`: RPC username (set at runtime)
- `RPC_PASSWORD`: RPC password (set at runtime)

### Build Arguments
- `GIT_TAG`: Git branch/tag to clone (default: `main`)
- `BUILD_TYPE`: CMake build type (default: `Release`)

---

## Ports

### Mainnet
- `7332`: P2P network port
- `7333`: RPC port
- `29000`: ZMQ notifications (if enabled)

### Testnet
- `27332`: P2P network port
- `27333`: RPC port

### Seeder
- `53/udp`: DNS port

---

## Volumes

All Dockerfiles use `/home/radiant/.radiant` as the data directory.

**Mount a volume for data persistence:**
```bash
docker run -v radiant-data:/home/radiant/.radiant radiant-core:latest
```

---

## Security

All production images:
- ✅ Run as non-root `radiant` user
- ✅ Use multi-stage builds for minimal attack surface
- ✅ Include health checks
- ✅ Build from verifiable GitHub source

---

## Further Documentation

See [doc/docker-guide.md](../doc/docker-guide.md) for complete documentation including:
- Docker Compose usage
- Advanced configuration
- Troubleshooting
- Migration guides
