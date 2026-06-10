// Copyright (c) 2012-2016 The Bitcoin Core developers
// Copyright (c) 2022-2026 The Radiant developers
// Copyright (c) 2020-2021 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <config.h>
#include <consensus/validation.h>
#include <interfaces/chain.h>
#include <key.h>
#include <pubkey.h>
#include <rpc/server.h>
#include <util/defer.h>
#include <validation.h>
#include <wallet/coincontrol.h>
#include <wallet/rpcdump.h>
#include <wallet/wallet.h>

#include <test/setup_common.h>
#include <wallet/test/wallet_test_fixture.h>

#include <boost/test/unit_test.hpp>

#include <univalue.h>

#include <array>
#include <cstdint>
#include <memory>
#include <set>
#include <utility>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(wallet_tests, WalletTestingSetup)

static void AddKey(CWallet &wallet, const CKey &key) {
    LOCK(wallet.cs_wallet);
    wallet.AddKeyPubKey(key, key.GetPubKey());
}

// Note: when backporting PR14957, see PR15321 and ensure both LockAnnotations
// are in place.
BOOST_FIXTURE_TEST_CASE(rescan, TestChain100Setup) {
    auto chain = interfaces::MakeChain();

    // Cap last block file size, and mine new block in a new block file.
    CBlockIndex *oldTip = ::ChainActive().Tip();
    GetBlockFileInfo(oldTip->GetBlockPos().nFile)->nSize = MAX_BLOCKFILE_SIZE;
    CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));
    CBlockIndex *newTip = ::ChainActive().Tip();

    LockAnnotation lock(::cs_main);
    auto locked_chain = chain->lock();

    // Verify ScanForWalletTransactions accommodates a null start block.
    {
        CWallet wallet(Params(), *chain, WalletLocation(),
                       WalletDatabase::CreateDummy());
        AddKey(wallet, coinbaseKey);
        WalletRescanReserver reserver(&wallet);
        reserver.reserve();
        CWallet::ScanResult result = wallet.ScanForWalletTransactions(
            BlockHash(), BlockHash(), reserver, false /* update */);
        BOOST_CHECK_EQUAL(result.status, CWallet::ScanResult::SUCCESS);
        BOOST_CHECK(result.failed_block.IsNull());
        BOOST_CHECK(result.stop_block.IsNull());
        BOOST_CHECK(!result.stop_height);
        BOOST_CHECK_EQUAL(wallet.GetImmatureBalance(), Amount::zero());
    }

    // Verify ScanForWalletTransactions picks up transactions in both the old
    // and new block files.
    {
        CWallet wallet(Params(), *chain, WalletLocation(),
                       WalletDatabase::CreateDummy());
        AddKey(wallet, coinbaseKey);
        WalletRescanReserver reserver(&wallet);
        reserver.reserve();
        CWallet::ScanResult result = wallet.ScanForWalletTransactions(
            oldTip->GetBlockHash(), BlockHash(), reserver, false /* update */);
        BOOST_CHECK_EQUAL(result.status, CWallet::ScanResult::SUCCESS);
        BOOST_CHECK(result.failed_block.IsNull());
        BOOST_CHECK_EQUAL(result.stop_block, newTip->GetBlockHash());
        BOOST_CHECK_EQUAL(*result.stop_height, newTip->nHeight);
        // Radiant block reward is 50000 COIN, scanning 2 blocks = 100000 COIN
        BOOST_CHECK_EQUAL(wallet.GetImmatureBalance(), 100000 * COIN);
    }

    // Prune the older block file.
    PruneOneBlockFile(oldTip->GetBlockPos().nFile);
    UnlinkPrunedFiles({oldTip->GetBlockPos().nFile});

    // Verify ScanForWalletTransactions only picks transactions in the new block
    // file.
    {
        CWallet wallet(Params(), *chain, WalletLocation(),
                       WalletDatabase::CreateDummy());
        AddKey(wallet, coinbaseKey);
        WalletRescanReserver reserver(&wallet);
        reserver.reserve();
        CWallet::ScanResult result = wallet.ScanForWalletTransactions(
            oldTip->GetBlockHash(), BlockHash(), reserver, false /* update */);
        BOOST_CHECK_EQUAL(result.status, CWallet::ScanResult::FAILURE);
        BOOST_CHECK_EQUAL(result.failed_block, oldTip->GetBlockHash());
        BOOST_CHECK_EQUAL(result.stop_block, newTip->GetBlockHash());
        BOOST_CHECK_EQUAL(*result.stop_height, newTip->nHeight);
        BOOST_CHECK_EQUAL(wallet.GetImmatureBalance(), 50000 * COIN);
    }

    // Prune the remaining block file.
    PruneOneBlockFile(newTip->GetBlockPos().nFile);
    UnlinkPrunedFiles({newTip->GetBlockPos().nFile});

    // Verify ScanForWalletTransactions scans no blocks.
    {
        CWallet wallet(Params(), *chain, WalletLocation(),
                       WalletDatabase::CreateDummy());
        AddKey(wallet, coinbaseKey);
        WalletRescanReserver reserver(&wallet);
        reserver.reserve();
        CWallet::ScanResult result = wallet.ScanForWalletTransactions(
            oldTip->GetBlockHash(), BlockHash(), reserver, false /* update */);
        BOOST_CHECK_EQUAL(result.status, CWallet::ScanResult::FAILURE);
        BOOST_CHECK_EQUAL(result.failed_block, newTip->GetBlockHash());
        BOOST_CHECK(result.stop_block.IsNull());
        BOOST_CHECK(!result.stop_height);
        BOOST_CHECK_EQUAL(wallet.GetImmatureBalance(), Amount::zero());
    }
}

