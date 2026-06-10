# Radiant Core 3.1.1 Release Notes

**Release Date**: June 2026
**Release Type**: Security point release (follow-up to 3.1.0)
**Consensus Activation**: none new — the `SCRIPT_SECURITY_UPGRADE` soft fork
height (mainnet block **444,444**) is unchanged from 3.1.0
**Git Tag**: v3.1.1

> **Recommended upgrade for all node operators.** 3.1.1 closes a remotely
> triggerable memory-exhaustion DoS that is live on the network *now* (before the
> 444,444 fork), fixes a double-spend-proof relay regression, and removes a
> silent wallet fund-loss footgun. There are **no new consensus rules** and **no
> breaking changes** for existing deployments (see "Compatibility" below).

---

## Overview

3.1.1 is a follow-up to the 3.1.0 security-remediation release. A second audit
pass (re-review of the 3.1.0 fixes plus the previously under-covered money-layer
and reference/covenant subsystems) confirmed the money layer is sound
(`MAX_MONEY` is 21,000,000,000 RXD, well below `INT64_MAX`, and every consensus
value sum re-checks `MoneyRange`, so no inflation vector exists) and surfaced a
small number of residual issues that are fixed here. The `test_bitcoin` suite
passes clean and the integration paths were validated on regtest.

---

## Security fixes

### Script memory-exhaustion DoS — closed at relay before the fork (High)

3.1.0 added a per-script peak stack-memory budget
(`MAX_SCRIPT_STACK_MEMORY_USAGE`) but enforced it only via the consensus
`SCRIPT_SECURITY_UPGRADE` flag, which does not activate on mainnet until block
444,444. Until then a single ~sub-12 MB transaction whose input script balloons
the stack past the budget (e.g. `OP_NUM2BIN`/`OP_DUP` of large elements) could
exhaust node memory during mempool acceptance — relayable today because mainnet
runs with `fRequireStandard=false`.

3.1.1 enforces the budget as an **always-on relay/policy guard** in
`AcceptToMemoryPool` via a new non-consensus script flag
(`SCRIPT_VERIFY_MEMORY_BUDGET`), independent of the fork height. A
budget-exceeding transaction is now rejected from the mempool immediately, and is
classified as a **non-mandatory** rejection (`ScriptError::STACK_MEMORY`) before
the fork — it does **not** DoS-ban the relaying peer and is **not** re-executed
under mandatory flags, so it cannot be used to either partition honest nodes or
re-trigger the bomb. Once `SCRIPT_SECURITY_UPGRADE` activates at 444,444 the same
budget is enforced as a consensus rule.

### Per-script stack-memory accounting is now O(1) (High)

The 3.1.0 budget recomputed the full stack+altstack byte total after every
opcode (O(n²) per script), which was itself a CPU-exhaustion vector once the fork
activated. 3.1.1 maintains the total incrementally (O(1) amortized) with
identical accept/reject results. The headroom was also raised from 64 MB to
128 MB (4× the 32 MB element size) — a relaxation of a never-yet-active limit.

### Double-spend-proof per-peer orphan accounting leak (High)

The 3.1.0 per-peer DSProof orphan cap leaked its counter on the expiry path
(`periodicCleanup`): the counter was incremented on add but never decremented on
expiry, so a long-lived peer would eventually be permanently refused new orphan
proofs even with zero resident orphans, and the per-peer map grew unbounded
across peer churn. 3.1.1 decrements the counter on expiry (matching every other
removal path) and clears a peer's entry on disconnect.

### Restore-from-seed no longer silently reports a zero balance (High)

`sethdseed` now scans the node's UTXO set for activity on **both** the legacy
(`m/0'/0'/k`) and Radiant-standard (`m/44'/512'/0'/0/k`) derivation paths of the
supplied seed before committing any wallet state. If funds are found only on the
alternate path it selects that path (or, for an explicit `coin_type` mismatch,
fails with a clear, actionable error **without** modifying the wallet) instead of
silently deriving the default path and showing a zero balance. The seed is set
and the keypool regenerated as an atomic final step, so a failed call never
leaves a half-applied wallet.

### Additional hardening

- **DSProof deserialization**: the `pushData` element count is now validated
  before bulk allocation, removing a ~24× transient memory amplification from a
  crafted proof message.
- **REST interface authentication**: a new `-restauth` option requires the same
  credentials as JSON-RPC for the `-rest` HTTP interface. It defaults to **off**
  to preserve compatibility for existing credential-less REST consumers; operators
  exposing the RPC port beyond loopback should set `-restauth=1` (a startup
  warning is logged otherwise).
- **`-persistdiscouraged`** is now honored (previously a no-op).
- **RPC cookie** file is created `0600` before the secret is written (no
  world/group-readable window under `-sysperms`).
- **`dumpwallet`/`importwallet`** open with `O_NOFOLLOW`/`O_EXCL` and write
  through the created descriptor, closing symlink/TOCTOU and reopen-truncation
  windows.
- **`CFeeRate`** fee/feerate math is computed in 128-bit and clamped, removing
  signed-overflow UB at extreme (economically-unreachable) fee values;
  prioritisation deltas are clamped to `MoneyRange`.
- **Defense-in-depth**: a `MoneyRange` guard on block-template fee accumulation,
  and an opcode-cost charge for `OP_AND`/`OP_OR`/`OP_XOR`/`OP_INVERT`.

### Documentation / non-code

- Clarified (in code comments) that the node does **not** enforce input-reference
  *conservation* for non-singleton refs — a colored-coin covenant must enforce
  its own supply via `OP_REFOUTPUTCOUNT_*`. Singleton uniqueness *is* enforced by
  consensus. Corrected the `OP_STATESEPARATOR` push-only-prefix comment to match
  actual behavior.

## Known limitations / deferred

- **Authenticated wallet encryption (encrypt-then-MAC)** remains deferred. It
  cannot be added backward-compatibly without a versioned `walletdb` record
  format (the current `ckey` blob has no version field), which is out of scope
  for a point release; shipping it in-band would risk misreading existing wallets.
  Tamper-detection continues to rely on public-key verification on decrypt.

---

## Compatibility

- **No new consensus rules.** Block validation is byte-for-byte identical to
  3.1.0 until the existing `SCRIPT_SECURITY_UPGRADE` activation at mainnet block
  444,444. The relay memory-budget guard is mempool policy only and never affects
  block validity.
- **No breaking changes** for existing deployments. The `Host`-header allowlist
  from 3.1.0 still applies (set `-rpcallowhost` as before). `-restauth` defaults
  off, so existing `-rest` consumers are unaffected.

## Credits

2026-06 Radiant Core audit follow-up (re-review + money-layer / reference-system
deep dive), remediation, and regtest validation.
