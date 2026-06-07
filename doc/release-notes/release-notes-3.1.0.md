# Radiant Core 3.1.0 Release Notes

**Release Date**: June 2026
**Release Type**: Security hardening + consensus soft fork
**Consensus Activation**: `SCRIPT_SECURITY_UPGRADE` — **mainnet block 444,444**, testnet/scalenet block 1, regtest from genesis
**Git Tag**: v3.1.0

> **⚠️ This is a mandatory upgrade for mainnet node operators.** v3.1.0 introduces
> a consensus soft fork that activates at **block 444,444**. Every mainnet node
> must be running v3.1.0 (or a compatible future release) before that height or
> it will reject blocks the rest of the network accepts and fall off the main
> chain. 3.0.x nodes stay consensus-compatible only *until* block 444,444.
>
> **⚠️ Breaking change for RPC operators — read "Action required" below.** v3.1.0
> adds an HTTP `Host`-header allowlist (DNS-rebinding defense). If you reach
> `radiantd`'s RPC through a reverse proxy or by hostname (e.g. a Docker Compose
> service name), you **must** set `-rpcallowhost` or RPC calls start returning
> `403 Forbidden`.

---

## Overview

Radiant Core 3.1.0 is the security-remediation release for the 2026-06 Radiant
Core audit (5 critical / 18 high / 14 medium findings). It carries every in-repo
fix across consensus, RPC/wallet, P2P/network, GUI, and build/supply-chain, and
it bundles the 3.0.1 hardening line. The `test_bitcoin` suite passes clean.

The consensus-level changes are gated behind a new script flag,
`SCRIPT_SECURITY_UPGRADE`, tied to `Consensus::SecurityUpgradeHeight`. The flag
only ever *tightens* script acceptance, so the activation is a clean soft fork:
pre-activation behavior is unchanged, and the new rules apply uniformly once the
height is reached. A chain scan (`contrib/audit/scan-hash-opcode-usage.py`)
verified that no historical transaction uses the constructs being tightened
before the chosen activation height, so no currently-valid script is
retroactively invalidated.

---

## Action required

### 1. Upgrade before mainnet block 444,444 (consensus)

The new consensus rules activate at **block 444,444** on mainnet (~225 days from
the 3.0.x audit baseline at block ~412,000). Operators must be on v3.1.0 before
then. There is no opt-out — a node still on 3.0.x past that height will diverge
from consensus.

### 2. Set `-rpcallowhost` if RPC is fronted by a proxy or reached by hostname (breaking)

v3.1.0 adds a standard DNS-rebinding defense: the HTTP RPC server now rejects any
request whose `Host` header is a **DNS name** not on an allowlist, returning
`403 Forbidden`. The allowlist always includes loopback identities (`localhost`,
`127.0.0.1`, `::1`) and every host in `-rpcbind`. Requests with **no** `Host`
header, or whose `Host` is a numeric **IP literal**, are always allowed (an IP
literal cannot be a rebinding vector), so direct `radiant-cli`/`127.0.0.1`
clients are unaffected.

What this breaks, and how to fix it:

- **Reverse proxy (Caddy/nginx/HAProxy) forwarding the public hostname.** The
  upstream request carries `Host: your.domain` — a DNS name. Add
  `-rpcallowhost=your.domain`.
- **Docker Compose / container networks reaching the node by service name.** A
  client connecting to `http://radiantd:7332/` sends `Host: radiantd`. Add
  `-rpcallowhost=radiantd`.
- **Multiple hostnames.** `-rpcallowhost` is repeatable: pass it once per host.
- **Opt out entirely** (only when a trusted proxy already enforces `Host`
  filtering): `-rpcallowhost=*` disables the check and logs a warning.

**Symptom if unset:** clients log `403 Forbidden` (an indexer/proxy may surface it
as `daemon service refused: Forbidden`) and stop receiving RPC responses. Example
for the reference `docker/full-stack` deployment, whose indexer connects by
service name and whose swap proxy forwards a public hostname:

```yaml
# radiantd command:
  -rpcallowip=0.0.0.0/0
  -rpcbind=0.0.0.0
  -rpcallowhost=radiantd                 # indexer → http://radiantd:7332/
  -rpcallowhost=swap.radiantcore.org     # reverse-proxied public RPC hostname
```

---

## Consensus changes (soft fork, activates at `SecurityUpgradeHeight`)

All of the following ride the new `SCRIPT_SECURITY_UPGRADE` flag and have **no
effect before activation**:

- **F1 — OP_K12 single-node bound corrected to 8191 bytes.** The off-spec digest
  produced for an exactly-8192-byte input is removed; the single-node input bound
  is now 8191 bytes, matching the KangarooTwelve spec.
- **H1 / M1 — per-script 64 MB peak-stack-memory budget.** Caps cumulative peak
  stack memory per script evaluation, ending the P2SH memory-bomb DoS vector.
- **M2 — per-opcode cost accounting for hash and bytewise opcodes.** Adds a cost
  budget so individual scripts can no longer amplify CPU via repeated
  hashing/bytewise operations.
