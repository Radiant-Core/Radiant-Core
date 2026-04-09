// Copyright (c) 2026 The Radiant developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// KangarooTwelve (K12) hash function implementation.
// Reference: https://keccak.team/kangarootwelve.html
// K12 uses Keccak-p[1600,12] — the same Keccak permutation as SHA-3 but with
// only 12 rounds (rounds 12-23 of the 24-round Keccak-f[1600]).
// This implementation supports single-block inputs only (< 8192 bytes).
// The permutation logic is adapted from the existing KeccakF() in sha3.cpp.

#include <crypto/k12.h>
#include <crypto/common.h>
#include <cstring>
#include <cassert>

namespace {
uint64_t Rotl(uint64_t x, int n) {
    return (x << n) | (x >> (64 - n));
}
} // namespace

void CK12::KeccakP12(uint64_t (&st)[25]) {
    // Round constants for Keccak-f[1600]. K12 uses rounds 12-23 (the last 12).
    static constexpr uint64_t RNDC[12] = {
        0x000000008000808bULL, 0x800000000000008bULL,
        0x8000000000008089ULL, 0x8000000000008003ULL,
        0x8000000000008002ULL, 0x8000000000000080ULL,
        0x000000000000800aULL, 0x800000008000000aULL,
        0x8000000080008081ULL, 0x8000000000008080ULL,
        0x0000000080000001ULL, 0x8000000080008008ULL
    };

    for (int round = 0; round < 12; ++round) {
        uint64_t bc0, bc1, bc2, bc3, bc4, t;

        // Theta
        bc0 = st[0] ^ st[5] ^ st[10] ^ st[15] ^ st[20];
        bc1 = st[1] ^ st[6] ^ st[11] ^ st[16] ^ st[21];
        bc2 = st[2] ^ st[7] ^ st[12] ^ st[17] ^ st[22];
        bc3 = st[3] ^ st[8] ^ st[13] ^ st[18] ^ st[23];
        bc4 = st[4] ^ st[9] ^ st[14] ^ st[19] ^ st[24];
        t = bc4 ^ Rotl(bc1, 1); st[0] ^= t; st[5] ^= t; st[10] ^= t; st[15] ^= t; st[20] ^= t;
        t = bc0 ^ Rotl(bc2, 1); st[1] ^= t; st[6] ^= t; st[11] ^= t; st[16] ^= t; st[21] ^= t;
        t = bc1 ^ Rotl(bc3, 1); st[2] ^= t; st[7] ^= t; st[12] ^= t; st[17] ^= t; st[22] ^= t;
        t = bc2 ^ Rotl(bc4, 1); st[3] ^= t; st[8] ^= t; st[13] ^= t; st[18] ^= t; st[23] ^= t;
        t = bc3 ^ Rotl(bc0, 1); st[4] ^= t; st[9] ^= t; st[14] ^= t; st[19] ^= t; st[24] ^= t;

        // Rho Pi
        t = st[1];
        bc0 = st[10]; st[10] = Rotl(t, 1); t = bc0;
        bc0 = st[7];  st[7]  = Rotl(t, 3); t = bc0;
        bc0 = st[11]; st[11] = Rotl(t, 6); t = bc0;
        bc0 = st[17]; st[17] = Rotl(t, 10); t = bc0;
        bc0 = st[18]; st[18] = Rotl(t, 15); t = bc0;
        bc0 = st[3];  st[3]  = Rotl(t, 21); t = bc0;
        bc0 = st[5];  st[5]  = Rotl(t, 28); t = bc0;
        bc0 = st[16]; st[16] = Rotl(t, 36); t = bc0;
        bc0 = st[8];  st[8]  = Rotl(t, 45); t = bc0;
        bc0 = st[21]; st[21] = Rotl(t, 55); t = bc0;
        bc0 = st[24]; st[24] = Rotl(t, 2); t = bc0;
        bc0 = st[4];  st[4]  = Rotl(t, 14); t = bc0;
        bc0 = st[15]; st[15] = Rotl(t, 27); t = bc0;
        bc0 = st[23]; st[23] = Rotl(t, 41); t = bc0;
        bc0 = st[19]; st[19] = Rotl(t, 56); t = bc0;
        bc0 = st[13]; st[13] = Rotl(t, 8); t = bc0;
        bc0 = st[12]; st[12] = Rotl(t, 25); t = bc0;
        bc0 = st[2];  st[2]  = Rotl(t, 43); t = bc0;
        bc0 = st[20]; st[20] = Rotl(t, 62); t = bc0;
        bc0 = st[14]; st[14] = Rotl(t, 18); t = bc0;
        bc0 = st[22]; st[22] = Rotl(t, 39); t = bc0;
        bc0 = st[9];  st[9]  = Rotl(t, 61); t = bc0;
        bc0 = st[6];  st[6]  = Rotl(t, 20); t = bc0;
        st[1] = Rotl(t, 44);

        // Chi Iota
        bc0 = st[0]; bc1 = st[1]; bc2 = st[2]; bc3 = st[3]; bc4 = st[4];
        st[0] = bc0 ^ (~bc1 & bc2) ^ RNDC[round];
        st[1] = bc1 ^ (~bc2 & bc3);
        st[2] = bc2 ^ (~bc3 & bc4);
        st[3] = bc3 ^ (~bc4 & bc0);
        st[4] = bc4 ^ (~bc0 & bc1);
        bc0 = st[5]; bc1 = st[6]; bc2 = st[7]; bc3 = st[8]; bc4 = st[9];
        st[5] = bc0 ^ (~bc1 & bc2);
        st[6] = bc1 ^ (~bc2 & bc3);
        st[7] = bc2 ^ (~bc3 & bc4);
        st[8] = bc3 ^ (~bc4 & bc0);
        st[9] = bc4 ^ (~bc0 & bc1);
        bc0 = st[10]; bc1 = st[11]; bc2 = st[12]; bc3 = st[13]; bc4 = st[14];
        st[10] = bc0 ^ (~bc1 & bc2);
        st[11] = bc1 ^ (~bc2 & bc3);
        st[12] = bc2 ^ (~bc3 & bc4);
        st[13] = bc3 ^ (~bc4 & bc0);
        st[14] = bc4 ^ (~bc0 & bc1);
        bc0 = st[15]; bc1 = st[16]; bc2 = st[17]; bc3 = st[18]; bc4 = st[19];
        st[15] = bc0 ^ (~bc1 & bc2);
        st[16] = bc1 ^ (~bc2 & bc3);
        st[17] = bc2 ^ (~bc3 & bc4);
        st[18] = bc3 ^ (~bc4 & bc0);
        st[19] = bc4 ^ (~bc0 & bc1);
        bc0 = st[20]; bc1 = st[21]; bc2 = st[22]; bc3 = st[23]; bc4 = st[24];
        st[20] = bc0 ^ (~bc1 & bc2);
        st[21] = bc1 ^ (~bc2 & bc3);
        st[22] = bc2 ^ (~bc3 & bc4);
        st[23] = bc3 ^ (~bc4 & bc0);
        st[24] = bc4 ^ (~bc0 & bc1);
    }
}

