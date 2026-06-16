# 📢 Radiant Core v3.1.2 — please upgrade

**v3.1.2 fixes the root cause of the June 15 network split and is a recommended
upgrade for every node operator.** It is **not** a consensus change — it is fully
compatible with v3.1.1, and the block-440,000 upgrade deadline is unchanged.

## What happened on June 15

A perfectly normal *orphan race* — a brief tie between two valid blocks during a
low-hashrate stretch — got turned into a **persistent network split** by
overly-aggressive deep-reorg defaults:

- Nodes auto-**finalized** a block after only **6** confirmations, declared it
  irreversible, and then **rejected the heavier, real chain** that the rest of the
  network was building.
- Worse, the "penalty" for a peer announcing that real chain was set equal to the
  ban score — so a stranded node **banned the very peers feeding it the canonical
  chain**, cut itself off, and the split stuck.

No attacker was involved. The defaults did it to themselves.

## What v3.1.2 changes (all local policy — no fork)

| | before | after |
|---|---|---|
| Ban score for a "finalization disagreement" | 100 (= ban threshold) | **0** — never bans a peer |
| Auto-finalization depth | 6 blocks | **69 blocks** (~5.75 h) — only deep, attack-grade reorgs |
| Deep-reorg / 51% backstop (`finalizeheaders`) | on | **on** (kept) |
| Canonical-chain checkpoint | …412,000 | **+ 438,204** (pins the real chain) |
| Block-440,000 soft fork | block 440,000 | **440,000** (unchanged) |

A normal orphan race now resolves by most-work like it should, and a node can no
longer isolate itself. Genuine deep-reorg protection is **retained** — finalization
stays on (just at attack-grade depth), and `parkdeepreorg` still resists deep
reorgs reversibly.

## Why upgrade now

You already have to be on v3.1.1's rules before **block 440,000 (~June 21)**.
v3.1.2 gets you there **and** immunizes your node against the partition — one
upgrade, both problems solved.

- **Node operators:** upgrade to v3.1.2. No config change needed; the new defaults
  apply automatically. Can't upgrade yet? Set `maxreorgdepth=69` and
  `finalizeheaderspenalty=0` in `radiant.conf` as an interim fix.
- **Indexer operators (RXinDexer / ElectrumX):** raise `REORG_LIMIT` to **69** to
  match the node (indexer ≥ node) and **delete any stale `REORG_LIMIT=6`** override.
- **Still stranded from June 15?** The checkpoint stops *new* syncs from adopting
  the dead fork but won't auto-rewind an already-stuck node. Set
  `finalizeheaders=0` + `parkdeepreorg=0`, restart, then
  `reconsiderblock <canonical-tip-hash>`. Full runbook in the release notes.

## Verify your download

Check `SHA256SUMS.txt` against its signature `SHA256SUMS.txt.asc`, signed by the
Radiant Core release key:

```
C605C872 AF05 6272 CE65 0E69  9D24 80A9 7B05 F3B4
Radiant Foundation (Release Signing) <art@radiantfoundation.org>
```

📄 Full release notes & operator runbook are attached to the GitHub release.
🔬 Shipped after a multi-agent adversarial design + security review (verdict: SHIP)
and a full unit + functional test pass.
