#!/usr/bin/env python3
# Copyright (c) 2026 The Radiant developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# =============================================================================
# scan-hash-opcode-usage.py  --  CRIT-1 pre-activation chain scanner
# =============================================================================
#
# WHY THIS EXISTS
# ---------------
# The `fix/security-audit-2026-06` branch tightens consensus around the Glyph
# v2 hash opcodes and the enhanced-reference opcodes:
#
#   * F1 / memory budget: OP_BLAKE3 (0xee) rejects an input stack element
#     larger than CBlake3::CHUNK_LEN = 1024 bytes; OP_K12 (0xef) rejects an
#     input >= CK12::MAX_INPUT_LEN = 8192 bytes (the audit specifically flagged
#     the *exactly 8192* boundary as previously off-spec).
#   * The enhanced-reference opcodes OP_REFHASHDATASUMMARY_UTXO (0xd4) and
#     OP_REFHASHVALUESUM_UTXOS (0xd5) are part of the same tightening.
#
# Tightening a consensus rule is only safe if NO already-confirmed transaction
# relies on the old, looser behaviour. If a historical, spendable output's
# script depends on (say) OP_K12 over an 8192-byte element, then activating the
# stricter rule would make that output unspendable -> a chain split / stuck
# funds. This tool scans the chain so the operator can decide, with evidence,
# whether it is safe to set the SecurityUpgradeHeight activation point.
#
# WHAT IT REPORTS
# ---------------
# For every transaction from --start-height (default 62000, the pre-V2 region
# the audit cares about) up to the chain tip, it decodes each input
# (scriptSig) and output (scriptPubKey) script and flags:
#
#   1. OP_BLAKE3 applied to a stack element > 1024 bytes.
#   2. OP_K12   applied to a stack element >= 8192 bytes (8192 is flagged
#      explicitly as the boundary case).
#   3. ANY use of opcode 0xd4 (OP_REFHASHDATASUMMARY_UTXO) or
#      0xd5 (OP_REFHASHVALUESUM_UTXOS).
#
# Then it prints a verdict:
#   CLEAN  -> "safe to activate SecurityUpgradeHeight at <tip+1>"
#   FOUND  -> the exact block height / txid / vin|vout / opcode / size.
#
# IMPORTANT LIMITATION (read before trusting a CLEAN verdict)
# -----------------------------------------------------------
# The size that OP_BLAKE3 / OP_K12 actually hashes is the *runtime* top of the
# script stack, which in general can only be known by fully executing the
# script with its spending context (introspection opcodes, prior pushes, etc.).
# This tool does NOT run the script interpreter. Instead it uses a sound static
# heuristic: it tracks the byte-length of the data element produced by the
# *immediately preceding* push operation. In the overwhelmingly common pattern
# `<push N bytes> OP_K12`, that is exactly the hashed element, so oversize
# *literal* inputs are caught precisely. When the element being hashed is
# produced dynamically (e.g. by OP_CAT / OP_NUM2BIN / introspection rather than
# a literal push), the static size is unknown; such cases are reported
# separately as INDETERMINATE so a human can review them rather than being
# silently treated as CLEAN. A CLEAN verdict means "no oversize literal usage
# and no 0xd4/0xd5 usage and no indeterminate hash sites" — review any
# INDETERMINATE findings before activating.
#
# USAGE
# -----
#   ./scan-hash-opcode-usage.py [options]
#
# Connection (auto-detected like other contrib scripts, override as needed):
#   --datadir PATH        Node datadir (default: ~/.radiant). The RPC cookie is
#                         read from <datadir>[/<net subdir>]/.cookie.
#   --rpchost HOST        RPC host (default: 127.0.0.1)
#   --rpcport PORT        RPC port (default: per --chain; mainnet 7332)
#   --rpcuser / --rpcpassword
#                         Static credentials instead of the cookie file.
#   --rpccookiefile PATH  Explicit cookie path (overrides --datadir logic).
#   --chain {main,test,regtest}  Selects default port + cookie subdir.
#
# Scan range:
#   --start-height N      Default 62000.
#   --end-height N        Default: chain tip.
#
# Output:
#   --verbose             Print per-finding lines as they are discovered.
#   --json                Emit a machine-readable JSON report to stdout.
#
# Requires only the Python 3 standard library (urllib, json) — no node build.
# It is safe to run against a live node; it only issues read-only RPC calls
# (getblockcount, getblockhash, getblock verbosity=2).
# =============================================================================

