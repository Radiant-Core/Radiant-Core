# Product Requirements Document (PRD)

## Title
Radiant Core (Radiant Node) — Product Requirements

## Document Purpose
This PRD defines the product requirements for Radiant Core ("Radiant Node"), the full node + (optional) wallet + (optional) Qt GUI implementation for the Radiant network. It consolidates requirements from:

- `doc/whitepaper/radiant.md`
- `doc/whitepaper/radiant-system-design.md`
- `doc/README.md`
- `UPGRADES.md`
- `ROADMAP.md`

Where documents conflict, this PRD is the decision point for product scope and acceptance criteria.

## Product Summary
Radiant Core is software that:

- Participates in the Radiant P2P network.
- Validates blocks and transactions under Radiant consensus rules.
- Stores chainstate and (optionally) full historical blocks.
- Exposes RPC interfaces for wallets, miners, and indexers.
- Supports distinct operational profiles aligned with the whitepaper’s node roles (Mining, Archive, Agent).

## Users / Personas

- **Node Operator (Archive)**
  Runs a full-history node for serving blocks/transactions to internal services (explorers, indexers) or for data retention.
- **Node Operator (Agent)**
  Runs a resource-constrained node for listening/filtering use cases, often paired with an external indexer.
- **Miner / Pool Operator (Mining)**
  Runs a node focused on producing blocks and fast validation/propagation.
- **Wallet Developer / Power User**
  Uses wallet functionality and RPCs to manage keys/UTXOs and construct transactions.
- **Indexer / Explorer Operator**
  Requires stable RPC behavior and predictable performance characteristics for large-scale ingestion.

## Goals

- **G1: Correctness**
  Maintain consensus correctness under Radiant rules, including Radiant-specific script and transaction semantics described in the whitepaper/system design.
- **G2: Operability**
  Provide clear build/run/verification workflows and support multiple operational profiles.
- **G3: Security**
  Minimize attack surface and keep security-critical dependencies current (e.g., OpenSSL in `depends/`).
- **G4: Performance Baseline**
  Provide a stable baseline for high-throughput operation (bandwidth/IO intensive) and for indexer workloads.

## Non-Goals

- **NG1: New consensus design**
  This PRD does not propose new consensus changes beyond what the protocol documentation already specifies.
- **NG2: Secondary layers / bridges**
  No reliance on trusted third parties for asset authenticity is introduced at the node consensus layer.
- **NG3: Indexer replacement**
  Radiant Core is not required to provide a full “explorer-grade” indexing database; it should provide primitives and stable RPCs. It is designed to work with external indexers, enabling flexible deployment patterns. RXinDexer is a separate service that can be used alongside Radiant Core to provide explorer-grade indexing.

## Scope

### In Scope

- P2P networking, block/tx validation, mempool policy.
- Storage of chainstate and optional full blocks.
- RPC server for node control, mining, wallet, and indexer integration.
- Build system and release verification workflows.
- Testing/CI requirements for protocol and infrastructure safety.

### Out of Scope (for this PRD)

- GUI feature parity requirements beyond “buildable and functional” (Qt UX is not specified here).
- New protocol features not described in the source documents.

## Functional Requirements

### FR1: Platform Build & Distribution

- **FR1.1** The project MUST build from source using CMake/Ninja as described in `doc/README.md`.
- **FR1.2** The project MUST support build configurations:
  - node-only (no wallet, no Qt)
  - node + wallet (no Qt)
  - node + wallet + Qt
- **FR1.3** The project MUST support binary verification flows for released artifacts as described in `doc/README.md` (GPG verification of signer keys and sha256 sums).

### FR2: Operational Profiles

- **FR2.1** The node MUST support distinct operational profiles via `-nodeprofile=<profile>`:
  - `archive` (default): keeps full history and index
  - `agent`: prunes to minimum size (UTXO focus), and disables `txindex` by default
  - `mining`: keeps ~10,000 recent blocks
- **FR2.2** Each profile MUST document its intended storage/indexing behavior (at minimum: pruning and `txindex` expectations).

### FR3: Consensus-Critical Protocol Support (Radiant)

Radiant Core MUST validate blocks/transactions consistent with the Radiant protocol described in the whitepaper and system design, including:

- **FR3.1 Unique reference model**
  Support the “unique reference” digital asset authenticity mechanism (e.g., `OP_PUSHINPUTREF` family as described).
- **FR3.2 Induction proof support**
  Support the transaction identifier hashing method required for induction-proof patterns (TxId Version 3 as described).
- **FR3.3 Transaction introspection / constrained outputs**
  Maintain script validation capabilities necessary for enforcing output constraints via transaction context (as described conceptually in the system design).
- **FR3.4 Signature hash behavior**
  Maintain signature hashing behavior consistent with the Radiant documentation (including output-hash segmentation described in the system design paper).

Note: exact opcode names/semantics are specified in the protocol documentation; the implementation MUST remain consistent with consensus tests.

### FR4: Networking & Message Handling

- **FR4.1** The node MUST maintain P2P connectivity sufficient for initial sync and steady-state operation.
- **FR4.2** The node MUST support the protocol message size limit required by Radiant operations (see `UPGRADES.md`: 8 MB protocol message support).
- **FR4.3** Messages propagating block content MUST NOT be subject to the generic protocol message length limit.

