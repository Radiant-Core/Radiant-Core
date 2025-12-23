// Copyright (c) 2025 The Radiant developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpc/server.h>
#include <rpc/util.h>
#include <index/swapindex.h>
#include <chainparams.h>
#include <coins.h>
#include <core_io.h>
#include <key_io.h>
#include <util/strencodings.h>
#include <validation.h>
#include <txmempool.h>

#include <univalue.h>

// Helper to build a swap offer JSON object
static UniValue::Object BuildSwapOfferObject(const SwapOffer &offer) {
    UniValue::Object obj;
    obj.reserve(10);
    obj.emplace_back("version", offer.version);
    obj.emplace_back("flags", offer.flags);
    obj.emplace_back("offered_type", offer.offeredType);
    obj.emplace_back("terms_type", offer.termsType);
    obj.emplace_back("tokenid", offer.tokenID.GetHex());
    if (!offer.wantTokenID.IsNull()) {
        obj.emplace_back("want_tokenid", offer.wantTokenID.GetHex());
    }
    
    UniValue::Object utxoObj;
    utxoObj.reserve(2);
    utxoObj.emplace_back("txid", offer.offeredUTXOHash.GetHex());
    utxoObj.emplace_back("vout", (uint64_t)offer.offeredUTXOIndex);
    obj.emplace_back("utxo", utxoObj);

    obj.emplace_back("price_terms", HexStr(offer.priceTerms));
    obj.emplace_back("signature", HexStr(offer.signature));
    obj.emplace_back("block_height", offer.blockHeight);
    
    return obj;
}

static UniValue getopenordersbywant(const Config &config,
                                    const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 3) {
        throw std::runtime_error(
            RPCHelpMan{"getopenordersbywant",
                "\nReturns a list of active swap advertisements for a given wanted token reference.\n",
                {
                    {"want_token_ref", RPCArg::Type::STR_HEX, /* opt */ false, /* default_val */ "", "The wanted token reference (TXID) to filter by"},
                    {"limit", RPCArg::Type::NUM, /* opt */ true, /* default_val */ "100", "Maximum number of results to return (max 1000)"},
                    {"offset", RPCArg::Type::NUM, /* opt */ true, /* default_val */ "0", "Number of results to skip for pagination"},
                }}
                .ToString() +
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"version\": n,         (numeric) Protocol version\n"
            "    \"flags\": n,           (numeric) Protocol flags\n"
            "    \"offered_type\": n,    (numeric) Offered asset type\n"
            "    \"terms_type\": n,      (numeric) Terms encoding type\n"
            "    \"tokenid\": \"hex\",     (string) Token ID\n"
            "    \"want_tokenid\": \"hex\", (string, optional) Wanted Token ID\n"
            "    \"utxo\": {\n"
            "      \"txid\": \"hex\",      (string) Offered UTXO TXID\n"
            "      \"vout\": n           (numeric) Offered UTXO index\n"
            "    },\n"
            "    \"price_terms\": \"hex\", (string) Serialized requested output\n"
            "    \"signature\": \"hex\",   (string) Partial signature\n"
            "    \"block_height\": n     (numeric) Block height when indexed\n"
            "  },\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n" +
            HelpExampleCli("getopenordersbywant", "\"<want_token_ref>\"") +
            HelpExampleCli("getopenordersbywant", "\"<want_token_ref>\" 50 0") +
            HelpExampleRpc("getopenordersbywant", "\"<want_token_ref>\", 50, 0"));
    }

    if (!g_swapindex) {
        throw JSONRPCError(RPC_MISC_ERROR, "Swap index not enabled. Use -swapindex=1 to enable.");
    }

    uint256 wantTokenID = ParseHashV(request.params[0], "want_token_ref");

    size_t limit = DEFAULT_SWAP_QUERY_LIMIT;
    size_t offset = 0;

    if (request.params.size() > 1 && !request.params[1].isNull()) {
        int64_t limitParam = request.params[1].get_int64();
        if (limitParam < 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "limit must be non-negative");
        }
        limit = static_cast<size_t>(std::min(limitParam, static_cast<int64_t>(MAX_SWAP_QUERY_LIMIT)));
    }

    if (request.params.size() > 2 && !request.params[2].isNull()) {
        int64_t offsetParam = request.params[2].get_int64();
        if (offsetParam < 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "offset must be non-negative");
        }
        offset = static_cast<size_t>(offsetParam);
    }

    std::vector<SwapOffer> orders;
    if (!g_swapindex->GetOpenOrdersByWant(wantTokenID, orders, limit, offset)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to retrieve orders from swap index");
    }

    LOCK(cs_main);
    CCoinsViewCache &view = *pcoinsTip;
    CCoinsViewMemPool viewMemPool(&view, g_mempool);

    UniValue::Array result;
    for (const auto &offer : orders) {
        COutPoint outpoint(TxId(offer.offeredUTXOHash), offer.offeredUTXOIndex);
        Coin coin;

        if (g_mempool.isSpent(outpoint) || !viewMemPool.GetCoin(outpoint, coin) || coin.IsSpent()) {
            continue;
        }

        result.emplace_back(BuildSwapOfferObject(offer));
    }

    return result;
}

