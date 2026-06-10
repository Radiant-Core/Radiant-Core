// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2022-2026 The Radiant developers
// Copyright (c) 2017-2019 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <feerate.h>

#include <amount.h>
#include <tinyformat.h>

namespace {

// Largest absolute satoshi magnitude we allow a fee rate (or intermediate
// product) to hold. Mirrors MAX_MONEY so that the fee/feerate machinery never
// has to multiply values whose products can exceed the signed-128-bit safe
// range, and never produces a wrapped (UB) signed int64 result. This keeps the
// arithmetic well-defined for the extreme inputs that operator-supplied fee
// deltas or adversarial inputs could produce, while leaving all normal,
// in-range fee values completely untouched.
constexpr int64_t MAX_FEE_SATOSHIS = 2100000000000000000LL; // == MAX_MONEY

// Extract the raw satoshi count from an Amount. Amount exposes no direct int64
// accessor, but Amount / Amount yields the underlying int64 (the existing
// ToString() implementation relies on the same idiom).
static int64_t ToSatoshis(const Amount amt) {
    return amt / SATOSHI;
}

// Clamp a 128-bit satoshi count into [-MAX_FEE_SATOSHIS, MAX_FEE_SATOSHIS] and
// narrow it back to a well-defined int64. Performing the narrowing here avoids
// the undefined behaviour of an out-of-range signed int64 conversion.
static int64_t ClampFeeSatoshis128(__int128 sats) {
    if (sats > __int128(MAX_FEE_SATOSHIS)) {
        return MAX_FEE_SATOSHIS;
    }
    if (sats < -__int128(MAX_FEE_SATOSHIS)) {
        return -MAX_FEE_SATOSHIS;
    }
    return int64_t(sats);
}

} // namespace

CFeeRate::CFeeRate(const Amount nFeePaid, size_t nBytes_) {
    assert(nBytes_ <= uint64_t(std::numeric_limits<int64_t>::max()));
    int64_t nSize = int64_t(nBytes_);

    if (nSize > 0) {
        // Compute 1000 * nFeePaid / nSize in 128-bit to avoid the signed int64
        // overflow (UB) that would occur for fees approaching MAX_MONEY, then
        // clamp/narrow the result back into the safe int64 range.
        const __int128 product = __int128(1000) * __int128(ToSatoshis(nFeePaid));
        nSatoshisPerK = ClampFeeSatoshis128(product / __int128(nSize)) * SATOSHI;
    } else {
        nSatoshisPerK = Amount::zero();
    }
}

template <bool ceil>
static Amount GetFee(size_t nBytes_, Amount nSatoshisPerK) {
    assert(nBytes_ <= uint64_t(std::numeric_limits<int64_t>::max()));
    int64_t nSize = int64_t(nBytes_);

    // Compute nSize * nSatoshisPerK in 128-bit to avoid the signed int64
    // overflow (UB) that would occur for large sizes and/or large rates. The
    // quotient/remainder against 1000 are then taken in 128-bit and the final
    // result clamped/narrowed back into the safe int64 range.
    const __int128 nProduct =
        __int128(nSize) * __int128(nSatoshisPerK / SATOSHI);

    // Ensure fee is rounded up when truncated if ceil is true.
    Amount nFee = Amount::zero();
    if (ceil) {
        const __int128 quotient = nProduct / 1000;
        const bool roundUp = (nProduct % 1000) > 0;
        nFee = ClampFeeSatoshis128(roundUp ? quotient + 1 : quotient) * SATOSHI;
    } else {
        nFee = ClampFeeSatoshis128(nProduct / 1000) * SATOSHI;
    }

    if (nFee == Amount::zero() && nSize != 0) {
        if (nSatoshisPerK > Amount::zero()) {
            nFee = SATOSHI;
        }
        if (nSatoshisPerK < Amount::zero()) {
            nFee = -SATOSHI;
        }
    }

    return nFee;
}

Amount CFeeRate::GetFee(size_t nBytes) const {
    return ::GetFee<false>(nBytes, nSatoshisPerK);
}

Amount CFeeRate::GetFeeCeiling(size_t nBytes) const {
    return ::GetFee<true>(nBytes, nSatoshisPerK);
}

std::string CFeeRate::ToString() const {
    int64_t nSize = nSatoshisPerK / COIN;
    int64_t nFrac = (nSatoshisPerK % COIN) / SATOSHI;
    if (nSatoshisPerK < Amount::zero()) {
        return strprintf("-%d.%08d %s/kB", std::abs(nSize), std::abs(nFrac), CURRENCY_UNIT);
    }
    return strprintf("%d.%08d %s/kB", nSize, nFrac, CURRENCY_UNIT);
}
