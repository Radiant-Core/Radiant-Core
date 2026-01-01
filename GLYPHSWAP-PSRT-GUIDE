# GLYPHSWAP / PSRT GUIDE

## Purpose
This document describes how **Glyph Swap Protocol (PSRT)** “swap offers” are **broadcast on-chain** in Radiant Core, how they are indexed by `SwapIndex`, and how clients should construct and discover offers.

In Radiant Core, swap offers are not gossiped via a special P2P message. They are broadcast as a **standard transaction** containing an `OP_RETURN` output with protocol tag `RSWP` and a structured, multi-push payload.

## Quick Glossary
- **Maker**: creates and broadcasts a swap advertisement transaction.
- **Taker**: finds a maker’s offer and spends the maker’s offered UTXO according to the off-chain/on-chain rules.
- **Offer / Order**: a single on-chain swap advertisement.
- **Token Ref / Token ID**: 32-byte identifier used to group offers (passed to RPC as `token_ref`).
- **Offered UTXO**: the UTXO the maker is offering; offer “completion/cancel” is detected when that UTXO is spent.

## Node Requirements
### Indexing / Querying
To query offers via RPC, the node must run with:
- `-swapindex=1`

Optional tuning:
- `-swapcache=<n>`: swap index cache in **MiB** (default 10 MiB)
- `-swaphistoryblocks=<n>`: retain spent/cancelled offers for **n blocks** (default 10000). Use `0` to keep all history.

### OP_RETURN relay policy (important)
Swap advertisements must be **standard** and relayable.

Radiant Core enforces a max total size of all `OP_RETURN` output scripts per transaction:
- Config: `-datacarriersize=<n>`
- Default: `1024` bytes

This limit counts the **entire output script length**, not just your payload bytes.

If your advertisement’s `OP_RETURN` script is too large, it will be rejected by policy with reason `oversize-op-return`.

## How offers are broadcast
### Broadcasting overview
A maker broadcasts a swap offer by publishing a transaction that includes an `OP_RETURN` output with the `RSWP` multi-push payload.

That transaction is:
- **relayed through the mempool** like any other transaction (subject to policy), and
- **indexed when confirmed in a block** by nodes with `-swapindex=1`.

There is no additional broadcast step beyond broadcasting the transaction.

## Photonic Wallet implementation notes (important)
Photonic Wallet’s “swap” implementation is a **PSRT-style partially signed transaction workflow**, but it is **not** currently using the on-chain `RSWP` `OP_RETURN` advertisements (and it does not call `getopenorders`/`getswaphistory`).

Instead:
- The maker constructs a *partially signed transaction hex* and shares it **out-of-band** (copy/paste / message / etc.).
- The taker loads that tx hex and completes/funds it, then broadcasts the final transaction.

### Photonic’s “PSRT” transaction format (as implemented)
In Photonic Wallet, the shared “PSRT” is effectively a transaction with:
- Exactly **1 input**
- Exactly **1 output**

The input spends the maker’s offered UTXO. The output is the maker’s requested payment output.

The maker signs with ECDSA using sighash flags:
- `SIGHASH_SINGLE | SIGHASH_ANYONECANPAY | SIGHASH_FORKID`

Practical meaning:
- The maker’s signature commits to **only their input** and the **corresponding output index** (index 0).
- A taker can add additional inputs/outputs (e.g. funding inputs, change outputs, and the “payout to taker” output) without invalidating the maker’s signature.

### Photonic maker procedure (high level)
Photonic first “reserves” the asset being offered by moving it to a dedicated **swap address** (derived from a separate HD path). That reserved UTXO becomes the offered UTXO.

Then the maker creates the partially-signed tx (“PSRT”) that spends that reserved UTXO using the sighash flags above and includes the maker’s requested payment output as output 0.

### Photonic taker procedure (high level)
The taker loads the maker’s partially-signed tx hex and then builds a final transaction that:
- Uses the maker’s offered UTXO as input 0 (with maker’s existing scriptSig preserved)
- Keeps the maker’s requested payment output as output 0
- Adds output 1 paying the offered asset to the taker
- Adds whatever additional inputs are needed to fund the payment + fees, plus change outputs