import argparse
import base64
import json
import os
import sys
import urllib.request
import urllib.error

# ---------------------------------------------------------------------------
# Opcodes of interest. Values mirror src/script/script.h on this branch.
# ---------------------------------------------------------------------------
OP_BLAKE3 = 0xEE
OP_K12 = 0xEF
OP_REFHASHDATASUMMARY_UTXO = 0xD4
OP_REFHASHVALUESUM_UTXOS = 0xD5

# Consensus size limits being enforced by the branch (src/crypto/blake3.h,
# src/crypto/k12.h). Kept here so the scanner's thresholds track the code.
BLAKE3_CHUNK_LEN = 1024     # OP_BLAKE3 rejects input > this
K12_MAX_INPUT_LEN = 8192    # OP_K12 rejects input >= this (8192 is the edge)

# Push-data opcode boundaries (Satoshi script encoding).
OP_0 = 0x00
OP_PUSHDATA1 = 0x4C
OP_PUSHDATA2 = 0x4D
OP_PUSHDATA4 = 0x4E
OP_1NEGATE = 0x4F
OP_1 = 0x51
OP_16 = 0x60

OPNAME = {
    OP_BLAKE3: "OP_BLAKE3",
    OP_K12: "OP_K12",
    OP_REFHASHDATASUMMARY_UTXO: "OP_REFHASHDATASUMMARY_UTXO",
    OP_REFHASHVALUESUM_UTXOS: "OP_REFHASHVALUESUM_UTXOS",
}


# ---------------------------------------------------------------------------
# Minimal JSON-RPC client (stdlib only). Mirrors the credential handling used
# by contrib/linearize/linearize-hashes.py and the node's own cookie scheme.
# ---------------------------------------------------------------------------
class RadiantRPC:
    def __init__(self, host, port, user, password):
        self.url = "http://{}:{}/".format(host, port)
        authpair = "{}:{}".format(user, password).encode("utf-8")
        self.authhdr = b"Basic " + base64.b64encode(authpair)
        self._id = 0

    def call(self, method, *params):
        self._id += 1
        payload = json.dumps({
            "jsonrpc": "1.0",
            "id": "scan-hash-opcode-usage-{}".format(self._id),
            "method": method,
            "params": list(params),
        }).encode("utf-8")
        req = urllib.request.Request(
            self.url, data=payload,
            headers={
                "Authorization": self.authhdr,
                "Content-Type": "application/json",
            },
        )
        try:
            with urllib.request.urlopen(req, timeout=120) as resp:
                body = json.loads(resp.read().decode("utf-8"))
        except urllib.error.HTTPError as e:
            # The node returns useful JSON-RPC errors in the body even on 500.
            try:
                body = json.loads(e.read().decode("utf-8"))
            except Exception:
                raise SystemExit(
                    "RPC HTTP error {} calling {} (check credentials / -server)"
                    .format(e.code, method))
        except urllib.error.URLError as e:
            raise SystemExit(
                "RPC connection failed ({}). Is radiantd running with -server "
                "and -rest? Try --rpchost/--rpcport.".format(e.reason))
        if body.get("error"):
            raise SystemExit("RPC error from {}: {}".format(method, body["error"]))
        return body["result"]


def default_port_for_chain(chain):
    return {"main": 7332, "test": 27332, "regtest": 18443}.get(chain, 7332)


