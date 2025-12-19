// Copyright (c) 2025 The Radiant developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <index/base.h>
#include <serialize.h>
#include <uint256.h>

#include <atomic>
#include <thread>

// Default configuration values (DEFAULT_SWAPINDEX is in validation.h)
static constexpr size_t DEFAULT_SWAP_CACHE_SIZE = 10 << 20; // 10MB
static constexpr int64_t DEFAULT_SWAP_HISTORY_BLOCKS = 10000; // ~35 days at 5 min blocks
static constexpr size_t DEFAULT_SWAP_QUERY_LIMIT = 100;
static constexpr size_t MAX_SWAP_QUERY_LIMIT = 1000;
static constexpr int64_t SWAP_PRUNE_INTERVAL = 60; // seconds between prune cycles

// Index version for migration support
static constexpr uint8_t SWAP_INDEX_VERSION = 1;

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
    int32_t blockHeight{0}; // Block height when the offer was indexed

    // Helper to serialize the offer for storage
    SERIALIZE_METHODS(SwapOffer, obj) {
        READWRITE(obj.version, obj.type, obj.tokenID, obj.offeredUTXOHash, 
                  obj.offeredUTXOIndex, obj.priceTerms, obj.signature, obj.blockHeight);
    }
};

/**
 * SwapOrderCounts holds counts for open and history orders.
 */
struct SwapOrderCounts {
    size_t openCount{0};
    size_t historyCount{0};
};

/**
 * SwapIndex is used to look up swap advertisements by Token ID.
 * The index is written to a LevelDB database.
 * 
 * Key Prefixes:
 *   'o' + TokenID + OfferedUTXOHash + OfferedUTXOIndex -> Open orders
 *   'h' + TokenID + OfferedUTXOHash + OfferedUTXOIndex -> History (spent/cancelled)
 *   'V' -> Index version byte
 * 
 * Value Format: Serialized SwapOffer
 */
class SwapIndex final : public BaseIndex {
protected:
    class DB;

private:
    const std::unique_ptr<DB> m_db;
    int64_t m_history_blocks; // How many blocks of history to retain
    
    // Background pruning thread
    std::thread m_prune_thread;
    std::atomic<bool> m_prune_interrupt{false};
    
    /// Background thread that periodically prunes old history entries
    void ThreadPrune();
    
    /// Move an order from open to history prefix
    bool MoveToHistory(const SwapOffer &offer);
    
    /// Delete old history entries beyond retention period
    bool PruneOldHistory(int32_t current_height);

protected:
    bool Init() override;

    bool WriteBlock(const CBlock &block, const CBlockIndex *pindex) override;

    BaseIndex::DB &GetDB() const override;

    const char *GetName() const override { return "swapindex"; }

public:
    explicit SwapIndex(size_t n_cache_size, int64_t history_blocks = DEFAULT_SWAP_HISTORY_BLOCKS,
                       bool f_memory = false, bool f_wipe = false);

    virtual ~SwapIndex() override;

    /// Look up open orders for a given token ID with pagination.
    bool GetOpenOrders(const uint256 &tokenID, std::vector<SwapOffer> &orders,
                       size_t limit = DEFAULT_SWAP_QUERY_LIMIT, size_t offset = 0);
    
    /// Look up historical (spent/cancelled) orders for a given token ID with pagination.
    bool GetHistoryOrders(const uint256 &tokenID, std::vector<SwapOffer> &orders,
                          size_t limit = DEFAULT_SWAP_QUERY_LIMIT, size_t offset = 0);
    
    /// Get counts of open and history orders for a token ID.
    bool GetOrderCounts(const uint256 &tokenID, SwapOrderCounts &counts);
    
    /// Check spent UTXOs and move them to history. Called during WriteBlock.
    bool ProcessSpentOrders(const CBlock &block, int32_t height);
    
    /// Interrupt the prune thread
    void InterruptPrune();
};

/// The global swap index, used in RPC calls. May be null.
extern std::unique_ptr<SwapIndex> g_swapindex;