### Key takeaway
Photonic Wallet’s “swap broadcast” is **broadcasting the final spend transaction**, not broadcasting an on-chain `RSWP` advertisement. Discovery is currently out-of-band.

## Swap advertisement payload format (RSWP)
Radiant Core’s `SwapIndex` parses swap advertisements from an `OP_RETURN` script that contains **multiple pushes**.

### Script structure
Two formats exist:
- **Legacy v1** (kept for compatibility with existing indexers)
- **Recommended v2** (required for full-featured swaps: want token indexing + multi-push multi-output terms)

#### Legacy v1 (version = 1)
The legacy structure is:

1. `OP_RETURN`
2. push bytes: ASCII `RSWP` (4 bytes)
3. push bytes: `version` (exactly 1 byte)
4. push bytes: `type` (exactly 1 byte)
5. push bytes: `tokenID` (exactly 32 bytes)
6. push bytes: `offeredUTXOHash` (exactly 32 bytes)
7. push bytes: `offeredUTXOIndex` (ScriptNum; minimally-encoded integer, up to 4 bytes, OR OP_N for 0..16)
8. push bytes: `priceTerms` (arbitrary bytes)
9. push bytes: `signature` (arbitrary bytes)

#### Recommended v2 (version = 2)
The v2 structure is:

1. `OP_RETURN`
2. push bytes: ASCII `RSWP` (4 bytes)
3. push bytes: `version` (1 byte, value `0x02`)
4. push bytes: `flags` (1 byte)
5. push bytes: `offered_type` (1 byte)
6. push bytes: `terms_type` (1 byte)
7. push bytes: `tokenID` (32 bytes)
8. push bytes: `wantTokenID` (32 bytes) **only if** `flags & 0x01` is set
9. push bytes: `offeredUTXOHash` (32 bytes)
10. push bytes: `offeredUTXOIndex` (ScriptNum; minimally-encoded integer, up to 4 bytes, OR OP_N for 0..16)
11+. push bytes: one or more `terms_part` pushes
last push: `signature`

Notes:
- `terms_part` pushes are **concatenated** by the node into `price_terms`.
- There MUST be at least **2 pushes** after `offeredUTXOIndex` (at least one `terms_part` plus the final `signature`).

### Field meanings
#### v2 header fields
- `version`: protocol version.
- `flags`:
  - bit `0` (`0x01`): `wantTokenID` is present.
- `offered_type`: offered asset class (recommended values):
  - `0`: unknown/unspecified
  - `1`: RXD
  - `2`: Fungible token (FT)
  - `3`: Non-fungible token (NFT)
- `terms_type`: the encoding of `priceTerms` (recommended values):
  - `1`: `MultiTxOutV1` (defined below)

#### Identifiers
- `tokenID`: identifier used to group offers by **what is being offered**.
- `wantTokenID`: identifier used to group offers by **what the maker wants to receive**.

#### UTXO
- `offeredUTXOHash` / `offeredUTXOIndex`: identifies the offered UTXO; offer is considered active until this outpoint is spent.

#### Opaque blobs (node stores/returns but does not interpret)
- `priceTerms`: concatenation of `terms_part` pushes.
- `signature`: final push bytes.

Important: Radiant Core **does not interpret** `priceTerms` or `signature` beyond storing them. Any semantics and validation happen at the application/protocol layer.

## Canonical encoding for self-contained swaps (recommended)

### tokenID / wantTokenID mapping
To align with Photonic’s semantics:

- For a token identified by an outpoint-like `ref` (36 bytes = `txid(32) || vout(4)` in display-hex byte order), define:
  - `tokenID = sha256(ref_bytes)`.

This matches Photonic’s `Outpoint.refHash()` behavior.

For RXD (no token ref), you need a stable convention. Recommended simple conventions:
- **RXD-as-null**: use `000...000` (32 bytes of zero).
- Or define an explicit constant (SDK-level) like `sha256("RXD")` and document it.

### terms_type = MultiTxOutV1 (recommended)
`priceTerms` encodes the exact outputs that the maker signature commits to.

Encoding:
- `nOutputs`: CompactSize/varint
- Repeat `nOutputs` times:
  - `value`: 8 bytes little-endian (satoshis)
  - `scriptPubKey`: CompactSize/varbytes

