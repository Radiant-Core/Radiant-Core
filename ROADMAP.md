# Radiant Core Roadmap

This document tracks planned future work for Radiant Core. For completed features, see `doc/release-notes.md`.

---

# Phase 1: Infrastructure (Immediate)

## 1. DNS Seeder Infrastructure
- [ ] **Deploy Seeder**: Set up a `radiant-seeder` instance on a high-availability VPS.
- [ ] **Update Code**: Add the new seeder's domain to `src/chainparams.cpp` (Mainnet & Testnet).
- [ ] **Verify Crawling**: Ensure the seeder is correctly crawling the network and serving valid peers.

## 2. PSRT Swap Protocol Enhancements
- [ ] **Client-Side Order Expiration**: Implement order filtering based on age using existing `blockHeight` field.
  - Add optional `max_age` parameter to swap RPCs (`getopenorders`, `getopenordersbywant`)
  - Implement expiration logic in Photonic Wallet and other clients
  - Document best practices for expiration thresholds (e.g., 30-90 days)
- [ ] **Price Terms Validation Research**: Define validation requirements and approaches.
  - Survey ecosystem needs for price reasonableness checks
  - Evaluate static range vs. market-reference validation models
  - Design schema validation for price term formats
- [ ] **Maker Reputation System Design**: Research off-chain reputation tracking approaches.
  - Define key metrics (success rate, fill time, dispute outcomes)
  - Evaluate centralized vs. distributed reputation models
  - Design API specifications for reputation data exchange

# Phase 2: Performance & Modernization (1-3 Months)

## 1. Asynchronous RPC & Network
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

## 3. Covenant & Scripting Enhancements
- [ ] **BIP119 CTV (CheckTemplateVerify)**: Implement `OP_CHECKTEMPLATEVERIFY` for trustless transaction covenants.
  - Enables congestion control, vaults, and batched payments.
  - Allows pre-committed transaction trees without requiring signatures.
  - Research activation mechanism (soft fork vs. Radiant-specific deployment).

# Phase 4: Cross-Chain & Advanced Security (6-12 Months)

## 1. Gravity Protocol Integration
- [ ] **Cross-Chain Vaults**: Implement Gravity protocol for trustless cross-chain asset custody.
  - Research Gravity's proof-of-work verification for external chain state.
  - Design vault contracts leveraging Radiant's unique reference model.
  - Implement SPV proof validation for supported chains (BTC, ETH L1).
- [ ] **Bridge Infrastructure**: Build secure bridge architecture for cross-chain transfers.
  - Federated signer coordination with threshold signatures.
  - Fraud proof mechanisms for vault security.
  - Emergency recovery procedures and timelock protections.
- [ ] **Interoperability Standards**: Define cross-chain messaging and asset representation standards.
  - Wrapped asset minting/burning protocols.
  - Cross-chain transaction finality guarantees.

## 2. PSRT Advanced Features
- [ ] **Cross-Chain Atomic Swaps**: Extend PSRT protocol for cross-chain swap coordination.
  - Research HTLC-based vs. oracle-based approaches for atomic settlement
  - Design timing coordination mechanisms for different block times and finality
  - Implement security models for fund custody during cross-chain swaps
  - Create failure handling protocols for partial chain failures
- [ ] **Privacy-Enhanced PSRT**: Implement gradual privacy improvements for swap protocol.
  - **Phase 1**: Encrypted price terms with selective revelation to committed counterparties
  - **Phase 2**: Mixnet-based order dissemination to separate discovery from blockchain posting
  - **Phase 3**: Research ZK-SNARK implementation for complete swap confidentiality
  - Evaluate privacy vs. performance trade-offs for each approach

## 3. Advanced Vault Security
- [ ] **Time-Locked Vaults**: Implement native timelock primitives for cold storage.
- [ ] **Multi-Signature Enhancements**: Improve CHECKMULTISIG performance and add Schnorr-based MuSig2.
- [ ] **Watchtower Infrastructure**: Design watchtower protocol for vault monitoring and automated response.

# Optional / Future Considerations

These items are tracked for potential future work but are not currently prioritized:

- [ ] **Rename Units (SATOSHI → PHOTON)**: Optional rebranding of internal unit names. Currently keeping `SATOSHI` as a nod to the creator of the industry.
