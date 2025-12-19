# Radiant Node Upgrade Log

This document tracks the modernization and upgrade efforts for the Radiant Node (radiant-node) software.

## ✅ Completed Upgrades

### 1. Infrastructure & Environment
- **Base OS**: Upgraded Docker base image from **Ubuntu 20.04** to **Ubuntu 24.04 LTS**.
- **Runtime**: Upgraded Node.js from v12 to **Node.js 20 LTS**.
- **Build System**: Upgraded minimum CMake requirement from 3.13 to **3.22** to support modern build features.
- **CI/CD**: Implemented a standardized build environment using `Dockerfile.ci` and `contrib/run-ci-local.sh` to ensure reproducible builds across platforms.
- **CI/CD Pipeline Enhancements**:
  - Updated `.gitlab-ci.yml` with ASan/UBSan jobs using `radiant-node-ci` Docker image.
  - Added `build-ubuntu-asan` and `test-ubuntu-asan-unittests` CI jobs for memory safety and undefined behavior detection.
  - Created `contrib/release/build-release.sh` to build Docker images, extract binaries, generate checksums, and sign releases.

### 2. Critical Dependencies
- **OpenSSL**: Upgraded from `1.1.1n` (EOL) to **`3.0.18` (LTS)**.
    - Updated build recipe in `depends/packages/openssl.mk`.
    - Removed deprecated SSLv2/SSLv3 flags.
    - Updated SHA256 checksums.
- **Boost C++ Libraries**: Upgraded from `1.70.0` to **`1.82.0`**.
    - Updated build recipe in `depends/packages/boost.mk`.
    - Removed obsolete patches specific to 1.70.
    - Configured for C++17 standard compatibility.
- **BIP70 Payment Protocol**: Removed BIP70 (payment protocol) support from code/build/tests/docs to reduce attack surface and remove protobuf/X.509 payment request dependencies.

### 3. Network Connectivity
- **Seed Nodes**: Updated `src/chainparamsseeds.h` with a new list of known reliable mainnet peers to resolve initial sync issues.
    - Added high-uptime peers (e.g., `54.38.195.201`, `170.205.37.208`).

### 4. Codebase Hygiene & Branding
- **Copyrights**: Added "Radiant Developers" copyright notices to key source files.
- **Tests**: Updated `src/test/cashaddrenc_tests.cpp` to include and verify Radiant address prefixes (`radaddr`, `radtest`, `radreg`).

### 5. Protocol Limits & Validation
- **Explicit Oversized Transaction Rejection**:
  - Added `REJECT_OVERSIZED` to `src/consensus/validation.h`.
  - Updated `AcceptToMemoryPoolWorker` in `src/validation.cpp` to return a detailed `"bad-txns-oversize"` error including actual and maximum sizes.
  - Added `tx_mempool_reject_oversized` to `src/test/txvalidation_tests.cpp` to cover the new behavior.
- **Status**: Implemented and validated via **local unit tests** and a Docker-based dev environment.

### 6. Protocol & Consensus Enhancements
- **DAA Update (ASERT Half-Life Tuning)**:
  - Mainnet and ScaleNet use ASERT for difficulty adjustment.
  - Prior to block height **400,000**, both networks use a **2-day** ASERT half-life.
  - Starting at block height **400,000**, both networks switch to a **12-hour** ASERT half-life.
  - Testnet and Testnet4 remain at a **1-hour** half-life.
- **Transaction Size Limit**:
  - Set `MAX_TX_SIZE` to **12 MB** in `src/consensus/consensus.h`.
  - Removed legacy `MAX_TX_SIZE_ENERGY` constant (not needed for this upgrade).
  - Simplified transaction size validation in `src/validation.cpp` to use a single `MAX_TX_SIZE` check.
  - With 12 MB max transaction size:
    - ~81,000 P2PKH inputs possible per transaction
    - 50% more capacity than the previous 8 MB limit
    - Resolves prior exchange consolidation issues
- **Network Message Limits** (Security Enhancement):
  - Set `MAX_PROTOCOL_MESSAGE_LENGTH` to **2 MB** for most protocol messages (INV, PING, ADDR, etc.).
  - Added `MAX_TX_MESSAGE_LENGTH` of **16 MB** specifically for TX messages.
  - Block-like messages remain unlimited (scale with block size).
  - This prevents DoS attacks via oversized non-transaction messages while allowing large transactions.
- **Regtest Mining Policy**:
  - Added regtest-only bypass for fee clamping in `src/miner.cpp` to allow functional tests to run with configurable `blockmintxfee`.
  - Uses `chainparams.MineBlocksOnDemand()` to detect regtest and skip the post-upgrade fee floor enforcement.

### 7. Glyph Swap Broadcast (PSRT)
- **On-Chain Swap Protocol**: Implemented native support for Partially Signed Radiant Transactions (PSRT) to enable trustless atomic swaps.
- **SwapIndex**: Added a new LevelDB-backed index (`-swapindex`) that tracks on-chain swap advertisements by Token ID.
- **New RPCs**:
  - `getopenorders <token_ref>`: Returns active (unspent) swap offers for a given token.
  - `getswaphistory <token_ref>`: Returns executed/cancelled swap offers (spent UTXOs).
- **Mechanism**: Makers broadcast transactions with OP_RETURN outputs containing swap details (UTXO, terms, partial signature). The index tracks these advertisements, and order completion is detected by monitoring UTXO spending.
- **Use Case**: Enables decentralized order books for Glyph tokens without requiring external infrastructure.

### 8. Modernization & Developer Experience
- **Modern C++ Migration (C++20)**:
  - Migrated codebase from C++17 to **C++20**.
  - Replaced Boost dependencies with `std::` equivalents (e.g., `std::filesystem`, `std::span`).
  - Improved type safety and build times.
  - Bumped `CMAKE_CXX_STANDARD` to `20` in `src/CMakeLists.txt`.
- **Build System Cleanup**:
  - Fixed duplicate library linker warnings by consolidating circular dependency handling between `server` and `wallet` libraries.
  - Removed redundant transitive library links in `src/wallet/CMakeLists.txt` and `src/CMakeLists.txt`.
  - Removed legacy unused height-checking code in `src/validation.cpp`.
- **Operational Profiles**:
  - Added `-nodeprofile=<profile>` argument to simplify configuration and align with the System Design Document.
  - **`archive`** (default): Full history, txindex enabled.
  - **`agent`**: Pruned (~550MB), txindex disabled.
  - **`mining`**: Balanced (~4GB), txindex disabled.

### 9. Configuration Tuning
- **Default Configuration Review**: Audited `src/init.cpp` and `src/config.cpp` for outdated default values.
- **Database Cache Optimization**: Increased default `dbcache` size (dynamic based on available RAM or higher static value).
- **RPC Performance**: Increased default `rpcworkqueue` and `rpcthreads` to handle modern indexer loads.

### 10. Security & Observability
- **Fuzz Testing**: Added `fuzz-radiant_opcodes` target for Radiant-specific consensus (unique references, introspection opcodes, `SCRIPT_PUSH_TX_STATE`).
- **Prometheus Metrics**: Integrated native **Prometheus** metrics export (`/metrics` endpoint) for block height, peer count, mempool size, and version info.

## 🔮 Future Development
For the active development roadmap and upcoming features, please refer to [ROADMAP.md](ROADMAP.md).
