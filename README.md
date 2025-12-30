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

## Features

- **C++20 Codebase**: Modern C++ with `std::filesystem` and improved type safety
- **Prometheus Metrics**: Native `/metrics` endpoint for monitoring (block height, peers, mempool)
- **Glyph Swap Protocol (PSRT)**: On-chain atomic swaps via `-swapindex` flag
- **Node Profiles**: `-nodeprofile=archive|agent|mining` for easy configuration
- **Large Transaction Support**: Up to 12 MB transactions (~81,000 inputs)

Quick Start: Docker Build (Recommended)
---------------------

We provide a standardized build environment using Docker to ensure consistency across all platforms. This is the recommended way to build Radiant Node for any host OS.

1. **Install Docker**: Ensure you have Docker installed and running on your machine.

2. **Run the CI Build Script**:
   ```bash
   ./contrib/run-ci-local.sh
   ```
   This script will:
   - Build the `radiant-node-ci` Docker image (Ubuntu 24.04, CMake 3.28+, Boost 1.83, OpenSSL 3.0, C++20)
   - Compile the Radiant Node software inside the container
   - Run the full test suite (unit tests + functional tests)

The build runs entirely inside Docker, so you can run the CI script on **any host OS** (Linux, macOS, Windows) as long as Docker/Orbstackis installed.

### Where You Can Run the CI

| Host OS | Run CI via Docker | Notes |
|---------|-------------------|-------|
| Linux | ✅ | Native Docker |
| macOS | ✅ | Docker Desktop or OrbStack |
| Windows | ✅ | Docker Desktop (WSL2) |

### Binaries Produced by CI

The CI builds and tests Linux binaries natively, and cross-compiles for other platforms:

| Target Binary | Build | Tests | Notes |
|---------------|-------|-------|-------|
| Linux x86_64 | ✅ | ✅ | Native build inside container, full test suite |
| Linux AArch64 | ✅ | ✅ | Cross-compiled, tested via QEMU |
| Linux ARM | ✅ | ❌ | Cross-compiled, build only |
| Windows x64 | ✅ | ❌ | Cross-compiled via MinGW |
| macOS | ❌ | ❌ | Not cross-compiled; use native build below |

**Note**: The CI produces working binaries for Linux and Windows. macOS binaries must be built natively on a Mac (see below).

Native Build: Ubuntu/Debian
---------------------

See [Ubuntu/Debian Builds](doc/build-unix.md) for detailed instructions.

**Requirements**: Ubuntu 20.04/22.04/24.04, CMake 3.16+, Ninja, GCC 10+ or Clang 11+

```bash
# Install dependencies
sudo apt-get install build-essential cmake ninja-build libboost-all-dev \
    libevent-dev libssl-dev libdb++-dev libminiupnpc-dev libzmq3-dev

# Build without Qt GUI
mkdir build && cd build
cmake -GNinja .. -DBUILD_RADIANT_QT=OFF
ninja

# Build with Qt GUI
mkdir build && cd build
cmake -GNinja .. -DBUILD_RADIANT_QT=ON
ninja

# Optional: Install system-wide
sudo ninja install
```

Native Build: macOS
---------------------

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

Native Build: Windows
---------------------

See [Windows Build](doc/build-windows.md) for MSYS2/MinGW instructions, or use the Docker cross-compilation method above.

Running Radiant Node
---------------------

### Node Profiles

Use `-nodeprofile` for quick configuration:

```bash
# Archive node (default): Full history, txindex enabled
radiantd -nodeprofile=archive

# Agent node: Pruned (~550MB), minimal footprint
radiantd -nodeprofile=agent

# Mining node: Balanced (~4GB), optimized for mining
radiantd -nodeprofile=mining
```

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
minrelaytxfee=0.1
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

### Running Tests

```bash
# Unit tests
./build/src/test/test_bitcoin

# Qt GUI tests
./build/src/qt/test/test_bitcoin-qt

# Functional tests
./build/test/functional/test_runner.py

# Specific test suite
./build/src/test/test_bitcoin --run_test=txvalidation_tests
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
- Fee-related configuration values are expressed in **RXD/kB** (e.g. `minrelaytxfee=0.1`).
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
- **Cross-compilation**: Windows (MinGW), ARM, AArch64
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

See [ROADMAP.md](ROADMAP.md) for planned features and [UPGRADES.md](UPGRADES.md) for completed enhancements.

Disclosure Policy
-----------------

We have a [Disclosure Policy](DISCLOSURE_POLICY.md) for responsible disclosure
of security issues.

Further Info
------------

See [doc/README.md](doc/README.md) for detailed documentation on installation, 
building, development, and RPC commands.

Radiant Core is a community-driven free software project, released under the MIT license.