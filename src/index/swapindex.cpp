// Copyright (c) 2025 The Radiant developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <index/swapindex.h>

#include <chain.h>
#include <chainparams.h>
#include <coins.h>
#include <script/script.h>
#include <script/standard.h>
#include <shutdown.h>
#include <ui_interface.h>
#include <util/system.h>
#include <validation.h>

// Key prefixes for the swap index database
constexpr char DB_OPEN_ORDER = 'o';    // Open (active) orders
constexpr char DB_HISTORY = 'h';       // Historical (spent/cancelled) orders  
constexpr char DB_VERSION = 'V';       // Index version marker
constexpr char DB_LEGACY = 's';        // Legacy prefix (for migration)

std::unique_ptr<SwapIndex> g_swapindex;

// Helper to construct a key with given prefix
static std::vector<uint8_t> MakeKey(char prefix, const SwapOffer &offer) {
    std::vector<uint8_t> key;
    key.reserve(1 + 32 + 32 + 4); // prefix + tokenID + utxoHash + utxoIndex
    key.push_back(static_cast<uint8_t>(prefix));
    key.insert(key.end(), offer.tokenID.begin(), offer.tokenID.end());
    key.insert(key.end(), offer.offeredUTXOHash.begin(), offer.offeredUTXOHash.end());
    // Big-endian index for consistent sorting
    uint32_t idx = offer.offeredUTXOIndex;
    key.push_back((idx >> 24) & 0xFF);
    key.push_back((idx >> 16) & 0xFF);
    key.push_back((idx >> 8) & 0xFF);
    key.push_back(idx & 0xFF);
    return key;
}

// Helper to construct a prefix for iteration
static std::vector<uint8_t> MakePrefix(char prefix, const uint256 &tokenID) {
    std::vector<uint8_t> result;
    result.reserve(1 + 32);
    result.push_back(static_cast<uint8_t>(prefix));
    result.insert(result.end(), tokenID.begin(), tokenID.end());
    return result;
}

class SwapIndex::DB : public BaseIndex::DB {
public:
    explicit DB(size_t n_cache_size, bool f_memory = false, bool f_wipe = false);

    // Write multiple swap entries
    bool WriteSwaps(const std::vector<std::pair<std::vector<uint8_t>, SwapOffer>> &v_swaps);
    
    // Read swaps with a given prefix, with pagination
    bool ReadSwapsWithPrefix(char prefix, const uint256 &tokenID, 
                             std::vector<SwapOffer> &orders,
                             size_t limit, size_t offset);
    
    // Count swaps with a given prefix
    size_t CountSwapsWithPrefix(char prefix, const uint256 &tokenID);
    
    // Delete a key
    bool DeleteKey(const std::vector<uint8_t> &key);
    
    // Batch operations for moving orders
    bool MoveOrderToHistory(const SwapOffer &offer);
    
    // Delete old history entries
    bool DeleteHistoryOlderThan(int32_t cutoffHeight);
    
    // Version management
    bool ReadVersion(uint8_t &version);
    bool WriteVersion(uint8_t version);
    
    // Migration from legacy format
    bool MigrateLegacyData();
};

SwapIndex::DB::DB(size_t n_cache_size, bool f_memory, bool f_wipe)
    : BaseIndex::DB(GetDataDir() / "indexes" / "swapindex", n_cache_size,
                    f_memory, f_wipe) {}

bool SwapIndex::DB::WriteSwaps(const std::vector<std::pair<std::vector<uint8_t>, SwapOffer>> &v_swaps) {
    CDBBatch batch(*this);
    for (const auto &pair : v_swaps) {
        batch.Write(pair.first, pair.second);
    }
    return WriteBatch(batch);
}

bool SwapIndex::DB::ReadSwapsWithPrefix(char prefix, const uint256 &tokenID,
                                        std::vector<SwapOffer> &orders,
                                        size_t limit, size_t offset) {
    std::vector<uint8_t> keyPrefix = MakePrefix(prefix, tokenID);
    
    std::unique_ptr<CDBIterator> it(NewIterator());
    size_t skipped = 0;
    size_t collected = 0;
    
    for (it->Seek(keyPrefix); it->Valid() && collected < limit; it->Next()) {
        std::vector<uint8_t> key;
        if (!it->GetKey(key) || key.size() < keyPrefix.size() ||
            std::memcmp(key.data(), keyPrefix.data(), keyPrefix.size()) != 0) {
            break;
        }
        
        if (skipped < offset) {
            ++skipped;
            continue;
        }
        
        SwapOffer offer;
        if (it->GetValue(offer)) {
            orders.push_back(offer);
            ++collected;
        }
    }
    return true;
}

