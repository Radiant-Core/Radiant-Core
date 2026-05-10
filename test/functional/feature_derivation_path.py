#!/usr/bin/env python3
# Copyright (c) 2024 The Bitcoin Core developers
# Copyright (c) 2022-2026 The Radiant developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test derivation path standardization to BIP44 SLIP-0044 (coin type 512).

This test verifies:
1. New wallets default to BIP44 Radiant Standard path (m/44'/512'/0'/0/k)
2. Legacy wallets (pre-3.0.0) continue using m/0'/0'/k
3. -derivationtype=legacy flag creates wallets with legacy path
4. createwallet RPC legacy_derivation parameter works
5. sethdseed RPC coin_type parameter works
6. getaddressinfo returns correct hdkeypath
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)

class DerivationPathTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [["-keypool=10"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]

        self.log.info("Test 1: New wallet defaults to BIP44 Radiant Standard path (m/44'/512'/0'/0/k)")
        wallet_standard = node.createwallet("standard_wallet")
        wallet_standard_rpc = node.get_wallet_rpc("standard_wallet")
        
        # Generate a new address and check its derivation path
        addr_standard = wallet_standard_rpc.getnewaddress()
        addr_info = wallet_standard_rpc.getaddressinfo(addr_standard)
        
        # New wallets should use m/44'/512'/0'/0/0 for the first external address
        expected_path_standard = "m/44'/512'/0'/0/0"
        assert addr_info["hdkeypath"] == expected_path_standard, \
            f"Expected path {expected_path_standard}, got {addr_info['hdkeypath']}"
        self.log.info(f"  ✓ Standard wallet address {addr_standard} uses path: {addr_info['hdkeypath']}")

        self.log.info("Test 2: Wallet with -derivationtype=legacy uses legacy path (m/0'/0'/k)")
        # Restart node with legacy derivation type
        self.restart_node(0, extra_args=["-keypool=10", "-derivationtype=legacy"])
        
        wallet_legacy = node.createwallet("legacy_wallet")
        wallet_legacy_rpc = node.get_wallet_rpc("legacy_wallet")
        
        addr_legacy = wallet_legacy_rpc.getnewaddress()
        addr_info_legacy = wallet_legacy_rpc.getaddressinfo(addr_legacy)
        
        # Legacy wallets should use m/0'/0'/0' for the first external address (hardened)
        expected_path_legacy = "m/0'/0'/0'"
        assert addr_info_legacy["hdkeypath"] == expected_path_legacy, \
            f"Expected path {expected_path_legacy}, got {addr_info_legacy['hdkeypath']}"
        self.log.info(f"  ✓ Legacy wallet address {addr_legacy} uses path: {addr_info_legacy['hdkeypath']}")

        self.log.info("Test 3: createwallet RPC with legacy_derivation=true uses legacy path")
        # Restart node without the flag to test RPC parameter
        self.restart_node(0, extra_args=["-keypool=10"])
        
        wallet_rpc_legacy = node.createwallet("rpc_legacy_wallet", False, False, True)
        wallet_rpc_legacy_rpc = node.get_wallet_rpc("rpc_legacy_wallet")
        
        addr_rpc_legacy = wallet_rpc_legacy_rpc.getnewaddress()
        addr_info_rpc_legacy = wallet_rpc_legacy_rpc.getaddressinfo(addr_rpc_legacy)
        
        assert addr_info_rpc_legacy["hdkeypath"] == "m/0'/0'/0'", \
            f"Expected legacy path, got {addr_info_rpc_legacy['hdkeypath']}"
        self.log.info(f"  ✓ RPC legacy wallet uses path: {addr_info_rpc_legacy['hdkeypath']}")

        self.log.info("Test 4: createwallet RPC without legacy_derivation uses standard path")
        wallet_rpc_standard = node.createwallet("rpc_standard_wallet", False, False, False)
        wallet_rpc_standard_rpc = node.get_wallet_rpc("rpc_standard_wallet")
        
        addr_rpc_standard = wallet_rpc_standard_rpc.getnewaddress()
        addr_info_rpc_standard = wallet_rpc_standard_rpc.getaddressinfo(addr_rpc_standard)
        
        assert addr_info_rpc_standard["hdkeypath"] == "m/44'/512'/0'/0/0", \
            f"Expected standard path, got {addr_info_rpc_standard['hdkeypath']}"
        self.log.info(f"  ✓ RPC standard wallet uses path: {addr_info_rpc_standard['hdkeypath']}")

        self.log.info("Test 5: Change addresses use correct internal path")
        # Standard wallet change address
        change_addr_standard = wallet_rpc_standard_rpc.getrawchangeaddress()
        change_info_standard = wallet_rpc_standard_rpc.getaddressinfo(change_addr_standard)
        
        # First change address should be m/44'/512'/0'/1/0
        expected_change_path = "m/44'/512'/0'/1/0"
        assert change_info_standard["hdkeypath"] == expected_change_path, \
            f"Expected change path {expected_change_path}, got {change_info_standard['hdkeypath']}"
        self.log.info(f"  ✓ Standard wallet change address uses path: {change_info_standard['hdkeypath']}")
        
        # Legacy wallet change address
        change_addr_legacy = wallet_rpc_legacy_rpc.getrawchangeaddress()
        change_info_legacy = wallet_rpc_legacy_rpc.getaddressinfo(change_addr_legacy)
        
        # First legacy change address should be m/0'/1'/0' (hardened)
        expected_legacy_change_path = "m/0'/1'/0'"
        assert change_info_legacy["hdkeypath"] == expected_legacy_change_path, \
            f"Expected legacy change path {expected_legacy_change_path}, got {change_info_legacy['hdkeypath']}"
        self.log.info(f"  ✓ Legacy wallet change address uses path: {change_info_legacy['hdkeypath']}")

        self.log.info("Test 6: sethdseed with coin_type parameter")
        # Get reference to standard wallet for operations
        wallet_standard_node = node.get_wallet_rpc("rpc_standard_wallet")
        
        # Need to generate blocks to exit IBD before calling sethdseed
        wallet_standard_node.generatetoaddress(1, wallet_standard_node.getnewaddress())
        
        # Create a blank wallet for testing sethdseed
        wallet_blank = node.createwallet("blank_wallet", False, True)
        wallet_blank_rpc = node.get_wallet_rpc("blank_wallet")
        
        # Use the standard wallet to generate a seed
        new_seed = wallet_standard_node.dumpprivkey(wallet_standard_node.getnewaddress())
        
        # Test sethdseed with coin_type=512 (default)
        result = wallet_blank_rpc.sethdseed(True, new_seed, 512)
        assert "warning" in result
        assert "Radiant Standard" in result["warning"]
        self.log.info(f"  ✓ sethdseed with coin_type=512: {result['warning']}")

        self.log.info("Test 7: Verify wallet flag persistence after restart")
        # The wallet flags should persist across restarts
        # Legacy wallet should continue using legacy path after restart
        self.restart_node(0, extra_args=["-keypool=10"])
        
        # Reload the legacy wallet after restart
        node.loadwallet("rpc_legacy_wallet")
        wallet_legacy_reloaded = node.get_wallet_rpc("rpc_legacy_wallet")
        addr_after_restart = wallet_legacy_reloaded.getnewaddress()
        addr_info_after_restart = wallet_legacy_reloaded.getaddressinfo(addr_after_restart)
        
        # After restart, the legacy wallet should still use legacy path
        # Second address should be m/0'/0'/1'
        assert addr_info_after_restart["hdkeypath"] == "m/0'/0'/1'", \
            f"Expected m/0'/0'/1' after restart, got {addr_info_after_restart['hdkeypath']}"
        self.log.info(f"  ✓ Legacy wallet persists after restart: {addr_info_after_restart['hdkeypath']}")

        self.log.info("Test 8: Invalid coin_type should be rejected")
        # Test that invalid coin_type values are rejected
        wallet_test = node.createwallet("test_coin_type", False, True)
        wallet_test_rpc = node.get_wallet_rpc("test_coin_type")
        # Load and get fresh reference to standard wallet after restart
        node.loadwallet("rpc_standard_wallet")
        wallet_std_fresh = node.get_wallet_rpc("rpc_standard_wallet")
        test_seed = wallet_std_fresh.dumpprivkey(wallet_std_fresh.getnewaddress())
        
        # coin_type 1 should fail (only 0 and 512 are valid)
        assert_raises_rpc_error(-8, "coin_type must be 0 (Legacy) or 512 (Radiant Standard)",
                                wallet_test_rpc.sethdseed, True, test_seed, 1)
        self.log.info("  ✓ Invalid coin_type (1) correctly rejected")
        
        # coin_type 999 should also fail
        assert_raises_rpc_error(-8, "coin_type must be 0 (Legacy) or 512 (Radiant Standard)",
                                wallet_test_rpc.sethdseed, True, test_seed, 999)
        self.log.info("  ✓ Invalid coin_type (999) correctly rejected")

        self.log.info("All derivation path tests passed!")


if __name__ == "__main__":
    DerivationPathTest().main()
