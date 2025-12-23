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
constexpr char DB_OPEN_WANT = 'p';     // Open orders indexed by wantTokenID
constexpr char DB_HISTORY_WANT = 'q';  // Historical orders indexed by wantTokenID
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

static std::vector<uint8_t> MakeWantKey(char prefix, const SwapOffer &offer) {
    std::vector<uint8_t> key;
    key.reserve(1 + 32 + 32 + 32 + 4); // prefix + wantTokenID + tokenID + utxoHash + utxoIndex
    key.push_back(static_cast<uint8_t>(prefix));
    key.insert(key.end(), offer.wantTokenID.begin(), offer.wantTokenID.end());
    key.insert(key.end(), offer.tokenID.begin(), offer.tokenID.end());
    key.insert(key.end(), offer.offeredUTXOHash.begin(), offer.offeredUTXOHash.end());

    uint32_t idx = offer.offeredUTXOIndex;
    key.push_back((idx >> 24) & 0xFF);
    key.push_back((idx >> 16) & 0xFF);
    key.push_back((idx >> 8) & 0xFF);
    key.push_back(idx & 0xFF);
    return key;
}

static size_t KeyLenForPrefix(char prefix) {
    switch (prefix) {
        case DB_OPEN_ORDER:
        case DB_HISTORY:
            return 1 + 32 + 32 + 4;
        case DB_OPEN_WANT:
        case DB_HISTORY_WANT:
            return 1 + 32 + 32 + 32 + 4;
        default:
            return 1;
    }
}

static std::vector<uint8_t> MakeMatchPrefix(char prefix, const uint256 &tokenID) {
    std::vector<uint8_t> result;
    result.reserve(1 + 32);
    result.push_back(static_cast<uint8_t>(prefix));
    result.insert(result.end(), tokenID.begin(), tokenID.end());
    return result;
}

static std::vector<uint8_t> MakeSeekKey(char prefix, const uint256 &tokenID) {
    const size_t key_len = KeyLenForPrefix(prefix);
    std::vector<uint8_t> result(key_len, 0);
    result[0] = static_cast<uint8_t>(prefix);
    if (key_len >= 1 + 32) {
        std::copy(tokenID.begin(), tokenID.end(), result.begin() + 1);
    }
    return result;
}