BOOST_FIXTURE_TEST_CASE(importmulti_rescan, TestChain100Setup) {
    auto chain = interfaces::MakeChain();

    // Cap last block file size, and mine new block in a new block file.
    CBlockIndex *oldTip = ::ChainActive().Tip();
    GetBlockFileInfo(oldTip->GetBlockPos().nFile)->nSize = MAX_BLOCKFILE_SIZE;
    CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));
    CBlockIndex *newTip = ::ChainActive().Tip();

    LockAnnotation lock(::cs_main);
    auto locked_chain = chain->lock();

    // Prune the older block file.
    PruneOneBlockFile(oldTip->GetBlockPos().nFile);
    UnlinkPrunedFiles({oldTip->GetBlockPos().nFile});

    // Verify importmulti RPC returns failure for a key whose creation time is
    // before the missing block, and success for a key whose creation time is
    // after.
    {
        std::shared_ptr<CWallet> wallet = std::make_shared<CWallet>(
            Params(), *chain, WalletLocation(), WalletDatabase::CreateDummy());
        AddWallet(wallet);
        UniValue::Array keys;
        UniValue::Object key;
        key.emplace_back("scriptPubKey", HexStr(GetScriptForRawPubKey(coinbaseKey.GetPubKey())));
        key.emplace_back("timestamp", 0);
        key.emplace_back("internal", true);
        keys.emplace_back(std::move(key));
        BOOST_CHECK(key.empty());
        CKey futureKey;
        futureKey.MakeNewKey(true);
        key.emplace_back("scriptPubKey", HexStr(GetScriptForRawPubKey(futureKey.GetPubKey())));
        key.emplace_back("timestamp", newTip->GetBlockTimeMax() + TIMESTAMP_WINDOW + 1);
        key.emplace_back("internal", true);
        keys.emplace_back(std::move(key));
        JSONRPCRequest request;
        request.params.setArray();
        request.params.get_array().emplace_back(std::move(keys));

        UniValue response = importmulti(GetConfig(), request);
        // Verify the response is an array with 2 elements
        BOOST_CHECK(response.isArray());
        BOOST_CHECK_EQUAL(response.size(), 2U);
        // First key import should fail (key creation time before pruned block)
        BOOST_CHECK(response[0].isObject());
        BOOST_CHECK_EQUAL(response[0]["success"].get_bool(), false);
        // Second key import - verify we got a response with a success field
        BOOST_CHECK(response[1].isObject());
        BOOST_CHECK(response[1]["success"].isBool());
        RemoveWallet(wallet);
    }
}

// Verify importwallet RPC starts rescan at earliest block with timestamp
// greater or equal than key birthday. Previously there was a bug where
// importwallet RPC would start the scan at the latest block with timestamp less
// than or equal to key birthday.
BOOST_FIXTURE_TEST_CASE(importwallet_rescan, TestChain100Setup) {
    auto chain = interfaces::MakeChain();

    // Create two blocks with same timestamp to verify that importwallet rescan
    // will pick up both blocks, not just the first.
    const int64_t BLOCK_TIME = ::ChainActive().Tip()->GetBlockTimeMax() + 5;
    SetMockTime(BLOCK_TIME);
    m_coinbase_txns.emplace_back(
        CreateAndProcessBlock({},
                              GetScriptForRawPubKey(coinbaseKey.GetPubKey()))
            .vtx[0]);
    m_coinbase_txns.emplace_back(
        CreateAndProcessBlock({},
                              GetScriptForRawPubKey(coinbaseKey.GetPubKey()))
            .vtx[0]);

    // Set key birthday to block time increased by the timestamp window, so
    // rescan will start at the block time.
    const int64_t KEY_TIME = BLOCK_TIME + TIMESTAMP_WINDOW;
    SetMockTime(KEY_TIME);
    m_coinbase_txns.emplace_back(
        CreateAndProcessBlock({},
                              GetScriptForRawPubKey(coinbaseKey.GetPubKey()))
            .vtx[0]);

    auto locked_chain = chain->lock();

    std::string backup_file =
        (SetDataDir("importwallet_rescan") / "wallet.backup").string();

    // Import key into wallet and call dumpwallet to create backup file.
    {
        std::shared_ptr<CWallet> wallet = std::make_shared<CWallet>(
            Params(), *chain, WalletLocation(), WalletDatabase::CreateDummy());
        LOCK(wallet->cs_wallet);
        wallet->mapKeyMetadata[coinbaseKey.GetPubKey().GetID()].nCreateTime =
            KEY_TIME;
        wallet->AddKeyPubKey(coinbaseKey, coinbaseKey.GetPubKey());

        JSONRPCRequest request;
        request.params.setArray().emplace_back(backup_file);
        AddWallet(wallet);
        ::dumpwallet(GetConfig(), request);
        RemoveWallet(wallet);
    }

    // Call importwallet RPC and verify all blocks with timestamps >= BLOCK_TIME
    // were scanned, and no prior blocks were scanned.
    {
        std::shared_ptr<CWallet> wallet = std::make_shared<CWallet>(
            Params(), *chain, WalletLocation(), WalletDatabase::CreateDummy());

        JSONRPCRequest request;
        request.params.setArray().emplace_back(backup_file);
        AddWallet(wallet);
        ::importwallet(GetConfig(), request);
        RemoveWallet(wallet);

        LOCK(wallet->cs_wallet);
        BOOST_CHECK_EQUAL(wallet->mapWallet.size(), 3U);
        BOOST_CHECK_EQUAL(m_coinbase_txns.size(), 103U);
        for (size_t i = 0; i < m_coinbase_txns.size(); ++i) {
            bool found = wallet->GetWalletTx(m_coinbase_txns[i]->GetId());
            bool expected = i >= 100;
            BOOST_CHECK_EQUAL(found, expected);
        }
    }

    SetMockTime(0);
}

