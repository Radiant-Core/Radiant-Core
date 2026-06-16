# Radiant Core v3.1.2 — operator advisory (TL;DR)

**What it is:** a network-stability release that fixes the root cause of the
2026-06-15 mainnet partition. **No consensus change.** The block-440,000 soft
fork is unchanged, so v3.1.2 is fully compatible with v3.1.1.

**Why you should upgrade (now):**
- On 2026-06-15 a routine orphan race (fork base block 438,203) was turned into a
  *persistent* network split by the old deep-reorg-protection defaults: nodes
  auto-finalized after only 6 blocks, rejected the heavier canonical chain, and
  then **banned the peers relaying it** — so a stranded node self-isolated. v3.1.2
  makes that impossible while keeping genuine deep-reorg (51%/double-spend)
  protection.
- You already have to be on v3.1.1's rules before **block 440,000 (~2026-06-21)**.
  Upgrading to v3.1.2 satisfies that deadline *and* gets you the partition fix in
  one step.

**What changed (all local policy defaults — no fork risk):**
| | old | new |
|---|---|---|
| `finalizeheaderspenalty` (peer ban score for a finalization disagreement) | 100 (= ban threshold) | **0** (never bans) |
| `maxreorgdepth` (auto-finalization depth) | 6 | **69** (~5.75 h; only deep, attack-grade reorgs) |
| `finalizeheaders` (deep-reorg backstop) | on | **on** (unchanged) |
| mainnet checkpoint | …412,000 | **+ 438,204** (pins the canonical chain) |
| `SCRIPT_SECURITY_UPGRADE` activation | block 440,000 | **440,000** (unchanged) |

**Action required:**
- **Node operators:** upgrade to v3.1.2 before block 440,000. No config change
  needed — the new defaults apply automatically. If you added
  `maxreorgdepth`/`finalizeheaders*` workarounds during the incident you can drop
  them.
- **Can't upgrade yet?** As an interim fix on a v3.1.x node, set
  `maxreorgdepth=69` and `finalizeheaderspenalty=0` in `radiant.conf`.
- **Indexer operators (RXinDexer / ElectrumX):** the node's `maxreorgdepth` and the
  indexer's `REORG_LIMIT` **must stay matched (indexer ≥ node)**. The Radiant coin
  default is now `REORG_LIMIT = 69`; **delete any stale `REORG_LIMIT=6`** from your
  `.env`/compose (an env override beats the code default and would force a full
  resync on a deep reorg). The `Dockerfile.radiantd` build pin is now `v3.1.2`.
  Note: the bundled full-stack compose intentionally runs the indexer-backing node
  with a *wider* window (`-maxreorgdepth=100 -finalizeheaders=0` + env
  `REORG_LIMIT=100`) — also valid, since `100 ≥ 100`. The one rule to never break:
  keep `REORG_LIMIT ≥ the node's maxreorgdepth`; don't pair a 69 indexer with a 100
  node (or vice-versa).
- **Still stranded from the incident?** The checkpoint stops *new* syncs from
  adopting the dead fork but does not auto-rewind an already-stranded node. Set
  `finalizeheaders=0` + `parkdeepreorg=0`, restart, then
  `reconsiderblock <canonical-tip-hash>` (and `unparkblock` if only parked).

**Verify the download:** check the release `SHA256SUMS.txt` against its detached
signature `SHA256SUMS.txt.asc`, signed by the Radiant Core release key
`C605C872 AF05 6272 CE65 0E69 9D24 80A9 7B05 F3B4`.
