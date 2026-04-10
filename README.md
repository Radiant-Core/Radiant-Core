Radiant Core Node
=================

The goal of Radiant Core Node is to create sound money and a digital value transfer
system that is usable by everyone in the world. This is civilization-changing 
technology which will dramatically increase human flourishing, freedom, and 
prosperity. The project aims to achieve this goal by focusing on high performance
scalability and an expressive programming language to realize any type of digital
value and money transfer imaginable.

What is Radiant?
---------------------

Radiant is a high performance blockchain for digital assets and enables instant 
payments and asset transfers to anyone, anywhere in the world. It uses 
peer-to-peer technology to operate with no central authority: managing 
transactions are carried out collectively by the network. Radiant is a L1 
network based on the original Bitcoin design. RXD is the native token of Radiant.

## 🖥️ Radiant Core GUI

**For most users**, the easiest way to run a Radiant node is the **Radiant Core GUI** - a simple desktop application for self-custody and helping distribute the network.

### Features
- **One-click node**: Start and stop your node with a single click
- **Built-in wallet**: Send and receive RXD with self-custody
- **Seed phrase backup**: BIP39 mnemonic support for wallet recovery
- **No technical knowledge required**: Just download, install, and run

### macOS Installation
1. Download the DMG file
2. Open and drag **Radiant Core** to Applications
3. If blocked by Gatekeeper: `xattr -rd com.apple.quarantine /Applications/Radiant\ Core.app`
4. Double-click to launch

---

## Radiant Core 2.3.0

**Activation Height:** Block 410,000 (mainnet & testnet3)

Radiant Core 2.1 introduces 6 new and re-enabled opcodes, gated behind the `SCRIPT_ENHANCED_REFERENCES` flag and activated at the V2 hard fork height:

| Opcode | Hex | Type | Purpose |
|--------|-----|------|---------|
| **OP_BLAKE3** | `0xee` | New hash opcode | On-chain Blake3 PoW validation for Glyph v2 dMint tokens |
| **OP_K12** | `0xef` | New hash opcode | On-chain KangarooTwelve PoW validation |
| **OP_LSHIFT** | `0x98` | Re-enabled | Bitwise left shift for on-chain ASERT-lite DAA computation |
| **OP_RSHIFT** | `0x99` | Re-enabled | Bitwise right shift for on-chain DAA computation |
| **OP_2MUL** | `0x8d` | Re-enabled | Multiply by 2 (numeric) |
| **OP_2DIV** | `0x8e` | Re-enabled | Divide by 2 with truncation toward zero |

**Why:** These opcodes enable fully on-chain proof-of-work validation for Glyph v2 decentralized minting (dMint) tokens using alternative hash algorithms (Blake3, KangarooTwelve). This eliminates indexer trust dependency and prevents griefing attacks. The shift and arithmetic opcodes enable on-chain difficulty adjustment (ASERT-lite DAA).

**Test Coverage:** 14/14 opcode functional tests + 29 integration tests across 7 scenarios (A-G) on 2-node regtest.

---

## Features

- **C++20 Codebase**: Modern C++ with `std::filesystem` and improved type safety
- **Prometheus Metrics**: Native `/metrics` endpoint for monitoring (block height, peers, mempool)
- **Glyph Swap Protocol (PSRT)**: On-chain atomic swaps via `-swapindex` flag
- **Node Profiles**: `-nodeprofile=archive|agent|mining` for easy configuration
- **Large Transaction Support**: Up to 12 MB transactions (~81,000 inputs)

## 🚀 Quick Start: Command-Line Builds

We provide comprehensive release build scripts for all platforms with automated dependency management and security verification.

### 📦 Pre-built Releases (Recommended)

