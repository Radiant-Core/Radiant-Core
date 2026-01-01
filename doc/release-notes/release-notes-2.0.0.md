# Radiant Core 2.0.0 (Phoenix) Release Notes

**Release Date**: December 2024  
**Hard Fork Activation**: Block 400,000 (~Late January 2026)

---

## Overview

Radiant Core 2.0.0 is a **mandatory upgrade** that includes consensus changes activating at block height **400,000**. All node operators, miners, exchanges, and wallet providers **must upgrade before block 400,000** to remain on the main chain.

### Current Network Status
- **Current Block Height**: ~385,830 (as of December 12, 2025)
- **Blocks Until Activation**: ~14,170
- **Estimated Activation Date**: Late January / Early February 2026

---

## ⚠️ IMPORTANT: Upgrade Deadline

**All participants MUST upgrade before block 400,000.**

| Participant | Urgency | Impact of Not Upgrading |
|-------------|---------|-------------------------|
| **Miners** | 🔴 Critical | Blocks will be orphaned due to incorrect difficulty calculation |
| **Exchanges** | 🟠 High | May fork onto minority chain, risk of double-spend attacks |
| **Node Operators** | 🟠 High | Will follow wrong chain after activation |
| **Wallet Users** | 🟡 Medium | Transactions may not confirm on main chain |

---

## Consensus Changes (Activate at Block 400,000)

### 1. ASERT Difficulty Adjustment Half-Life Tuning

The ASERT (Absolutely Scheduled Exponentially Rising Targets) difficulty adjustment algorithm half-life is reduced from **2 days** to **12 hours**.

**Impact**:
- Faster difficulty adjustment to hashrate changes
- More stable block times during hashrate fluctuations
- Improved network responsiveness

**Technical Details**:
- Pre-upgrade: `nASERTHalfLife = 172,800 seconds` (2 days)
- Post-upgrade: `nASERTHalfLife = 43,200 seconds` (12 hours)
- Activation: Height-based at block 400,000

### 2. Minimum Fee Policy Enforcement

A new minimum transaction fee policy is enforced at the consensus level to protect against DoS attacks and ensure miner revenue sustainability.

**Fee Schedule**:
| Block Height | Minimum Fee | RXD/kB |
|--------------|-------------|--------|
| < 400,000 | 1,000,000 sat/kB | 0.01 RXD/kB |
| ≥ 400,000 | 10,000,000 sat/kB | 0.1 RXD/kB |

**Impact**:
- Miners cannot set `-blockmintxfee` above the maximum for their height
- Prevents "empty block" attacks where miners reject all transactions
- Ensures minimum economic viability for miners

**Credit**: Empty Block Miner vulnerability reported by **iotapi from Vipor.net**.

---

## Non-Consensus Changes

These changes do not affect consensus and are backward-compatible:

### Infrastructure & Build System
- Upgraded to **C++20** standard
- Upgraded Docker base image to **Ubuntu 24.04 LTS**
- Upgraded OpenSSL to **3.0.18 LTS**
- Upgraded Boost to **1.82.0**
- Upgraded CMake minimum to **3.22**

### Transaction & Network Limits
- **Transaction Size**: Increased `MAX_TX_SIZE` to **12 MB** (~81,000 P2PKH inputs per transaction)
  - 50% more capacity than the previous 8 MB limit
  - Resolves exchange consolidation issues with many inputs
- **Network Message Limits** (Security Enhancement):
  - Set `MAX_PROTOCOL_MESSAGE_LENGTH` to **2 MB** for most protocol messages (INV, PING, ADDR, etc.)
  - Added `MAX_TX_MESSAGE_LENGTH` of **16 MB** specifically for TX messages
  - Block-like messages remain unlimited (scale with block size)
  - Prevents DoS attacks via oversized non-transaction messages while allowing large transactions

### Branding & Naming
- Renamed from "Radiant Node" to **"Radiant Core"**
- Version bumped to **2.0.0**
- Removed "pre-release test build" warnings

### Address Format
- **Removed cashaddr support** (Bitcoin Cash address format)
- Radiant now uses **Base58Check addresses only** (legacy Bitcoin format)
- Payment URIs use `radiant:` prefix