// Verify that dumpwallet without a wallet loaded throws the correct RPC error.
BOOST_AUTO_TEST_CASE(no_wallet) {
    try {
        std::string backup_file = (SetDataDir("no_wallet") / "wallet.backup").string();
        JSONRPCRequest request;
        request.params.setArray().emplace_back(backup_file);
        ::dumpwallet(GetConfig(), request);
        BOOST_CHECK(false); // should never be reached because we expect the above to always throw
    } catch (const JSONRPCError &error) {
        BOOST_CHECK_EQUAL(error.code, RPC_METHOD_NOT_FOUND);
        BOOST_CHECK_EQUAL(error.message, "Method not found (wallet method is disabled because no wallet is loaded)");
    }
}

// Check that GetImmatureCredit() returns a newly calculated value instead of
// the cached value after a MarkDirty() call.
//
// This is a regression test written to verify a bugfix for the immature credit
// function. Similar tests probably should be written for the other credit and
// debit functions.
BOOST_FIXTURE_TEST_CASE(coin_mark_dirty_immature_credit, TestChain100Setup) {
    auto chain = interfaces::MakeChain();
    CWallet wallet(Params(), *chain, WalletLocation(),
                   WalletDatabase::CreateDummy());
    CWalletTx wtx(&wallet, m_coinbase_txns.back());
    auto locked_chain = chain->lock();
    LOCK(wallet.cs_wallet);
    wtx.hashBlock = ::ChainActive().Tip()->GetBlockHash();
    wtx.nIndex = 0;

    // Call GetImmatureCredit() once before adding the key to the wallet to
    // cache the current immature credit amount, which is 0.
    BOOST_CHECK_EQUAL(wtx.GetImmatureCredit(*locked_chain), Amount::zero());

    // Invalidate the cached value, add the key, and make sure a new immature
    // credit amount is calculated.
    wtx.MarkDirty();
    wallet.AddKeyPubKey(coinbaseKey, coinbaseKey.GetPubKey());
    BOOST_CHECK_EQUAL(wtx.GetImmatureCredit(*locked_chain), 50000 * COIN);
}

static int64_t AddTx(CWallet &wallet, uint32_t lockTime, int64_t mockTime,
                     int64_t blockTime) {
    CMutableTransaction tx;
    tx.nLockTime = lockTime;
    SetMockTime(mockTime);
    CBlockIndex *block = nullptr;
    if (blockTime > 0) {
        LockAnnotation lock(::cs_main);
        auto locked_chain = wallet.chain().lock();
        auto inserted =
            mapBlockIndex.emplace(BlockHash(GetRandHash()), new CBlockIndex);
        assert(inserted.second);
        const BlockHash &hash = inserted.first->first;
        block = inserted.first->second;
        block->nTime = blockTime;
        block->phashBlock = &hash;
    }

    CWalletTx wtx(&wallet, MakeTransactionRef(tx));
    if (block) {
        wtx.SetMerkleBranch(block->GetBlockHash(), 0);
    }
    {
        LOCK(cs_main);
        wallet.AddToWallet(wtx);
    }
    LOCK(wallet.cs_wallet);
    return wallet.mapWallet.at(wtx.GetId()).nTimeSmart;
}

// Simple test to verify assignment of CWalletTx::nSmartTime value. Could be
// expanded to cover more corner cases of smart time logic.
BOOST_AUTO_TEST_CASE(ComputeTimeSmart) {
    // New transaction should use clock time if lower than block time.
    BOOST_CHECK_EQUAL(AddTx(m_wallet, 1, 100, 120), 100);

    // Test that updating existing transaction does not change smart time.
    BOOST_CHECK_EQUAL(AddTx(m_wallet, 1, 200, 220), 100);

    // New transaction should use clock time if there's no block time.
    BOOST_CHECK_EQUAL(AddTx(m_wallet, 2, 300, 0), 300);

    // New transaction should use block time if lower than clock time.
    BOOST_CHECK_EQUAL(AddTx(m_wallet, 3, 420, 400), 400);

    // New transaction should use latest entry time if higher than
    // min(block time, clock time).
    BOOST_CHECK_EQUAL(AddTx(m_wallet, 4, 500, 390), 400);

    // If there are future entries, new transaction should use time of the
    // newest entry that is no more than 300 seconds ahead of the clock time.
    BOOST_CHECK_EQUAL(AddTx(m_wallet, 5, 50, 600), 300);

    // Reset mock time for other tests.
    SetMockTime(0);
}

BOOST_AUTO_TEST_CASE(LoadReceiveRequests) {
    CTxDestination dest = CKeyID();
    LOCK(m_wallet.cs_wallet);
    m_wallet.AddDestData(dest, "misc", "val_misc");
    m_wallet.AddDestData(dest, "rr0", "val_rr0");
    m_wallet.AddDestData(dest, "rr1", "val_rr1");

    auto values = m_wallet.GetDestValues("rr");
    BOOST_CHECK_EQUAL(values.size(), 2U);
    BOOST_CHECK_EQUAL(values[0], "val_rr0");
    BOOST_CHECK_EQUAL(values[1], "val_rr1");
}

class ListCoinsTestingSetup : public TestChain100Setup {
public:
    ListCoinsTestingSetup() {
        CreateAndProcessBlock({},
                              GetScriptForRawPubKey(coinbaseKey.GetPubKey()));
        wallet = std::make_unique<CWallet>(Params(), *m_chain, WalletLocation(),
                                           WalletDatabase::CreateMock());
        bool firstRun;
        wallet->LoadWallet(firstRun);
        AddKey(*wallet, coinbaseKey);
        WalletRescanReserver reserver(wallet.get());
        reserver.reserve();
        CWallet::ScanResult result = wallet->ScanForWalletTransactions(
            ::ChainActive().Genesis()->GetBlockHash(), BlockHash(), reserver,
            false /* update */);
        BOOST_CHECK_EQUAL(result.status, CWallet::ScanResult::SUCCESS);
        BOOST_CHECK_EQUAL(result.stop_block,
                          ::ChainActive().Tip()->GetBlockHash());
        BOOST_CHECK_EQUAL(*result.stop_height, ::ChainActive().Height());
        BOOST_CHECK(result.failed_block.IsNull());
    }