def cookie_subdir_for_chain(chain):
    # Matches CBaseChainParams data subdirectories.
    return {"main": "", "test": "testnet", "regtest": "regtest"}.get(chain, "")


def resolve_credentials(args):
    """Return (user, password). Explicit user/pass wins; else read .cookie."""
    if args.rpcuser is not None and args.rpcpassword is not None:
        return args.rpcuser, args.rpcpassword

    cookie_path = args.rpccookiefile
    if cookie_path is None:
        datadir = args.datadir or os.path.join(os.path.expanduser("~"), ".radiant")
        subdir = cookie_subdir_for_chain(args.chain)
        cookie_path = os.path.join(datadir, subdir, ".cookie") if subdir \
            else os.path.join(datadir, ".cookie")

    if not os.path.isfile(cookie_path):
        raise SystemExit(
            "No RPC cookie at {}.\n"
            "Pass --rpcuser/--rpcpassword, or --rpccookiefile, or point "
            "--datadir at the node's data directory.".format(cookie_path))
    with open(cookie_path, "r") as f:
        content = f.read().strip()
    if ":" not in content:
        raise SystemExit("Malformed cookie file: {}".format(cookie_path))
    user, _, password = content.partition(":")
    return user, password


# ---------------------------------------------------------------------------
# Script scanning core.
# ---------------------------------------------------------------------------
class Finding:
    def __init__(self, kind, height, txid, location, opcode, size, detail):
        self.kind = kind            # "OVERSIZE" | "REF_OPCODE" | "INDETERMINATE"
        self.height = height
        self.txid = txid
        self.location = location    # e.g. "vin[0].scriptSig"
        self.opcode = opcode
        self.size = size            # element size in bytes, or None
        self.detail = detail

    def to_dict(self):
        return {
            "kind": self.kind,
            "height": self.height,
            "txid": self.txid,
            "location": self.location,
            "opcode": OPNAME.get(self.opcode, "0x%02x" % self.opcode),
            "opcode_hex": "0x%02x" % self.opcode,
            "element_size": self.size,
            "detail": self.detail,
        }

    def __str__(self):
        sz = "" if self.size is None else " size={}B".format(self.size)
        return "[{}] block {} {} {} {}{} :: {}".format(
            self.kind, self.height, self.txid, self.location,
            OPNAME.get(self.opcode, "0x%02x" % self.opcode), sz, self.detail)


