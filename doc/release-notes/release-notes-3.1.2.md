# Radiant Core 3.1.2 Release Notes

**Release Date**: June 2026
**Release Type**: Network-stability release (deep-reorg / finalization hardening)
**Consensus Activation**: unchanged — `SCRIPT_SECURITY_UPGRADE` still activates at
mainnet block **440,000** (testnet/scalenet block 1, regtest from genesis)
**Git Tag**: v3.1.2

> **⚠️ Recommended upgrade for ALL mainnet node operators.** 3.1.2 changes the
> default deep-reorg-protection behavior so that an ordinary orphan race can
> **never again silently partition the network**, while keeping genuine
> deep-reorg (51% / double-spend) protection. It does **not** change consensus
> rules and does **not** move the block-440,000 soft-fork — so 3.1.2 is fully
> compatible with 3.1.1 at the consensus layer. Because every mainnet node must
> already be on 3.1.1's rules before block 440,000 (~2026-06-21), upgrade
> straight to 3.1.2 and get the partition fix at the same time.
>
> **Indexer operators (RXinDexer / ElectrumX): see "Action required" below — you
> must also raise `REORG_LIMIT` to 69 and clear any stale `REORG_LIMIT=6`
> override**, or a deep reorg can force a full indexer resync.

---

## Background — the 2026-06-15 mainnet partition

On 2026-06-15 a routine, greater-than-6-block orphan race on mainnet (fork base
block **438,203**, diverging at 438,204) — the kind of thing that happens during
low-hashrate / difficulty-instability periods — was escalated into a
**persistent network partition** by the v3.x deep-reorg-protection *defaults*.
The root mechanics (all in the validation/networking layer, none of them a
consensus rule):

- **`DEFAULT_MAX_REORG_DEPTH = 6`** auto-finalized a branch after any orphan race
  deeper than 6 blocks.
- Once a node finalized a block, the heavier competing (canonical) chain that
  forked below it was marked **invalid**, and that chain's headers were rejected
  — the most-work rule was effectively disabled below the finalized block.
- **`DEFAULT_FINALIZE_HEADERS_PENALTY = 100`** equalled the ban threshold
  (`DEFAULT_BANSCORE_THRESHOLD = 100`), so a **single** honest announcement of the
  canonical chain's headers would discourage/disconnect the peer that sent it.
  A stranded node therefore **self-isolated** from exactly the peers that could
  have healed it, entrenching the split.

The net effect: 3.1.x nodes stranded around height 438,216, while 2.1.x nodes
(which do not auto-finalize) followed most-work to the canonical tip. Recovery
required manual `reconsiderblock` plus disabling finalization. The canonical
chain was verified fully valid under 3.1.1 — this was a *policy* split, not a
consensus disagreement.

---

## What changed

### 1. A finalization disagreement no longer bans peers (`-finalizeheaderspenalty` default 100 → 0)

This is the load-bearing fix. Finalization is a **per-node, timing-dependent
heuristic**: which block a node finalizes depends on which chain it happened to
extend past `maxreorgdepth` and when, so two honest nodes can finalize different
blocks. Penalizing a peer for announcing the objectively heavier most-work chain
is therefore wrong — and with the old default equal to the ban threshold, it was
catastrophic, self-isolating stranded nodes.

3.1.2 sets the default penalty to **0**. A header that forks below the locally
finalized block is **still rejected** (the deep-reorg protection is fully
retained), but `Misbehaving()` is a no-op at score 0, so the announcing peer's
reputation is untouched and the node keeps its honest connections. The moment an
operator clears finalization, the node re-converges over those same peers.
Generic header-spam DoS protection is unaffected (it is handled by independent,
unchanged paths). A compile-time invariant now enforces
`penalty < ban threshold` so this can never silently regress.

Operators who deliberately relied on the old discouraging behavior can restore it
with `-finalizeheaderspenalty=<n>` (range 0–100), but this is not recommended.

### 2. Auto-finalization only fires on attack-grade reorgs (`-maxreorgdepth` default 6 → 69)

Finalization at depth 6 fired on ordinary orphan races. 3.1.2 raises the default
to **69 blocks** (~5.75 hours at 5-minute spacing). Natural races observed during
the incident were 13–28 blocks deep; 69 sits comfortably above any plausible race
(~2.5× the worst observed) but well within "this is an attack or a catastrophic
netsplit" territory — exactly where the irreversible backstop is wanted. Shallower
reorgs continue to resolve by most-work, and deep reorgs below this depth are
still resisted *reversibly* by `parkdeepreorg` (which requires ~2× post-fork work
to switch), so an ordinary race can no longer strand a node.

### 3. Deep-reorg protection is retained (`-finalizeheaders` default stays `true`)

3.1.2 keeps auto-finalization **enabled** by default. With changes (1) and (2) it
now only triggers on a >69-block reorg and never bans peers, so the incident
cannot recur — but the irreversibility backstop against a sustained 51% /
deep-double-spend reorg is preserved. (Operators who prefer to always follow
most-work — e.g. an indexer-backing node that must never stall — can still set
`-finalizeheaders=0`, optionally with `-parkdeepreorg=0`. Whether to make `false`
the network-wide default is being evaluated for a future release after testnet
soak; it is a deliberate hardening decision, not an emergency one.)

### 4. Canonical-chain checkpoint at block 438,204

The mainnet checkpoint set now pins block **438,204**
(`00000000000000660f27b62d38e4e55d74fb253f5845697b268233ccbe78529d`) — the first
block of the canonical chain above the fork base. Because the abandoned minority
fork has a *different* block at that height, the checkpoint makes that fork
permanently unreachable: a fresh sync can never adopt it. `defaultAssumeValid`
and `nMinimumChainWork` are bumped to the same block (the latter monotonically
increasing), giving defense-in-depth against a low-work eclipse of a new node.
These are standard anti-fork measures and do not affect normal operation.

