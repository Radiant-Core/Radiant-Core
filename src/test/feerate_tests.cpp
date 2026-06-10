// Copyright (c) 2016 The Bitcoin Core developers
// Copyright (c) 2022-2026 The Radiant developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <amount.h>

#include <test/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <array>

BOOST_FIXTURE_TEST_SUITE(feerate_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(GetFeeTest) {
    CFeeRate feeRate, altFeeRate;

    feeRate = CFeeRate(Amount::zero());
    // Must always return 0
    BOOST_CHECK_EQUAL(feeRate.GetFee(0), Amount::zero());
    BOOST_CHECK_EQUAL(feeRate.GetFee(1e5), Amount::zero());

    feeRate = CFeeRate(1000 * SATOSHI);
    // Must always just return the arg
    BOOST_CHECK_EQUAL(feeRate.GetFee(0), Amount::zero());
    BOOST_CHECK_EQUAL(feeRate.GetFee(1), SATOSHI);
    BOOST_CHECK_EQUAL(feeRate.GetFee(121), 121 * SATOSHI);
    BOOST_CHECK_EQUAL(feeRate.GetFee(999), 999 * SATOSHI);
    BOOST_CHECK_EQUAL(feeRate.GetFee(1000), 1000 * SATOSHI);
    BOOST_CHECK_EQUAL(feeRate.GetFee(9000), 9000 * SATOSHI);

    feeRate = CFeeRate(-1000 * SATOSHI);
    // Must always just return -1 * arg
    BOOST_CHECK_EQUAL(feeRate.GetFee(0), Amount::zero());
    BOOST_CHECK_EQUAL(feeRate.GetFee(1), -SATOSHI);
    BOOST_CHECK_EQUAL(feeRate.GetFee(121), -121 * SATOSHI);
    BOOST_CHECK_EQUAL(feeRate.GetFee(999), -999 * SATOSHI);
    BOOST_CHECK_EQUAL(feeRate.GetFee(1000), -1000 * SATOSHI);
    BOOST_CHECK_EQUAL(feeRate.GetFee(9000), -9000 * SATOSHI);

    feeRate = CFeeRate(123 * SATOSHI);
    // Truncates the result, if not integer
    BOOST_CHECK_EQUAL(feeRate.GetFee(0), Amount::zero());
    // Special case: returns 1 instead of 0
    BOOST_CHECK_EQUAL(feeRate.GetFee(8), SATOSHI);
    BOOST_CHECK_EQUAL(feeRate.GetFee(9), SATOSHI);
    BOOST_CHECK_EQUAL(feeRate.GetFee(121), 14 * SATOSHI);
    BOOST_CHECK_EQUAL(feeRate.GetFee(122), 15 * SATOSHI);
    BOOST_CHECK_EQUAL(feeRate.GetFee(999), 122 * SATOSHI);
    BOOST_CHECK_EQUAL(feeRate.GetFee(1000), 123 * SATOSHI);
    BOOST_CHECK_EQUAL(feeRate.GetFee(9000), 1107 * SATOSHI);

    feeRate = CFeeRate(-123 * SATOSHI);
    // Truncates the result, if not integer
    BOOST_CHECK_EQUAL(feeRate.GetFee(0), Amount::zero());
    // Special case: returns -1 instead of 0
    BOOST_CHECK_EQUAL(feeRate.GetFee(8), -SATOSHI);
    BOOST_CHECK_EQUAL(feeRate.GetFee(9), -SATOSHI);

    // Check ceiling results
    feeRate = CFeeRate(18 * SATOSHI);
    // Truncates the result, if not integer
    BOOST_CHECK_EQUAL(feeRate.GetFeeCeiling(0), Amount::zero());
    BOOST_CHECK_EQUAL(feeRate.GetFeeCeiling(100), 2 * SATOSHI);
    BOOST_CHECK_EQUAL(feeRate.GetFeeCeiling(200), 4 * SATOSHI);
    BOOST_CHECK_EQUAL(feeRate.GetFeeCeiling(1000), 18 * SATOSHI);

    // Check alternate constructor
    feeRate = CFeeRate(1000 * SATOSHI);
    altFeeRate = CFeeRate(feeRate);
    BOOST_CHECK_EQUAL(feeRate.GetFee(100), altFeeRate.GetFee(100));

    // Check full constructor
    BOOST_CHECK(CFeeRate(-SATOSHI, 0) == CFeeRate(Amount::zero()));
    BOOST_CHECK(CFeeRate(Amount::zero(), 0) == CFeeRate(Amount::zero()));
    BOOST_CHECK(CFeeRate(SATOSHI, 0) == CFeeRate(Amount::zero()));
    // default value
    BOOST_CHECK(CFeeRate(-SATOSHI, 1000) == CFeeRate(-SATOSHI));
    BOOST_CHECK(CFeeRate(Amount::zero(), 1000) == CFeeRate(Amount::zero()));
    BOOST_CHECK(CFeeRate(SATOSHI, 1000) == CFeeRate(SATOSHI));
    // lost precision (can only resolve satoshis per kB)
    BOOST_CHECK(CFeeRate(SATOSHI, 1001) == CFeeRate(Amount::zero()));
    BOOST_CHECK(CFeeRate(2 * SATOSHI, 1001) == CFeeRate(SATOSHI));
    // some more integer checks
    BOOST_CHECK(CFeeRate(26 * SATOSHI, 789) == CFeeRate(32 * SATOSHI));
    BOOST_CHECK(CFeeRate(27 * SATOSHI, 789) == CFeeRate(34 * SATOSHI));
    // Maximum size in bytes, should not crash
    CFeeRate(MAX_MONEY, std::numeric_limits<size_t>::max() >> 1).GetFeePerK();
}

BOOST_AUTO_TEST_CASE(CFeeRateNormalUnchanged) {
    // The hardened (128-bit) arithmetic must produce byte-for-byte identical
    // results to the historical int64 path for all normal, in-range inputs.

    // Full constructor: typical fee paid over a typical size.
    BOOST_CHECK(CFeeRate(1000 * SATOSHI, 1000) == CFeeRate(1000 * SATOSHI));
    BOOST_CHECK(CFeeRate(26 * SATOSHI, 789) == CFeeRate(32 * SATOSHI));
    BOOST_CHECK(CFeeRate(27 * SATOSHI, 789) == CFeeRate(34 * SATOSHI));
    BOOST_CHECK(CFeeRate(SATOSHI, 1001) == CFeeRate(Amount::zero()));
    BOOST_CHECK(CFeeRate(2 * SATOSHI, 1001) == CFeeRate(SATOSHI));

    // GetFee / GetFeeCeiling on a normal rate must be unchanged.
    CFeeRate feeRate = CFeeRate(123 * SATOSHI);
    BOOST_CHECK_EQUAL(feeRate.GetFee(8), SATOSHI);
    BOOST_CHECK_EQUAL(feeRate.GetFee(121), 14 * SATOSHI);
    BOOST_CHECK_EQUAL(feeRate.GetFee(9000), 1107 * SATOSHI);
    feeRate = CFeeRate(18 * SATOSHI);
    BOOST_CHECK_EQUAL(feeRate.GetFeeCeiling(100), 2 * SATOSHI);
    BOOST_CHECK_EQUAL(feeRate.GetFeeCeiling(1000), 18 * SATOSHI);

    // Negative rate path must also be unchanged.
    feeRate = CFeeRate(-123 * SATOSHI);
    BOOST_CHECK_EQUAL(feeRate.GetFee(8), -SATOSHI);
    BOOST_CHECK_EQUAL(feeRate.GetFee(1000), -123 * SATOSHI);
}

BOOST_AUTO_TEST_CASE(CFeeRateExtremeNoOverflow) {
    // 1000 * nFeePaid for a fee near MAX_MONEY is ~2.1e21, which overflows
    // signed int64 (UB) in the historical implementation. The hardened path
    // must compute this in 128-bit and clamp the result into a sane range
    // without wrapping negative.

    // A 1-byte "transaction" paying nearly MAX_MONEY: 1000 * fee / 1 would be
    // ~2.1e21, far past int64. Result must be clamped to the positive max and
    // never go negative.
    CFeeRate huge(MAX_MONEY, 1);
    BOOST_CHECK(huge.GetFeePerK() >= Amount::zero());
    BOOST_CHECK(huge.GetFeePerK() <= MAX_MONEY);
    // Same with the largest representable size; must not crash or wrap.
    BOOST_CHECK(CFeeRate(MAX_MONEY, std::numeric_limits<size_t>::max() >> 1)
                    .GetFeePerK() >= Amount::zero());

    // The negative extreme must clamp to the negative max, not wrap positive.
    CFeeRate hugeNeg(-MAX_MONEY, 1);
    BOOST_CHECK(hugeNeg.GetFeePerK() <= Amount::zero());
    BOOST_CHECK(hugeNeg.GetFeePerK() >= -MAX_MONEY);
}

BOOST_AUTO_TEST_CASE(GetFeeLargeSizeNoWrap) {
    // GetFee multiplies nSize * nSatoshisPerK; with a large rate and a large
    // size this product overflows signed int64 in the historical path. The
    // hardened path must clamp into range and never wrap negative for a
    // positive rate (nor wrap positive for a negative rate).

    // Large positive rate, large size: result must stay non-negative and bounded.
    CFeeRate bigRate(MAX_MONEY);
    Amount fee = bigRate.GetFee(std::numeric_limits<size_t>::max() >> 1);
    BOOST_CHECK(fee >= Amount::zero());
    BOOST_CHECK(fee <= MAX_MONEY);

    // Ceiling variant must also stay in range.
    Amount feeCeil = bigRate.GetFeeCeiling(std::numeric_limits<size_t>::max() >> 1);
    BOOST_CHECK(feeCeil >= Amount::zero());
    BOOST_CHECK(feeCeil <= MAX_MONEY);

    // Large negative rate, large size: must stay non-positive (no wrap positive).
    CFeeRate bigNegRate(-MAX_MONEY);
    Amount negFee = bigNegRate.GetFee(std::numeric_limits<size_t>::max() >> 1);
    BOOST_CHECK(negFee <= Amount::zero());
    BOOST_CHECK(negFee >= -MAX_MONEY);
}

BOOST_AUTO_TEST_CASE(ToString) {
    BOOST_CHECK_EQUAL(CFeeRate{Amount::zero()}.ToString(), "0.00000000 RXD/kB");
    BOOST_CHECK_EQUAL(CFeeRate{SATOSHI}.ToString(), "0.00000001 RXD/kB");
    BOOST_CHECK_EQUAL(CFeeRate{Amount{123'456'000 * SATOSHI}}.ToString(), "1.23456000 RXD/kB");
    BOOST_CHECK_EQUAL(CFeeRate{Amount{1230 * COIN}}.ToString(), "1230.00000000 RXD/kB");
    BOOST_CHECK_EQUAL(CFeeRate{Amount{-123'456'000 * SATOSHI}}.ToString(), "-1.23456000 RXD/kB");
    BOOST_CHECK_EQUAL(CFeeRate{Amount{-1230 * COIN}}.ToString(), "-1230.00000000 RXD/kB");
}

BOOST_AUTO_TEST_SUITE_END()
