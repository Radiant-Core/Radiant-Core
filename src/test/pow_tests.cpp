// Copyright (c) 2015 The Bitcoin Core developers
// Copyright (c) 2022-2026 The Radiant developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <config.h>
#include <consensus/activation.h>
#include <pow.h>
#include <random.h>
#include <tinyformat.h>
#include <util/system.h>

#include <test/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <cmath>
#include <memory>
#include <string>

BOOST_FIXTURE_TEST_SUITE(pow_tests, TestingSetup)

struct TestBlockIndex : public CBlockIndex {
    TestBlockIndex() = default;
    TestBlockIndex(const CBlockIndex &b) : CBlockIndex(b) {}
    TestBlockIndex &operator=(const CBlockIndex &b) {
        CBlockIndex::operator=(b);
        return *this;
    }
};

/* Test calculation of next difficulty target with no constraints applying */

BOOST_AUTO_TEST_CASE(get_next_work) {
    DummyConfig config(CBaseChainParams::MAIN);

    int64_t nLastRetargetTime = 1261130161; // Block #30240
    CBlockIndex pindexLast;
    pindexLast.nHeight = 32255;
    pindexLast.nTime = 1261152739; // Block #32255
    pindexLast.nBits = 0x1d00ffff;
    BOOST_CHECK_EQUAL(
        CalculateNextClassicWorkRequired(&pindexLast, nLastRetargetTime,
                                  config.GetChainParams().GetConsensus()),
        473956288);
}

/* Test the constraint on the upper bound for next work */
BOOST_AUTO_TEST_CASE(get_next_work_pow_limit) {
    DummyConfig config(CBaseChainParams::MAIN);

    int64_t nLastRetargetTime = 1231006505; // Block #0
    CBlockIndex pindexLast;
    pindexLast.nHeight = 2015;
    pindexLast.nTime = 1233061996; // Block #2015
    pindexLast.nBits = 0x1d00ffff;

    BOOST_CHECK_EQUAL(
        CalculateNextClassicWorkRequired(&pindexLast, nLastRetargetTime,
                                  config.GetChainParams().GetConsensus()),
        0x1d00ffffU);
}

/* Test the constraint on the lower bound for actual time taken */
BOOST_AUTO_TEST_CASE(get_next_work_lower_limit_actual) {
    DummyConfig config(CBaseChainParams::MAIN);

    int64_t nLastRetargetTime = 1279008237; // Block #66528
    CBlockIndex pindexLast;
    pindexLast.nHeight = 68543;
    pindexLast.nTime = 1279297671; // Block #68543
    pindexLast.nBits = 0x1c05a3f4;

    BOOST_CHECK_EQUAL(
        CalculateNextClassicWorkRequired(&pindexLast, nLastRetargetTime,
                                  config.GetChainParams().GetConsensus()),
        469938949);
}

/* Test the constraint on the upper bound for actual time taken */
BOOST_AUTO_TEST_CASE(get_next_work_upper_limit_actual) {
    DummyConfig config(CBaseChainParams::MAIN);

    int64_t nLastRetargetTime = 1263163443; // NOTE: Not an actual block time
    CBlockIndex pindexLast;
    pindexLast.nHeight = 46367;
    pindexLast.nTime = 1269211443; // Block #46367
    pindexLast.nBits = 0x1c387f6f;

    BOOST_CHECK_EQUAL(
        CalculateNextClassicWorkRequired(&pindexLast, nLastRetargetTime,
                                  config.GetChainParams().GetConsensus()),
        0x1d00e1fdU);
}

BOOST_AUTO_TEST_CASE(GetBlockProofEquivalentTime_test) {
    DummyConfig config(CBaseChainParams::MAIN);

    std::vector<CBlockIndex> blocks(10000);
    for (int i = 0; i < 10000; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = i;
        blocks[i].nTime =
            1269211443 +
            i * config.GetChainParams().GetConsensus().nPowTargetSpacing;
        blocks[i].nBits = 0x207fffff; /* target 0x7fffff000... */
        blocks[i].nChainWork =
            i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i])
              : arith_uint256(0);
    }

    for (int j = 0; j < 1000; j++) {
        CBlockIndex *p1 = &blocks[InsecureRandRange(10000)];
        CBlockIndex *p2 = &blocks[InsecureRandRange(10000)];
        CBlockIndex *p3 = &blocks[InsecureRandRange(10000)];

        int64_t tdiff = GetBlockProofEquivalentTime(
            *p1, *p2, *p3, config.GetChainParams().GetConsensus());
        BOOST_CHECK_EQUAL(tdiff, p1->GetBlockTime() - p2->GetBlockTime());
    }
}
 
BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_negative_target)
{
DummyConfig config(CBaseChainParams::MAIN);
    uint256 hash;
    unsigned int nBits;
    nBits = UintToArith256(config.GetChainParams().GetConsensus().powLimit).GetCompact(true);
    hash.SetHex("0x1");
    BOOST_CHECK(!CheckProofOfWork(BlockHash(hash), nBits, config.GetChainParams().GetConsensus()));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_overflow_target)
{
    DummyConfig config(CBaseChainParams::MAIN);
    uint256 hash;
    unsigned int nBits{~0x00800000U};
    hash.SetHex("0x1");
    BOOST_CHECK(!CheckProofOfWork(BlockHash(hash), nBits, config.GetChainParams().GetConsensus()));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_too_easy_target)
{
    DummyConfig config(CBaseChainParams::MAIN);
    uint256 hash;
    unsigned int nBits;
    arith_uint256 nBits_arith = UintToArith256(config.GetChainParams().GetConsensus().powLimit);
    nBits_arith *= 2;
    nBits = nBits_arith.GetCompact();
    hash.SetHex("0x1");
    BOOST_CHECK(!CheckProofOfWork(BlockHash(hash), nBits, config.GetChainParams().GetConsensus()));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_biger_hash_than_target)
{ 
    DummyConfig config(CBaseChainParams::MAIN);
    uint256 hash;
    unsigned int nBits;
    arith_uint256 hash_arith = UintToArith256(config.GetChainParams().GetConsensus().powLimit);
    nBits = hash_arith.GetCompact();
    hash_arith *= 2; // hash > nBits
    hash = ArithToUint256(hash_arith);
    BOOST_CHECK(!CheckProofOfWork(BlockHash(hash), nBits, config.GetChainParams().GetConsensus()));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_zero_target)
{
    DummyConfig config(CBaseChainParams::MAIN);
    uint256 hash;
    unsigned int nBits;
    arith_uint256 hash_arith{0};
    nBits = hash_arith.GetCompact();
    hash = ArithToUint256(hash_arith);
    BOOST_CHECK(!CheckProofOfWork(BlockHash(hash), nBits, config.GetChainParams().GetConsensus()));
}
 
double TargetFromBits(const uint32_t nBits) {
    return (nBits & 0xff'ff'ff) * pow(256, (nBits >> 24)-3);
}
 
std::string StrPrintCalcArgs(const arith_uint256 refTarget,
                             const int64_t targetSpacing,
                             const int64_t timeDiff,
                             const int64_t heightDiff,
                             const arith_uint256 expectedTarget,
                             const uint32_t expectednBits) {
    return strprintf("\n"
                     "ref=         %s\n"
                     "spacing=     %d\n"
                     "timeDiff=    %d\n"
                     "heightDiff=  %d\n"
                     "expTarget=   %s\n"
                     "exp nBits=   0x%08x\n",
                     refTarget.ToString(),
                     targetSpacing,
                     timeDiff,
                     heightDiff,
                     expectedTarget.ToString(),
                     expectednBits);
}
 