Wallet rule for Photonic-style PSRT:
- Maker uses `SIGHASH_SINGLE | SIGHASH_ANYONECANPAY | SIGHASH_FORKID` and MUST ensure that the output at index `i` is the output committed by the signature for input `i`.
- In the simple “one maker input” case, maker commits to output index `0`.

Multi-output terms in v2 allow future variants where a maker can commit to multiple outputs (e.g. fees, royalties, multiple recipients) while still being self-contained.

### signature encoding (recommended)
For self-contained fulfillment, `signature` SHOULD be the full unlocking script bytes required to spend the offered UTXO input.

Example for P2PKH:
- `signature = scriptSigBytes = PUSH(<DER_sig || sighashType>) PUSH(<pubkey>)`

Clients MUST use the sighash flags:
- `SIGHASH_SINGLE | SIGHASH_ANYONECANPAY | SIGHASH_FORKID`

## New RPCs for want-token discovery
When v2 offers include `wantTokenID`, Radiant Core provides secondary indexing so takers can discover by what they want to pay:

- `getopenordersbywant <want_token_ref> [limit] [offset]`
- `getswaphistorybywant <want_token_ref> [limit] [offset]`
- `getswapcountbywant <want_token_ref>`

### Multi-push requirement
Many wallet/RPC helpers can only create `OP_RETURN <single-push>` outputs (for example `createrawtransaction` “data” output creates one push).

**SwapIndex expects multiple pushes** as listed above.

If you are building transactions using RPC, you will likely need to:
- build the raw transaction manually using a library that can compose a script, or
- construct the tx hex, append the `OP_RETURN` output script, then sign it.

(See `test/functional/feature_swap.py` for an example of constructing a multi-push `OP_RETURN` script.)

## Byte order / endianness requirements
This is the most common integration pitfall.

### Token ID and TXIDs in scripts
RPC parameters like `token_ref` are provided as hex strings in “display order” (human-readable TXID form).

Internally, `uint256`/`TxId` values are stored little-endian. The `SwapIndex` parser does:
- `offer.tokenID = uint256(data)`
- `offer.offeredUTXOHash = uint256(data)`

Therefore, to make RPC querying line up with what you place in the script, your script should push TXID bytes in **little-endian (reversed) order**.

Practical rule:
- If you have a txid string like `"aabb..."` (64 hex chars), then the script push should be `bytes.fromhex(txid)[::-1]`.

This matches the functional test logic in `test/functional/feature_swap.py`.

### offeredUTXOIndex encoding
`offeredUTXOIndex` is parsed using ScriptNum semantics:
- Prefer a minimally-encoded ScriptNum push (1–4 bytes)
- For small values 0..16, you can use an `OP_N` encoding

In the functional test, the vout is pushed as a single byte for small indices.

## Indexing behavior (what the node does)
### Open vs history
The index stores:
- **Open offers** under prefix `o`
- **History (spent/cancelled)** under prefix `h`

Offers move from open to history when the offered UTXO is detected as spent in a connected block.

### Mempool-aware RPC behavior
- `getopenorders` filters out offers whose UTXO is spent in the mempool (so it won’t show an offer that is already being filled).
- `getswaphistory` returns indexed history entries, and also *adds* open offers that are spent in the mempool but not yet confirmed (so clients can see “just-filled” offers quickly).

### History retention / pruning
History entries are retained for `-swaphistoryblocks` blocks (default 10000), and then pruned.

## RPC workflow (discovery)
All RPCs require `-swapindex=1`.

### Get open orders
`getopenorders <token_ref> [limit] [offset] [max_age]`
- `limit` default 100, max 1000
- `offset` default 0
- `max_age` optional: Maximum age in blocks (null = no filter)

**Age filtering examples:**
- `getopenorders <token_ref> 100 0 720` - orders from last 6 hours (720 blocks)
- `getopenorders <token_ref> 100 0 1440` - orders from last 12 hours (1440 blocks)
- `getopenorders <token_ref> 100 0 0` - no orders (age = 0)

### Get open orders by wanted token
`getopenordersbywant <want_token_ref> [limit] [offset] [max_age]`
- Same parameters as `getopenorders`
- Useful for discovering orders based on what the maker wants to receive

