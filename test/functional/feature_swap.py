#!/usr/bin/env python3
# Copyright (c) 2025 The Radiant developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the swap index and RPCs."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal
from test_framework.script import (
    CScript,
    OP_RETURN,
)
from test_framework.messages import (
    CTransaction,
    CTxOut,
    COIN,
    COutPoint,
    CTxIn,
    FromHex,
)
import struct
import codecs

class SwapTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-swapindex=1", "-txindex=1"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]

        # Generate some blocks to get coins
        self.log.info("Mining blocks...")
        address = node.getnewaddress()
        self.generatetoaddress(node, 101, address)

        # 1. Create a "Token REF" (just a random TXID for this test)
        # We simulate a token by creating a tx and using its hash
        token_txid = node.sendtoaddress(node.getnewaddress(), 1)
        # token_txid is hex string (big-endian display format).
        # ParseHashV reverses the hex bytes when storing in uint256.
        # So we need to reverse the bytes in the script to match.
        token_id_bytes = bytes.fromhex(token_txid)[::-1]

        # 2. Create an "Offered UTXO" - mine it first so we can find the correct vout
        offered_utxo_txid = node.sendtoaddress(node.getnewaddress(), 10)
        node.generate(1)  # Confirm the offered UTXO tx
        
        # Find the actual vout for our 10 RXD output
        offered_tx = node.getrawtransaction(offered_utxo_txid, True)
        offered_utxo_vout = None
        for i, vout in enumerate(offered_tx['vout']):
            if vout['value'] == 10:
                offered_utxo_vout = i
                break
        assert offered_utxo_vout is not None, "Could not find 10 RXD output in offered UTXO tx"
        
        # For UTXO lookup: The UTXO set is indexed by TxId which stores bytes in little-endian.
        # When we push bytes into the script and store in uint256, GetHex() reverses for display.
        # To match the UTXO set's TxId, we need the same internal byte order.
        # Since TxId internal bytes are little-endian (reverse of display hex), we reverse.
        offered_utxo_hash_bytes = bytes.fromhex(offered_utxo_txid)[::-1]

        # Prevent the wallet from selecting the offered UTXO as an input for other
        # transactions we create later in this test.
        assert node.lockunspent(False, [{"txid": offered_utxo_txid, "vout": offered_utxo_vout}])
        
        self.log.info(f"Offered UTXO: {offered_utxo_txid}:{offered_utxo_vout}")
        
        # 3. Create Advertisement Transaction
        self.log.info("Creating advertisement transaction...")
        
        protocol_id = b"RSWP"
        version = b'\x01'
        swap_type = b'\x01' # SELL
        
        # Price terms (just some random bytes for now)
        price_terms = b'\x01\x02\x03'
        
        # Signature (random bytes)
        signature = b'\x04\x05\x06'

        # Build the OP_RETURN script manually
        # The indexer expects: OP_RETURN <RSWP> <Version> <Type> <TokenID> <UTXOHash> <UTXOIndex> <PriceTerms> <Signature>
        # But createrawtransaction's "data" output creates a single push, not multiple pushes.
        # For this test, we'll create a simplified format that the indexer can parse.
        
        # Build OP_RETURN script with multiple pushes
        script_pubkey = CScript([
            OP_RETURN,
            protocol_id,  # RSWP
            version,  # Version (1 byte)
            swap_type,  # Type (1 byte)
            token_id_bytes,  # TokenID (32 bytes)
            offered_utxo_hash_bytes,  # UTXOHash (32 bytes)
            bytes([offered_utxo_vout]),  # UTXOIndex as single byte (for small values)
            price_terms,  # PriceTerms
            signature  # Signature
        ])
        
        # Create a simple transaction, fund it with extra fee, then add OP_RETURN
        # Use sendtoaddress to create a UTXO we can spend
        funding_txid = node.sendtoaddress(node.getnewaddress(), 5)
        node.generate(1)  # Confirm it
        
        # Get the UTXO details
        utxos = node.listunspent(1, 9999999, [], True, {"minimumAmount": 4})
        assert len(utxos) > 0, "No UTXOs available"
        # Ensure we don't accidentally spend the offered UTXO when creating the
        # advertisement transaction.
        utxo = None
        for candidate in utxos:
            if candidate["txid"] == offered_utxo_txid and candidate["vout"] == offered_utxo_vout:
                continue
            utxo = candidate
            break
        assert utxo is not None, "No suitable funding UTXO available (offered UTXO was the only candidate)"
        
        # Create raw transaction spending this UTXO
        inputs = [{"txid": utxo["txid"], "vout": utxo["vout"]}]
        # Leave 0.1 RXD for fee (very generous)
        change_amount = float(utxo["amount"]) - 0.1
        outputs = {node.getnewaddress(): change_amount}
        
        tx_hex = node.createrawtransaction(inputs, outputs)
        tx = FromHex(CTransaction(), tx_hex)
        
        # Add OP_RETURN output
        tx.vout.append(CTxOut(0, script_pubkey))
        tx_hex = tx.serialize().hex()
        
        # Sign and send
        tx_signed = node.signrawtransactionwithwallet(tx_hex)
        assert tx_signed['complete'], f"Signing failed: {tx_signed}"
        
        adv_txid = node.sendrawtransaction(tx_signed['hex'])
        self.log.info(f"Advertisement TXID: {adv_txid}")
        
        # Mine block
        node.generate(1)
        
        # 4. Check getopenorders
        self.log.info("Checking getopenorders...")
        orders = node.getopenorders(token_txid)
        assert_equal(len(orders), 1)
        order = orders[0]
        assert_equal(order['tokenid'], token_txid)
        assert_equal(order['utxo']['txid'], offered_utxo_txid)
        assert_equal(order['utxo']['vout'], offered_utxo_vout)
        assert_equal(order['type'], 1)
        assert_equal(order['version'], 1)
        assert_equal(order['price_terms'], price_terms.hex())
        assert_equal(order['signature'], signature.hex())

        # 5. Spend the offered UTXO and verify it disappears from open orders
        self.log.info("Spending offered UTXO to fill/cancel the order...")
        spend_inputs = [{"txid": offered_utxo_txid, "vout": offered_utxo_vout}]
        spend_outputs = {node.getnewaddress(): 9.0}  # Spend 10 RXD output, leave generous fee
        spend_hex = node.createrawtransaction(spend_inputs, spend_outputs)
        spend_signed = node.signrawtransactionwithwallet(spend_hex)
        assert spend_signed['complete'], f"Signing spend tx failed: {spend_signed}"
        spend_txid = node.sendrawtransaction(spend_signed['hex'])
        self.log.info(f"Spend TXID: {spend_txid}")

        # Mempool-aware filtering: offer should no longer be open
        orders = node.getopenorders(token_txid)
        assert_equal(len(orders), 0)
        history = node.getswaphistory(token_txid)
        assert_equal(len(history), 1)

        # Mine the spend tx and verify results persist
        node.generate(1)
        orders = node.getopenorders(token_txid)
        assert_equal(len(orders), 0)
        history = node.getswaphistory(token_txid)
        assert_equal(len(history), 1)
        
        # Verify history entry has block_height field
        assert 'block_height' in history[0], "History entry should have block_height"
        assert history[0]['block_height'] > 0, "block_height should be positive"
        
        # 6. Test getswapcount RPC
        self.log.info("Testing getswapcount...")
        counts = node.getswapcount(token_txid)
        assert_equal(counts['open'], 0)
        assert_equal(counts['history'], 1)
        
        self.log.info("Swap index test passed!")

if __name__ == '__main__':
    SwapTest().main()