static std::vector<uint8_t> MakeTypeSeekKey(char prefix) {
    const size_t key_len = KeyLenForPrefix(prefix);
    std::vector<uint8_t> result(key_len, 0);
    result[0] = static_cast<uint8_t>(prefix);
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
    
    // Move order from history back to open (for reorg handling)
    bool MoveOrderToOpen(const SwapOffer &offer);
    
    // Delete old history entries
    bool DeleteHistoryOlderThan(int32_t cutoffHeight);
    
    // Version management
    bool ReadVersion(uint8_t &version);
    bool WriteVersion(uint8_t version);
    
    // Migration from legacy format
    bool MigrateLegacyData();

    // Migration from v1 SwapOffer schema to v2 SwapOffer schema
    bool MigrateOfferSchema(uint8_t old_version);
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
    std::vector<uint8_t> matchPrefix = MakeMatchPrefix(prefix, tokenID);
    std::vector<uint8_t> seekKey = MakeSeekKey(prefix, tokenID);
    
    std::unique_ptr<CDBIterator> it(NewIterator());
    size_t skipped = 0;
    size_t collected = 0;
    
    for (it->Seek(seekKey); it->Valid() && collected < limit; it->Next()) {
        std::vector<uint8_t> key;
        if (!it->GetKey(key) || key.size() < matchPrefix.size() ||
            std::memcmp(key.data(), matchPrefix.data(), matchPrefix.size()) != 0) {
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
    std::vector<uint8_t> matchPrefix = MakeMatchPrefix(prefix, tokenID);
    std::vector<uint8_t> seekKey = MakeSeekKey(prefix, tokenID);
    
    std::unique_ptr<CDBIterator> it(NewIterator());
    size_t count = 0;
    
    for (it->Seek(seekKey); it->Valid() && count < MAX_SWAP_COUNT_ITERATIONS; it->Next()) {
        std::vector<uint8_t> key;
        if (!it->GetKey(key) || key.size() < matchPrefix.size() ||
            std::memcmp(key.data(), matchPrefix.data(), matchPrefix.size()) != 0) {
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

    if (!offer.wantTokenID.IsNull()) {
        std::vector<uint8_t> openWantKey = MakeWantKey(DB_OPEN_WANT, offer);
        batch.Erase(openWantKey);
    }
    
    // Add to history
    std::vector<uint8_t> histKey = MakeKey(DB_HISTORY, offer);
    batch.Write(histKey, offer);

    if (!offer.wantTokenID.IsNull()) {
        std::vector<uint8_t> histWantKey = MakeWantKey(DB_HISTORY_WANT, offer);
        batch.Write(histWantKey, offer);
    }
    
    return WriteBatch(batch);
}

bool SwapIndex::DB::MoveOrderToOpen(const SwapOffer &offer) {
    CDBBatch batch(*this);
    
    // Delete from history
    std::vector<uint8_t> histKey = MakeKey(DB_HISTORY, offer);
    batch.Erase(histKey);

    if (!offer.wantTokenID.IsNull()) {
        std::vector<uint8_t> histWantKey = MakeWantKey(DB_HISTORY_WANT, offer);
        batch.Erase(histWantKey);
    }
    
    // Add back to open orders
    std::vector<uint8_t> openKey = MakeKey(DB_OPEN_ORDER, offer);
    batch.Write(openKey, offer);

    if (!offer.wantTokenID.IsNull()) {
        std::vector<uint8_t> openWantKey = MakeWantKey(DB_OPEN_WANT, offer);
        batch.Write(openWantKey, offer);
    }
    
    return WriteBatch(batch);
}

bool SwapIndex::DB::DeleteHistoryOlderThan(int32_t cutoffHeight) {
    // Iterate all history entries and delete those older than cutoff
    std::vector<uint8_t> histSeekKey = MakeTypeSeekKey(DB_HISTORY);
    
    std::vector<std::vector<uint8_t>> keysToDelete;
    
    std::unique_ptr<CDBIterator> it(NewIterator());
    for (it->Seek(histSeekKey); it->Valid(); it->Next()) {
        std::vector<uint8_t> key;
        if (!it->GetKey(key) || key.empty() || key[0] != DB_HISTORY) {
            break;
        }
        
        SwapOffer offer;
        if (it->GetValue(offer) && offer.blockHeight < cutoffHeight) {
            keysToDelete.push_back(key);
        }
    }

    std::vector<uint8_t> histWantSeekKey = MakeTypeSeekKey(DB_HISTORY_WANT);
    for (it->Seek(histWantSeekKey); it->Valid(); it->Next()) {
        std::vector<uint8_t> key;
        if (!it->GetKey(key) || key.empty() || key[0] != DB_HISTORY_WANT) {
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

struct SwapOfferV1 {
    uint8_t version;
    uint8_t type;
    uint256 tokenID;
    uint256 offeredUTXOHash;
    uint32_t offeredUTXOIndex;
    std::vector<uint8_t> priceTerms;
    std::vector<uint8_t> signature;
    int32_t blockHeight{0};

    SERIALIZE_METHODS(SwapOfferV1, obj) {
        READWRITE(obj.version, obj.type, obj.tokenID, obj.offeredUTXOHash,
                  obj.offeredUTXOIndex, obj.priceTerms, obj.signature, obj.blockHeight);
    }
};

bool SwapIndex::DB::MigrateLegacyData() {
    // Migrate data from old 's' prefix to new 'o' prefix
    std::vector<uint8_t> legacyPrefix;
    legacyPrefix.push_back(static_cast<uint8_t>(DB_LEGACY));
    
    std::vector<std::vector<uint8_t>> toDelete;
    
    std::unique_ptr<CDBIterator> it(NewIterator());
    for (it->Seek(legacyPrefix); it->Valid(); it->Next()) {
        std::vector<uint8_t> key;
        if (!it->GetKey(key) || key.empty() || key[0] != DB_LEGACY) {
            break;
        }

        SwapOfferV1 legacy;
        if (!it->GetValue(legacy)) {
            continue;
        }

        SwapOffer offer;
        offer.version = legacy.version;
        offer.flags = 0;
        offer.offeredType = 0;
        offer.termsType = 0;
        offer.tokenID = legacy.tokenID;
        offer.wantTokenID.SetNull();
        offer.offeredUTXOHash = legacy.offeredUTXOHash;
        offer.offeredUTXOIndex = legacy.offeredUTXOIndex;
        offer.priceTerms = legacy.priceTerms;
        offer.signature = legacy.signature;
        offer.blockHeight = legacy.blockHeight;

        CDBBatch batch(*this);
        std::vector<uint8_t> newKey = MakeKey(DB_OPEN_ORDER, offer);
        batch.Write(newKey, offer);
        batch.Erase(key);
        if (!WriteBatch(batch)) {
            return false;
        }
    }

    return true;
}

bool SwapIndex::DB::MigrateOfferSchema(uint8_t old_version) {
    if (old_version >= SWAP_INDEX_VERSION) {
        return true;
    }

    std::vector<std::vector<uint8_t>> keysToRewrite;

    std::unique_ptr<CDBIterator> it(NewIterator());
    std::vector<uint8_t> openSeekKey = MakeTypeSeekKey(DB_OPEN_ORDER);
    for (it->Seek(openSeekKey); it->Valid(); it->Next()) {
        std::vector<uint8_t> key;
        if (!it->GetKey(key) || key.empty() || key[0] != DB_OPEN_ORDER) {
            break;
        }
        keysToRewrite.push_back(key);
    }

    std::vector<uint8_t> histSeekKey = MakeTypeSeekKey(DB_HISTORY);
    for (it->Seek(histSeekKey); it->Valid(); it->Next()) {
        std::vector<uint8_t> key;
        if (!it->GetKey(key) || key.empty() || key[0] != DB_HISTORY) {
            break;
        }
        keysToRewrite.push_back(key);
    }

    if (keysToRewrite.empty()) {
        return true;
    }

    CDBBatch batch(*this);
    for (const auto &key : keysToRewrite) {
        SwapOfferV1 legacy;
        if (!Read(key, legacy)) {
            continue;
        }

        SwapOffer offer;
        offer.version = legacy.version;
        offer.flags = 0;
        offer.offeredType = 0;
        offer.termsType = 0;
        offer.tokenID = legacy.tokenID;
        offer.wantTokenID.SetNull();
        offer.offeredUTXOHash = legacy.offeredUTXOHash;
        offer.offeredUTXOIndex = legacy.offeredUTXOIndex;
        offer.priceTerms = legacy.priceTerms;
        offer.signature = legacy.signature;
        offer.blockHeight = legacy.blockHeight;

        batch.Write(key, offer);
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
        version = 1;
    }

    if (version < SWAP_INDEX_VERSION) {
        if (!m_db->MigrateOfferSchema(version)) {
            return error("%s: Failed to migrate swap offer schema", __func__);
        }
        if (!m_db->WriteVersion(SWAP_INDEX_VERSION)) {
            return error("%s: Failed to write swap index version", __func__);
        }
        LogPrintf("SwapIndex: Initialized with version %d\n", SWAP_INDEX_VERSION);
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

bool SwapIndex::MoveToOpen(const SwapOffer &offer) {
    return m_db->MoveOrderToOpen(offer);
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
    std::vector<uint8_t> openSeekKey = MakeTypeSeekKey(DB_OPEN_ORDER);
    
    std::vector<SwapOffer> toMove;
    
    std::unique_ptr<CDBIterator> it(m_db->NewIterator());
    for (it->Seek(openSeekKey); it->Valid(); it->Next()) {
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
        const std::string txid = tx->GetId().GetHex();
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

            if (offer.version == 2) {
                static constexpr uint8_t FLAG_HAS_WANT = 1;

                if (!txout.scriptPubKey.GetOp(it, opcode, data)) continue;
                if (data.size() != 1) continue;
                offer.flags = data[0];

                if (!txout.scriptPubKey.GetOp(it, opcode, data)) continue;
                if (data.size() != 1) continue;
                offer.offeredType = data[0];

                if (!txout.scriptPubKey.GetOp(it, opcode, data)) continue;
                if (data.size() != 1) continue;
                offer.termsType = data[0];

                if (!txout.scriptPubKey.GetOp(it, opcode, data)) continue;
                if (data.size() != 32) continue;
                offer.tokenID = uint256(data);

                if (offer.flags & FLAG_HAS_WANT) {
                    if (!txout.scriptPubKey.GetOp(it, opcode, data)) continue;
                    if (data.size() != 32) continue;
                    offer.wantTokenID = uint256(data);
                } else {
                    offer.wantTokenID.SetNull();
                }

                if (!txout.scriptPubKey.GetOp(it, opcode, data)) continue;
                if (data.size() != 32) continue;
                offer.offeredUTXOHash = uint256(data);

                if (!txout.scriptPubKey.GetOp(it, opcode, data)) continue;
                try {
                    CScriptNum n(data, false, 4);
                    offer.offeredUTXOIndex = static_cast<uint32_t>(n.getint32());
                } catch (...) {
                    if (opcode >= OP_0 && opcode <= OP_16) {
                        offer.offeredUTXOIndex = CScript::DecodeOP_N(opcode);
                    } else {
                        continue;
                    }
                }

                std::vector<std::vector<uint8_t>> tail;
                while (txout.scriptPubKey.GetOp(it, opcode, data)) {
                    if (opcode > OP_PUSHDATA4) {
                        tail.clear();
                        break;
                    }
                    tail.push_back(data);
                }

                if (tail.size() < 2) {
                    continue;
                }

                offer.priceTerms.clear();
                for (size_t i = 0; i + 1 < tail.size(); ++i) {
                    offer.priceTerms.insert(offer.priceTerms.end(), tail[i].begin(), tail[i].end());
                }
                offer.signature = tail.back();
            } else {
                // Legacy v1 format
                offer.flags = 0;
                offer.offeredType = 0;
                offer.termsType = 0;
                offer.wantTokenID.SetNull();

                if (!txout.scriptPubKey.GetOp(it, opcode, data)) continue;
                if (data.size() != 1) continue;

                if (!txout.scriptPubKey.GetOp(it, opcode, data)) continue;
                if (data.size() != 32) continue;
                offer.tokenID = uint256(data);

                if (!txout.scriptPubKey.GetOp(it, opcode, data)) continue;
                if (data.size() != 32) continue;
                offer.offeredUTXOHash = uint256(data);

                if (!txout.scriptPubKey.GetOp(it, opcode, data)) continue;
                try {
                    CScriptNum n(data, false, 4);
                    offer.offeredUTXOIndex = static_cast<uint32_t>(n.getint32());
                } catch (...) {
                    if (opcode >= OP_0 && opcode <= OP_16) {
                        offer.offeredUTXOIndex = CScript::DecodeOP_N(opcode);
                    } else {
                        continue;
                    }
                }

                if (!txout.scriptPubKey.GetOp(it, opcode, data)) continue;
                offer.priceTerms = data;

                if (!txout.scriptPubKey.GetOp(it, opcode, data)) continue;
                offer.signature = data;
            }

            // Construct Key with open order prefix
            std::vector<uint8_t> key = MakeKey(DB_OPEN_ORDER, offer);
            vSwaps.emplace_back(key, offer);

            if (!offer.wantTokenID.IsNull()) {
                std::vector<uint8_t> wantKey = MakeWantKey(DB_OPEN_WANT, offer);
                vSwaps.emplace_back(wantKey, offer);
            }
        }
    }

    if (vSwaps.empty()) {
        return true;
    }

    return m_db->WriteSwaps(vSwaps);
}

void SwapIndex::BlockDisconnected(const std::shared_ptr<const CBlock> &block) {
    // During a reorg, we need to handle orders that may have been moved to history
    // when their UTXOs were spent in the now-disconnected block
    ProcessDisconnectedBlock(*block);
}

bool SwapIndex::ProcessDisconnectedBlock(const CBlock &block) {
    // Collect all outpoints that were spent in this block
    // These spends are now being undone, so any orders that were moved to history
    // because of these spends should be moved back to open
    std::set<COutPoint> restoredOutpoints;
    for (const auto &tx : block.vtx) {
        if (tx->IsCoinBase()) {
            continue;
        }
        for (const auto &txin : tx->vin) {
            restoredOutpoints.insert(txin.prevout);
        }
    }
    
    if (restoredOutpoints.empty()) {
        return true;
    }
    
    // Check history orders to see if any should be restored to open
    std::vector<uint8_t> histSeekKey = MakeTypeSeekKey(DB_HISTORY);
    
    std::vector<SwapOffer> toRestore;
    
    std::unique_ptr<CDBIterator> it(m_db->NewIterator());
    for (it->Seek(histSeekKey); it->Valid(); it->Next()) {
        std::vector<uint8_t> key;
        if (!it->GetKey(key) || key.empty() || key[0] != DB_HISTORY) {
            break;
        }
        
        SwapOffer offer;
        if (it->GetValue(offer)) {
            COutPoint outpoint(TxId(offer.offeredUTXOHash), offer.offeredUTXOIndex);
            if (restoredOutpoints.count(outpoint) > 0) {
                toRestore.push_back(offer);
            }
        }
    }
    
    // Move restored orders back to open
    for (auto &offer : toRestore) {
        // Reset blockHeight to original indexing height (we don't have this info,
        // so we keep it as-is for now - it represents when it was last moved)
        if (!MoveToOpen(offer)) {
            LogPrintf("SwapIndex: Warning - failed to restore order to open during reorg\n");
        }
    }
    
    // Also need to remove any swap advertisements that were indexed in this block
    // We identify them by iterating open orders and checking if they were indexed
    // at the height that's being disconnected
    // Note: We don't have exact block height in the disconnected block easily,
    // so we remove ads whose offered UTXO was created in this block
    std::set<TxId> blockTxIds;
    for (const auto &tx : block.vtx) {
        blockTxIds.insert(tx->GetId());
    }
    
    // Check if any open orders reference UTXOs from this block's transactions
    // (meaning the advertisement tx was in this block)
    std::vector<uint8_t> openSeekKey = MakeTypeSeekKey(DB_OPEN_ORDER);
    std::vector<std::vector<uint8_t>> keysToDelete;
    std::vector<std::vector<uint8_t>> wantKeysToDelete;
    
    for (it->Seek(openSeekKey); it->Valid(); it->Next()) {
        std::vector<uint8_t> key;
        if (!it->GetKey(key) || key.empty() || key[0] != DB_OPEN_ORDER) {
            break;
        }
        
        SwapOffer offer;
        if (it->GetValue(offer)) {
            // Check if this offer's advertisement was in the disconnected block
            // by checking if the txid is in our block's transactions
            // Note: This is an approximation - ideally we'd track the ad txid
            for (const auto &tx : block.vtx) {
                for (const auto &txout : tx->vout) {
                    if (txout.scriptPubKey.empty() || txout.scriptPubKey[0] != OP_RETURN) {
                        continue;
                    }
                    // Check if this is an RSWP output matching our offer
                    opcodetype opcode;
                    std::vector<uint8_t> data;
                    CScript::const_iterator sit = txout.scriptPubKey.begin();
                    if (!txout.scriptPubKey.GetOp(sit, opcode, data)) continue;
                    if (!txout.scriptPubKey.GetOp(sit, opcode, data)) continue;
                    if (data.size() != 4 || std::memcmp(data.data(), "RSWP", 4) != 0) continue;
                    
                    // This is an RSWP ad - check if it matches our offer's UTXO
                    // Skip version and other fields to get to UTXO hash
                    if (!txout.scriptPubKey.GetOp(sit, opcode, data)) continue; // version
                    uint8_t version = data.empty() ? 0 : data[0];
                    
                    if (version == 2) {
                        if (!txout.scriptPubKey.GetOp(sit, opcode, data)) continue; // flags
                        uint8_t flags = data.empty() ? 0 : data[0];
                        if (!txout.scriptPubKey.GetOp(sit, opcode, data)) continue; // offeredType
                        if (!txout.scriptPubKey.GetOp(sit, opcode, data)) continue; // termsType
                        if (!txout.scriptPubKey.GetOp(sit, opcode, data)) continue; // tokenID
                        if (data.size() != 32) continue;
                        uint256 tokenID(data);
                        
                        if (flags & 0x01) {
                            if (!txout.scriptPubKey.GetOp(sit, opcode, data)) continue; // wantTokenID
                        }
                        
                        if (!txout.scriptPubKey.GetOp(sit, opcode, data)) continue; // utxoHash
                        if (data.size() != 32) continue;
                        uint256 utxoHash(data);
                        
                        if (tokenID == offer.tokenID && utxoHash == offer.offeredUTXOHash) {
                            keysToDelete.push_back(key);
                            if (!offer.wantTokenID.IsNull()) {
                                wantKeysToDelete.push_back(MakeWantKey(DB_OPEN_WANT, offer));
                            }
                        }
                    }
                }
            }
        }
    }
    
    // Delete the keys for ads from the disconnected block
    if (!keysToDelete.empty() || !wantKeysToDelete.empty()) {
        CDBBatch batch(*m_db);
        for (const auto &key : keysToDelete) {
            batch.Erase(key);
        }
        for (const auto &key : wantKeysToDelete) {
            batch.Erase(key);
        }
        m_db->WriteBatch(batch);
    }
    
    LogPrintf("SwapIndex: Processed disconnected block, restored %zu orders, removed %zu ads\n", 
              toRestore.size(), keysToDelete.size());
    
    return true;
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

bool SwapIndex::GetOpenOrdersByWant(const uint256 &wantTokenID, std::vector<SwapOffer> &orders,
                                    size_t limit, size_t offset) {
    if (limit > MAX_SWAP_QUERY_LIMIT) {
        limit = MAX_SWAP_QUERY_LIMIT;
    }
    return m_db->ReadSwapsWithPrefix(DB_OPEN_WANT, wantTokenID, orders, limit, offset);
}

bool SwapIndex::GetHistoryOrdersByWant(const uint256 &wantTokenID, std::vector<SwapOffer> &orders,
                                       size_t limit, size_t offset) {
    if (limit > MAX_SWAP_QUERY_LIMIT) {
        limit = MAX_SWAP_QUERY_LIMIT;
    }
    return m_db->ReadSwapsWithPrefix(DB_HISTORY_WANT, wantTokenID, orders, limit, offset);
}

bool SwapIndex::GetOrderCountsByWant(const uint256 &wantTokenID, SwapOrderCounts &counts) {
    counts.openCount = m_db->CountSwapsWithPrefix(DB_OPEN_WANT, wantTokenID);
    counts.historyCount = m_db->CountSwapsWithPrefix(DB_HISTORY_WANT, wantTokenID);
    return true;
}
