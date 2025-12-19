// Copyright (c) 2025 The Radiant developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <index/swapindex.h>

#include <chain.h>
#include <chainparams.h>
#include <script/script.h>
#include <script/standard.h>
#include <shutdown.h>
#include <ui_interface.h>
#include <util/system.h>

constexpr char DB_SWAPINDEX = 's';

std::unique_ptr<SwapIndex> g_swapindex;

class SwapIndex::DB : public BaseIndex::DB {
public:
    explicit DB(size_t n_cache_size, bool f_memory = false, bool f_wipe = false);

    bool WriteSwaps(const std::vector<std::pair<std::vector<uint8_t>, SwapOffer>> &v_swaps);
    bool ReadSwaps(const uint256 &tokenID, std::vector<SwapOffer> &orders);
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

bool SwapIndex::DB::ReadSwaps(const uint256 &tokenID, std::vector<SwapOffer> &orders) {
    // Iterate over keys starting with 's' + tokenID
    std::vector<uint8_t> prefix;
    prefix.push_back(DB_SWAPINDEX);
    prefix.insert(prefix.end(), tokenID.begin(), tokenID.end());

    std::unique_ptr<CDBIterator> it(NewIterator());
    for (it->Seek(prefix); it->Valid(); it->Next()) {
        std::vector<uint8_t> key;
        if (!it->GetKey(key) || key.size() < prefix.size() || 
            std::memcmp(key.data(), prefix.data(), prefix.size()) != 0) {
            break; // Finished the range for this tokenID
        }

        SwapOffer offer;
        if (it->GetValue(offer)) {
            orders.push_back(offer);
        }
    }
    return true;
}

SwapIndex::SwapIndex(size_t n_cache_size, bool f_memory, bool f_wipe)
    : m_db(std::make_unique<SwapIndex::DB>(n_cache_size, f_memory, f_wipe)) {}

SwapIndex::~SwapIndex() {}

bool SwapIndex::Init() {
    return BaseIndex::Init();
}

bool SwapIndex::WriteBlock(const CBlock &block, const CBlockIndex *pindex) {
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

            // Construct Key: 's' + TokenID + OfferedUTXOHash + OfferedUTXOIndex
            std::vector<uint8_t> key;
            key.push_back(DB_SWAPINDEX);
            key.insert(key.end(), offer.tokenID.begin(), offer.tokenID.end());
            key.insert(key.end(), offer.offeredUTXOHash.begin(), offer.offeredUTXOHash.end());
            
            // Append index (big endian for sorting?) - actually strict byte comparison is enough for uniqueness
            // We just need a unique key.
            // Let's use simple serialization for the index part or just raw bytes if we want specific sorting.
            // Since we query by TokenID prefix, the suffix order matters less, but consistency is key.
            // Using 4 bytes for index.
            uint32_t idx = offer.offeredUTXOIndex;
            key.push_back((idx >> 24) & 0xFF);
            key.push_back((idx >> 16) & 0xFF);
            key.push_back((idx >> 8) & 0xFF);
            key.push_back(idx & 0xFF);

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

bool SwapIndex::GetOpenOrders(const uint256 &tokenID, std::vector<SwapOffer> &orders) {
    return m_db->ReadSwaps(tokenID, orders);
}