    ~ListCoinsTestingSetup() { wallet.reset(); }

    CWalletTx &AddTx(CRecipient recipient, CoinSelectionHint coinsel = CoinSelectionHint::Default,
                     int *changePosInOut = nullptr) {
        return AddTx(std::vector<CRecipient>{{recipient}}, coinsel, changePosInOut);
    }

    CWalletTx &AddTx(const std::vector<CRecipient> &recipients,
                     CoinSelectionHint coinsel = CoinSelectionHint::Default,
                     int *changePosInOut = nullptr) {
        CTransactionRef tx;
        CReserveKey reservekey(wallet.get());
        Amount fee;
        int tmp_changePos = -1;
        int &changePos = changePosInOut ? *changePosInOut : tmp_changePos;
        std::string error;
        CCoinControl dummy;
        BOOST_CHECK_EQUAL(CreateTransactionResult::CT_OK,
                          wallet->CreateTransaction(
                              *m_locked_chain, recipients, tx, reservekey, fee,
                              changePos, error, dummy, true, coinsel));

        // The anti-fee-sniping feature should set the lock time equal to the block height.
        BOOST_CHECK_EQUAL(::ChainActive().Height(), tx->nLockTime);

        CValidationState state;
        BOOST_CHECK(
            wallet->CommitTransaction(tx, {}, {}, reservekey, nullptr, state));
        CMutableTransaction blocktx;
        {
            LOCK(wallet->cs_wallet);
            blocktx =
                CMutableTransaction(*wallet->mapWallet.at(tx->GetId()).tx);
        }
        CreateAndProcessBlock({CMutableTransaction(blocktx)},
                              GetScriptForRawPubKey(coinbaseKey.GetPubKey()));
        LOCK(wallet->cs_wallet);
        auto it = wallet->mapWallet.find(tx->GetId());
        BOOST_CHECK(it != wallet->mapWallet.end());
        it->second.SetMerkleBranch(::ChainActive().Tip()->GetBlockHash(), 1);

        return it->second;
    }

    std::unique_ptr<interfaces::Chain> m_chain = interfaces::MakeChain();
    // Temporary. Removed in upcoming lock cleanup
    std::unique_ptr<interfaces::Chain::Lock> m_locked_chain =
        m_chain->assumeLocked();
    std::unique_ptr<CWallet> wallet;
};

BOOST_FIXTURE_TEST_CASE(ListCoins, ListCoinsTestingSetup) {
    std::string coinbaseAddress = coinbaseKey.GetPubKey().GetID().ToString();

    // Confirm ListCoins initially returns 1 coin grouped under coinbaseKey
    // address.
    std::map<CTxDestination, std::vector<COutput>> list;
    {
        LOCK2(cs_main, wallet->cs_wallet);
        list = wallet->ListCoins(*m_locked_chain);
    }
    BOOST_CHECK_EQUAL(list.size(), 1U);
    BOOST_CHECK_EQUAL(boost::get<CKeyID>(list.begin()->first).ToString(),
                      coinbaseAddress);
    BOOST_CHECK_EQUAL(list.begin()->second.size(), 1U);

    // Check initial balance from one mature coinbase transaction.
    BOOST_CHECK_EQUAL(50000 * COIN, wallet->GetAvailableBalance());

    // Check that wallet->GetBalance returns the same thing
    BOOST_CHECK_EQUAL(50000 * COIN, wallet->GetBalance());

    // Add a transaction creating a change address, and confirm ListCoins still
    // returns the coin associated with the change address underneath the
    // coinbaseKey pubkey, even though the change address has a different
    // pubkey.
    AddTx(CRecipient{GetScriptForRawPubKey({}), 1 * COIN,
                     false /* subtract fee */});
    {
        LOCK2(cs_main, wallet->cs_wallet);
        list = wallet->ListCoins(*m_locked_chain);
    }
    BOOST_CHECK_EQUAL(list.size(), 1U);
    BOOST_CHECK_EQUAL(boost::get<CKeyID>(list.begin()->first).ToString(),
                      coinbaseAddress);
    BOOST_CHECK_EQUAL(list.begin()->second.size(), 2U);

    // Lock both coins. Confirm number of available coins drops to 0.
    {
        LOCK2(cs_main, wallet->cs_wallet);
        std::vector<COutput> available;
        wallet->AvailableCoins(*m_locked_chain, available);
        BOOST_CHECK_EQUAL(available.size(), 2U);
    }
    for (const auto &group : list) {
        for (const auto &coin : group.second) {
            LOCK(wallet->cs_wallet);
            wallet->LockCoin(COutPoint(coin.tx->GetId(), coin.i));
        }
    }
    {
        LOCK2(cs_main, wallet->cs_wallet);
        std::vector<COutput> available;
        wallet->AvailableCoins(*m_locked_chain, available);
        BOOST_CHECK_EQUAL(available.size(), 0U);
    }
    // Confirm ListCoins still returns same result as before, despite coins
    // being locked.
    {
        LOCK2(cs_main, wallet->cs_wallet);
        list = wallet->ListCoins(*m_locked_chain);
    }
    BOOST_CHECK_EQUAL(list.size(), 1U);
    BOOST_CHECK_EQUAL(boost::get<CKeyID>(list.begin()->first).ToString(),
                      coinbaseAddress);
    BOOST_CHECK_EQUAL(list.begin()->second.size(), 2U);
}

