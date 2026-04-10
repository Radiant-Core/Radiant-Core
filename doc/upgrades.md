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
  - Prior to block height **410,000**, both networks use a **2-day** ASERT half-life.
  - Starting at block height **410,000**, both networks switch to a **12-hour** ASERT half-life.
  - Testnet activates at block **1,000** with a **1-hour** half-life for fast testing.
- **Relay/Wallet Minimum Fee Policy (Grace Period)**:
  - The transaction relay/mempool minimum fee (`minrelaytxfee`) and the wallet required fee floor are height-gated.
  - A **5,000 block grace period** is applied after the Radiant Core 2.0 activation height.
  - Effective schedule:
    - **< 410,000**: `1,000,000 sat/kB` (`0.01 RXD/kB`)
    - **410,000 - 414,999**: `1,000,000 sat/kB` (`0.01 RXD/kB`) (grace period)
    - **>= 415,000**: `10,000,000 sat/kB` (`0.1 RXD/kB`)
  - This improves pre-activation mempool compatibility while still enforcing the new fee policy after the grace period.
  - Operational guidance:
    - Node operators should generally avoid manually setting `-minrelaytxfee` unless intentionally running a restrictive relay policy.
    - Wallet providers should ensure fee estimation meets the effective relay floor (wallet will clamp to the required minimum).
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

### 9. Mining Suite Integration
- **Complete Mining Package**: Added comprehensive mining solution in `mining/` directory with both CPU and GPU support.
- **GPU Acceleration**: Implemented PyOpenCL-based GPU miner achieving **50-100x speedup** over CPU mining.
  - Custom OpenCL kernel for Radiant's SHA-512/256d hashing algorithm
  - Batch processing with configurable sizes (default: 4,194,304 nonces)
  - Auto-detection of GPU devices and fallback to CPU if unavailable
- **CPU Miner**: Updated Python-based miner with proper SHA-512/256d implementation and BIP34 height encoding.
- **Node Profile Integration**: Mining suite integrates with Radiant's `-nodeprofile` system:
  - **`archive`**: Full node with txindex (not recommended for mining)
  - **`agent`**: Minimal node (heavy pruning affects mining)
  - **`mining`**: Optimized profile with balanced storage (~4GB) and recent block retention
- **Easy Installation**: One-command setup with `./mining/install.sh` that:
  - Detects and installs Python dependencies
  - Sets up PyOpenCL for GPU mining
  - Creates default configuration
  - Validates installation
- **User-Friendly Scripts**:
  - `start_mining_node.sh`: Launches radiantd with mining profile
  - `start_mining.sh`: Auto-detects and starts best available miner
  - `start_gpu_miner.sh`: GPU mining launcher
  - `start_cpu_miner.sh`: CPU mining launcher
- **Network Support**: Works with both testnet and mainnet through environment variables
- **BIP34 Compliance**: Fixed coinbase height encoding using proper OP codes for heights 1-16
- **Correct Genesis Hash**: Updated to use testnet genesis: `000000000d8ada264d16f87a590b2af320cd3c7e3f9be5482163e830fd00aca2`
- **ASIC Support**:
  - Added `mining/setup_asic_mining.sh` for automated ASIC configuration.
  - Documented support for native SHA-512/256d miners (DragonBall A11, Iceriver RX0).
  - Verified compatibility with SHA-256 miners (Antminer, WhatsMiner) via Stratum Proxy.

### 10. Testnet & QA Infrastructure
- **Testnet Support**:
  - Added full support for **Testnet** (accessed via `-testnet`) as the new stable public test network (replacing historical Testnet3).
  - Configured dedicated ports (P2P: `27333`, RPC: `27332`) and DNS seeds.
  - Aligned with mainnet consensus except for `radiantCore2UpgradeHeight` testing.
- **Regtest Improvements**:
  - Enhanced `regtest` mode for instant deterministic block generation via `generatetoaddress`.
  - Removed requirement for mining pools in local development/CI.
  - Set `radiantCore2UpgradeHeight = 200` for rapid validation of ASERT/consensus upgrades.
- **Documentation**:
  - Added `doc/testnet-guide.md`: Comprehensive guide covering local regtest, private multi-node nets, and public testnet participation.
  - Documented critical troubleshooting for Windows builds and libevent linkage.

