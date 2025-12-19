// Copyright (c) 2025 The Radiant developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <index/base.h>
#include <serialize.h>
#include <uint256.h>

/**
 * SwapOffer represents a parsed swap advertisement.
 */
struct SwapOffer {
    uint8_t version;
    uint8_t type;
    uint256 tokenID;
    uint256 offeredUTXOHash;
    uint32_t offeredUTXOIndex;
    std::vector<uint8_t> priceTerms;
    std::vector<uint8_t> signature;

    // Helper to serialize the offer for storage
    SERIALIZE_METHODS(SwapOffer, obj) {
        READWRITE(obj.version, obj.type, obj.tokenID, obj.offeredUTXOHash, obj.offeredUTXOIndex, obj.priceTerms, obj.signature);
    }
};

/**
 * SwapIndex is used to look up swap advertisements by Token ID.
 * The index is written to a LevelDB database.
 * 
 * Key Format: 's' + TokenID + OfferedUTXOHash + OfferedUTXOIndex
 * Value Format: Serialized SwapOffer
 */
class SwapIndex final : public BaseIndex {
protected:
    class DB;

private:
    const std::unique_ptr<DB> m_db;

protected:
    bool Init() override;

    bool WriteBlock(const CBlock &block, const CBlockIndex *pindex) override;

    BaseIndex::DB &GetDB() const override;

    const char *GetName() const override { return "swapindex"; }

public:
    explicit SwapIndex(size_t n_cache_size, bool f_memory = false,
                       bool f_wipe = false);

    virtual ~SwapIndex() override;

    /// Look up open orders for a given token ID.
    bool GetOpenOrders(const uint256 &tokenID, std::vector<SwapOffer> &orders);
};

/// The global swap index, used in RPC calls. May be null.
extern std::unique_ptr<SwapIndex> g_swapindex;