size_t SwapIndex::DB::CountSwapsWithPrefix(char prefix, const uint256 &tokenID) {
    std::vector<uint8_t> keyPrefix = MakePrefix(prefix, tokenID);
    
    std::unique_ptr<CDBIterator> it(NewIterator());
    size_t count = 0;
    
    for (it->Seek(keyPrefix); it->Valid(); it->Next()) {
        std::vector<uint8_t> key;
        if (!it->GetKey(key) || key.size() < keyPrefix.size() ||
            std::memcmp(key.data(), keyPrefix.data(), keyPrefix.size()) != 0) {
            break;
        }
        ++count;
    }
    return count;
}

bool SwapIndex::DB::DeleteKey(const std::vector<uint8_t> &key) {
    CDBBatch batch(*this);
    batch.Erase(key);
    return WriteBatch(batch);
}

bool SwapIndex::DB::MoveOrderToHistory(const SwapOffer &offer) {
    CDBBatch batch(*this);
    
    // Delete from open orders
    std::vector<uint8_t> openKey = MakeKey(DB_OPEN_ORDER, offer);
    batch.Erase(openKey);
    
    // Add to history
    std::vector<uint8_t> histKey = MakeKey(DB_HISTORY, offer);
    batch.Write(histKey, offer);
    
    return WriteBatch(batch);
}

bool SwapIndex::DB::DeleteHistoryOlderThan(int32_t cutoffHeight) {
    // Iterate all history entries and delete those older than cutoff
    std::vector<uint8_t> histPrefix;
    histPrefix.push_back(static_cast<uint8_t>(DB_HISTORY));
    
    std::vector<std::vector<uint8_t>> keysToDelete;
    
    std::unique_ptr<CDBIterator> it(NewIterator());
    for (it->Seek(histPrefix); it->Valid(); it->Next()) {
        std::vector<uint8_t> key;
        if (!it->GetKey(key) || key.empty() || key[0] != DB_HISTORY) {
            break;
        }
        
        SwapOffer offer;
        if (it->GetValue(offer) && offer.blockHeight < cutoffHeight) {
            keysToDelete.push_back(key);
        }
    }
    
    if (keysToDelete.empty()) {
        return true;
    }
    
    CDBBatch batch(*this);
    for (const auto &key : keysToDelete) {
        batch.Erase(key);
    }
    return WriteBatch(batch);
}

bool SwapIndex::DB::ReadVersion(uint8_t &version) {
    std::vector<uint8_t> key{static_cast<uint8_t>(DB_VERSION)};
    return Read(key, version);
}

bool SwapIndex::DB::WriteVersion(uint8_t version) {
    std::vector<uint8_t> key{static_cast<uint8_t>(DB_VERSION)};
    CDBBatch batch(*this);
    batch.Write(key, version);
    return WriteBatch(batch);
}

bool SwapIndex::DB::MigrateLegacyData() {
    // Migrate data from old 's' prefix to new 'o' prefix
    std::vector<uint8_t> legacyPrefix;
    legacyPrefix.push_back(static_cast<uint8_t>(DB_LEGACY));
    
    std::vector<std::pair<std::vector<uint8_t>, SwapOffer>> toMigrate;
    std::vector<std::vector<uint8_t>> toDelete;
    
    std::unique_ptr<CDBIterator> it(NewIterator());
    for (it->Seek(legacyPrefix); it->Valid(); it->Next()) {
        std::vector<uint8_t> key;
        if (!it->GetKey(key) || key.empty() || key[0] != DB_LEGACY) {
            break;
        }
        
        SwapOffer offer;
        if (it->GetValue(offer)) {
            // Create new key with open prefix
            std::vector<uint8_t> newKey = MakeKey(DB_OPEN_ORDER, offer);
            toMigrate.emplace_back(newKey, offer);
            toDelete.push_back(key);
        }
    }
    
    if (toMigrate.empty()) {
        return true;
    }
    
    LogPrintf("SwapIndex: Migrating %zu legacy entries to new format\n", toMigrate.size());
    
    CDBBatch batch(*this);
    for (const auto &pair : toMigrate) {
        batch.Write(pair.first, pair.second);
    }
    for (const auto &key : toDelete) {
        batch.Erase(key);
    }
    return WriteBatch(batch);
}