### 11. Configuration Tuning
- **Default Configuration Review**: Audited `src/init.cpp` and `src/config.cpp` for outdated default values.
- **Database Cache Optimization**: Increased default `dbcache` size (dynamic based on available RAM or higher static value).
- **RPC Performance**: Increased default `rpcworkqueue` and `rpcthreads` to handle modern indexer loads.
- **Fee Policy Defaults**: Updated default fee-related values for better alignment with RXD economics and to reduce noisy warnings while keeping relay/mining policy consistent.
  - **Incremental relay fee**: `MEMPOOL_FULL_FEE_INCREMENT` default set to **0.01 RXD/kB** (`1,000,000 sat/kB`).
  - **Wallet incremental relay fee**: `WALLET_INCREMENTAL_RELAY_FEE` default set to **0.01 RXD/kB** (`1,000,000 sat/kB`).
  - **Wallet fallback fee**: `DEFAULT_FALLBACK_FEE` default set to **0.1 RXD/kB** (`10,000,000 sat/kB`).
  - **High-fee warning threshold**: `HIGH_TX_FEE_PER_KB` set to **1 RXD/kB** (`1 * COIN`) so defaults do not trigger "very high" fee warnings.
  - **Note**: Dust / ultra-small output policy was intentionally left unchanged.

### 12. Security & Observability
- **Fuzz Testing**: Added `fuzz-radiant_opcodes` target for Radiant-specific consensus (unique references, introspection opcodes, `SCRIPT_PUSH_TX_STATE`).
- **Prometheus Metrics**: Integrated native **Prometheus** metrics export (`/metrics` endpoint) for block height, peer count, mempool size, and version info.

### 13. V2 Hard Fork — New Opcodes (Radiant Core 2.1.0)
- **OP_BLAKE3** (`0xee`): New hash opcode implementing BLAKE3 cryptographic hash (32-byte output). ~500 LOC in `src/crypto/blake3.cpp`. Enables on-chain PoW validation for Blake3 dMint tokens.
- **OP_K12** (`0xef`): New hash opcode implementing KangarooTwelve (32-byte output). ~300 LOC in `src/crypto/k12.cpp`, leveraging existing `KeccakF` from `sha3.cpp`. Enables on-chain PoW validation for K12 dMint tokens.
- **OP_LSHIFT** (`0x98`): Re-enabled bitwise left shift. Code existed but was unconditionally disabled. Now fork-gated behind `SCRIPT_ENHANCED_REFERENCES`.
- **OP_RSHIFT** (`0x99`): Re-enabled bitwise right shift. Same gating mechanism.
- **OP_2MUL** (`0x8d`): Re-enabled multiply-by-2 with `safeMul(2)` overflow protection. Fork-gated.
- **OP_2DIV** (`0x8e`): Re-enabled divide-by-2 with truncation toward zero (e.g., `-3 / 2 = -1`). Fork-gated.
- **Activation**: All opcodes gated behind `SCRIPT_ENHANCED_REFERENCES` flag, activated at `radiantCore2UpgradeHeight`:
  - Mainnet & Testnet3: block 410,000
  - Scalenet: block 1,000
  - Regtest: block 200
- **Purpose**: Enables fully on-chain Glyph v2 dMint proof-of-work validation using alternative hash algorithms (Blake3, KangarooTwelve). Eliminates indexer trust dependency for PoW verification. Shift opcodes enable on-chain ASERT-lite DAA computation.
- **Test Coverage**: 15/15 crypto unit tests, 14/14 functional tests (`feature_v2_hash_opcodes.py`), 29 integration tests across 7 scenarios (`feature_v2_phase10_integration.py`).
- **Ecosystem**: All 9 downstream repositories updated (radiantjs, radiantblockchain-constants, RadiantScript, rxdeb, RXinDexer, Photonic Wallet, Glyph-miner, Glyph Token Standards).
- **Release Notes**: See `doc/release-notes/release-notes-2.1.0.md`.

### 14. Version 2.3.0 — Maintenance Release
- **Version Bump**: Updated all version strings across the codebase to 2.3.0:
  - `CMakeLists.txt`: `VERSION 2.3.0`
  - `gui/setup.py`: `APP_VERSION = '2.3.0'`
  - `macos-app-build/setup.py`: `APP_VERSION = '2.3.0'`
  - `gui/radiant_node_web.py`: `GITHUB_RELEASE_URL` updated to v2.3.0
  - `macos-app-build/radiant_node_web.py`: `GITHUB_RELEASE_URL` updated to v2.3.0
  - Build scripts: Default versions updated in `build-gui-release.sh`, `build-docker-release.sh`, `build-all-releases.sh`, `build-macos-app.sh`
  - Docker files: LABEL version updated to 2.3.0
  - Documentation: `README.md`, `docker/README.md`, `docker-guide.md`, `releases/README.txt` updated
- **Release Notes**: See `doc/release-notes/release-notes-2.3.0.md`.

## 🔮 Future Development
For the active development roadmap and upcoming features, please refer to [roadmap.md](roadmap.md).