### FR5: Validation Policy (Mempool)

- **FR5.1** Oversized transactions MUST be explicitly rejected with a detailed and stable reason string (see `UPGRADES.md`: `bad-txns-oversize`).
- **FR5.2** Reject codes SHOULD use `REJECT_OVERSIZED` for oversized transaction policy failures.
- **FR5.2** Policy behavior MUST be test-covered.

Note: Current implementation rejects oversized transactions with `REJECT_INVALID` and reason `bad-txns-oversize`; `REJECT_OVERSIZED` exists but is not currently used in all policy paths.

### FR6: RPC Interfaces

- **FR6.1** RPC MUST provide node operation endpoints needed by operators and indexers (status, chain queries, mempool queries, shutdown).
- **FR6.2** RPC SHOULD support high-concurrency indexer loads by providing sensible defaults/tuning for work queues/threads (see `ROADMAP.md` completed tuning items).

### FR7: Wallet Functionality (Optional Build)

- **FR7.1** When wallet is enabled, wallet RPCs MUST function for key management and transaction creation/sending.
- **FR7.2** Wallet-related security-sensitive dependencies and features MUST avoid unnecessary attack surface.

### FR8: CI/CD and Reproducibility

- **FR8.1** CI builds MUST be reproducible using the standardized Docker-based CI environment described in `UPGRADES.md`.
- **FR8.2** The repository MUST provide a local CI runner workflow (`contrib/run-ci-local.sh`) that matches CI behavior.

## Non-Functional Requirements

### NFR1: Security

- **NFR1.1** Security-critical dependencies MUST be maintained at supported versions (see `UPGRADES.md`: OpenSSL 3.x upgrade).
- **NFR1.2** Remove/avoid legacy protocols and features that increase attack surface without strong product justification (see `UPGRADES.md`: BIP70 removal).

As of the upgrade log baseline, the product security/maintenance posture includes:

- OpenSSL upgraded to 3.0.18 (LTS line).
- Boost upgraded to 1.82.0.
- Minimum CMake upgraded to 3.22.

### NFR2: Reliability

- **NFR2.1** Node MUST handle long-running operation (weeks/months) without requiring restarts for typical usage.
- **NFR2.2** Data integrity MUST be preserved across unclean shutdowns within the bounds of the underlying storage guarantees.

### NFR3: Performance

- **NFR3.1** Node MUST provide stable performance under sustained sync and indexer query loads appropriate to the selected profile.
- **NFR3.2** Defaults SHOULD be tuned for modern hardware where feasible (dbcache, RPC threads/queue), while allowing overrides.

### NFR4: Maintainability

- **NFR4.1** The codebase MUST remain buildable on supported toolchains (see `UPGRADES.md`: C++20 migration).
- **NFR4.2** Test coverage MUST exist for Radiant-specific consensus/policy behavior.

### NFR5: Multi-Environment Support

- **NFR5.1** The node MUST support dev/test/prod-like environments via network selection and configuration (e.g., mainnet/testnet/regtest, where applicable).
- **NFR5.2** Testing hooks or policies MUST NOT weaken production security; any test-only mining/policy changes must be restricted to test/regtest contexts (see `ROADMAP.md`: regtest mining for functional tests).

## Acceptance Criteria

- **AC1 Build Matrix**
  All supported build configurations (node-only, node+wallet, node+wallet+Qt) build successfully.
- **AC2 Sync & Basic Operation**
  Node starts, connects to peers, and can sync headers/blocks appropriate to the selected profile.
- **AC3 Protocol Message Limit**
  Message size handling aligns with the documented limit (8 MB).
- **AC4 Oversized Transaction Rejection**
  Oversized transactions are rejected with the documented reject code/message and covered by tests.
- **AC5 Security Baseline**
  No BIP70 support remains; OpenSSL in `depends/` is maintained at the documented version line.
- **AC6 CI Reproducibility**
  Local CI runner and CI pipeline are consistent and pass the relevant test suites.

## Traceability to Source Documents

- **Whitepaper (`radiant.md`)**
  Drives the node-role model (Mining/Archive/Agent) and high-level protocol intent (unique identifiers, induction proofs).
- **System design (`radiant-system-design.md`)**
  Drives detailed protocol mechanics (reference opcodes, TxId v3 concept, signature hash preimage fields).
- **Node setup (`doc/README.md`)**
  Drives build/run and release verification requirements.
- **Upgrade log (`UPGRADES.md`)**
  Drives concrete modernization/security requirements (OpenSSL upgrade, BIP70 removal, message sizing, reject codes, profiles).
- **Roadmap (`ROADMAP.md`)**
  Identifies future work that is NOT acceptance-criteria required unless promoted into this PRD.

## Appendix: Network Parameters (Informational)

From `doc/whitepaper/radiant-system-design.md`:

- Network name: Radiant
- Abbreviation: RXD
- Mining algorithm: SHA512/256 PoW
- Target block time: 5 minutes
- Current block size: 256 MB
- Max supply: 21,000,000,000 RXD
- Decimal places: 8
- Current Block reward: 25,000 RXD per block
- Reward halving interval: 210,000 blocks
- Consensus rules: SHA512/256 PoW, v3 transaction format