def scan_script(script_bytes, height, txid, location):
    """Walk one script's opcodes. Returns a list[Finding].

    Heuristic: `last_push_size` holds the byte length of the data element
    produced by the most recent PUSH operation. A hash opcode consuming that
    element gives a precise size when it was a literal push; otherwise the size
    is unknown and we report INDETERMINATE.
    """
    findings = []
    i = 0
    n = len(script_bytes)
    last_push_size = None  # None => top of stack not a known literal push

    while i < n:
        op = script_bytes[i]
        i += 1

        # --- data pushes -----------------------------------------------------
        if op == OP_0:
            last_push_size = 0
            continue
        if op < OP_PUSHDATA1:  # direct push of `op` bytes (1..75)
            datalen = op
            i += datalen
            last_push_size = datalen
            continue
        if op == OP_PUSHDATA1:
            if i + 1 > n:
                break
            datalen = script_bytes[i]
            i += 1 + datalen
            last_push_size = datalen
            continue
        if op == OP_PUSHDATA2:
            if i + 2 > n:
                break
            datalen = int.from_bytes(script_bytes[i:i + 2], "little")
            i += 2 + datalen
            last_push_size = datalen
            continue
        if op == OP_PUSHDATA4:
            if i + 4 > n:
                break
            datalen = int.from_bytes(script_bytes[i:i + 4], "little")
            i += 4 + datalen
            last_push_size = datalen
            continue
        if op == OP_1NEGATE or (OP_1 <= op <= OP_16):
            # OP_1..OP_16 / OP_1NEGATE push a single small-int (1 byte element).
            last_push_size = 1
            continue

        # --- opcodes of interest --------------------------------------------
        if op in (OP_REFHASHDATASUMMARY_UTXO, OP_REFHASHVALUESUM_UTXOS):
            findings.append(Finding(
                "REF_OPCODE", height, txid, location, op, None,
                "enhanced-reference opcode present (consensus-tightened on "
                "this branch)"))
            # These opcodes push a hash result; treat new top as non-literal.
            last_push_size = None
            continue

        if op in (OP_BLAKE3, OP_K12):
            if last_push_size is None:
                findings.append(Finding(
                    "INDETERMINATE", height, txid, location, op, None,
                    "hash opcode operates on a dynamically-produced element; "
                    "static size unknown — review manually"))
            else:
                size = last_push_size
                if op == OP_BLAKE3 and size > BLAKE3_CHUNK_LEN:
                    findings.append(Finding(
                        "OVERSIZE", height, txid, location, op, size,
                        "OP_BLAKE3 input > {} bytes (would be rejected by F1)"
                        .format(BLAKE3_CHUNK_LEN)))
                elif op == OP_K12 and size >= K12_MAX_INPUT_LEN:
                    boundary = " (EXACT 8192-byte boundary case)" \
                        if size == K12_MAX_INPUT_LEN else ""
                    findings.append(Finding(
                        "OVERSIZE", height, txid, location, op, size,
                        "OP_K12 input >= {} bytes{} (would be rejected by F1)"
                        .format(K12_MAX_INPUT_LEN, boundary)))
            # Hash result replaces the consumed element on the stack.
            last_push_size = None
            continue

        # Any other opcode: the previous literal push (if any) has now been
        # consumed/transformed, so we can no longer assume the stack top is a
        # known literal.
        last_push_size = None

    return findings


def hex_to_bytes(h):
    if not h:
        return b""
    return bytes.fromhex(h)


def scan_tx(tx, height):
    findings = []
    txid = tx.get("txid", "<unknown>")
    for vin_index, vin in enumerate(tx.get("vin", [])):
        # Coinbase inputs carry arbitrary data in "coinbase", not a script.
        ss = vin.get("scriptSig", {})
        asm_hex = ss.get("hex")
        if asm_hex:
            findings += scan_script(
                hex_to_bytes(asm_hex), height, txid,
                "vin[{}].scriptSig".format(vin_index))
    for vout_index, vout in enumerate(tx.get("vout", [])):
        spk = vout.get("scriptPubKey", {})
        spk_hex = spk.get("hex")
        if spk_hex:
            findings += scan_script(
                hex_to_bytes(spk_hex), height, txid,
                "vout[{}].scriptPubKey".format(vout_index))
    return findings