SwapIndex::SwapIndex(size_t n_cache_size, int64_t history_blocks, bool f_memory, bool f_wipe)
    : m_db(std::make_unique<SwapIndex::DB>(n_cache_size, f_memory, f_wipe)),
      m_history_blocks(history_blocks) {}

SwapIndex::~SwapIndex() {
    InterruptPrune();
    if (m_prune_thread.joinable()) {
        m_prune_thread.join();
    }
}

void SwapIndex::InterruptPrune() {
    m_prune_interrupt = true;
}

bool SwapIndex::Init() {
    // Check and handle version migration
    uint8_t version = 0;
    if (!m_db->ReadVersion(version)) {
        // No version found - either new index or legacy format
        // Try to migrate legacy data
        if (!m_db->MigrateLegacyData()) {
            LogPrintf("SwapIndex: Warning - failed to migrate legacy data\n");
        }
        // Write current version
        if (!m_db->WriteVersion(SWAP_INDEX_VERSION)) {
            return error("%s: Failed to write swap index version", __func__);
        }
        LogPrintf("SwapIndex: Initialized with version %d\n", SWAP_INDEX_VERSION);
    } else if (version < SWAP_INDEX_VERSION) {
        // Future: handle version upgrades here
        LogPrintf("SwapIndex: Upgrading from version %d to %d\n", version, SWAP_INDEX_VERSION);
        if (!m_db->WriteVersion(SWAP_INDEX_VERSION)) {
            return error("%s: Failed to update swap index version", __func__);
        }
    }
    
    // Start background prune thread
    m_prune_thread = std::thread(&SwapIndex::ThreadPrune, this);
    
    return BaseIndex::Init();
}