static UniValue getswapcountbywant(const Config &config,
                                   const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            RPCHelpMan{"getswapcountbywant",
                "\nReturns the count of open and historical swap orders for a given wanted token reference.\n"
                "Useful for pagination planning before fetching orders.\n",
                {
                    {"want_token_ref", RPCArg::Type::STR_HEX, /* opt */ false, /* default_val */ "", "The wanted token reference (TXID) to filter by"},
                }}
                .ToString() +
            "\nResult:\n"
            "{\n"
            "  \"open\": n,      (numeric) Number of active (unspent) orders\n"
            "  \"history\": n    (numeric) Number of historical (spent) orders\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getswapcountbywant", "\"<want_token_ref>\"") +
            HelpExampleRpc("getswapcountbywant", "\"<want_token_ref>\""));
    }

    if (!g_swapindex) {
        throw JSONRPCError(RPC_MISC_ERROR, "Swap index not enabled. Use -swapindex=1 to enable.");
    }

    uint256 wantTokenID = ParseHashV(request.params[0], "want_token_ref");

    SwapOrderCounts counts;
    if (!g_swapindex->GetOrderCountsByWant(wantTokenID, counts)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to retrieve order counts from swap index");
    }

    UniValue::Object result;
    result.emplace_back("open", (uint64_t)counts.openCount);
    result.emplace_back("history", (uint64_t)counts.historyCount);

    return result;
}

