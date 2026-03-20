# Radiant Core v2.2.0 Release Notes

**Release Date:** March 19, 2026  
**Maintenance & Network Hardening Release**

---

## Summary

Radiant Core v2.2.0 is a maintenance release that hardens the network with updated checkpoints, refreshed seed nodes, improved DNS seeder capabilities, and enhanced debug logging. All V2 hard fork features (OP_BLAKE3, OP_K12, OP_LSHIFT, OP_RSHIFT, OP_2MUL, OP_2DIV) activated at block 410,000 remain fully operational.

---

## Changes in v2.2.0

### Network Hardening

- **New checkpoints** at blocks 410,000 and 412,000 — confirms the V2 hard fork activation and subsequent chain progress
- **Updated assume-valid block** to height 412,000 — faster initial sync for new nodes
- **Updated minimum chain work** — reflects cumulative proof-of-work through block 412,000
- **Refreshed chain transaction data** — updated to height 412,000 (28.5M total transactions)

### Seed Node Refresh

- **Updated seed node list** — removed offline/unreachable nodes, added new reliable peers
- Ensures new nodes can bootstrap and find peers on first startup

### DNS Seeder Improvements

- **Version filtering** (`-minclientversion`, `-minheight`) — seeder can now filter peers by client version and block height
- **DNS seeds enabled** — improved peer discovery for new nodes joining the network

### Improved Debug Logging

- **Enhanced difficulty mismatch logging** in block validation — now logs both actual and expected nBits values for easier debugging of consensus issues

### Version Updates

- GUI Web interface updated to v2.2.0
- Docker images updated to v2.2.0
- Build scripts updated to v2.2.0

---

## Upgrade Instructions

### Standard Upgrade

1. **Stop your node:**
   ```bash
   radiant-cli stop
   ```

2. **Backup your data directory** (optional but recommended):
   ```bash
   # macOS
   cp -r ~/Library/Application\ Support/Radiant ~/Library/Application\ Support/Radiant.backup
   
   # Linux
   cp -r ~/.radiant ~/.radiant.backup
   ```

3. **Install v2.2.0 binaries** (replace your existing installation)

4. **Start the node:**
   ```bash
   radiantd -daemon
   ```

5. **Verify upgrade:**
   ```bash
   radiant-cli getnetworkinfo | grep subversion
   ```
   
   You should see: `"subversion": "/Radiant:2.2.0/"`

### Impact

**No blockchain rollback required** — this is a forward-compatible upgrade. Existing chain data is fully compatible.

---

## Modified Files

- `src/chainparams.cpp` — New checkpoints (410k, 412k), updated chain tx data
- `src/chainparamsconstants.h` — Updated assume-valid and minimum chain work
- `src/chainparamsseeds.h` — Refreshed seed node list
- `src/validation.cpp` — Enhanced difficulty mismatch debug logging
- `src/seeder/` — Version filtering and DNS seed improvements
- `CMakeLists.txt` — Version bump to 2.2.0

---

## Download

**Binaries:**
- macOS (ARM64): `radiant-core-macos-arm64-v2.2.0.tar.gz`
- Linux (x64): `radiant-core-linux-x64-v2.2.0.tar.gz`
- Qt Wallet (macOS ARM64): `radiant-core-qt-wallet-macos-arm64-v2.2.0.zip`
- Qt Wallet (Linux x64): `radiant-qt-linux-x64-v2.2.0.tar.gz`
- Node Web GUI (macOS): `Radiant-Node-Web-GUI-2.2.0.dmg`
- Docker image: `radiant-core-docker-v2.2.0.tar.gz`

**SHA256 checksums:** See `SHA256SUMS.txt` in release folder

---

## Support

**Questions or issues?**
- Discord: https://discord.gg/radiantblockchain
- Telegram: https://t.me/RadiantBlockchain
- GitHub Issues: https://github.com/Radiant-Core/Radiant-Core/issues

---

## Version History

- **v2.2.0** (2026-03-19) - Maintenance: checkpoints, seed refresh, seeder improvements, debug logging
- **v2.1.2** (2026-03-10) - Critical fix: align miner fee enforcement with grace period
- **v2.1.1** (2026-03-09) - Emergency fix for difficulty spike and getblocktemplate cache
- **v2.1.0** (2026-02-12) - V2 hard fork with new opcodes (OP_BLAKE3, OP_K12, etc.)
- **v2.0.1** (2025-11-15) - Maintenance release