void SwapIndex::ThreadPrune() {
    LogPrintf("SwapIndex: Prune thread started\n");
    
    while (!m_prune_interrupt && !ShutdownRequested()) {
        // Sleep for the prune interval
        for (int64_t i = 0; i < SWAP_PRUNE_INTERVAL && !m_prune_interrupt && !ShutdownRequested(); ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        if (m_prune_interrupt || ShutdownRequested()) {
            break;
        }
        
        // Get current chain height
        int32_t currentHeight = 0;
        {
            LOCK(cs_main);
            if (::ChainActive().Tip()) {
                currentHeight = ::ChainActive().Tip()->nHeight;
            }
        }
        
        if (currentHeight > 0 && m_history_blocks > 0) {
            int32_t cutoffHeight = currentHeight - static_cast<int32_t>(m_history_blocks);
            if (cutoffHeight > 0) {
                PruneOldHistory(cutoffHeight);
            }
        }
    }
    
    LogPrintf("SwapIndex: Prune thread stopped\n");
}

bool SwapIndex::MoveToHistory(const SwapOffer &offer) {
    return m_db->MoveOrderToHistory(offer);
}

bool SwapIndex::PruneOldHistory(int32_t cutoffHeight) {
    return m_db->DeleteHistoryOlderThan(cutoffHeight);
}

bool SwapIndex::ProcessSpentOrders(const CBlock &block, int32_t height) {
    // Collect all spent outpoints from this block's inputs
    std::set<COutPoint> spentOutpoints;
    for (const auto &tx : block.vtx) {
        if (tx->IsCoinBase()) {
            continue;
        }
        for (const auto &txin : tx->vin) {
            spentOutpoints.insert(txin.prevout);
        }
    }
    
    if (spentOutpoints.empty()) {
        return true;
    }
    
    // Check all open orders to see if any were spent
    std::vector<uint8_t> openPrefix;
    openPrefix.push_back(static_cast<uint8_t>(DB_OPEN_ORDER));
    
    std::vector<SwapOffer> toMove;
    
    std::unique_ptr<CDBIterator> it(m_db->NewIterator());
    for (it->Seek(openPrefix); it->Valid(); it->Next()) {
        std::vector<uint8_t> key;
        if (!it->GetKey(key) || key.empty() || key[0] != DB_OPEN_ORDER) {
            break;
        }
        
        SwapOffer offer;
        if (it->GetValue(offer)) {
            COutPoint outpoint(TxId(offer.offeredUTXOHash), offer.offeredUTXOIndex);
            if (spentOutpoints.count(outpoint) > 0) {
                toMove.push_back(offer);
            }
        }
    }
    
    // Move spent orders to history
    for (auto &offer : toMove) {
        offer.blockHeight = height; // Record when it was spent
        if (!MoveToHistory(offer)) {
            LogPrintf("SwapIndex: Warning - failed to move spent order to history\n");
        }
    }
    
    return true;
}

bool SwapIndex::WriteBlock(const CBlock &block, const CBlockIndex *pindex) {
    int32_t height = pindex->nHeight;
    
    // First, process any spent orders from this block
    ProcessSpentOrders(block, height);
    
    // Then, index new swap advertisements
    std::vector<std::pair<std::vector<uint8_t>, SwapOffer>> vSwaps;

    for (const auto &tx : block.vtx) {
        for (const auto &txout : tx->vout) {
            // Check for OP_RETURN
            if (txout.scriptPubKey.empty() || txout.scriptPubKey[0] != OP_RETURN) {
                continue;
            }

            // Parse payload
            // Expected format: OP_RETURN <ProtocolID> <Version> <Type> <TokenID> <OfferedUTXOHash> <OfferedUTXOIndex> <PriceTerms> <Signature>
            // ProtocolID = "RSWP" (0x52535750)
            
            opcodetype opcode;
            std::vector<uint8_t> data;
            CScript::const_iterator it = txout.scriptPubKey.begin();
            
            // Skip OP_RETURN
            if (!txout.scriptPubKey.GetOp(it, opcode, data)) continue;

            // Get Protocol ID
            if (!txout.scriptPubKey.GetOp(it, opcode, data)) continue;
            if (data.size() != 4) continue;
            if (std::memcmp(data.data(), "RSWP", 4) != 0) continue;

            SwapOffer offer;
            offer.blockHeight = height;

            // Version
            if (!txout.scriptPubKey.GetOp(it, opcode, data)) continue;
            if (data.size() != 1) continue;
            offer.version = data[0];

            // Type
            if (!txout.scriptPubKey.GetOp(it, opcode, data)) continue;
            if (data.size() != 1) continue;
            offer.type = data[0];

            // TokenID
            if (!txout.scriptPubKey.GetOp(it, opcode, data)) continue;
            if (data.size() != 32) continue;
            offer.tokenID = uint256(data);

            // OfferedUTXOHash
            if (!txout.scriptPubKey.GetOp(it, opcode, data)) continue;
            if (data.size() != 32) continue;
            offer.offeredUTXOHash = uint256(data);

            // OfferedUTXOIndex
            if (!txout.scriptPubKey.GetOp(it, opcode, data)) continue;
            try {
                CScriptNum n(data, false, 4); 
                offer.offeredUTXOIndex = static_cast<uint32_t>(n.getint32());
            } catch (...) {
                // If it's a small int opcode (which GetOp returns as empty data but sets opcode)
                if (opcode >= OP_0 && opcode <= OP_16) {
                    offer.offeredUTXOIndex = CScript::DecodeOP_N(opcode);
                } else {
                    continue; 
                }
            }

            // Price/Terms
            if (!txout.scriptPubKey.GetOp(it, opcode, data)) continue;
            offer.priceTerms = data;

            // Signature
            if (!txout.scriptPubKey.GetOp(it, opcode, data)) continue;
            offer.signature = data;

            // Construct Key with open order prefix
            std::vector<uint8_t> key = MakeKey(DB_OPEN_ORDER, offer);
            vSwaps.emplace_back(key, offer);
        }
    }

    if (vSwaps.empty()) {
        return true;
    }

    return m_db->WriteSwaps(vSwaps);
}

BaseIndex::DB &SwapIndex::GetDB() const {
    return *m_db;
}

bool SwapIndex::GetOpenOrders(const uint256 &tokenID, std::vector<SwapOffer> &orders,
                              size_t limit, size_t offset) {
    // Clamp limit to max
    if (limit > MAX_SWAP_QUERY_LIMIT) {
        limit = MAX_SWAP_QUERY_LIMIT;
    }
    return m_db->ReadSwapsWithPrefix(DB_OPEN_ORDER, tokenID, orders, limit, offset);
}

bool SwapIndex::GetHistoryOrders(const uint256 &tokenID, std::vector<SwapOffer> &orders,
                                 size_t limit, size_t offset) {
    // Clamp limit to max
    if (limit > MAX_SWAP_QUERY_LIMIT) {
        limit = MAX_SWAP_QUERY_LIMIT;
    }
    return m_db->ReadSwapsWithPrefix(DB_HISTORY, tokenID, orders, limit, offset);
}

bool SwapIndex::GetOrderCounts(const uint256 &tokenID, SwapOrderCounts &counts) {
    counts.openCount = m_db->CountSwapsWithPrefix(DB_OPEN_ORDER, tokenID);
    counts.historyCount = m_db->CountSwapsWithPrefix(DB_HISTORY, tokenID);
    return true;
}