### Monitoring & Observability
- Added native **Prometheus metrics** endpoint (`/metrics` on port 7332)
- Metrics: `radiant_block_height`, `radiant_peers_connected`, `radiant_mempool_size`

### Glyph Swap Broadcast (PSRT)
- Added native support for **Partially Signed Radiant Transactions (PSRT)** enabling trustless atomic swaps
- **SwapIndex**: New LevelDB-backed index tracks on-chain swap advertisements by Token ID
- **Enhanced RPCs with age filtering**:
  - `getopenorders <token_ref> [limit] [offset] [max_age]`: Returns active swap offers with optional age filtering
  - `getopenordersbywant <want_token_ref> [limit] [offset] [max_age]`: Find orders by wanted token with age filtering
  - `getswaphistory <token_ref> [limit] [offset]`: Returns executed/cancelled swap offers (spent UTXOs)
  - `getswapindexinfo`: Returns swap index status and performance metrics
- **Order expiration**: `max_age` parameter allows filtering stale orders (e.g., 720 blocks = 6 hours)
- **Performance monitoring**: `getswapindexinfo` provides index health, order counts, and configuration
- Makers broadcast transactions with OP_RETURN outputs containing swap details (UTXO, terms, partial signature)
- Enables decentralized order books for Glyph tokens without external infrastructure
- Enable with `-swapindex` flag

### Node Profiles
- Added `-nodeprofile=<profile>` for simplified configuration:
  - `archive` (default): Full history, txindex enabled
  - `agent`: Pruned (~550MB), txindex disabled
  - `mining`: Balanced (~4GB), txindex disabled

### Block Finality
- Changed `DEFAULT_MAX_REORG_DEPTH` from **10 to 6** blocks for quicker block finality

---

## Replay Protection

Radiant maintains **strong replay protection** via `SIGHASH_FORKID`:

- All Radiant transactions include a chain-specific fork ID in the signature hash
- Transactions from other chains (BTC, BCH) cannot be replayed on Radiant
- Radiant transactions cannot be replayed on other chains
- **No additional replay protection is needed** for this upgrade

---

## Upgrade Instructions

### For Node Operators

```bash
# Stop current node
radiant-cli stop

# Download and install Radiant Core 2.0.0
# (Replace with actual download URL)
wget https://github.com/Radiant-Core/Radiant-Core/releases/download/v2.0.0/radiant-core-2.0.0-linux64.tar.gz
tar xzf radiant-core-2.0.0-linux64.tar.gz

# Start new node
./radiantd
```

### For Miners

1. Upgrade to Radiant Core 2.0.0 **before block 400,000**
2. No configuration changes required
3. The fee policy will automatically adjust at activation

### For Exchanges

1. Upgrade to Radiant Core 2.0.0
2. Test deposit/withdrawal functionality on testnet
3. Monitor for any address format issues (cashaddr removed)

### For Wallet Developers

1. Update to use Base58Check addresses only
2. Update URI scheme to `radiant:`
3. Adjust fee estimation for new minimum (0.1 RXD/kB post-activation)

---

## Testing

### Testnet

Testnet also activates at block 400,000. Use testnet to verify your integration before mainnet activation.

```bash
./radiantd -testnet
```

### Regtest

Regtest activates at block 200 for rapid testing:

```bash
./radiantd -regtest
./radiant-cli -regtest generatetoaddress 201 <address>
```

---

## Known Potential Issues

- **BIP70 Removed**: Payment protocol support has been removed. (Unused in the ecosystem)

---

## Full Changelog

This document.
For a complete list of Roadmap of future changes, see [UPGRADES.md](../../UPGRADES.md).

---

## Support

- **Discord**: [Radiant Blockchain Discord](https://discord.com/invite/radiantblockchain)
- **Telegram**: [@RadiantBlockchain](https://t.me/RadiantBlockchain)

---

## Contributors

Thank you to all contributors to this release, with special thanks to:
- **iotapi from Vipor.net** for reporting the Empty Block Miner vulnerability 
- **CraigD** for reporting and assisting with the oversize transaction vulnerability 
- **Razoo** for all his work on Radiant and PSRT/Glyphs 
- **Antares** for all he does for Radiant 

---

*This is a mandatory upgrade. Please upgrade before block 400,000 to remain on the Radiant main chain.*