> **Note:** the checkpoint stops *new* syncs from adopting the dead fork; it does
> **not** automatically rewind a node that is *already* stranded on the minority
> tip. Such a node still needs the recovery runbook below.

### 5. Soft-fork activation height unchanged (block 440,000)

`SCRIPT_SECURITY_UPGRADE` still activates at mainnet block **440,000**. Moving it
would create a window in which 3.1.1 nodes (already deployed expecting 440,000)
enforce the tightened script rules while 3.1.2 nodes do not — a heterogeneous
*rules* divergence, the exact fork class this release exists to prevent. The
partition fixes above take effect immediately on upgrade (they are runtime/policy
defaults, not height-gated), so partition recovery is entirely independent of the
soft fork.

---

## Is 3.1.2 fork-safe in a mixed-version network?

**Yes.** Every changed knob is consensus-*adjacent*: each code path it touches can
only make a node *reject* / *refuse-to-reorg* (stricter), never *accept* a block
that stricter nodes reject. A more-permissive node therefore always converges
toward the same most-work valid chain everyone else holds — it can never be the
node that splits off. In a network running 2.1.x / 3.1.0 / 3.1.1 / 3.1.2:

- A 3.1.2 node is *more* willing to follow most-work than 3.1.0/3.1.1 (depth 69
  vs 6, and it never bans over finalization), and at least as willing as 2.1.x for
  any reorg shallower than 69 blocks.
- The one directional change — the 438,204 checkpoint — can only reject a chain
  that forks at/below 438,204 with a *different* hash, i.e. the **already-abandoned
  minority fork**. The canonical chain's block at 438,204 *is* the checkpointed
  hash, so 3.1.2 never rejects a chain the rest of the network accepts.

The only residual divergence is a *temporary liveness stall* on an un-upgraded
(stricter) 3.1.x node during a >6-block reorg — i.e. the very symptom being fixed
— recoverable with the runbook below.

---

## Action required

### All mainnet node operators
- **Upgrade to 3.1.2 before block 440,000** (~2026-06-21). This satisfies both the
  partition fix and the mandatory 440,000 soft-fork upgrade in one step.
- No configuration change is required to get the fix — the new defaults apply
  automatically. If you previously added `-maxreorgdepth` / `-finalizeheaders` /
  `-finalizeheaderspenalty` / `-parkdeepreorg` to your `radiant.conf` as a manual
  workaround during the incident, you can remove them (or keep them; they remain
  valid overrides).

### Operators who cannot upgrade immediately
As an interim workaround on an un-upgraded 3.1.x node, set in `radiant.conf`:
```
maxreorgdepth=69
finalizeheaderspenalty=0
```
This reproduces the 3.1.2 defaults and prevents self-isolation until you upgrade.

### Indexer operators (RXinDexer / ElectrumX) — important
The node's `maxreorgdepth` and the indexer's `REORG_LIMIT` **must stay matched
(indexer ≥ node)**, because the indexer keeps block-undo data only for the last
`REORG_LIMIT` blocks. If the node reorgs deeper than the indexer can undo, the
indexer raises `ChainError` and needs a **full resync**.

- The bundled RXinDexer Radiant mainnet coin default is raised to
  **`REORG_LIMIT = 69`** to match this release.
- **Clear any stale explicit `REORG_LIMIT=6`** in your `.env` / compose /
  environment — an env override beats the code default and would re-introduce the
  resync risk on a deep reorg.
- Be aware that a one-time >6-block reorg during the upgrade window may force a
  resync on any indexer not yet bumped to 69.
- The `Dockerfile.radiantd` build pin is bumped to `v3.1.2`.

---

## Recovery runbook (for a node still stranded from the incident)

A node already stranded on a minority tip (e.g. ~438,216) is not auto-healed by
the checkpoint. To recover:

1. In `radiant.conf` set `finalizeheaders=0` and `parkdeepreorg=0` (and optionally
   `maxreorgdepth=69`), then restart.
2. `radiant-cli reconsiderblock <canonical-tip-hash>` (and/or
   `unparkblock <canonical-tip-hash>` if only parked, not finalized).
3. Confirm `getbestblockhash` / `getchaintips` now follow the canonical chain and
   the "network does not appear to fully agree" warning clears.
4. For an indexer that was stranded deeper than its old `REORG_LIMIT`, a one-time
   resync past the fork is required.

The escape-hatch flags (`-maxreorgdepth=-1` to follow most-work unconditionally,
`-finalizeheaders=0`, `-parkdeepreorg=0`) remain available for operators who want
a pure most-work policy.

---

## Compatibility

- **No consensus change.** Block/transaction validity is identical to 3.1.1. The
  `SCRIPT_SECURITY_UPGRADE` soft fork still activates at block 440,000.
- All changes are local network-policy defaults; they only *relax* strictness
  toward following most-work, so they cannot create a new fork (see fork-safety
  above).
- The checkpoint-mismatch rejection deliberately still applies a DoS score of 100
  (checkpoints *are* network-wide consensus, unlike per-node finalization) — the
  penalty change in (1) intentionally does not touch it.
- The 3.1.0 `Host`-header allowlist and the 3.1.1 `-restauth` default-on and
  authenticated-wallet-encryption changes all still apply.

## Credits

2026-06-15 deep-reorg partition incident response: root-cause analysis, default
re-tuning, multi-agent adversarial design + security review, and unit/functional
test coverage for the changed finalization behavior.