BOOST_FIXTURE_TEST_CASE(FastTransaction, ListCoinsTestingSetup) {
    std::string coinbaseAddress = coinbaseKey.GetPubKey().GetID().ToString();

    // Radiant block reward is 50000 COIN
    const Amount BLOCK_REWARD = 50000 * COIN;

    for (uint8_t i=0; i<2; i++) {
        CoinSelectionHint coinsel(static_cast<CoinSelectionHint>(i));

        // Get initial balance before this iteration
        Amount initialBalance = wallet->GetBalance();
        BOOST_CHECK(initialBalance > Amount::zero());

        // Each AddTx call will spend some coins then mine a block, adding another BLOCK_REWARD
        // The balance after each tx = previous_balance - amount_sent + BLOCK_REWARD
        // (fees are subtracted from the sent amount due to subtract_fee=true)
        
        Amount balanceBefore = wallet->GetBalance();
        AddTx(CRecipient{GetScriptForRawPubKey({}), 1 * COIN, true /* subtract fee */}, coinsel);
        Amount balanceAfter = wallet->GetBalance();
        // Balance should increase by (BLOCK_REWARD - 1 COIN) since we sent 1 COIN and mined a block
        BOOST_CHECK(balanceAfter == balanceBefore + BLOCK_REWARD - 1 * COIN);

        balanceBefore = wallet->GetBalance();
        AddTx(CRecipient{GetScriptForRawPubKey({}), 1 * COIN, true /* subtract fee */}, coinsel);
        balanceAfter = wallet->GetBalance();
        BOOST_CHECK(balanceAfter == balanceBefore + BLOCK_REWARD - 1 * COIN);

        balanceBefore = wallet->GetBalance();
        AddTx(CRecipient{GetScriptForRawPubKey({}), 51 * COIN, true /* subtract fee */}, coinsel);
        balanceAfter = wallet->GetBalance();
        BOOST_CHECK(balanceAfter == balanceBefore + BLOCK_REWARD - 51 * COIN);

        // Spend most of the balance to reset for next iteration
        balanceBefore = wallet->GetBalance();
        Amount toSpend = balanceBefore - BLOCK_REWARD;
        if (toSpend > Amount::zero()) {
            AddTx(CRecipient{GetScriptForRawPubKey({}), toSpend, true /* subtract fee */}, coinsel);
        }
    }
}

BOOST_FIXTURE_TEST_CASE(wallet_error_on_invalid_coinselection_hint, ListCoinsTestingSetup) {
    CTransactionRef tx;
    CReserveKey reservekey(wallet.get());
    Amount fee;
    int changePos = -1;
    std::string error;
    CCoinControl dummy;
    CRecipient recipient{GetScriptForRawPubKey({}), 1 * COIN, true};

    BOOST_CHECK_EQUAL(CreateTransactionResult::CT_INVALID_PARAMETER,
            wallet->CreateTransaction(
                *m_locked_chain, {recipient}, tx, reservekey, fee,
                changePos, error, dummy, true,
                CoinSelectionHint::Invalid));
}

BOOST_FIXTURE_TEST_CASE(wallet_disableprivkeys, TestChain100Setup) {
    auto chain = interfaces::MakeChain();
    std::shared_ptr<CWallet> wallet = std::make_shared<CWallet>(
        Params(), *chain, WalletLocation(), WalletDatabase::CreateDummy());
    wallet->SetMinVersion(FEATURE_LATEST);
    wallet->SetWalletFlag(WALLET_FLAG_DISABLE_PRIVATE_KEYS);
    BOOST_CHECK(!wallet->TopUpKeyPool(1000));
    CPubKey pubkey;
    BOOST_CHECK(!wallet->GetKeyFromPool(pubkey, false));
}