Download official releases from [GitHub Releases](https://github.com/Radiant-Core/Radiant-Core/releases) with verified checksums:

**🔐 Security Verification:**

### 🛠️ Build from Source

Choose your platform below for automated build scripts:

#### **Windows (via WSL2)** - Recommended
```powershell
# Install WSL2 (run as Administrator)
wsl --install -d Ubuntu-22.04

# Then follow Linux build instructions inside WSL2
```
**Requirements:** Windows 10/11 with WSL2. See [build-windows-portable.md](doc/build-windows-portable.md) for details.

#### **Linux Build** (Ubuntu/Debian/CentOS/Fedora)
```bash
# Automated build with dependency installation
./scripts/build-linux-release.sh

# Multi-platform build (if on Linux)
./scripts/build-all-releases.sh
```
**Requirements:** Linux x86_64, GCC 10+ or Clang 11+, CMake 3.16+

#### **macOS Build** (Universal Binary)
```bash
# Universal Binary (Intel + Apple Silicon)
./scripts/build-macos-release.sh

# Create DMG installer
./create-dmg.sh
```
**Requirements:** macOS 10.15+, Xcode 12+, Homebrew

#### **Docker Build** (Any Platform)
```bash
# Build Docker image and extract binaries
./scripts/build-docker-release.sh

# Run directly from Docker
docker run -d --name radiant-node \
  -p 7332:7332 -p 7333:7333 \
  -v radiant-data:/home/radiant/.radiant \
  radiant-core:latest
```
**Requirements:** Docker Engine 20.10+

#### **All Platforms** (Multi-Platform Build)
```bash
# Interactive build for all platforms
./build-all-releases.sh

# Creates:
# - Linux tar.gz
# - Docker image
# - macOS universal binary + DMG
# - All with SHA256 checksums
```

### 📋 Build System Features

- ✅ **Automated dependency installation**
- ✅ **Cross-platform compatibility** 
- ✅ **Release optimization**
- ✅ **Security verification** (SHA256 checksums)
- ✅ **Universal binaries** (macOS Intel + Apple Silicon)
- ✅ **Docker multi-stage builds**
- ✅ **WSL2 support for Windows users**
- ✅ **Professional installers** (macOS DMG)

### 🐳 Docker Quick Start

**Recommended: Use Docker Compose** (builds from GitHub automatically)
```bash
# Start node
docker-compose up -d

# Check status
docker-compose exec radiant-node radiant-cli getblockchaininfo

# View logs
docker-compose logs -f radiant-node

# Stop node
docker-compose down
```

**Alternative: Standalone Docker** (builds from GitHub)
```bash
# Build from GitHub
docker build -f docker/Dockerfile.release -t radiant-core:latest .

# Run
docker run -d --name radiant-node \
  -p 7332:7332 -p 7333:7333 \
  -v radiant-data:/home/radiant/.radiant \
  radiant-core:latest

# Check status
docker exec radiant-node radiant-cli getblockchaininfo
```

**See [doc/docker-guide.md](doc/docker-guide.md) for complete Docker documentation.**

### ⚡ Quick Test (After Installation)

```bash
# Test daemon
radiantd --version

# Test RPC client  
radiant-cli --version

# Test transaction utility
radiant-tx --help

# Start daemon with default profile
radiantd -nodeprofile=archive
```

---

## 📚 Advanced Build Options

### Development & CI Builds

For development and continuous integration, we provide additional build methods:

#### **CI Build (Docker-based Testing)**
```bash
# Full CI build with testing
./contrib/run-ci-local.sh
```
This builds in a standardized Docker environment with:
- Ubuntu 24.04, CMake 3.28+, Boost 1.83, OpenSSL 3.0, C++20
- Full test suite (unit tests + functional tests)
- Cross-compilation for multiple platforms

#### **Native Build Options**

**Ubuntu/Debian:**
```bash
# Install dependencies
sudo apt-get install build-essential cmake ninja-build libboost-all-dev \
    libevent-dev libssl-dev libdb++-dev libminiupnpc-dev libzmq3-dev

# Build
mkdir build && cd build
cmake -GNinja .. -DBUILD_RADIANT_QT=OFF
ninja
```

**macOS:**
```bash
# Install dependencies via Homebrew
brew install cmake ninja boost libevent openssl berkeley-db miniupnpc zeromq qt5

# Build without Qt GUI
mkdir build && cd build
cmake -GNinja .. -DBUILD_RADIANT_QT=OFF
ninja

# Build with Qt GUI
export CMAKE_PREFIX_PATH="/opt/homebrew/opt/qt@5"
mkdir build && cd build
cmake -GNinja .. -DBUILD_RADIANT_QT=ON
ninja
```

**Windows (WSL2):**
```powershell
# Install WSL2 with Ubuntu
wsl --install -d Ubuntu-22.04

# Inside WSL2, follow Linux/Ubuntu build instructions above
```

Native Build: Windows
---------------------

### Option 1: RadiantCoreNode+Wallet (Recommended for End Users)

A standalone single-file GUI with built-in node management and wallet:

- **Download:** 
- **No DLLs or installation required** — just double-click to run
- Opens a browser-based interface at `http://127.0.0.1:8765`
- One-click node start/stop, built-in wallet, BIP39 seed phrase backup

### Option 2: RadiantCore Qt GUI (Classic Desktop Wallet)

The traditional Qt-based desktop wallet and node manager:

1. Download and extract 
2. Double-click `RadiantCore.exe`
3. All required DLLs (Qt5, ICU, MinGW runtime, etc.) are included in the zip

See [gui/README.md](gui/README.md) for detailed instructions.

### Option 3: WSL2 (Recommended for Developers)

For development and building from source, we recommend WSL2:

```powershell
# Install WSL2 (run as Administrator)
wsl --install -d Ubuntu-22.04

# Then follow Linux build instructions inside WSL2
```

See [build-windows-portable.md](doc/build-windows-portable.md) for WSL2 setup instructions.

Running Radiant Node
---------------------

### Node Profiles

Radiant supports three node profiles optimized for different use cases. Use `-nodeprofile` for easy configuration:

#### **Archive Node (Default)**
**Traditional full node configuration - recommended for most users**
- **Storage**: Full blockchain (no pruning)
- **Transaction Index**: Enabled (`txindex=1`)
- **Use Case**: General purpose, blockchain explorers, wallet services, historical queries
- **Disk Usage**: Full blockchain size (~25GB+ growing)
- **RPC Support**: Full transaction lookup via `getrawtransaction`

```bash
# Archive node (default behavior)
radiantd
# or explicitly:
radiantd -nodeprofile=archive
```

#### **Agent Node**
**Lightweight configuration for resource-constrained environments**
- **Storage**: Pruned to ~550MB minimum
- **Transaction Index**: Disabled (`txindex=0`)
- **Use Case**: Embedded systems, IoT devices, mobile applications
- **Disk Usage**: Minimal footprint (~550MB)
- **RPC Support**: Limited to recent transactions only
- **Security**: Full validation, UTXO-focused operation

```bash
# Agent node (minimal footprint)
radiantd -nodeprofile=agent
```

#### **Mining Node**
**Optimized configuration for mining operations**
- **Storage**: Pruned to ~4GB (keeps ~10,000 recent blocks)
- **Transaction Index**: Disabled (`txindex=0`)
- **Use Case**: Mining pools, solo mining, mining operations
- **Disk Usage**: Moderate (~4GB)
- **RPC Support**: Recent transactions only
- **Security**: Full validation, mining-optimized performance

```bash
# Mining node (balanced performance)
radiantd -nodeprofile=mining
```

#### **Configuration Override**

User-specified settings always take precedence over profile defaults:
```bash
# Archive profile with custom pruning
radiantd -nodeprofile=archive -prune=10000

# Mining profile with transaction index (not recommended)
radiantd -nodeprofile=mining -txindex=1
```

#### **Security Note**
All node profiles maintain full security guarantees. The transaction index (`txindex`) is only a convenience feature for historical transaction queries and does not affect validation, consensus, or double-spend protection.

### Systemd Service

Create `/etc/systemd/system/radiantd.service`:

```ini
[Unit]
Description=Radiant Node
After=network.target

[Service]
ExecStart=/usr/local/bin/radiantd -nodeprofile=archive -rest -server
User=radiant
Group=radiant
Restart=always
LimitNOFILE=400000
TimeoutStopSec=30min

[Install]
WantedBy=multi-user.target
```

### Sample radiant.conf

Place in `~/.radiant/radiant.conf`:

```ini
# RPC settings
rpcuser=youruser
rpcpassword=yourpassword
rpcallowip=127.0.0.1

# Fee policy (amounts are in RXD/kB)
# Defaults are tuned for RXD economics. Override only if you understand the tradeoffs.
# Relay/mempool minimum fee is height-gated:
# - < 410,000: 0.01 RXD/kB
# - 410,000 - 414,999 (grace period): 0.01 RXD/kB
# - >= 415,000: 0.1 RXD/kB
# Avoid setting minrelaytxfee=0.1 prior to 415,000 unless you intentionally want to relay fewer transactions.
minrelaytxfee=0.01
incrementalrelayfee=0.01
blockmintxfee=0.1
fallbackfee=0.1

# Indexing (required for explorers/indexers)
txindex=1

# Optional: Enable swap index for PSRT
swapindex=1

# Optional: Prometheus metrics
prometheusmetrics=1
```

### Docker / Container Setup (Persistence, RPC safety, logging)

If you run `radiantd` in Docker, make sure you persist the datadir and avoid exposing RPC publicly.

#### Persist `~/.radiant`

The default datadir is `~/.radiant` (inside a container this is usually `/root/.radiant`). Without a bind mount or Docker volume, you will lose chainstate/indexes when the container is removed.

```bash
docker volume create radiant-datadir
docker run --name radiant-mainnet \
  -p 7333:7333 \
  -p 127.0.0.1:7332:7332 \
  -v radiant-datadir:/root/.radiant \
  radiant-core-local \
  ./radiantd -nodeprofile=archive -server -rest
```

#### RPC hardening (recommended)

- **Do not use** `-rpcallowip=0.0.0.0/0` unless you fully understand the exposure and have network-layer controls.
- Bind RPC to localhost (or a private management network) and use strong authentication.

If you need RPC from outside the host, prefer placing it behind a VPN / reverse proxy with authentication and IP allowlisting.

Prefer `rpcauth` over plaintext `rpcpassword`. The repository includes tooling in `share/rpcauth/` to generate `rpcauth` entries.

#### Logging verbosity

Avoid running with `-debug=net` unless you are actively debugging P2P behavior. It produces very large `debug.log` files and adds disk I/O overhead.

#### Indexing and fee policy notes

- **`txindex=1`** is useful for explorers/indexers and increases disk usage. Disable it if you do not need arbitrary transaction lookups.
- If you see `Warning: -minrelaytxfee is set very high!` in logs, check your config/flags and remove or lower the override unless intentionally running a restrictive relay policy.
- Fee-related configuration values are expressed in **RXD/kB** (e.g. `minrelaytxfee=0.01`).
- `minrelaytxfee` controls transaction relay/mempool acceptance. It should generally be left at default so the node can apply the network's height-gated fee policy (including the grace period).
- `incrementalrelayfee` controls the minimum fee-rate increase used for mempool limiting / replacement behavior.

Development & CI
--------------------------

### Running Tests

```bash
# Unit tests
./build/src/test/test_bitcoin

# Functional tests
./build/test/functional/test_runner.py

# Specific test suite
./build/src/test/test_bitcoin --run_test=txvalidation_tests
```

### CI Pipeline

The GitLab CI pipeline includes:

- **Static Analysis**: Linting, code quality checks
- **Multi-compiler Builds**: GCC, Clang (Debug & Release)
- **Sanitizer Builds**: AddressSanitizer (ASan) + UndefinedBehaviorSanitizer (UBSan)
- **Cross-compilation**: ARM, AArch64
- **Fuzz Testing**: Radiant-specific opcode fuzzing (`fuzz-radiant_opcodes`)
- **Full Test Suite**: Unit tests, functional tests, benchmarks

About Radiant Node
--------------------------

[Radiant Core Node](https://radiantcore.org) is open-source software which 
enables the use of Radiant. It is a descendant of [Bitcoin Cash Node](https://bitcoincashnode.org), [Bitcoin Core](https://bitcoincore.org), [Bitcoin ABC](https://www.bitcoinabc.org), and [Radiant Node](https://radiantblockchain.org).

License
-------

Radiant Core Node is released under the terms of the MIT license. See
[COPYING](COPYING) for more information or see
[https://opensource.org/licenses/MIT](https://opensource.org/licenses/MIT).

This product includes software developed by the OpenSSL Project for use in the
[OpenSSL Toolkit](https://www.openssl.org/), cryptographic software written by
[Eric Young](mailto:eay@cryptsoft.com), and UPnP software written by Thomas
Bernard.

Development
-------------------

Radiant Core Node development takes place at [https://github.com/radiantblockchain/radiant-node](https://github.com/radiantblockchain/radiant-node)

See [roadmap.md](doc/roadmap.md) for planned features and [upgrades.md](doc/upgrades.md) for completed enhancements.

Disclosure Policy
-----------------

We have a [Disclosure Policy](doc/disclosure-policy.md) for responsible disclosure
of security issues.

Further Info
------------

See [doc/README.md](doc/README.md) for detailed documentation on installation, 
building, development, and RPC commands.

Radiant Core is a community-driven free software project, released under the MIT license.