### Get history
`getswaphistory <token_ref> [limit] [offset]`
- Same paging parameters
- Note: `max_age` not available for history (orders already spent/cancelled)

### Get counts (for pagination planning)
`getswapcount <token_ref>`
Returns:
- `open`: number of active offers
- `history`: number of history entries

### Get swap index status
`getswapindexinfo`
Returns index status and performance metrics:
- `enabled`: whether swap index is enabled
- `current_height`: current block height
- `total_orders`: total number of orders (open + history)
- `open_orders`: number of active orders
- `history_orders`: number of historical orders
- `history_blocks`: history retention period in blocks

## End-to-end procedure
### Maker procedure (broadcasting an offer)
1. **Choose / determine the `token_ref`** you are advertising under.
2. **Create the offered UTXO** (the outpoint you are offering).
3. **Construct the `OP_RETURN` payload** using the exact multi-push sequence above.
4. **Create the advertisement transaction**:
   - include inputs to pay fees
   - include the `OP_RETURN` output (0-value)
   - include change output(s)
5. **Sign and broadcast** the tx (`sendrawtransaction`).
6. Wait for confirmation; indexers will surface it as an open order.

### Taker procedure (filling an offer)
1. Discover offers using:
   - `getswapcount` (optional) to size paging
   - `getopenorders` to fetch offers, optionally with `max_age` to filter stale orders
   - `getopenordersbywant` to find orders for specific tokens you want to spend
2. Select an offer and interpret its `priceTerms`/`signature` using your protocol rules.
3. Construct and broadcast the fill transaction that spends `offeredUTXOHash:offeredUTXOIndex`.
4. After the spend is in mempool:
   - `getopenorders` will no longer show it
   - `getswaphistory` may show it immediately (mempool-aware)
5. After confirmation, the offer is moved to history in the index.

**Best practices for takers:**
- Use `max_age` parameter to filter out potentially stale orders (e.g., `max_age=1440` for 12 hours)
- Use `getswapindexinfo` to check index health and current block height
- Consider orders with very large `block_height` differences as potentially stale

## Canonical on-chain swap protocol (recommended)
This section specifies a **canonical** interpretation of the `RSWP` advertisement so that swaps can be:
- **fully self-contained** (taker can fill purely from chain data), and
- directly aligned with Photonic Wallet’s PSRT semantics.

The node still treats `priceTerms` and `signature` as opaque bytes, but this guide defines what clients SHOULD encode.

## Operational notes / best practices
- Keep the `OP_RETURN` script small (default relay limit is 1024 bytes).
- Use paging (`limit`/`offset`) for large token orderbooks.
- Use `getswapcount` before paginating through large datasets.
- For long-running indexers, configure `-swaphistoryblocks` based on your storage and analytics needs.
- **Order age filtering**: Use `max_age` parameter to improve UX by filtering stale orders:
  - 6 hours (720 blocks) for active trading
  - 12-24 hours (1440-2880 blocks) for general discovery
  - 0 for testing (returns no orders)
- **Performance monitoring**: Use `getswapindexinfo` to monitor:
  - Index health and synchronization status
  - Total system load (order counts)
  - History retention configuration

## Complete v2 advertisement example (hex)

This section provides a complete, annotated example of a v2 `RSWP` advertisement `OP_RETURN` script in hex format.

### Example scenario
- **Token ID** (what is being offered): `a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2` (32 bytes)
- **Want Token ID** (what maker wants): `f1e2d3c4b5a6f1e2d3c4b5a6f1e2d3c4b5a6f1e2d3c4b5a6f1e2d3c4b5a6f1e2` (32 bytes)
- **Offered UTXO**: `deadbeef...` txid at vout 0
- **Price terms**: `0108e0930400000000001976a914...` (MultiTxOutV1 encoding)
- **Signature**: Full scriptSig for offered input

### Hex breakdown