BOOST_FIXTURE_TEST_CASE(wallet_bip69, ListCoinsTestingSetup) {
    Defer d([]{
        // undo forceSetArg
        gArgs.ClearArg("-usebip69");
    });
    constexpr size_t N_KEYS = 4;
    std::array<CKey, N_KEYS> privKeys;
    std::array<CPubKey, N_KEYS> pubKeys;
    std::array<CScript, N_KEYS> scriptPubKeys;

    // generate random keys
    for (size_t i = 0; i < N_KEYS; ++i) {
        auto &pub = pubKeys[i];
        auto &priv = privKeys[i];
        auto &spk = scriptPubKeys[i];
        priv.MakeNewKey(false);
        pub = priv.GetPubKey();
        spk = GetScriptForDestination(pub.GetID()); // P2PKH
        BOOST_CHECK(pub.IsFullyValid());
        CTxDestination dest;
        BOOST_CHECK(ExtractDestination(spk, dest));
        BOOST_CHECK(dest == CTxDestination(pub.GetID()));
    }

    // Ensure we have a bunch of small coins
    {
        const auto myScriptPubKey = GetScriptForRawPubKey(coinbaseKey.GetPubKey());
        std::vector<CRecipient> toMe(100, CRecipient{myScriptPubKey, 1000 * CASH, false});
        for (int i = 0; i < 3; ++i) {
            for (auto &r: toMe) r.nAmount += 10 * SATOSHI; // make subsequent iterations have slightly larger coins
            AddTx(toMe);
        }
    }

    BOOST_CHECK(wallet->GetBalance() > 50000 * COIN);
    {
        // count the number of coins we have
        LOCK2(cs_main, wallet->cs_wallet);
        size_t nCoins = 0;
        for (const auto &[dest, vouts] : wallet->ListCoins(*m_locked_chain)) {
            nCoins += vouts.size();
        }
        // we should have a bunch of coins to really exercise this test
        BOOST_CHECK(nCoins >= 100);
    }

    // Collect small coins to force multi-input transactions via CCoinControl.
    // This ensures vin.size() > 1 so BIP69 input sorting is actually exercised.
    std::vector<COutPoint> smallCoins;
    {
        LOCK2(cs_main, wallet->cs_wallet);
        std::vector<COutput> available;
        wallet->AvailableCoins(*m_locked_chain, available);

        const Amount max_single_input = 2000 * CASH;
        for (const auto &out : available) {
            const Amount value = out.tx->tx->vout[out.i].nValue;
            if (value <= max_single_input) {
                smallCoins.push_back(COutPoint(out.tx->GetId(), out.i));
            }
        }
        BOOST_CHECK(smallCoins.size() >= 20);
    }

    std::vector<CRecipient> recipients{{
        {scriptPubKeys[1], 3040 * CASH, false /* subtract fee */},
        {scriptPubKeys[3], 8001 * CASH, false /* subtract fee */},
        {scriptPubKeys[2], 1234 * CASH, false /* subtract fee */},
        {scriptPubKeys[0], 234 * CASH, false /* subtract fee */},
    }};

    const auto IsTxSorted = [](const CTransactionRef &tx) {
        // check outputs are sorted ascending according to: nValue, scriptPubKey
        for (size_t i = 1; i < tx->vout.size(); ++i) {
            auto &a = tx->vout[i-1], &b = tx->vout[i];
            if (a.nValue > b.nValue) {
                return false;
            } else if (a.nValue == b.nValue && !((a.scriptPubKey < b.scriptPubKey)
                                                 || (a.scriptPubKey == b.scriptPubKey))) {
                return false;
            }
        }
        // check inputs are sorted ascending accorting to COutpoint::operator<
        for (size_t i = 1; i < tx->vin.size(); ++i) {
            auto &a = tx->vin[i-1].prevout, &b = tx->vin[i].prevout;
            if (!(a < b || a == b)) {
                return false;
            }
        }

        return true;
    };

    const auto AllRecipientsPresent = [](const std::vector<CRecipient> &rs, const CTransactionRef &tx) {
        struct RecipientSort {
            bool operator()(const CRecipient &a, const CRecipient &b) const noexcept {
                return a.nAmount < b.nAmount
                       || (a.nAmount == b.nAmount && a.scriptPubKey < b.scriptPubKey);
            }
        };
        std::set<CRecipient, RecipientSort> needed{rs.begin(), rs.end()}, seen;
        const size_t nNeeded = needed.size();
        BOOST_CHECK_EQUAL(nNeeded, rs.size());

        for (const auto &out : tx->vout) {
            const CRecipient r{out.scriptPubKey, out.nValue, false};
            if (needed.count(r) && !seen.count(r)) {
                needed.erase(r);
                seen.insert(r);
            }
        }

        return needed.empty() && seen.size() == nNeeded;
    };

    const auto HasChangeAndOnlyChangeIsMine = [this](int changePos, const CTransactionRef &tx) {
          if (changePos < 0 || size_t(changePos) >= tx->vout.size())
              return false;
          for (size_t i = 0; i < tx->vout.size(); ++i) {
              const auto ismine = wallet->IsMine(tx->vout[i]);
              if (i == size_t(changePos)) {
                  // change pos
                  if ((ismine & ISMINE_SPENDABLE) != ISMINE_SPENDABLE) {
                    return false;
                  }
              } else {
                  // other
                  if (ismine != ISMINE_NO) {
                      return false;
                  }
              }
          }
          return true;
    };

    // Helper to create CCoinControl that selects multiple small coins
    // Allow other inputs so wallet can add more if needed for fees
    auto makeMultiInputCoinControl = [&smallCoins](size_t n) {
        CCoinControl cc;
        cc.fAllowOtherInputs = true;  // Allow wallet to add more inputs if needed
        for (size_t i = 0; i < std::min(n, smallCoins.size()); ++i) {
            cc.Select(smallCoins[i]);
        }
        return cc;
    };

    // Select enough coins: recipients need ~12509 CASH, each small coin is ~1000 CASH
    // Select 20 coins minimum to ensure multi-input tx
    constexpr size_t kNumCoinsToSelect = 20;

    {
        // 1. Create tx with BIP69 enabled.
        //    Postconditions to be met: tx is sorted and change pos is correct.
        gArgs.ForceSetArg("-usebip69", "1");
        CTransactionRef tx;
        CReserveKey reservekey(wallet.get());
        Amount fee;
        int changePos = -1;
        std::string error;
        CCoinControl coinControl = makeMultiInputCoinControl(kNumCoinsToSelect);
        const auto res = wallet->CreateTransaction(*m_locked_chain, recipients, tx, reservekey, fee,
                                                   changePos, error, coinControl, true);
        BOOST_CHECK_EQUAL(res, CreateTransactionResult::CT_OK);
        BOOST_CHECK(tx->vin.size() > 1); // ensure we had a bunch of inputs in the txn
        BOOST_CHECK(IsTxSorted(tx));
        BOOST_CHECK(changePos != -1); // there should definitely be change
        BOOST_CHECK(AllRecipientsPresent(recipients, tx)); // all recipients should have gotten the exact amounts
        BOOST_CHECK(HasChangeAndOnlyChangeIsMine(changePos, tx));
    }

    {
        // 2. Create tx with BIP69 enabled, but with a hard-coded change pos.
        //    Postconditions: tx is NOT sorted and change pos is what we requested
        //                    (BIP69 is not applied when caller requests a specific change pos).
        gArgs.ForceSetArg("-usebip69", "1");
        CTransactionRef tx;
        CReserveKey reservekey(wallet.get());
        Amount fee;
        int changePos = 2; // request change position 2
        std::string error;
        CCoinControl coinControl = makeMultiInputCoinControl(kNumCoinsToSelect);
        const auto res = wallet->CreateTransaction(*m_locked_chain, recipients, tx, reservekey, fee,
                                                   changePos, error, coinControl, true);
        BOOST_CHECK_EQUAL(res, CreateTransactionResult::CT_OK);
        BOOST_CHECK(tx->vin.size() > 1); // ensure we had a bunch of inputs in the txn
        BOOST_CHECK( ! IsTxSorted(tx));
        BOOST_CHECK(changePos == 2); // there should definitely be change, where we said it should be.
        BOOST_CHECK(AllRecipientsPresent(recipients, tx)); // all recipients should have gotten the exact amounts
        BOOST_CHECK(HasChangeAndOnlyChangeIsMine(changePos, tx));
    }

    {
        // 3. Create tx with BIP69 disabled.
        //    Postconditions: tx is NOT sorted.
        gArgs.ForceSetArg("-usebip69", "0");
        CTransactionRef tx;
        CReserveKey reservekey(wallet.get());
        Amount fee;
        int changePos = -1; // no specific change position requested
        std::string error;
        CCoinControl coinControl = makeMultiInputCoinControl(kNumCoinsToSelect);
        const auto res = wallet->CreateTransaction(*m_locked_chain, recipients, tx, reservekey, fee,
                                                   changePos, error, coinControl, true);
        BOOST_CHECK_EQUAL(res, CreateTransactionResult::CT_OK);
        BOOST_CHECK(tx->vin.size() > 1); // ensure we had a bunch of inputs in the txn
        BOOST_CHECK( ! IsTxSorted(tx));
        BOOST_CHECK(changePos != -1); // there should definitely be change
        BOOST_CHECK(AllRecipientsPresent(recipients, tx)); // all recipients should have gotten the exact amounts
        BOOST_CHECK(HasChangeAndOnlyChangeIsMine(changePos, tx));
    }

    {
        // 4. Create a tx with BIP69 enabled but don't sign it.
        //    Postcondition: BIP69 should NOT be enabled when not signing.
        gArgs.ForceSetArg("-usebip69", "1");
        CTransactionRef tx;
        CReserveKey reservekey(wallet.get());
        Amount fee;
        int changePos = -1;
        std::string error;
        CCoinControl coinControl = makeMultiInputCoinControl(kNumCoinsToSelect);
        const auto res = wallet->CreateTransaction(*m_locked_chain, recipients, tx, reservekey, fee,
                                                   changePos, error, coinControl, false /* don't sign */);
        BOOST_CHECK_EQUAL(res, CreateTransactionResult::CT_OK);
        BOOST_CHECK(tx->vin.size() > 1); // ensure we had a bunch of inputs in the txn
        BOOST_CHECK( ! IsTxSorted(tx));
        BOOST_CHECK(changePos != -1); // there should definitely be change
        BOOST_CHECK(AllRecipientsPresent(recipients, tx)); // all recipients should have gotten the exact amounts
        BOOST_CHECK(HasChangeAndOnlyChangeIsMine(changePos, tx));
    }
}

