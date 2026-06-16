# Why Radiant v3.1.2 makes the network *harder* to attack — by doing less

On June 15, 2026, the Radiant mainnet did something alarming: a routine orphan
race split the network into stranded islands of nodes, each convinced it was on
the right chain. No attacker was involved. The defaults did it to themselves.

Radiant Core v3.1.2 fixes this. And the interesting part — the part worth
understanding if you care about Radiant's security — is that it does so by making
each node **less aggressive**, not more. v3.1.2 ends up with *better* 51%
resistance and far more robust stability precisely because it removes a mechanism
that *felt* like protection but was actually a liability. Here's why.

## The intuition trap: "stronger finality = more security"

The setting at the heart of the incident is **auto-finalization**. Below a certain
depth (`maxreorgdepth`), a node declares a block *irreversible* and refuses to
reorg past it — even onto a chain with more accumulated work. The old default was
**6 blocks**.

It's tempting to read "irreversible after 6 blocks" as a *strong* guarantee: fast,
hard finality, surely good against a 51% attacker trying to rewrite history. But
finality that a single node decides on its own, after only 6 blocks, on a
timing-dependent heuristic, is not strength. It's brittleness. Two honest nodes
that saw blocks in a slightly different order will finalize *different* blocks —
and then each rejects the other's chain. That is exactly what happened: nodes
finalized a minority branch, rejected the heavier canonical chain as "invalid,"
and — because the finalization penalty equalled the peer-ban threshold — **banned
the honest peers** trying to feed them the real chain. A stranded node sealed
itself off. The split entrenched with no attacker lifting a finger.

A protection that fragments the honest network on a *non-event* is not protecting
you against a 51% attacker. It's doing the attacker's job for free.

## What actually defends Radiant against deep reorgs

Auto-finalization was never Radiant's real deep-reorg defense. Two other
mechanisms do the heavy lifting, and **v3.1.2 keeps both of them unchanged:**

1. **`parkdeepreorg` — reversible, work-weighted resistance.** When a competing
   chain would rewind more than a block, the node *parks* it and only switches once
   that chain proves it has roughly **2× the honest network's work since the fork**.
   This operates across the entire shallow-to-deep range. To flip a node, an
   attacker doesn't just need a bare majority — they need to out-produce the honest
   chain *two-to-one*. And because parking is **reversible**, a mistake heals
   itself; it never strands a node on a dead chain.

2. **Economic finality — the confirmations you already require.** The real reason a
   double-spend against an exchange is hard is that reversing *N* confirmations
   means out-mining *N* blocks of honest work, which costs more than the
   double-spend is worth. This is the same model Bitcoin has used safely for 15
   years. It is unchanged by v3.1.2.

Auto-finalization sat *on top* of these as an extra, **irreversible** wall. The
problem was never that the wall existed — it's that the wall was placed 6 blocks
from the tip, where ordinary orphan races trip it, and that crossing it banned
honest peers.

## What v3.1.2 actually changes

Three small default changes, all local policy — no consensus rules touched, no
fork:

- **The finalization penalty drops from 100 to 0.** A finalization disagreement is
  a per-node timing heuristic, not a network-wide truth, so a peer announcing the
  heavier canonical chain is *not* misbehaving — it's right. v3.1.2 still *rejects*
  the below-finalized header locally, but it never touches the peer's reputation.
  The self-isolation amplifier — the single thing that turned a brief split into a
  persistent one — is gone. A compile-time check now guarantees this penalty can
  never again be set to the ban threshold.

- **The finalization depth rises from 6 to 69 blocks (~5.75 hours).** Auto-
  finalization now fires only on genuinely deep, attack-grade reorgs — far beyond
  the 13–28-block races actually observed — so ordinary races simply resolve by
  most-work. The irreversible backstop still exists for a catastrophic >69-block
  rewrite; it's just no longer triggered by noise.

- **A checkpoint pins the canonical chain at block 438,204.** The abandoned
  minority fork is now permanently unreachable: a freshly syncing node can never
  adopt it.

## Why this is *better* 51% protection, not weaker

Here's the honest accounting, including the one case where the old setting "helped."

A 51% attacker's goal is to reverse confirmed transactions. v3.1.2 leaves the two
mechanisms that actually cost the attacker — `parkdeepreorg`'s 2×-work bar and your
confirmation requirement — **fully intact**. What it changes is *when an
irreversible wall slams down*, moving it from 6 blocks to 69.

Does a deeper wall give the attacker more room? In a narrow theoretical sense:
to overturn a transaction between 6 and 69 blocks deep, the old setting would have
refused the reorg outright, while v3.1.2 (after parking it and demanding 2× work)
could eventually accept it. But to get there the attacker needs **more than twice
the honest hashrate** — a regime in which the chain is already broken and in which
the *same attacker could simply trigger the cheap partition attack the old 6-block
setting enabled in the first place.* Refusing the reorg, meanwhile, meant the
honest node split from the network and showed phantom history. That's not a win.

So the trade is: give up a brittle, easily-misfiring, easily-weaponized
"protection" that fragments honest consensus — and keep the work-weighted,
reversible, economically-grounded defenses that a real attacker actually has to
beat. A network that *stays together* under stress is far harder to 51%-attack
than one that shatters into bannable islands. **Robustness is security.**

For perspective: Bitcoin and Bitcoin Cash ship with *no* irreversible
auto-finalization at all and rely entirely on most-work plus economic finality.
Radiant at depth 69 is still *more* conservative than that — it keeps a hard
backstop for catastrophic reorgs — while shedding the failure mode that bit us.

## The bottom line

v3.1.2 makes Radiant more secure and more stable by replacing eager, fragile,
self-inflicted "finality" with the defenses that actually hold up under an attack:

- **No more self-isolation.** A node can never again ban the peers carrying the
  real chain.
- **No more accidental splits.** Orphan races resolve by most-work; the irreversible
  wall only guards against attack-grade depth.
- **Deep-reorg / 51% protection retained.** `parkdeepreorg`'s 2×-work bar, your
  confirmations, and a hard backstop at 69 blocks all remain.
- **The dead fork is sealed off.** A checkpoint pins the canonical chain.

Stronger security didn't come from adding a bigger lock. It came from removing one
that was locking the network out of its own consensus.