```
6a                              # OP_RETURN
04 52535750                     # PUSH 4 bytes: "RSWP" (protocol ID)
01 02                           # PUSH 1 byte: version = 2
01 01                           # PUSH 1 byte: flags = 0x01 (has wantTokenID)
01 00                           # PUSH 1 byte: offered_type = 0 (unspecified)
01 01                           # PUSH 1 byte: terms_type = 1 (MultiTxOutV1)
20 a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2
                                # PUSH 32 bytes: tokenID (little-endian)
20 f1e2d3c4b5a6f1e2d3c4b5a6f1e2d3c4b5a6f1e2d3c4b5a6f1e2d3c4b5a6f1e2
                                # PUSH 32 bytes: wantTokenID (little-endian)
20 efbeaddeefbeaddeefbeaddeefbeaddeefbeaddeefbeaddeefbeaddeefbeadde
                                # PUSH 32 bytes: offeredUTXOHash (little-endian)
00                              # OP_0: offeredUTXOIndex = 0
1a 0108e09304000000001976a914aabbccdd...
                                # PUSH 26 bytes: priceTerms (MultiTxOutV1)
47 3044022012345678...          # PUSH 71 bytes: signature (DER sig + pubkey)
```

### Full hex (concatenated)
```
6a0452535750010201010100012020a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b220f1e2d3c4b5a6f1e2d3c4b5a6f1e2d3c4b5a6f1e2d3c4b5a6f1e2d3c4b5a6f1e220efbeaddeefbeaddeefbeaddeefbeaddeefbeaddeefbeaddeefbeaddeefbeadde001a0108e09304000000001976a914aabbccdd...473044022012345678...
```

### Python construction example
```python
from test_framework.script import CScript, OP_RETURN

def build_rswp_v2_script(token_id_bytes, want_token_id_bytes, 
                         utxo_hash_bytes, utxo_index, 
                         price_terms, signature):
    """
    Build a v2 RSWP advertisement OP_RETURN script.
    
    All ID/hash bytes should be in little-endian (reversed from display hex).
    """
    return CScript([
        OP_RETURN,
        b"RSWP",                    # Protocol ID
        b'\x02',                    # Version = 2
        b'\x01',                    # Flags = 0x01 (has wantTokenID)
        b'\x00',                    # Offered type
        b'\x01',                    # Terms type (MultiTxOutV1)
        token_id_bytes,             # 32 bytes
        want_token_id_bytes,        # 32 bytes
        utxo_hash_bytes,            # 32 bytes
        bytes([utxo_index]) if utxo_index < 256 else encode_scriptnum(utxo_index),
        price_terms,                # Variable length
        signature                   # Variable length
    ])

# Example usage:
token_id = bytes.fromhex("a1b2c3d4...")[::-1]  # Reverse for little-endian
script = build_rswp_v2_script(token_id, want_token_id, utxo_hash, 0, terms, sig)
```

## RPC error codes reference

This section documents common RPC error codes and their meanings when interacting with swap RPCs.

### Error codes table

| Code | Constant | Description | Common Cause |
|------|----------|-------------|--------------|
| -1 | `RPC_MISC_ERROR` | Swap index not enabled | Node not started with `-swapindex=1` |
| -5 | `RPC_INVALID_ADDRESS_OR_KEY` | Invalid token reference | Malformed hex string for `token_ref` |
| -8 | `RPC_INVALID_PARAMETER` | Invalid parameter value | Negative `limit`, `offset`, or `max_age` value |
| -32600 | `RPC_INTERNAL_ERROR` | Internal index error | Database read failure |

### Common error messages

#### "Swap index not enabled"
```json
{
  "error": {
    "code": -1,
    "message": "Swap index not enabled. Use -swapindex=1 to enable."
  }
}
```
**Solution**: Restart the node with `-swapindex=1` flag.

#### "limit must be non-negative"
```json
{
  "error": {
    "code": -8,
    "message": "limit must be non-negative"
  }
}
```
**Solution**: Provide a non-negative integer for the `limit` parameter.

#### "offset must be non-negative"
```json
{
  "error": {
    "code": -8,
    "message": "offset must be non-negative"
  }
}
```
**Solution**: Provide a non-negative integer for the `offset` parameter.

#### "max_age must be non-negative"
```json
{
  "error": {
    "code": -8,
    "message": "max_age must be non-negative"
  }
}
```
**Solution**: Provide a non-negative integer or omit the `max_age` parameter for no age filtering.

#### Invalid token_ref format
```json
{
  "error": {
    "code": -5,
    "message": "token_ref must be hexadecimal string (not '...')"
  }
}
```
**Solution**: Provide a valid 64-character hex string for the token reference.

## Troubleshooting