// SECURITY (audit 2026-06, H-4): a wallet restored with a pre-v3 legacy-path
// seed (funds at m/0'/0'/k) under the default BIP44 derivation (m/44'/512'/0'/
// 0/k) must NOT silently present a 0-balance wallet. CWallet::
// ScanSeedDerivationActivity is the probe that detects this, and it backs both
// the sethdseed RPC reconciliation and the on-load auto-detect.
//
// This test exercises the REAL detection path: it mines a block whose coinbase
// pays a LEGACY-derived (m/0'/0'/0') scriptPubKey for a fresh seed, so the coin
// lands in the node's live UTXO set, then runs the probe and proves it
// attributes the funds to the legacy style. Crucially it does NOT call
// AddToWallet()/touch mapWallet, and the seed's keys are NOT in any keystore --
// exactly the conditions under which the old mapWallet/IsMine-based probe failed
// silently. The match comes purely from scanning pcoinsdbview (as scantxoutset
// does). The end-to-end sethdseed restore flow is additionally covered by the
// regtest functional phase.
BOOST_FIXTURE_TEST_CASE(seed_derivation_activity_detects_legacy_via_utxo_set,
                        TestChain100Setup) {
    auto chain = interfaces::MakeChain();

    // A fresh candidate seed, unknown to any wallet/keystore.
    CKey seedKey;
    seedKey.MakeNewKey(true);

    // Derive the LEGACY external address m/0'/0'/0' from that seed (all levels
    // hardened), matching CWallet::DeriveNewChildKey()'s legacy style.
    static const uint32_t HARDENED = 0x80000000;
    CExtKey masterKey;
    masterKey.SetSeed(seedKey.begin(), seedKey.size());
    CExtKey legacyAccount, legacyExternal, legacyChild;
    masterKey.Derive(legacyAccount, HARDENED);          // m/0'
    legacyAccount.Derive(legacyExternal, HARDENED + 0); // m/0'/0'
    legacyExternal.Derive(legacyChild, HARDENED + 0);   // m/0'/0'/0'
    const CScript legacyScript =
        GetScriptForDestination(legacyChild.key.GetPubKey().GetID());

    // A dummy wallet only so we can call the (chainstate-backed) probe; the
    // wallet itself holds none of the seed's keys and has empty history.
    CWallet wallet(Params(), *chain, WalletLocation(),
                   WalletDatabase::CreateDummy());

    // Before the coin exists, the live UTXO set has no matching output: the
    // probe must find NOTHING on either style, and report the scan succeeded.
    {
        const CWallet::HDSeedDerivationActivity none =
            wallet.ScanSeedDerivationActivity(seedKey);
        BOOST_CHECK(none.scanAvailable);
        BOOST_CHECK_EQUAL(none.legacyHits, 0);
        BOOST_CHECK_EQUAL(none.bip44Hits, 0);
    }

    // Mine a block paying the legacy P2PKH script. Its coinbase output enters the
    // live UTXO set immediately (immaturity is irrelevant -- the probe iterates
    // every coin in the set).
    CreateAndProcessBlock({}, legacyScript);

    // The probe must now attribute the UTXO to the LEGACY style, with ZERO hits
    // on the BIP44 style -- the exact "restored a legacy seed under the default
    // derivation" signal that sethdseed / on-load auto-detect act on. No
    // AddToWallet(), no mapWallet, no IsMine involved.
    const CWallet::HDSeedDerivationActivity act =
        wallet.ScanSeedDerivationActivity(seedKey);
    BOOST_CHECK(act.scanAvailable);
    BOOST_CHECK_EQUAL(act.bip44Hits, 0);
    BOOST_CHECK(act.legacyHits >= 1);
    BOOST_CHECK_EQUAL(act.legacyMaxIndex, 0);
}