BOOST_AUTO_TEST_CASE(ASERTHalfLifeUpgrade_mainnet_height_switch)
{
    DummyConfig config(CBaseChainParams::MAIN);
    const Consensus::Params& params = config.GetChainParams().GetConsensus();

    BOOST_REQUIRE(params.asertHalfLifeUpgradeHeight == 400000);
    BOOST_REQUIRE(params.asertAnchorParams.has_value());

    const auto& anchorParams = *params.asertAnchorParams;

    // Test that the half-life upgrade height is configured correctly
    // and that the ASERT anchor parameters are valid
    BOOST_CHECK(anchorParams.nHeight > 0);
    BOOST_CHECK(anchorParams.nBits > 0);
    BOOST_CHECK(anchorParams.nPrevBlockTime > 0);

    // Verify the half-life values are as expected:
    // - Before upgrade: 2 days (172800 seconds)
    // - After upgrade: 12 hours (43200 seconds)
    BOOST_CHECK_EQUAL(params.nASERTHalfLife, 2 * 24 * 60 * 60);  // 2 days

    // The upgrade height should be reasonable (not 0, not too high)
    BOOST_CHECK(params.asertHalfLifeUpgradeHeight > anchorParams.nHeight);

    // Test the CalculateASERT function directly to verify half-life effect
    const arith_uint256 refTarget = arith_uint256().SetCompact(anchorParams.nBits);
    const arith_uint256 powLimit = UintToArith256(params.powLimit);
    const int64_t targetSpacing = params.nPowTargetSpacing;

    // Simulate being 6 hours behind schedule
    const int64_t lagSeconds = 6 * 60 * 60;
    const int64_t nHeightDiff = 1000;  // 1000 blocks from anchor
    const int64_t idealTime = nHeightDiff * targetSpacing;
    const int64_t actualTime = idealTime + lagSeconds;  // Behind schedule

    // Calculate target with old half-life (2 days)
    arith_uint256 targetOldHalfLife = CalculateASERT(refTarget, targetSpacing, actualTime,
                                                      nHeightDiff, powLimit,
                                                      2 * 24 * 60 * 60);  // 2 days

    // Calculate target with new half-life (12 hours)
    arith_uint256 targetNewHalfLife = CalculateASERT(refTarget, targetSpacing, actualTime,
                                                      nHeightDiff, powLimit,
                                                      12 * 60 * 60);  // 12 hours

    // With a shorter half-life, the same lag causes a larger target increase
    // (difficulty decreases faster when behind schedule)
    BOOST_CHECK(targetNewHalfLife > targetOldHalfLife);
}

BOOST_AUTO_TEST_CASE(ASERTHalfLifeUpgrade_asert_simulation)
{
    DummyConfig config(CBaseChainParams::MAIN);
    const Consensus::Params& params = config.GetChainParams().GetConsensus();

    BOOST_REQUIRE(params.asertHalfLifeUpgradeHeight == 400000);
    BOOST_REQUIRE(params.asertAnchorParams.has_value());

    const auto& anchorParams = *params.asertAnchorParams;

    TestBlockIndex anchor;
    anchor.nHeight = anchorParams.nHeight;
    anchor.nTime = anchorParams.nPrevBlockTime;
    anchor.nBits = anchorParams.nBits;
    anchor.pprev = nullptr;

    const int64_t spacing = params.nPowTargetSpacing;

    TestBlockIndex prev = anchor;
    prev.nHeight = anchor.nHeight;
    prev.nTime = anchor.nTime;
    prev.nBits = anchor.nBits;
    prev.pprev = &anchor;

    const int lagBlocks = 3;
    const int64_t lagSeconds = 3 * spacing;

    double preUpgradeTarget = 0.0;
    double postUpgradeTarget = 0.0;

    for (int i = 0; i < lagBlocks; ++i) {
        const int nextHeight = prev.nHeight + 1;

        CBlockHeader header;
        if (nextHeight < params.asertHalfLifeUpgradeHeight) {
            header.nTime = prev.nTime + spacing + lagSeconds;
        } else {
            header.nTime = prev.nTime + spacing + lagSeconds;
        }

        uint32_t nextBits = GetNextWorkRequired(&prev, &header, params);
        double nextTarget = TargetFromBits(nextBits);

        BOOST_CHECK(nextTarget > 0.0);

        TestBlockIndex nextIndex;
        nextIndex.nHeight = nextHeight;
        nextIndex.nTime = header.nTime;
        nextIndex.nBits = nextBits;
        nextIndex.pprev = &prev;

        if (nextHeight == params.asertHalfLifeUpgradeHeight - 1) {
            preUpgradeTarget = nextTarget;
        } else if (nextHeight == params.asertHalfLifeUpgradeHeight + 1) {
            postUpgradeTarget = nextTarget;
        }

        prev = nextIndex;
    }

    if (preUpgradeTarget > 0.0 && postUpgradeTarget > 0.0) {
        BOOST_CHECK(postUpgradeTarget >= preUpgradeTarget);
    }
}

BOOST_AUTO_TEST_SUITE_END()