### oversize-op-return rejection

**Symptom**: Transaction rejected with error:
```
error code: -26
error message: 64: oversize-op-return
```

**Cause**: The total size of all `OP_RETURN` output scripts in the transaction exceeds the `-datacarriersize` limit (default: 1024 bytes).

**Solutions**:

1. **Reduce payload size**
   - Use shorter `priceTerms` encoding
   - Minimize signature size (use compressed pubkeys)
   - Avoid unnecessary padding

2. **Increase node limit** (if you control the node)
   ```bash
   radiantd -datacarriersize=2048
   ```
   Note: This only affects your node's relay policy. Other nodes with default settings will still reject the transaction.

3. **Check script size before broadcast**
   ```python
   script = build_rswp_v2_script(...)
   if len(script) > 1024:
       print(f"Warning: Script is {len(script)} bytes, exceeds default limit")
   ```

### Typical v2 advertisement sizes

| Component | Size (bytes) |
|-----------|-------------|
| OP_RETURN | 1 |
| "RSWP" push | 5 (1 + 4) |
| Version | 2 (1 + 1) |
| Flags | 2 (1 + 1) |
| Offered type | 2 (1 + 1) |
| Terms type | 2 (1 + 1) |
| TokenID | 33 (1 + 32) |
| WantTokenID | 33 (1 + 32) |
| UTXO hash | 33 (1 + 32) |
| UTXO index | 1-5 |
| **Header total** | **~114 bytes** |

Remaining budget for `priceTerms` + `signature`: **~910 bytes**

A typical P2PKH signature is ~107 bytes, leaving **~800 bytes** for `priceTerms`.

### Order not appearing in getopenorders

**Possible causes**:

1. **Transaction not confirmed**: Orders are indexed when the block is connected. Check that the advertisement transaction has confirmations.

2. **Wrong byte order for token_ref**: Ensure you're querying with the display-hex format of the token ID, not the internal little-endian bytes.

3. **UTXO already spent**: The offer's UTXO may have been spent (filled or cancelled). Check `getswaphistory` instead.

4. **Parsing failure**: The `OP_RETURN` script may not match the expected format. Verify:
   - Protocol ID is exactly "RSWP" (4 bytes)
   - Version byte is present and valid (1 or 2)
   - All required fields have correct sizes

5. **Index not synced**: The swap index may still be syncing. Check node logs for "Syncing swapindex" messages.

6. **Age filtering**: If using `max_age` parameter, orders older than the specified block count will be filtered out. Try without `max_age` or with a larger value.

### Order not appearing with max_age filter

**Possible causes**:

1. **Order too old**: The order's `block_height` may be older than the specified `max_age`. Check the order's age with `getopenorders` without `max_age` first.

2. **Block height mismatch**: If the node's current height is very close to the order's `block_height`, age calculation may filter it out unexpectedly.

**Debugging steps**:
```bash
# Check current block height and order age
radiant-cli getswapindexinfo
radiant-cli getopenorders <token_ref>  # Without max_age to see all orders
radiant-cli getopenorders <token_ref> 100 0 10000  # With large max_age to include old orders
```

### Order stuck in open after being filled

**Possible causes**:

1. **Spend transaction not confirmed**: The order moves to history only when the spend is confirmed in a block.

2. **Reorg occurred**: If the block containing the spend was orphaned, the order returns to open status.

**Debugging steps**:
```bash
# Check if UTXO is spent
radiant-cli gettxout <utxo_txid> <utxo_vout>
# Returns null if spent

# Check mempool for pending spend
radiant-cli getmempoolentry <spend_txid>
```

### History entries missing

**Possible causes**:

1. **Pruning**: History entries are pruned after `-swaphistoryblocks` blocks (default: 10000). For longer retention, increase this value or set to 0 for unlimited.

2. **Recent spend**: The spend may still be in mempool. `getswaphistory` includes mempool-spent offers, but with a slight delay.

### Reorg handling

The swap index handles chain reorganizations automatically:
- When a block is disconnected, orders that were moved to history are restored to open status
- Advertisements indexed in disconnected blocks are removed
- No manual intervention is required

To verify reorg handling is working, check debug.log for:
```
SwapIndex: Processed disconnected block, restored N orders, removed M ads
```
