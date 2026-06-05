// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2022-2026 The Radiant developers
// Copyright (c) 2021 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <cstdint>

/** 1MB */
inline constexpr uint64_t ONE_MEGABYTE = 1000000;
/** The maximum allowed size for a transaction, in bytes */
inline constexpr uint64_t MAX_TX_SIZE = 12 * ONE_MEGABYTE;
/** The maximum allowed size for a transaction legacy, in bytes */
inline constexpr uint64_t MAX_TX_SIZE_CONSENSUS_LEGACY = ONE_MEGABYTE;
/** The minimum allowed size for a transaction, in bytes */
inline constexpr uint64_t MIN_TX_SIZE = 32;
/** The maximum allowed size for a block, before the UAHF */
inline constexpr uint64_t LEGACY_MAX_BLOCK_SIZE = ONE_MEGABYTE;
/** Default setting for maximum allowed size for a block, in bytes */
inline constexpr uint64_t DEFAULT_EXCESSIVE_BLOCK_SIZE = 256 * ONE_MEGABYTE;

/** Default setting for maximum allowed size for a block, in bytes legacy */
inline constexpr uint64_t DEFAULT_EXCESSIVE_BLOCK_SIZE_LEGACY = 32 * ONE_MEGABYTE;
/**
 *  Maximum excessive blocks size: 2GB. This is a temporary limit
 *  to prevent consensus failure between 32-bit and 64-bit platforms,
 *  until we drop 32-bit platform support altogether, at which point
 *  this constant should be raised well beyond 32-bit addressing limits.
 */
inline constexpr uint64_t MAX_EXCESSIVE_BLOCK_SIZE = uint64_t(2000) * ONE_MEGABYTE;
/** Allowed number of signature check operations per transaction. */
inline constexpr uint64_t MAX_TX_SIGCHECKS = UINT64_MAX;
/**
 * The ratio between the maximum allowable block size and the maximum allowable
 * SigChecks (executed signature check operations) in the block. (network rule).
 */
inline constexpr int BLOCK_MAXBYTES_MAXSIGCHECKS_RATIO = 1;
/**
 * Coinbase transaction outputs can only be spent after this number of new
 * blocks (network rule).
 */
inline constexpr int COINBASE_MATURITY = 100;
/** Coinbase scripts have their own script size limit. */
inline constexpr int MAX_COINBASE_SCRIPTSIG_SIZE = 2048;

/**
 * Per-script peak memory budget (bytes) enforced only when the 2026-06
 * SCRIPT_SECURITY_UPGRADE flag is active. Caps the cumulative byte size of all
 * elements held across the main stack + altstack at any instant, so a script
 * cannot balloon to multiple GB via repeated OP_DUP/OP_CAT/etc. of a large
 * element. Chosen at 64 MB = 2x MAX_SCRIPT_ELEMENT_SIZE (32 MB), comfortably
 * above any realistic legitimate script (a single max element is 32 MB; this
 * permits two of them simultaneously plus headroom) while rejecting a memory
 * bomb that would otherwise consume up to MAX_STACK_SIZE * MAX_SCRIPT_ELEMENT_SIZE
 * (~1 PB) of address space.
 */
inline constexpr uint64_t MAX_SCRIPT_STACK_MEMORY_USAGE = uint64_t(64) * ONE_MEGABYTE;

/**
 * Per-script opcode-cost budget for hashing / bytewise opcodes, enforced only
 * when SCRIPT_SECURITY_UPGRADE is active. Each such opcode accrues cost
 * proportional to the size of the data it processes; the running total may not
 * exceed this budget. Chosen at 1 GB-equivalent of processed bytes — far above
 * any plausible legitimate script (e.g. tens of MB-class hashes) but bounding
 * the total CPU a single script can demand from hashing/bytewise primitives.
 */
inline constexpr uint64_t MAX_SCRIPT_OPCODE_COST = uint64_t(1000) * ONE_MEGABYTE;

/** Flags for nSequence and nLockTime locks */
/** Interpret sequence numbers as relative lock-time constraints. */
inline constexpr unsigned int LOCKTIME_VERIFY_SEQUENCE = (1 << 0);
/** Use GetMedianTimePast() instead of nTime for end point timestamp. */
inline constexpr unsigned int LOCKTIME_MEDIAN_TIME_PAST = (1 << 1);

/**
 * Compute the maximum number of sigchecks that can be contained in a block
 * given the MAXIMUM block size as parameter. The maximum sigchecks scale
 * linearly with the maximum block size and do not depend on the actual
 * block size. The returned value is rounded down (there are no fractional
 * sigchecks so the fractional part is meaningless).
 */
inline constexpr uint64_t GetMaxBlockSigChecksCount(uint64_t maxBlockSize) {
    return maxBlockSize / BLOCK_MAXBYTES_MAXSIGCHECKS_RATIO;
}