def main():
    ap = argparse.ArgumentParser(
        description="CRIT-1 pre-activation scan for OP_BLAKE3/OP_K12 oversize "
                    "usage and OP_REFHASHDATASUMMARY_UTXO / "
                    "OP_REFHASHVALUESUM_UTXOS usage.")
    ap.add_argument("--chain", choices=["main", "test", "regtest"],
                    default="main")
    ap.add_argument("--datadir", default=None)
    ap.add_argument("--rpchost", default="127.0.0.1")
    ap.add_argument("--rpcport", type=int, default=None)
    ap.add_argument("--rpcuser", default=None)
    ap.add_argument("--rpcpassword", default=None)
    ap.add_argument("--rpccookiefile", default=None)
    ap.add_argument("--start-height", type=int, default=62000)
    ap.add_argument("--end-height", type=int, default=None)
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    port = args.rpcport or default_port_for_chain(args.chain)
    user, password = resolve_credentials(args)
    rpc = RadiantRPC(args.rpchost, port, user, password)

    tip = rpc.call("getblockcount")
    start = args.start_height
    end = args.end_height if args.end_height is not None else tip
    if start < 0:
        start = 0
    if end > tip:
        end = tip
    if start > end:
        raise SystemExit(
            "start-height {} is beyond end/tip {}".format(start, end))

    if not args.json:
        sys.stderr.write(
            "Scanning blocks {}..{} (tip={}) on chain '{}' via {}:{}\n".format(
                start, end, tip, args.chain, args.rpchost, port))

    all_findings = []
    blocks_scanned = 0
    txs_scanned = 0

    for height in range(start, end + 1):
        block_hash = rpc.call("getblockhash", height)
        block = rpc.call("getblock", block_hash, 2)  # verbosity 2 = full txs
        for tx in block.get("tx", []):
            txs_scanned += 1
            f = scan_tx(tx, height)
            if f:
                all_findings += f
                if args.verbose and not args.json:
                    for finding in f:
                        sys.stderr.write(str(finding) + "\n")
        blocks_scanned += 1
        if not args.json and blocks_scanned % 1000 == 0:
            sys.stderr.write(
                "  ... {} blocks scanned (at height {})\n".format(
                    blocks_scanned, height))

    oversize = [f for f in all_findings if f.kind == "OVERSIZE"]
    ref_opcode = [f for f in all_findings if f.kind == "REF_OPCODE"]
    indeterminate = [f for f in all_findings if f.kind == "INDETERMINATE"]

    clean = not oversize and not ref_opcode and not indeterminate
    next_height = end + 1

    if args.json:
        report = {
            "chain": args.chain,
            "tip": tip,
            "scanned_start": start,
            "scanned_end": end,
            "blocks_scanned": blocks_scanned,
            "txs_scanned": txs_scanned,
            "counts": {
                "oversize": len(oversize),
                "ref_opcode": len(ref_opcode),
                "indeterminate": len(indeterminate),
            },
            "verdict": "CLEAN" if clean else "USAGE_FOUND",
            "suggested_activation_height": next_height if clean else None,
            "findings": [f.to_dict() for f in all_findings],
        }
        print(json.dumps(report, indent=2))
        return 0 if clean else 1

    # Human-readable summary.
    print("")
    print("=" * 70)
    print("  CRIT-1 hash-opcode usage scan  (chain={})".format(args.chain))
    print("=" * 70)
    print("  Blocks scanned : {} ({}..{}, tip={})".format(
        blocks_scanned, start, end, tip))
    print("  Transactions   : {}".format(txs_scanned))
    print("  OVERSIZE hits  : {}".format(len(oversize)))
    print("  0xd4/0xd5 hits : {}".format(len(ref_opcode)))
    print("  INDETERMINATE  : {}".format(len(indeterminate)))
    print("-" * 70)

    for f in oversize:
        print("  " + str(f))
    for f in ref_opcode:
        print("  " + str(f))
    for f in indeterminate:
        print("  " + str(f))

    print("=" * 70)
    if clean:
        print("  VERDICT: CLEAN — no oversize OP_BLAKE3/OP_K12 usage, no")
        print("           0xd4/0xd5 usage, no indeterminate hash sites in the")
        print("           scanned range.")
        print("           Safe to activate SecurityUpgradeHeight at {}."
              .format(next_height))
        print("           (This gates the F1 / memory-budget consensus")
        print("            tightening shipped on fix/security-audit-2026-06.)")
    else:
        print("  VERDICT: USAGE FOUND — do NOT activate SecurityUpgradeHeight")
        print("           until the findings above are reviewed. Activating")
        print("           the tightened rules could invalidate existing")
        print("           transactions/outputs and split the chain.")
        if indeterminate and not oversize and not ref_opcode:
            print("           NOTE: all findings are INDETERMINATE (dynamic")
            print("           hash inputs). Manual review may still clear it.")
    print("=" * 70)
    return 0 if clean else 1


if __name__ == "__main__":
    sys.exit(main())