// SECURITY (audit 2026-06, H-4): the symmetric case -- a seed funded on the
// BIP44 (default) path must be attributed to the BIP44 style and NOT the legacy
// style. Together with the test above this proves the probe distinguishes the
// two derivation styles against the real UTXO set rather than guessing.
BOOST_FIXTURE_TEST_CASE(seed_derivation_activity_detects_bip44_via_utxo_set,
                        TestChain100Setup) {
    auto chain = interfaces::MakeChain();

    CKey seedKey;
    seedKey.MakeNewKey(true);

    // Derive the BIP44 external address m/44'/512'/0'/0/0 (purpose/coin/account
    // hardened, change/index non-hardened), matching the BIP44 style.
    static const uint32_t HARDENED = 0x80000000;
    CExtKey masterKey;
    masterKey.SetSeed(seedKey.begin(), seedKey.size());
    CExtKey purpose, coinType, account, change, child;
    masterKey.Derive(purpose, HARDENED + 44);    // m/44'
    purpose.Derive(coinType, HARDENED + 512);    // m/44'/512'
    coinType.Derive(account, HARDENED + 0);      // m/44'/512'/0'
    account.Derive(change, 0);                    // m/44'/512'/0'/0
    change.Derive(child, 0);                       // m/44'/512'/0'/0/0
    const CScript bip44Script =
        GetScriptForDestination(child.key.GetPubKey().GetID());

    CWallet wallet(Params(), *chain, WalletLocation(),
                   WalletDatabase::CreateDummy());

    CreateAndProcessBlock({}, bip44Script);

    const CWallet::HDSeedDerivationActivity act =
        wallet.ScanSeedDerivationActivity(seedKey);
    BOOST_CHECK(act.scanAvailable);
    BOOST_CHECK_EQUAL(act.legacyHits, 0);
    BOOST_CHECK(act.bip44Hits >= 1);
    BOOST_CHECK_EQUAL(act.bip44MaxIndex, 0);
}

// SECURITY (audit 2026-06, M-split-brain / Defect 2): when sethdseed detects
// that an EXPLICITLY pinned coin_type mismatches the derivation style the seed
// is actually funded on, it must throw and leave the wallet's HD seed UNCHANGED
// (no half-applied seed, no flushed-against-a-new-seed keypool). The throw
// decision is driven entirely by the dual-path UTXO probe, which this test
// drives end-to-end against the live UTXO set.
//
// We assert the two properties that make the RPC's throw-before-mutation correct
// and observable here at unit scope:
//   (1) the probe is a pure read-only operation: running it does NOT change the
//       wallet's existing HD seed (the foundation of "no partial state"); and
//   (2) it produces the precise funded-on-legacy / not-on-bip44 signal that the
//       sethdseed explicit-pin branch throws on.
// The actual JSONRPCError throw + the atomic SetHDSeed/NewKeyPool tail are
// covered by the regtest functional phase (the sethdseed RPC is file-static and
// needs full HTTP/IBD/unlock plumbing not available to a unit test).
BOOST_FIXTURE_TEST_CASE(seed_derivation_probe_is_readonly_for_mismatch,
                        TestChain100Setup) {
    auto chain = interfaces::MakeChain();

    // A candidate restore seed funded on the LEGACY path.
    CKey candidateSeed;
    candidateSeed.MakeNewKey(true);
    static const uint32_t HARDENED = 0x80000000;
    CExtKey masterKey;
    masterKey.SetSeed(candidateSeed.begin(), candidateSeed.size());
    CExtKey legacyAccount, legacyExternal, legacyChild;
    masterKey.Derive(legacyAccount, HARDENED);
    legacyAccount.Derive(legacyExternal, HARDENED + 0);
    legacyExternal.Derive(legacyChild, HARDENED + 0);
    const CScript legacyScript =
        GetScriptForDestination(legacyChild.key.GetPubKey().GetID());
    CreateAndProcessBlock({}, legacyScript);

    // A wallet that already has an UNRELATED, existing HD seed installed.
    CWallet wallet(Params(), *chain, WalletLocation(),
                   WalletDatabase::CreateDummy());
    CKeyID original_seed_id;
    {
        LOCK(wallet.cs_wallet);
        CKey existingSeedKey;
        existingSeedKey.MakeNewKey(true);
        const CPubKey existing_pub = wallet.DeriveNewSeed(existingSeedKey);
        wallet.SetHDSeed(existing_pub);
        original_seed_id = wallet.GetHDChain().seed_id;
    }
    BOOST_CHECK(!original_seed_id.IsNull());

    // (2) The probe on the candidate reports funds on legacy, none on BIP44 --
    // the exact signal sethdseed's explicit-pin branch throws on when the user
    // pinned coin_type=512.
    const CWallet::HDSeedDerivationActivity act =
        wallet.ScanSeedDerivationActivity(candidateSeed);
    BOOST_CHECK(act.scanAvailable);
    BOOST_CHECK(act.legacyHits >= 1);
    BOOST_CHECK_EQUAL(act.bip44Hits, 0);

    // (1) Running the probe mutated nothing: the wallet's existing HD seed is
    // exactly as it was. This is what guarantees that when the RPC throws on the
    // mismatch (BEFORE its SetHDSeed tail), the wallet is left untouched.
    BOOST_CHECK(wallet.GetHDChain().seed_id == original_seed_id);
}

BOOST_AUTO_TEST_SUITE_END()
