// Copyright (c) 2026 The Radiant developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// BLAKE3 hash function implementation.
// Reference: https://github.com/BLAKE3-team/BLAKE3/blob/master/reference_impl/reference_impl.rs
// This is a portable, single-threaded implementation supporting single-chunk
// inputs only (< 1024 bytes), which covers all Radiant script use cases.

#include <crypto/blake3.h>
#include <cstring>
#include <cassert>

// Message word schedule for each round (7 rounds total).
// Each row is a permutation of indices 0..15 applied to the message words.
const uint8_t CBlake3::MSG_SCHEDULE[7][16] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {2, 6, 3, 10, 7, 0, 4, 13, 1, 11, 12, 5, 9, 14, 15, 8},
    {3, 4, 10, 12, 13, 2, 7, 14, 6, 5, 9, 0, 11, 15, 8, 1},
    {10, 7, 12, 9, 14, 3, 13, 15, 4, 0, 11, 2, 5, 8, 1, 6},
    {12, 13, 9, 11, 15, 10, 14, 8, 7, 2, 5, 3, 0, 1, 6, 4},
    {9, 14, 11, 5, 8, 12, 15, 1, 13, 3, 0, 10, 2, 6, 4, 7},
    {11, 15, 5, 0, 1, 9, 8, 6, 14, 10, 2, 12, 3, 4, 7, 13},
};

uint32_t CBlake3::RotR(uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}

void CBlake3::G(uint32_t state[16], size_t a, size_t b, size_t c, size_t d,
                uint32_t mx, uint32_t my) {
    state[a] = state[a] + state[b] + mx;
    state[d] = RotR(state[d] ^ state[a], 16);
    state[c] = state[c] + state[d];
    state[b] = RotR(state[b] ^ state[c], 12);
    state[a] = state[a] + state[b] + my;
    state[d] = RotR(state[d] ^ state[a], 8);
    state[c] = state[c] + state[d];
    state[b] = RotR(state[b] ^ state[c], 7);
}

void CBlake3::Round(uint32_t state[16], const uint32_t msg[16]) {
    // Column step
    G(state, 0, 4,  8, 12, msg[0],  msg[1]);
    G(state, 1, 5,  9, 13, msg[2],  msg[3]);
    G(state, 2, 6, 10, 14, msg[4],  msg[5]);
    G(state, 3, 7, 11, 15, msg[6],  msg[7]);
    // Diagonal step
    G(state, 0, 5, 10, 15, msg[8],  msg[9]);
    G(state, 1, 6, 11, 12, msg[10], msg[11]);
    G(state, 2, 7,  8, 13, msg[12], msg[13]);
    G(state, 3, 4,  9, 14, msg[14], msg[15]);
}

void CBlake3::Compress(const uint32_t cv[8], const uint8_t block[BLOCK_LEN],
                        uint8_t block_len, uint64_t counter, uint8_t flags,
                        uint32_t out[16]) {
    // Parse message block into 16 little-endian uint32 words
    uint32_t msg[16];
    for (int i = 0; i < 16; i++) {
        msg[i] = (uint32_t)block[4 * i]
               | ((uint32_t)block[4 * i + 1] << 8)
               | ((uint32_t)block[4 * i + 2] << 16)
               | ((uint32_t)block[4 * i + 3] << 24);
    }

    // Initialize state: [cv[0..7], IV[0..3], counter_low, counter_high, block_len, flags]
    uint32_t state[16] = {
        cv[0], cv[1], cv[2], cv[3],
        cv[4], cv[5], cv[6], cv[7],
        IV[0], IV[1], IV[2], IV[3],
        (uint32_t)(counter),
        (uint32_t)(counter >> 32),
        (uint32_t)block_len,
        (uint32_t)flags
    };

    // 7 rounds with message schedule permutation
    for (int r = 0; r < 7; r++) {
        uint32_t scheduled[16];
        for (int i = 0; i < 16; i++) {
            scheduled[i] = msg[MSG_SCHEDULE[r][i]];
        }
        Round(state, scheduled);
    }

    // Produce output: XOR the two halves of the state
    for (int i = 0; i < 8; i++) {
        out[i] = state[i] ^ state[i + 8];
    }
    for (int i = 8; i < 16; i++) {
        out[i] = state[i] ^ cv[i - 8];
    }
}

CBlake3::CBlake3() {
    Reset();
}

CBlake3 &CBlake3::Reset() {
    memcpy(m_cv, IV, sizeof(IV));
    memset(m_block, 0, BLOCK_LEN);
    m_block_len = 0;
    m_counter = 0;
    m_flags = CHUNK_START;
    m_bytes_consumed = 0;
    return *this;
}

CBlake3 &CBlake3::Write(const uint8_t *data, size_t len) {
    while (len > 0) {
        // If block is full, compress it and start a new block
        if (m_block_len == BLOCK_LEN) {
            uint32_t out[16];
            Compress(m_cv, m_block, BLOCK_LEN, m_counter, m_flags, out);
            // First 8 words become the new chaining value
            memcpy(m_cv, out, 8 * sizeof(uint32_t));
            // Note: m_counter is the CHUNK counter, not a block counter.
            // Within a single chunk (≤1024 bytes), it stays constant.
            // It only increments when moving to the next chunk.
            m_block_len = 0;
            memset(m_block, 0, BLOCK_LEN);
            // After the first block, clear CHUNK_START flag
            m_flags &= ~CHUNK_START;
        }

        // Copy data into block buffer
        size_t want = BLOCK_LEN - m_block_len;
        size_t take = (len < want) ? len : want;
        memcpy(m_block + m_block_len, data, take);
        m_block_len += (uint8_t)take;
        data += take;
        len -= take;
        m_bytes_consumed += take;
    }
    return *this;
}

bool CBlake3::Finalize(uint8_t *output) {
    // Single-chunk mode only supports inputs <= 1024 bytes.
    // Return false instead of crashing if the caller exceeds this.
    if (m_bytes_consumed > CHUNK_LEN) {
        return false;
    }

    // Set CHUNK_END and ROOT flags for the final compression
    uint8_t final_flags = m_flags | CHUNK_END | ROOT;

    uint32_t out[16];
    Compress(m_cv, m_block, m_block_len, m_counter, final_flags, out);

    // Write the first 8 words (32 bytes) as little-endian output
    for (int i = 0; i < 8; i++) {
        output[4 * i + 0] = (uint8_t)(out[i]);
        output[4 * i + 1] = (uint8_t)(out[i] >> 8);
        output[4 * i + 2] = (uint8_t)(out[i] >> 16);
        output[4 * i + 3] = (uint8_t)(out[i] >> 24);
    }
    return true;
}