static UniValue getswaphistorybywant(const Config &config,
                                     const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 3) {
        throw std::runtime_error(
            RPCHelpMan{"getswaphistorybywant",
                "\nReturns a list of executed (or cancelled) swap advertisements for a given wanted token reference.\n"
                "These are advertisements where the offered UTXO has been spent.\n"
                "History is retained for a configurable number of blocks (default: 10000).\n",
                {
                    {"want_token_ref", RPCArg::Type::STR_HEX, /* opt */ false, /* default_val */ "", "The wanted token reference (TXID) to filter by"},
                    {"limit", RPCArg::Type::NUM, /* opt */ true, /* default_val */ "100", "Maximum number of results to return (max 1000)"},
                    {"offset", RPCArg::Type::NUM, /* opt */ true, /* default_val */ "0", "Number of results to skip for pagination"},
                }}
                .ToString() +
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"version\": n,         (numeric) Protocol version\n"
            "    \"flags\": n,           (numeric) Protocol flags\n"
            "    \"offered_type\": n,    (numeric) Offered asset type\n"
            "    \"terms_type\": n,      (numeric) Terms encoding type\n"
            "    \"tokenid\": \"hex\",     (string) Token ID\n"
            "    \"want_tokenid\": \"hex\", (string, optional) Wanted Token ID\n"
            "    \"utxo\": {\n"
            "      \"txid\": \"hex\",      (string) Offered UTXO TXID\n"
            "      \"vout\": n           (numeric) Offered UTXO index\n"
            "    },\n"
            "    \"price_terms\": \"hex\", (string) Serialized requested output\n"
            "    \"signature\": \"hex\",   (string) Partial signature\n"
            "    \"block_height\": n     (numeric) Block height when spent\n"
            "  },\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n" +
            HelpExampleCli("getswaphistorybywant", "\"<want_token_ref>\"") +
            HelpExampleCli("getswaphistorybywant", "\"<want_token_ref>\" 50 0") +
            HelpExampleRpc("getswaphistorybywant", "\"<want_token_ref>\", 50, 0"));
    }

    if (!g_swapindex) {
        throw JSONRPCError(RPC_MISC_ERROR, "Swap index not enabled. Use -swapindex=1 to enable.");
    }

    uint256 wantTokenID = ParseHashV(request.params[0], "want_token_ref");

    size_t limit = DEFAULT_SWAP_QUERY_LIMIT;
    size_t offset = 0;

    if (request.params.size() > 1 && !request.params[1].isNull()) {
        int64_t limitParam = request.params[1].get_int64();
        if (limitParam < 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "limit must be non-negative");
        }
        limit = static_cast<size_t>(std::min(limitParam, static_cast<int64_t>(MAX_SWAP_QUERY_LIMIT)));
    }

    if (request.params.size() > 2 && !request.params[2].isNull()) {
        int64_t offsetParam = request.params[2].get_int64();
        if (offsetParam < 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "offset must be non-negative");
        }
        offset = static_cast<size_t>(offsetParam);
    }

    std::vector<SwapOffer> historyOrders;
    if (!g_swapindex->GetHistoryOrdersByWant(wantTokenID, historyOrders, limit, offset)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to retrieve history from swap index");
    }

    UniValue::Array result;
    for (const auto &offer : historyOrders) {
        result.emplace_back(BuildSwapOfferObject(offer));
    }

    if (result.size() < limit) {
        LOCK(cs_main);

        std::vector<SwapOffer> openOrders;
        if (g_swapindex->GetOpenOrdersByWant(wantTokenID, openOrders, MAX_SWAP_QUERY_LIMIT, 0)) {
            for (const auto &offer : openOrders) {
                if (result.size() >= limit) break;
                COutPoint outpoint(TxId(offer.offeredUTXOHash), offer.offeredUTXOIndex);
                if (g_mempool.isSpent(outpoint)) {
                    result.emplace_back(BuildSwapOfferObject(offer));
                }
            }
        }
    }

    return result;
}

static UniValue getopenorders(const Config &config,
                              const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 3) {
        throw std::runtime_error(
            RPCHelpMan{"getopenorders",
                "\nReturns a list of active swap advertisements for a given token reference.\n",
                {
                    {"token_ref", RPCArg::Type::STR_HEX, /* opt */ false, /* default_val */ "", "The token reference (TXID) to filter by"},
                    {"limit", RPCArg::Type::NUM, /* opt */ true, /* default_val */ "100", "Maximum number of results to return (max 1000)"},
                    {"offset", RPCArg::Type::NUM, /* opt */ true, /* default_val */ "0", "Number of results to skip for pagination"},
                }}
                .ToString() +
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"version\": n,         (numeric) Protocol version\n"
            "    \"flags\": n,           (numeric) Protocol flags\n"
            "    \"offered_type\": n,    (numeric) Offered asset type\n"
            "    \"terms_type\": n,      (numeric) Terms encoding type\n"
            "    \"tokenid\": \"hex\",     (string) Token ID\n"
            "    \"want_tokenid\": \"hex\", (string, optional) Wanted Token ID\n"
            "    \"utxo\": {\n"
            "      \"txid\": \"hex\",      (string) Offered UTXO TXID\n"
            "      \"vout\": n           (numeric) Offered UTXO index\n"
            "    },\n"
            "    \"price_terms\": \"hex\", (string) Serialized requested output\n"
            "    \"signature\": \"hex\",   (string) Partial signature\n"
            "    \"block_height\": n     (numeric) Block height when indexed\n"
            "  },\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n" +
            HelpExampleCli("getopenorders", "\"<token_ref>\"") +
            HelpExampleCli("getopenorders", "\"<token_ref>\" 50 0") +
            HelpExampleRpc("getopenorders", "\"<token_ref>\", 50, 0"));
    }

    if (!g_swapindex) {
        throw JSONRPCError(RPC_MISC_ERROR, "Swap index not enabled. Use -swapindex=1 to enable.");
    }

    uint256 tokenID = ParseHashV(request.params[0], "token_ref");
    
    size_t limit = DEFAULT_SWAP_QUERY_LIMIT;
    size_t offset = 0;
    
    if (request.params.size() > 1 && !request.params[1].isNull()) {
        int64_t limitParam = request.params[1].get_int64();
        if (limitParam < 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "limit must be non-negative");
        }
        limit = static_cast<size_t>(std::min(limitParam, static_cast<int64_t>(MAX_SWAP_QUERY_LIMIT)));
    }
    
    if (request.params.size() > 2 && !request.params[2].isNull()) {
        int64_t offsetParam = request.params[2].get_int64();
        if (offsetParam < 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "offset must be non-negative");
        }
        offset = static_cast<size_t>(offsetParam);
    }

    std::vector<SwapOffer> orders;
    if (!g_swapindex->GetOpenOrders(tokenID, orders, limit, offset)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to retrieve orders from swap index");
    }

    // Additional mempool-aware filtering for orders that may have been spent
    // but not yet confirmed in a block
    LOCK(cs_main);
    CCoinsViewCache &view = *pcoinsTip;
    CCoinsViewMemPool viewMemPool(&view, g_mempool);

    UniValue::Array result;

    for (const auto &offer : orders) {
        COutPoint outpoint(TxId(offer.offeredUTXOHash), offer.offeredUTXOIndex);
        Coin coin;

        // Skip if spent in mempool or not in UTXO set
        if (g_mempool.isSpent(outpoint) || !viewMemPool.GetCoin(outpoint, coin) || coin.IsSpent()) {
            continue;
        }

        result.emplace_back(BuildSwapOfferObject(offer));
    }

    return result;
}