CK12::CK12() {
    Reset();
}

CK12 &CK12::Reset() {
    memset(m_state, 0, sizeof(m_state));
    memset(m_buffer, 0, sizeof(m_buffer));
    m_buf_pos = 0;
    m_bytes_consumed = 0;
    return *this;
}

CK12 &CK12::Write(const uint8_t *data, size_t len) {
    while (len > 0) {
        size_t space = RATE - m_buf_pos;
        size_t take = (len < space) ? len : space;
        memcpy(m_buffer + m_buf_pos, data, take);
        m_buf_pos += take;
        m_bytes_consumed += take;
        data += take;
        len -= take;

        if (m_buf_pos == RATE) {
            // XOR buffer into state and permute
            for (size_t i = 0; i < RATE / 8; i++) {
                m_state[i] ^= ReadLE64(m_buffer + 8 * i);
            }
            KeccakP12(m_state);
            m_buf_pos = 0;
            memset(m_buffer, 0, sizeof(m_buffer));
        }
    }
    return *this;
}

void CK12::Finalize(uint8_t *output) {
    // Enforce single-block mode limit (no tree hashing support)
    assert(m_bytes_consumed <= MAX_INPUT_LEN);

    // K12 single-leaf finalization (with empty custom string C=""):
    // Per K12 spec: K12(M, C) = TurboSHAKE128(M || C || length_encode(|C|), 0x07)
    // For empty C: length_encode(0) = 0x00
    // So we absorb M || 0x00, then pad with 0x07 domain separator.

    // Append K12 length_encode(0) = {0x00} for empty custom string
    // Note: K12's encoding differs from NIST SP 800-185 right_encode.
    // For x=0, K12 uses a single 0x00 byte (no length suffix).
    static const uint8_t framing[1] = {0x00};
    Write(framing, 1);

    // Pad: add K12 domain separator byte
    m_buffer[m_buf_pos] = 0x07;

    // Set last byte of rate to 0x80
    m_buffer[RATE - 1] |= 0x80;

    // XOR padded buffer into state
    for (size_t i = 0; i < RATE / 8; i++) {
        m_state[i] ^= ReadLE64(m_buffer + 8 * i);
    }

    // Final permutation
    KeccakP12(m_state);

    // Squeeze: extract OUTPUT_SIZE bytes from state
    for (size_t i = 0; i < OUTPUT_SIZE / 8; i++) {
        WriteLE64(output + 8 * i, m_state[i]);
    }
}