- **HIGH-1 — `OP_REFHASHDATASUMMARY_UTXO` / `OP_REFHASHVALUESUM_UTXOS` added to
  the disabled-opcode list** (unconditionally). These have been dormant on
  mainnet since the Enhanced References height (62000); they are now explicitly
  rejected.

**New flag / parameter:** `SCRIPT_SECURITY_UPGRADE` / `Consensus::SecurityUpgradeHeight`.

| Network | `SecurityUpgradeHeight` | Status |
|---------|------------------------|--------|
| **mainnet** | **444,444** | future activation |
| testnet / scalenet | 1 | active from block 1 |
| regtest | 0 | active from genesis |

> Because testnet, scalenet, and regtest activate the new rules at/near genesis,
> contract authors can exercise covenants under the tightened script limits today
> without waiting for mainnet block 444,444.

**Activation gate / tooling:** `contrib/audit/scan-hash-opcode-usage.py` scans the
chain from block 62000 for any `OP_BLAKE3 > 1024 B`, `OP_K12 ≥ 8192 B`, or opcode
`0xd4` / `0xd5` usage. It must run clean before an operator-set activation height,
and was used to confirm the mainnet 444,444 choice is safe.

---

## RPC / wallet hardening

- **HTTP `Host`-header allowlist (`-rpcallowhost`)** — DNS-rebinding defense; see
  "Action required" above.
- **`/metrics` endpoint now requires RPC authentication** (`-metricsauth`, default
  on). Previously the metrics endpoint was unauthenticated.
- **`httptrace` log redaction** — `Authorization` headers and the bodies of
  secret-bearing RPC methods (`dumpprivkey`, `dumpwallet`, `importprivkey`,
  `signrawtransaction*`, `walletpassphrase`, `encryptwallet`, `sethdseed`, …) are
  redacted, so enabling `-debug=httptrace` never writes private keys, seeds, or
  passphrases to the log.
- **`dumpwallet` / `importwallet`** refuse symlink targets and force `0600`
  permissions on the output file.
- **RPC cookie** file forced to `0600`.
- **Wallet KDF iteration floor raised to 200,000** for new encryptions
  (backward-compatible: existing encrypted wallets are unaffected).

## P2P / network DoS hardening

- Per-peer rate limits for `ADDR`, `INV`, and large-`TX` messages.
- Orphan-transaction pool byte budget (100 MB).
- DSProof per-peer orphan cap and early sanity validation.
- `prevector` allocation failure now raises `bad_alloc` instead of an OOM abort.
- Compact-block early-PoW pre-check (reject before expensive reconstruction).
- Peer discouragement persisted across restarts (`-persistdiscouraged`).

## GUI security

- Auto-downloader is now **fail-closed** on placeholder or missing artifact
  hashes (a release with an un-updated hash manifest refuses to download).
- WIF / private-key arguments are routed via `radiant-cli -stdin`, never on the
  command line (no longer visible in `ps`).
- Backup/import argument validation; constant-time token comparison; exact `Host`
  match; two previously-broken `apiCall` paths fixed.
- GUI wallet derivation uses the correct SLIP-0044 coin type 512 (the 3.0.0 GUI
  could derive the Bitcoin coin-type-0 path).

## Supply chain / build

- **~805 MB of tracked release and GUI binaries removed** from git history
  tracking (`git rm --cached`; now gitignored).
- All GitHub Action references **SHA-pinned** with least-privilege permissions.
- Release process now requires **signed git tags and GPG-signed `SHA256SUMS`**
  (see `RELEASE_SECURITY_PROCESS.md`).
- Reproducible builds for Linux / Docker / Windows; partial reproducibility on
  macOS.

---

## Upgrade instructions

Standard binary upgrade — stop the node, replace the binaries, restart. Chain
data is forward-compatible; **no reindex is required** for the version bump.

1. Stop `radiantd`.
2. Install the v3.1.0 binaries (or rebuild from the `v3.1.0` tag).
3. **If your RPC is reached via a proxy or hostname, add `-rpcallowhost`** (see
   "Action required"). Verify with a cross-host RPC call after restart.
4. Restart `radiantd`. Confirm `getnetworkinfo` reports
   `"subversion": "/Radiant Core:3.1.0(...)/"` and that height continuity is
   preserved (no resync).
5. Ensure the node is upgraded **before mainnet block 444,444**.

For the Dockerized reference deployment (`docker/full-stack`), the radiantd image
clones and builds from source; pin the build to the `v3.1.0` tag and add the
`-rpcallowhost` flags shown above.

---

## Compatibility summary

| Concern | 3.1.0 behavior |
|---------|----------------|
| **Consensus (pre-444,444)** | Identical to 3.0.x. No divergence. |
| **Consensus (block ≥ 444,444, mainnet)** | New script limits enforced. 3.0.x nodes diverge — must upgrade. |
| **Chain data** | Forward-compatible; no reindex. |
| **RPC over loopback / `radiant-cli`** | Unchanged. |
| **RPC via proxy or hostname** | Requires `-rpcallowhost` (else `403 Forbidden`). |
| **Existing encrypted wallets** | Unchanged (KDF floor applies to new encryptions only). |
