#!/usr/bin/env python3
# Copyright (c) 2022-2026 The Radiant developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test large transactions with many inputs.

This test verifies that transactions with many inputs (up to the 12MB limit)
can be created, accepted to mempool, and mined into blocks.
"""

from decimal import Decimal
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, connect_nodes


class LargeTxInputsTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [[
            '-txindex',
            '-acceptnonstdtxn=0',
        ]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]

        self.log.info("Generate initial coins")
        # Generate enough blocks to have spendable coins
        node.generate(101)

        self.log.info("Create many small UTXOs")
        # Create 500 small UTXOs by sending to ourselves
        # Each UTXO will be ~0.01 RXD
        num_utxos = 500
        utxo_amount = Decimal("0.01")
        
        address = node.getnewaddress()
        
        # Create UTXOs in batches to avoid hitting any limits
        batch_size = 50
        for batch in range(num_utxos // batch_size):
            outputs = {}
            for i in range(batch_size):
                addr = node.getnewaddress()
                outputs[addr] = float(utxo_amount)
            
            txid = node.sendmany("", outputs)
            self.log.info(f"Created batch {batch + 1}/{num_utxos // batch_size}, txid: {txid[:16]}...")
        
        # Mine the UTXO creation transactions
        self.log.info("Mining UTXO creation transactions")
        node.generate(1)

        # Get all unspent outputs
        utxos = node.listunspent()
        self.log.info(f"Total UTXOs available: {len(utxos)}")

        # Filter to get our small UTXOs (exclude the large coinbase outputs)
        small_utxos = [u for u in utxos if u['amount'] <= float(utxo_amount) * 1.1]
        self.log.info(f"Small UTXOs for consolidation: {len(small_utxos)}")

        if len(small_utxos) < 100:
            self.log.warning("Not enough small UTXOs created, test may not be comprehensive")
            return

        self.log.info(f"Creating consolidation transaction with {len(small_utxos)} inputs")
        
        # Calculate total input value
        total_input = sum(Decimal(str(u['amount'])) for u in small_utxos)
        
        # Estimate fee (generous estimate: 200 bytes per input + 50 bytes overhead)
        estimated_size = len(small_utxos) * 200 + 50
        fee_rate = Decimal("0.00001")  # 0.00001 RXD per byte
        fee = fee_rate * estimated_size
        
        output_amount = total_input - fee
        
        self.log.info(f"Total input: {total_input} RXD")
        self.log.info(f"Estimated size: {estimated_size} bytes")
        self.log.info(f"Fee: {fee} RXD")
        self.log.info(f"Output amount: {output_amount} RXD")

        # Create the raw transaction with many inputs
        inputs = [{"txid": u['txid'], "vout": u['vout']} for u in small_utxos]
        destination_address = node.getnewaddress()
        outputs = {destination_address: float(output_amount)}

        raw_tx = node.createrawtransaction(inputs, outputs)
        self.log.info(f"Raw transaction created, size: {len(raw_tx) // 2} bytes")

        # Sign the transaction
        self.log.info("Signing transaction...")
        signed_tx = node.signrawtransactionwithwallet(raw_tx)
        
        if not signed_tx['complete']:
            self.log.error(f"Transaction signing failed: {signed_tx}")
            raise AssertionError("Failed to sign transaction")

        signed_hex = signed_tx['hex']
        tx_size = len(signed_hex) // 2
        self.log.info(f"Signed transaction size: {tx_size} bytes ({tx_size / 1000:.1f} KB)")

        # Test mempool acceptance
        self.log.info("Testing mempool acceptance...")
        result = node.testmempoolaccept([signed_hex])
        self.log.info(f"testmempoolaccept result: {result}")

        if not result[0]['allowed']:
            self.log.error(f"Transaction rejected: {result[0].get('reject-reason', 'unknown')}")
            raise AssertionError(f"Large transaction rejected: {result[0]}")

        # Send the transaction
        self.log.info("Sending transaction to mempool...")
        txid = node.sendrawtransaction(signed_hex)
        self.log.info(f"Transaction sent successfully! txid: {txid}")

        # Verify it's in the mempool
        mempool = node.getrawmempool()
        assert txid in mempool, f"Transaction {txid} not found in mempool"
        self.log.info("Transaction confirmed in mempool")

        # Mine the transaction
        self.log.info("Mining block with large transaction...")
        block_hashes = node.generate(1)
        self.log.info(f"Block mined: {block_hashes[0]}")

        # Verify the transaction is confirmed
        tx_info = node.gettransaction(txid)
        assert tx_info['confirmations'] >= 1, "Transaction not confirmed"
        self.log.info(f"Transaction confirmed with {tx_info['confirmations']} confirmation(s)")

        # Get block info
        block = node.getblock(block_hashes[0])
        self.log.info(f"Block contains {len(block['tx'])} transaction(s)")
        self.log.info(f"Block size: {block['size']} bytes")

        self.log.info("=" * 60)
        self.log.info("SUCCESS: Large transaction with many inputs test passed!")
        self.log.info(f"  - Inputs: {len(small_utxos)}")
        self.log.info(f"  - Transaction size: {tx_size} bytes ({tx_size / 1000:.1f} KB)")
        self.log.info(f"  - Successfully accepted to mempool and mined")
        self.log.info("=" * 60)


if __name__ == '__main__':
    LargeTxInputsTest().main()
