// Copyright (c) 2026 The Radiant developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// BLAKE3 hash function implementation for OP_BLAKE3 opcode.
// Based on the BLAKE3 specification: https://github.com/BLAKE3-team/BLAKE3
// Single-threaded, portable C++ implementation (no SIMD).
// Only supports single-chunk mode (inputs < 1024 bytes) which is sufficient
// for all script/PoW use cases in Radiant.

#ifndef BITCOIN_CRYPTO_BLAKE3_H
#define BITCOIN_CRYPTO_BLAKE3_H

#include <cstdint>
#include <cstdlib>

class CBlake3 {
public:
    static const size_t OUTPUT_SIZE = 32;
    static const size_t BLOCK_LEN = 64;
    static const size_t CHUNK_LEN = 1024;

    CBlake3();
    CBlake3 &Write(const uint8_t *data, size_t len);
    // Finalize computation and write 32-byte hash to output.
    // Returns false if input exceeded CHUNK_LEN (1024 bytes), true on success.
    // Note: Unlike CK12::Finalize() which returns void, this returns bool
    // to allow graceful handling of oversized inputs in single-chunk mode.
    bool Finalize(uint8_t *output);
    CBlake3 &Reset();

private:
    static constexpr uint32_t IV[8] = {
        0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
        0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19
    };

    // Domain separation flags
    static constexpr uint8_t CHUNK_START = 1 << 0;
    static constexpr uint8_t CHUNK_END   = 1 << 1;
    static constexpr uint8_t ROOT        = 1 << 3;

    // Message schedule permutation
    static const uint8_t MSG_SCHEDULE[7][16];

    uint32_t m_cv[8];
    uint8_t m_block[BLOCK_LEN];
    uint8_t m_block_len;
    uint64_t m_counter;
    uint8_t m_flags;
    size_t m_bytes_consumed;

    static uint32_t RotR(uint32_t x, int n);
    static void G(uint32_t state[16], size_t a, size_t b, size_t c, size_t d,
                  uint32_t mx, uint32_t my);
    static void Round(uint32_t state[16], const uint32_t msg[16]);
    static void Compress(const uint32_t cv[8], const uint8_t block[BLOCK_LEN],
                         uint8_t block_len, uint64_t counter, uint8_t flags,
                         uint32_t out[16]);
};

#endif // BITCOIN_CRYPTO_BLAKE3_H
