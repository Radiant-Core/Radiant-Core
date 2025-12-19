# Phase 1: Stability & Foundation (Immediate)

## 1. Finalize CI/CD Pipeline
- [x] **Verify CI Build**: Local CI build (`./contrib/run-ci-local.sh`) passes successfully.
- [x] **Integrate with GitLab/GitHub**: Updated `.gitlab-ci.yml` with ASan/UBSan jobs and uses `radiant-node-ci` Docker image.
- [x] **Release Automation**: Created `contrib/release/build-release.sh` to build Docker images, extract binaries, generate checksums, and sign releases.

## 2. DNS Seeder Infrastructure
- [ ] **Deploy Seeder**: Set up a `radiant-seeder` instance on a high-availability VPS.
- [ ] **Update Code**: Add the new seeder's domain to `src/chainparams.cpp` (Mainnet & Testnet).
- [ ] **Verify Crawling**: Ensure the seeder is correctly crawling the network and serving valid peers.

## 3. Configuration Tuning
- [x] **Review Defaults**: Audit `src/init.cpp` and `src/config.cpp` for outdated default values.
- [x] **Optimize `dbcache`**: Increase default database cache size (dynamic based on RAM or higher static value).
- [x] **RPC Threads**: Increase default `rpcworkqueue` and `rpcthreads` to handle modern indexer loads.

## 4. Protocol Limits & Validation
- [x] **Network Message Limits (Security Enhancement)**:
  - Set `MAX_PROTOCOL_MESSAGE_LENGTH` to **2 MB** for most protocol messages (INV, PING, ADDR, etc.) to prevent DoS attacks.
  - Added `MAX_TX_MESSAGE_LENGTH` of **16 MB** specifically for TX messages.
  - Block-like messages remain unlimited (scale with block size).
- [x] **Transaction Size Limit**: Set `MAX_TX_SIZE` to **12 MB** (~81,000 P2PKH inputs per transaction).
- [x] **Oversized Transaction Handling**: Add `REJECT_OVERSIZED` and improved `"bad-txns-oversize"` messaging in `AcceptToMemoryPoolWorker`, covered by `tx_mempool_reject_oversized` in `txvalidation_tests.cpp`.
- [x] **Regtest Mining for Functional Tests**: Added regtest-only bypass for fee clamping in `src/miner.cpp` to allow functional tests to run with configurable `blockmintxfee`.

## 5. Security & Observability
- [x] **Fuzz Testing**: Added `fuzz-radiant_opcodes` target for Radiant-specific consensus (unique references, introspection opcodes, `SCRIPT_PUSH_TX_STATE`).
- [x] **Sanitizers**: Added ASan/UBSan CI jobs (`build-ubuntu-asan`, `test-ubuntu-asan-unittests`) to `.gitlab-ci.yml` for memory safety and undefined behavior detection.
- [x] **Prometheus Metrics**: Integrated native **Prometheus** metrics export (`/metrics` endpoint) for block height, peer count, mempool size, and version info.

# Phase 2: Performance & Modernization (1-3 Months)

## 1. C++20 Migration
- [x] **Compiler Support**: Verify all build targets (including cross-compilation) support C++20.
- [x] **Update CMake**: Bump `CMAKE_CXX_STANDARD` to `20` in `src/CMakeLists.txt`.
- [x] **Refactor**: Begin replacing boost headers with `std::` equivalents (e.g., `std::span`, `std::filesystem`).

## 2. Codebase Refactoring
- [x] **Code Cleanup**: Completed as part of C++20 migration (unused variables, thread safety, etc.).

## 3. Asynchronous RPC & Network
- [ ] **Async JSON-RPC**: Refactor the JSON-RPC server to be fully asynchronous (using `libevent` or C++20 coroutines).
- [ ] **Block Propagation**: Implement **Graphene** or **Xthinner** to reduce bandwidth usage.
- [ ] **Neutrino Support**: Enhance **BIP157/158** support for private light client syncing.

# Phase 3: Protocol Expansion (3-6 Months)

## 1. P2P Encryption (BIP324)
- [ ] **Port Reference**: Analyze Bitcoin Core's BIP324 implementation.
- [ ] **Implementation**: Integrate v2 transport protocol into `src/net.cpp` and `src/net_processing.cpp`.

## 2. Consensus & Database
- [ ] **DAA Research**: Simulate ASERT v2 performance under volatile hashrate.
- [ ] **VM Research**: Evaluate EVM/WASM integration paths (Sidechain vs. Native Extension).
- [ ] **Storage Engine**: Investigate migrating from LevelDB to **RocksDB** for NVMe optimization.
- [ ] **Quantum Resistance**: Research quantum-safe signature schemes (e.g., Lamport, STARKs).

# Optional / Future Considerations

These items are tracked for potential future work but are not currently prioritized:

- [ ] **Rename Units (SATOSHI → PHOTON)**: Optional rebranding of internal unit names. Currently keeping `SATOSHI` as a nod to the creator of the industry.