static UniValue getswaphistory(const Config &config,
                               const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 3) {
        throw std::runtime_error(
            RPCHelpMan{"getswaphistory",
                "\nReturns a list of executed (or cancelled) swap advertisements for a given token reference.\n"
                "These are advertisements where the offered UTXO has been spent.\n"
                "History is retained for a configurable number of blocks (default: 10000).\n",
                {
                    {"token_ref", RPCArg::Type::STR_HEX, /* opt */ false, /* default_val */ "", "The token reference (TXID) to filter by"},
                    {"limit", RPCArg::Type::NUM, /* opt */ true, /* default_val */ "100", "Maximum number of results to return (max 1000)"},
                    {"offset", RPCArg::Type::NUM, /* opt */ true, /* default_val */ "0", "Number of results to skip for pagination"},
                }}
                .ToString() +
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"version\": n,         (numeric) Protocol version\n"
            "    \"flags\": n,           (numeric) Protocol flags\n"
            "    \"offered_type\": n,    (numeric) Offered asset type\n"
            "    \"terms_type\": n,      (numeric) Terms encoding type\n"
            "    \"tokenid\": \"hex\",     (string) Token ID\n"
            "    \"want_tokenid\": \"hex\", (string, optional) Wanted Token ID\n"
            "    \"utxo\": {\n"
            "      \"txid\": \"hex\",      (string) Offered UTXO TXID\n"
            "      \"vout\": n           (numeric) Offered UTXO index\n"
            "    },\n"
            "    \"price_terms\": \"hex\", (string) Serialized requested output\n"
            "    \"signature\": \"hex\",   (string) Partial signature\n"
            "    \"block_height\": n     (numeric) Block height when spent\n"
            "  },\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n" +
            HelpExampleCli("getswaphistory", "\"<token_ref>\"") +
            HelpExampleCli("getswaphistory", "\"<token_ref>\" 50 0") +
            HelpExampleRpc("getswaphistory", "\"<token_ref>\", 50, 0"));
    }

    if (!g_swapindex) {
        throw JSONRPCError(RPC_MISC_ERROR, "Swap index not enabled. Use -swapindex=1 to enable.");
    }

    uint256 tokenID = ParseHashV(request.params[0], "token_ref");
    
    size_t limit = DEFAULT_SWAP_QUERY_LIMIT;
    size_t offset = 0;
    
    if (request.params.size() > 1 && !request.params[1].isNull()) {
        int64_t limitParam = request.params[1].get_int64();
        if (limitParam < 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "limit must be non-negative");
        }
        limit = static_cast<size_t>(std::min(limitParam, static_cast<int64_t>(MAX_SWAP_QUERY_LIMIT)));
    }
    
    if (request.params.size() > 2 && !request.params[2].isNull()) {
        int64_t offsetParam = request.params[2].get_int64();
        if (offsetParam < 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "offset must be non-negative");
        }
        offset = static_cast<size_t>(offsetParam);
    }

    // Get orders from the history index
    std::vector<SwapOffer> historyOrders;
    if (!g_swapindex->GetHistoryOrders(tokenID, historyOrders, limit, offset)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to retrieve history from swap index");
    }

    UniValue::Array result;
    for (const auto &offer : historyOrders) {
        result.emplace_back(BuildSwapOfferObject(offer));
    }
    
    // Also check open orders for any that are spent in mempool but not yet confirmed
    // This provides mempool-aware history before the block is mined
    if (result.size() < limit) {
        LOCK(cs_main);
        CCoinsViewCache &view = *pcoinsTip;
        CCoinsViewMemPool viewMemPool(&view, g_mempool);
        
        std::vector<SwapOffer> openOrders;
        // Get more than we need since some will be filtered out
        if (g_swapindex->GetOpenOrders(tokenID, openOrders, MAX_SWAP_QUERY_LIMIT, 0)) {
            for (const auto &offer : openOrders) {
                if (result.size() >= limit) break;
                
                COutPoint outpoint(TxId(offer.offeredUTXOHash), offer.offeredUTXOIndex);
                Coin coin;
                
                // If the coin is spent in mempool, include it in history
                if (g_mempool.isSpent(outpoint)) {
                    result.emplace_back(BuildSwapOfferObject(offer));
                }
            }
        }
    }

    return result;
}

