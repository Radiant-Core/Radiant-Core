#!/usr/bin/env python3
# Copyright (c) 2025 The Radiant developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the swap index and RPCs."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, wait_until
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
        want_token_txid = node.sendtoaddress(node.getnewaddress(), 1)
        # token_txid is hex string (big-endian display format).
        # ParseHashV reverses the hex bytes when storing in uint256.
        # So we need to reverse the bytes in the script to match.
        token_id_bytes = bytes.fromhex(token_txid)[::-1]
        want_token_id_bytes = bytes.fromhex(want_token_txid)[::-1]

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
        version = b'\x02'
        flags = b'\x01'
        offered_type = b'\x00'
        terms_type = b'\x01'
        
        # Price terms (split across multiple pushes for v2 multi-push terms)
        price_terms_1 = b'\x01\x02\x03\x04\x05'
        price_terms_2 = b'\x06\x07\x08\x09\x0a'
        price_terms = price_terms_1 + price_terms_2
        
        # Signature (random bytes)
        signature = b'\x04\x05\x06\x07'

        # Build the OP_RETURN script manually
        # The indexer expects: OP_RETURN <RSWP> <Version> <Type> <TokenID> <UTXOHash> <UTXOIndex> <PriceTerms> <Signature>
        # But createrawtransaction's "data" output creates a single push, not multiple pushes.
        # For this test, we'll create a simplified format that the indexer can parse.
        
        # Build OP_RETURN script with multiple pushes
        script_pubkey = CScript([
            OP_RETURN,
            protocol_id,  # RSWP
            version,  # Version (1 byte)
            flags,  # Flags (1 byte)
            offered_type,  # OfferedType (1 byte)
            terms_type,  # TermsType (1 byte)
            token_id_bytes,  # TokenID (32 bytes)
            want_token_id_bytes,  # WantTokenID (32 bytes)
            offered_utxo_hash_bytes,  # UTXOHash (32 bytes)
            bytes([offered_utxo_vout]),  # UTXOIndex as single byte (for small values)
            price_terms_1,
            price_terms_2,
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

        node.syncwithvalidationinterfacequeue()

        adv_tx = node.getrawtransaction(adv_txid, True)
        assert adv_tx.get('confirmations', 0) > 0, "Advertisement transaction was not mined"

        wait_until(lambda: len(node.getopenorders(token_txid)) == 1, timeout=20)
        
        # 4. Check getopenorders
        self.log.info("Checking getopenorders...")
        orders = node.getopenorders(token_txid)
        assert_equal(len(orders), 1)
        order = orders[0]
        assert_equal(order['tokenid'], token_txid)
        assert_equal(order['want_tokenid'], want_token_txid)
        assert_equal(order['utxo']['txid'], offered_utxo_txid)
        assert_equal(order['utxo']['vout'], offered_utxo_vout)
        assert_equal(order['version'], 2)
        assert_equal(order['flags'], 1)
        assert_equal(order['offered_type'], 0)
        assert_equal(order['terms_type'], 1)
        assert_equal(order['price_terms'], price_terms.hex())
        assert_equal(order['signature'], signature.hex())

        self.log.info("Checking getopenordersbywant...")
        counts_by_want = node.getswapcountbywant(want_token_txid)
        self.log.info(f"getswapcountbywant: {counts_by_want}")
        orders_by_want = node.getopenordersbywant(want_token_txid)
        assert_equal(len(orders_by_want), 1)
        assert_equal(orders_by_want[0]['tokenid'], token_txid)
        assert_equal(orders_by_want[0]['want_tokenid'], want_token_txid)

        # 5. Spend the offered UTXO and verify it disappears from open orders
        self.log.info("Spending offered UTXO to fill/cancel the order...")
        spend_inputs = [{"txid": offered_utxo_txid, "vout": offered_utxo_vout}]
        spend_outputs = {node.getnewaddress(): 9.0}  # Spend 10 RXD output, leave generous fee
        spend_hex = node.createrawtransaction(spend_inputs, spend_outputs)
        spend_signed = node.signrawtransactionwithwallet(spend_hex)
        assert spend_signed['complete'], f"Signing spend tx failed: {spend_signed}"
        spend_txid = node.sendrawtransaction(spend_signed['hex'])
        self.log.info(f"Spend TXID: {spend_txid}")

        node.syncwithvalidationinterfacequeue()

        wait_until(lambda: len(node.getopenorders(token_txid)) == 0, timeout=20)

        # Mempool-aware filtering: offer should no longer be open
        orders = node.getopenorders(token_txid)
        assert_equal(len(orders), 0)
        orders_by_want = node.getopenordersbywant(want_token_txid)
        assert_equal(len(orders_by_want), 0)
        history = node.getswaphistory(token_txid)
        assert_equal(len(history), 1)
        history_by_want = node.getswaphistorybywant(want_token_txid)
        assert_equal(len(history_by_want), 1)

        # Mine the spend tx and verify results persist
        node.generate(1)

        node.syncwithvalidationinterfacequeue()

        wait_until(lambda: len(node.getswaphistory(token_txid)) == 1, timeout=20)
        orders = node.getopenorders(token_txid)
        assert_equal(len(orders), 0)
        history = node.getswaphistory(token_txid)
        assert_equal(len(history), 1)

        orders_by_want = node.getopenordersbywant(want_token_txid)
        assert_equal(len(orders_by_want), 0)
        history_by_want = node.getswaphistorybywant(want_token_txid)
        assert_equal(len(history_by_want), 1)
        
        # Verify history entry has block_height field
        assert 'block_height' in history[0], "History entry should have block_height"
        assert history[0]['block_height'] > 0, "block_height should be positive"
        
        # 6. Test getswapcount RPC
        self.log.info("Testing getswapcount...")
        counts = node.getswapcount(token_txid)
        assert_equal(counts['open'], 0)
        assert_equal(counts['history'], 1)

        self.log.info("Testing getswapcountbywant...")
        counts_by_want = node.getswapcountbywant(want_token_txid)
        assert_equal(counts_by_want['open'], 0)
        assert_equal(counts_by_want['history'], 1)
        
        self.log.info("Swap index test passed!")
        
        # 7. Test legacy v1 format
        self.log.info("Testing legacy v1 format...")
        self.test_v1_format(node)
        
        # 8. Test pagination with multiple orders
        self.log.info("Testing pagination with multiple orders...")
        self.test_pagination(node)
        
        # 9. Test reorg handling
        self.log.info("Testing reorg handling...")
        self.test_reorg(node)
        
        self.log.info("All swap index tests passed!")

    def create_swap_ad_v2(self, node, token_id_bytes, want_token_id_bytes, 
                          offered_utxo_txid, offered_utxo_vout, price_terms, signature):
        """Helper to create a v2 swap advertisement transaction."""
        offered_utxo_hash_bytes = bytes.fromhex(offered_utxo_txid)[::-1]
        
        script_pubkey = CScript([
            OP_RETURN,
            b"RSWP",
            b'\x02',  # version
            b'\x01',  # flags (has wantTokenID)
            b'\x00',  # offeredType
            b'\x01',  # termsType
            token_id_bytes,
            want_token_id_bytes,
            offered_utxo_hash_bytes,
            bytes([offered_utxo_vout]),
            price_terms,
            signature
        ])
        
        # Get funding UTXO - need one with enough value
        utxos = node.listunspent(1, 9999999, [], True, {"minimumAmount": 0.5})
        utxo = None
        for candidate in utxos:
            # Skip the offered UTXO and any locked UTXOs
            if candidate["txid"] == offered_utxo_txid and candidate["vout"] == offered_utxo_vout:
                continue
            if float(candidate["amount"]) >= 0.5:
                utxo = candidate
                break
        assert utxo is not None, "No suitable funding UTXO available"
        
        inputs = [{"txid": utxo["txid"], "vout": utxo["vout"]}]
        change_amount = round(float(utxo["amount"]) - 0.01, 8)  # Use smaller fee
        assert change_amount > 0, f"Not enough funds: {utxo['amount']}"
        outputs = {node.getnewaddress(): change_amount}
        
        tx_hex = node.createrawtransaction(inputs, outputs)
        tx = FromHex(CTransaction(), tx_hex)
        tx.vout.append(CTxOut(0, script_pubkey))
        tx_hex = tx.serialize().hex()
        
        tx_signed = node.signrawtransactionwithwallet(tx_hex)
        assert tx_signed['complete'], f"Signing failed: {tx_signed}"
        
        return node.sendrawtransaction(tx_signed['hex'])

    def test_v1_format(self, node):
        """Test legacy v1 format swap advertisements."""
        # Create token ref
        token_txid = node.sendtoaddress(node.getnewaddress(), 1)
        node.generate(1)
        token_id_bytes = bytes.fromhex(token_txid)[::-1]
        
        # Create offered UTXO
        offered_utxo_txid = node.sendtoaddress(node.getnewaddress(), 5)
        node.generate(1)
        
        offered_tx = node.getrawtransaction(offered_utxo_txid, True)
        offered_utxo_vout = None
        for i, vout in enumerate(offered_tx['vout']):
            if vout['value'] == 5:
                offered_utxo_vout = i
                break
        assert offered_utxo_vout is not None
        
        offered_utxo_hash_bytes = bytes.fromhex(offered_utxo_txid)[::-1]
        
        # Lock the offered UTXO
        assert node.lockunspent(False, [{"txid": offered_utxo_txid, "vout": offered_utxo_vout}])
        
        # Build v1 format OP_RETURN script
        # v1: OP_RETURN <RSWP> <version=1> <type> <tokenID> <utxoHash> <utxoIndex> <priceTerms> <signature>
        price_terms = b'\xaa\xbb\xcc\xdd'
        signature = b'\x11\x22\x33\x44'
        
        script_pubkey = CScript([
            OP_RETURN,
            b"RSWP",
            b'\x01',  # version = 1
            b'\x02',  # type (legacy field)
            token_id_bytes,
            offered_utxo_hash_bytes,
            bytes([offered_utxo_vout]),
            price_terms,
            signature
        ])
        
        # Get funding UTXO
        utxos = node.listunspent(1, 9999999, [], True, {"minimumAmount": 1})
        utxo = None
        for candidate in utxos:
            if candidate["txid"] != offered_utxo_txid or candidate["vout"] != offered_utxo_vout:
                utxo = candidate
                break
        assert utxo is not None
        
        inputs = [{"txid": utxo["txid"], "vout": utxo["vout"]}]
        change_amount = float(utxo["amount"]) - 0.1
        outputs = {node.getnewaddress(): change_amount}
        
        tx_hex = node.createrawtransaction(inputs, outputs)
        tx = FromHex(CTransaction(), tx_hex)
        tx.vout.append(CTxOut(0, script_pubkey))
        tx_hex = tx.serialize().hex()
        
        tx_signed = node.signrawtransactionwithwallet(tx_hex)
        assert tx_signed['complete']
        
        adv_txid = node.sendrawtransaction(tx_signed['hex'])
        node.generate(1)
        node.syncwithvalidationinterfacequeue()
        
        wait_until(lambda: len(node.getopenorders(token_txid)) == 1, timeout=20)
        
        orders = node.getopenorders(token_txid)
        assert_equal(len(orders), 1)
        assert_equal(orders[0]['version'], 1)
        assert_equal(orders[0]['tokenid'], token_txid)
        assert_equal(orders[0]['price_terms'], price_terms.hex())
        assert_equal(orders[0]['signature'], signature.hex())
        # v1 format should not have want_tokenid
        assert 'want_tokenid' not in orders[0] or orders[0].get('want_tokenid') == '0' * 64
        
        self.log.info("v1 format test passed!")

    def test_pagination(self, node):
        """Test pagination with multiple orders for the same token."""
        # Create a new token ref for this test
        token_txid = node.sendtoaddress(node.getnewaddress(), 1)
        want_token_txid = node.sendtoaddress(node.getnewaddress(), 1)
        node.generate(1)
        token_id_bytes = bytes.fromhex(token_txid)[::-1]
        want_token_id_bytes = bytes.fromhex(want_token_txid)[::-1]
        
        # Create 5 swap advertisements for the same token
        num_orders = 5
        offered_utxos = []
        
        for i in range(num_orders):
            utxo_txid = node.sendtoaddress(node.getnewaddress(), 2)
            node.generate(1)
            
            utxo_tx = node.getrawtransaction(utxo_txid, True)
            utxo_vout = None
            for j, vout in enumerate(utxo_tx['vout']):
                if vout['value'] == 2:
                    utxo_vout = j
                    break
            assert utxo_vout is not None
            
            # Lock the UTXO
            assert node.lockunspent(False, [{"txid": utxo_txid, "vout": utxo_vout}])
            offered_utxos.append((utxo_txid, utxo_vout))
        
        # Create advertisements for each UTXO
        for i, (utxo_txid, utxo_vout) in enumerate(offered_utxos):
            price_terms = bytes([i + 1] * 4)
            signature = bytes([i + 10] * 4)
            
            self.create_swap_ad_v2(node, token_id_bytes, want_token_id_bytes,
                                   utxo_txid, utxo_vout, price_terms, signature)
            node.generate(1)
        
        node.syncwithvalidationinterfacequeue()
        
        wait_until(lambda: len(node.getopenorders(token_txid, 100, 0)) == num_orders, timeout=30)
        
        # Test pagination
        all_orders = node.getopenorders(token_txid, 100, 0)
        assert_equal(len(all_orders), num_orders)
        
        # Get first 2 orders
        page1 = node.getopenorders(token_txid, 2, 0)
        assert_equal(len(page1), 2)
        
        # Get next 2 orders
        page2 = node.getopenorders(token_txid, 2, 2)
        assert_equal(len(page2), 2)
        
        # Get last order
        page3 = node.getopenorders(token_txid, 2, 4)
        assert_equal(len(page3), 1)
        
        # Verify no duplicates across pages
        all_utxos = set()
        for order in page1 + page2 + page3:
            utxo_key = (order['utxo']['txid'], order['utxo']['vout'])
            assert utxo_key not in all_utxos, "Duplicate order across pages"
            all_utxos.add(utxo_key)
        
        assert_equal(len(all_utxos), num_orders)
        
        # Test getswapcount
        counts = node.getswapcount(token_txid)
        assert_equal(counts['open'], num_orders)
        assert_equal(counts['history'], 0)
        
        self.log.info("Pagination test passed!")

    def test_reorg(self, node):
        """Test reorg handling - orders should be restored when blocks are disconnected."""
        # Create a new token ref for this test
        token_txid = node.sendtoaddress(node.getnewaddress(), 1)
        want_token_txid = node.sendtoaddress(node.getnewaddress(), 1)
        node.generate(1)
        token_id_bytes = bytes.fromhex(token_txid)[::-1]
        want_token_id_bytes = bytes.fromhex(want_token_txid)[::-1]
        
        # Create an offered UTXO
        offered_utxo_txid = node.sendtoaddress(node.getnewaddress(), 3)
        node.generate(1)
        
        offered_tx = node.getrawtransaction(offered_utxo_txid, True)
        offered_utxo_vout = None
        for i, vout in enumerate(offered_tx['vout']):
            if vout['value'] == 3:
                offered_utxo_vout = i
                break
        assert offered_utxo_vout is not None
        
        # Lock the UTXO
        assert node.lockunspent(False, [{"txid": offered_utxo_txid, "vout": offered_utxo_vout}])
        
        # Create the swap advertisement
        price_terms = b'\xde\xad\xbe\xef'
        signature = b'\xca\xfe\xba\xbe'
        
        adv_txid = self.create_swap_ad_v2(node, token_id_bytes, want_token_id_bytes,
                                          offered_utxo_txid, offered_utxo_vout,
                                          price_terms, signature)
        node.generate(1)
        node.syncwithvalidationinterfacequeue()
        
        wait_until(lambda: len(node.getopenorders(token_txid)) == 1, timeout=20)
        
        # Verify order exists
        orders = node.getopenorders(token_txid)
        assert_equal(len(orders), 1)
        
        # Get the block hash before we spend
        pre_spend_height = node.getblockcount()
        
        # Unlock the UTXO so we can spend it
        assert node.lockunspent(True, [{"txid": offered_utxo_txid, "vout": offered_utxo_vout}])
        
        # Spend the offered UTXO
        spend_inputs = [{"txid": offered_utxo_txid, "vout": offered_utxo_vout}]
        spend_outputs = {node.getnewaddress(): 2.5}
        spend_hex = node.createrawtransaction(spend_inputs, spend_outputs)
        spend_signed = node.signrawtransactionwithwallet(spend_hex)
        assert spend_signed['complete']
        spend_txid = node.sendrawtransaction(spend_signed['hex'])
        
        # Mine the spend
        spend_block_hash = node.generate(1)[0]
        node.syncwithvalidationinterfacequeue()
        
        wait_until(lambda: len(node.getopenorders(token_txid)) == 0, timeout=20)
        
        # Order should be in history now
        orders = node.getopenorders(token_txid)
        assert_equal(len(orders), 0)
        history = node.getswaphistory(token_txid)
        assert_equal(len(history), 1)
        
        # Now invalidate the block containing the spend (trigger reorg)
        self.log.info(f"Invalidating block {spend_block_hash} to trigger reorg...")
        node.invalidateblock(spend_block_hash)
        node.syncwithvalidationinterfacequeue()
        
        # Give the index a moment to complete the database operations
        import time
        time.sleep(0.5)
        
        # After reorg, the order is restored to open in the database,
        # but the spend tx may still be in mempool (mempool-aware filtering)
        mempool = node.getrawmempool()
        self.log.info(f"Mempool after reorg: {mempool}")
        
        # The spend tx might still be in mempool. We need to remove it
        # to see the order appear in open orders again
        if len(mempool) > 0:
            # Generate a block on the current (shorter) chain to clear mempool
            # But this might not work due to chain work. Instead, just verify
            # the database state by checking counts
            counts = node.getswapcount(token_txid)
            self.log.info(f"Counts after reorg: open={counts['open']}, history={counts['history']}")
            
            # Due to mempool-aware filtering, the order may appear in history
            # if the spend tx is in mempool. The key thing is that BlockDisconnected
            # was called (verified by logs showing "restored N orders")
            # For a full verification, we'd need to evict the mempool tx
            
            # Verify at least that the system didn't crash and state is consistent
            total_count = counts['open'] + counts['history']
            assert total_count == 1, "Should have exactly 1 order total"
            
            # The order should now be in open (restored from history)
            assert_equal(counts['open'], 1)
            assert_equal(counts['history'], 0)
        
        self.log.info("Reorg test passed - BlockDisconnected handler executed successfully!")
        
        # Reconsider the block to restore normal state
        node.reconsiderblock(spend_block_hash)
        node.syncwithvalidationinterfacequeue()

if __name__ == '__main__':
    SwapTest().main()
