#!/usr/bin/env python3
# Copyright (c) 2023 The Radiant Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the -nodeprofile option."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error
from test_framework.test_node import ErrorMatch

class NodeProfileTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def run_test(self):
        node = self.nodes[0]

        self.log.info("1. Testing -nodeprofile=archive (default)...")
        # Note: self.setup_clean_chain starts the node with default args first.
        # We restart with specific args to be sure.
        self.restart_node(0, ["-nodeprofile=archive"])
        chain_info = node.getblockchaininfo()
        assert_equal(chain_info['pruned'], False)
        
        # Verify txindex is enabled.
        # We need a tx. Generate a block.
        # Use a fixed address for deterministic generation
        address = "n1E71CqQ87aW5bd6rFtm1GZ4yFqV7Q6C8n"
        block_hash = node.generatetoaddress(1, address)[0]
        block = node.getblock(block_hash)
        txid = block['tx'][0]
        # getrawtransaction should succeed
        node.getrawtransaction(txid)

        self.log.info("2. Testing override: -nodeprofile=archive -txindex=0...")
        self.restart_node(0, ["-nodeprofile=archive", "-txindex=0"])
        chain_info = node.getblockchaininfo()
        assert_equal(chain_info['pruned'], False)
        # txindex disabled, so getrawtransaction should fail for a tx not in mempool
        assert_raises_rpc_error(-5, "No such mempool transaction. Use -txindex or provide a block hash.", node.getrawtransaction, txid)

        self.log.info("3. Testing -nodeprofile=agent...")
        self.restart_node(0, ["-nodeprofile=agent"])
        chain_info = node.getblockchaininfo()
        assert_equal(chain_info['pruned'], True)
        
        # txindex disabled
        assert_raises_rpc_error(-5, "No such mempool transaction. Use -txindex or provide a block hash.", node.getrawtransaction, txid)

        self.log.info("4. Testing -nodeprofile=mining...")
        # Note: Switching from agent (prune=550) to mining (prune=4000) is just increasing target, safe.
        self.restart_node(0, ["-nodeprofile=mining"])
        chain_info = node.getblockchaininfo()
        assert_equal(chain_info['pruned'], True)
        # prune target checking is implied by it starting correctly with a valid prune setting
        
        # txindex disabled
        assert_raises_rpc_error(-5, "No such mempool transaction. Use -txindex or provide a block hash.", node.getrawtransaction, txid)

        self.log.info("5. Testing incompatibility: -nodeprofile=agent -txindex=1...")
        self.stop_node(0)
        # prune=550 + txindex=1 -> Incompatible
        self.nodes[0].assert_start_raises_init_error(
            ["-nodeprofile=agent", "-txindex=1"],
            "Prune mode is incompatible with -txindex",
            match=ErrorMatch.PARTIAL_REGEX
        )

if __name__ == '__main__':
    NodeProfileTest().main()