static UniValue getswapcount(const Config &config,
                             const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            RPCHelpMan{"getswapcount",
                "\nReturns the count of open and historical swap orders for a given token reference.\n"
                "Useful for pagination planning before fetching orders.\n",
                {
                    {"token_ref", RPCArg::Type::STR_HEX, /* opt */ false, /* default_val */ "", "The token reference (TXID) to filter by"},
                }}
                .ToString() +
            "\nResult:\n"
            "{\n"
            "  \"open\": n,      (numeric) Number of active (unspent) orders\n"
            "  \"history\": n    (numeric) Number of historical (spent) orders\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getswapcount", "\"<token_ref>\"") +
            HelpExampleRpc("getswapcount", "\"<token_ref>\""));
    }

    if (!g_swapindex) {
        throw JSONRPCError(RPC_MISC_ERROR, "Swap index not enabled. Use -swapindex=1 to enable.");
    }

    uint256 tokenID = ParseHashV(request.params[0], "token_ref");

    SwapOrderCounts counts;
    if (!g_swapindex->GetOrderCounts(tokenID, counts)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to retrieve order counts from swap index");
    }

    UniValue::Object result;
    result.emplace_back("open", (uint64_t)counts.openCount);
    result.emplace_back("history", (uint64_t)counts.historyCount);

    return result;
}

// clang-format off
static const ContextFreeRPCCommand commands[] = {
    //  category            name                      actor (function)        argNames
    //  ------------------- ------------------------  ----------------------  ----------
    { "blockchain",         "getopenorders",          getopenorders,          {"token_ref", "limit", "offset"} },
    { "blockchain",         "getopenordersbywant",    getopenordersbywant,    {"want_token_ref", "limit", "offset"} },
    { "blockchain",         "getswaphistory",         getswaphistory,         {"token_ref", "limit", "offset"} },
    { "blockchain",         "getswaphistorybywant",   getswaphistorybywant,   {"want_token_ref", "limit", "offset"} },
    { "blockchain",         "getswapcount",           getswapcount,           {"token_ref"} },
    { "blockchain",         "getswapcountbywant",     getswapcountbywant,     {"want_token_ref"} },
};
// clang-format on

void RegisterSwapRPCCommands(CRPCTable &t) {
    for (size_t i = 0; i < std::size(commands); i++) {
        t.appendCommand(commands[i].name, &commands[i]);
    }
}
