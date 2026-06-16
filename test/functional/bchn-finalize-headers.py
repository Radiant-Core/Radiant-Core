#!/usr/bin/env python3
# Copyright (c) 2020 The Bitcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test processing of -finalizeheaders and -finalizeheaderspenalty

Setup: three nodes, not connected to each other.

  node0 — header finalization ENABLED, explicit penalty 50 (two below-finalized
          headers are needed to reach the ban threshold of 100, then it
          disconnects the submitter).
  node1 — header finalization DISABLED (below-finalized headers are accepted).
  node2 — header finalization ENABLED, DEFAULT penalty (0). This is the v3.1.2
          default: a below-finalized header is still REJECTED, but the peer is
          NEVER penalized/disconnected. This is the regression guard for the
          2026-06-15 deep-reorg partition, where the old default penalty (100 ==
          ban threshold) made a single honest announcement of the canonical
          chain disconnect the peer, so a stranded node self-isolated.

All nodes run with an EXPLICIT -maxreorgdepth so the test is independent of the
C++ default (which v3.1.2 raised to 69). The constant below MUST match the
-maxreorgdepth passed in extra_args, not the C++ DEFAULT_MAX_REORG_DEPTH.

Blocks are created so that the chains contain a finalized block. Then headers for
blocks that would replace the finalized one, and deeper, are created, and the
per-node ban/accept behavior is checked.
"""

import time

from test_framework.blocktools import (
    create_block,
    create_coinbase
)
from test_framework.messages import (
    CBlockHeader,
    msg_headers,
)
from test_framework.p2p import P2PInterface
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

# The -maxreorgdepth this test pins explicitly (see extra_args). Independent of
# the C++ DEFAULT_MAX_REORG_DEPTH so the test does not break when the default
# changes (it was raised 6 -> 69 in v3.1.2).
TEST_MAXREORGDEPTH = 10


def mine_header(prevblockhash, coinbase, timestamp):
    # Create a valid block and return its header
    block = create_block(int("0x" + prevblockhash, 0), coinbase, timestamp)
    block.solve()
    return CBlockHeader(block)


class FinalizedHeadersTest(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3
        # Pin maxreorgdepth and disable the finalization delay + parking so the
        # test is deterministic and independent of the C++ defaults.
        common_extra_args = ["-finalizationdelay=0", "-noparkdeepreorg",
                             "-maxreorgdepth={}".format(TEST_MAXREORGDEPTH)]
        self.extra_args = [
            # node0: explicit penalty 50 -> two below-finalized headers ban.
            common_extra_args + ["-finalizeheaders=1",
                                 "-finalizeheaderspenalty=50"],
            # node1: finalization disabled -> below-finalized headers accepted.
            common_extra_args + ["-finalizeheaders=0"],
            # node2: finalization enabled, DEFAULT penalty (0) -> reject but
            # never ban. (Deliberately no -finalizeheaderspenalty override.)
            common_extra_args + ["-finalizeheaders=1"],
        ]

    def setup_network(self):
        self.setup_nodes()

    def run_test(self):
        # Setup the p2p connections
        # node_with_finalheaders connects to node0
        node_with_finalheaders = self.nodes[0].add_p2p_connection(P2PInterface())
        # node_without_finalheaders connects to node1
        node_without_finalheaders = self.nodes[1].add_p2p_connection(P2PInterface())
        # node_default_penalty connects to node2 (v3.1.2 default penalty = 0)
        node_default_penalty = self.nodes[2].add_p2p_connection(P2PInterface())

        genesis_hash = [n.getbestblockhash() for n in self.nodes]
        assert_equal(genesis_hash[0], genesis_hash[1])
        assert_equal(genesis_hash[0], genesis_hash[2])

        for n in self.nodes:
            assert_equal(n.getblockcount(), 0)

        # Have nodes mine enough blocks to get them to finalize
        for i in range(2 * TEST_MAXREORGDEPTH + 1):
            [self.generatetoaddress(n, 1, n.get_deterministic_priv_key().address)
                for n in self.nodes]
            for n in self.nodes:
                assert_equal(n.getblockcount(), i + 1)

        for n in self.nodes:
            assert_equal(n.getblockcount(), 2 * TEST_MAXREORGDEPTH + 1)

        # Finalized block's height is now TEST_MAXREORGDEPTH (== 10)

        def construct_header_for(node, height, time_stamp):
            parent_hash = node.getblockhash(height - 1)
            return mine_header(parent_hash, create_coinbase(height), time_stamp)

        # For all nodes:
        # Replacement headers for block from tip down to last
        # non-finalized block should be accepted.
        block_time = int(time.time())
        node_0_blockheight = self.nodes[0].getblockcount()
        node_1_blockheight = self.nodes[1].getblockcount()
        node_2_blockheight = self.nodes[2].getblockcount()
        for i in range(1, TEST_MAXREORGDEPTH):
            # Create a header for node 0 and submit it
            headers_message = msg_headers()
            headers_message.headers.append(construct_header_for(self.nodes[0],
                                                                node_0_blockheight - i,
                                                                block_time))
            node_with_finalheaders.send_and_ping(headers_message)

            # Create a header for node 1 and submit it
            headers_message = msg_headers()
            headers_message.headers.append(construct_header_for(self.nodes[1],
                                                                node_1_blockheight - i,
                                                                block_time))
            node_without_finalheaders.send_and_ping(headers_message)

            # Create a header for node 2 and submit it
            headers_message = msg_headers()
            headers_message.headers.append(construct_header_for(self.nodes[2],
                                                                node_2_blockheight - i,
                                                                block_time))
            node_default_penalty.send_and_ping(headers_message)

            # All nodes remain connected in this loop because
            # the new headers do not attempt to replace the finalized block
            assert node_with_finalheaders.is_connected
            assert node_without_finalheaders.is_connected
            assert node_default_penalty.is_connected

        # Now, headers that would replace the finalized block...
        # The header-finalizing node (node0) should reject the deeper header
        # and get a DoS score of 50 while the non-header-finalizing node (node1)
        # will accept the header.
        headers_message = msg_headers()
        headers_message.headers.append(construct_header_for(self.nodes[0],
                                                            node_0_blockheight - TEST_MAXREORGDEPTH - 1,
                                                            block_time))
        # Node 0 has not yet been disconnected, but it got a rejection logged and penalized
        expected_header_rejection_msg = ["peer=0 (0 -> 50) reason: bad-header-finalization", ]
        with self.nodes[0].assert_debug_log(expected_msgs=expected_header_rejection_msg, timeout=10):
            node_with_finalheaders.send_and_ping(headers_message)
            # The long sleep below is for GitLab CI.
            # On local modern test machines a sleep of 1 second worked
            # very reliably.
            time.sleep(4)
        assert node_with_finalheaders.is_connected

        headers_message = msg_headers()
        headers_message.headers.append(construct_header_for(self.nodes[1],
                                                            node_1_blockheight - TEST_MAXREORGDEPTH - 1,
                                                            block_time))
        node_without_finalheaders.send_message(headers_message)
        time.sleep(1)
        assert node_without_finalheaders.is_connected

        # Now, one more header on node0...
        # The header-finalizing node should disconnect while the
        # non-header-finalizing node will accept the header.
        headers_message = msg_headers()
        headers_message.headers.append(construct_header_for(self.nodes[0],
                                                            node_0_blockheight - TEST_MAXREORGDEPTH - 1,
                                                            block_time))
        # Node 0 should disconnect when we send again
        expected_header_rejection_msg = ["peer=0 (50 -> 100) reason: bad-header-finalization", ]
        with self.nodes[0].assert_debug_log(expected_msgs=expected_header_rejection_msg, timeout=10):
            node_with_finalheaders.send_message(headers_message)
            # Again, a long sleep below only for GitLab CI.
            time.sleep(4)
        assert not node_with_finalheaders.is_connected

        headers_message = msg_headers()
        headers_message.headers.append(construct_header_for(self.nodes[1],
                                                            node_1_blockheight - TEST_MAXREORGDEPTH - 1,
                                                            block_time))
        node_without_finalheaders.send_message(headers_message)
        time.sleep(1)
        assert node_without_finalheaders.is_connected

        # v3.1.2 regression: node2 has finalization ENABLED with the DEFAULT
        # penalty (0). A header that would replace its finalized block must be
        # REJECTED, but the peer must NEVER be penalized/disconnected — no matter
        # how many times it is re-announced. With the old default penalty (100 ==
        # ban threshold) a SINGLE such header would have disconnected the peer.
        below_final_header = construct_header_for(
            self.nodes[2], node_2_blockheight - TEST_MAXREORGDEPTH - 1, block_time)
        # Send the below-finalized header many times (far more than the 2 that
        # would have banned node0 at penalty 50, and the 1 that the old default
        # of 100 would have banned on).
        expected_rejection = ["rejected due to existing finalized header"]
        with self.nodes[2].assert_debug_log(expected_msgs=expected_rejection, timeout=10):
            for _ in range(5):
                headers_message = msg_headers()
                headers_message.headers.append(below_final_header)
                node_default_penalty.send_message(headers_message)
                time.sleep(0.2)
            time.sleep(2)
        # The header was rejected (node2 did not reorg onto it)...
        assert_equal(self.nodes[2].getblockcount(), 2 * TEST_MAXREORGDEPTH + 1)
        # ...but the peer is still connected: penalty 0 never bans.
        assert node_default_penalty.is_connected


if __name__ == '__main__':
    FinalizedHeadersTest().main()
