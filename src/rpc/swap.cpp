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

static UniValue getopenorders(const Config &config,
                              const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            RPCHelpMan{"getopenorders",
                "\nReturns a list of active swap advertisements for a given token reference.\n",
                {
                    {"token_ref", RPCArg::Type::STR_HEX, /* opt */ false, /* default_val */ "", "The token reference (TXID) to filter by"},
                }}
                .ToString() +
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"version\": n,       (numeric) Protocol version\n"
            "    \"type\": n,          (numeric) Swap type (1=Sell, 2=Buy)\n"
            "    \"tokenid\": \"hex\",   (string) Token ID\n"
            "    \"utxo\": {\n"
            "      \"txid\": \"hex\",    (string) Offered UTXO TXID\n"
            "      \"vout\": n         (numeric) Offered UTXO index\n"
            "    },\n"
            "    \"price_terms\": \"hex\", (string) Serialized requested output\n"
            "    \"signature\": \"hex\"    (string) Partial signature\n"
            "  },\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n" +
            HelpExampleCli("getopenorders", "\"<token_ref>\"") +
            HelpExampleRpc("getopenorders", "\"<token_ref>\""));
    }

    if (!g_swapindex) {
        throw JSONRPCError(RPC_MISC_ERROR, "Swap index not enabled. Use -swapindex=1 to enable.");
    }

    uint256 tokenID = ParseHashV(request.params[0], "token_ref");

    std::vector<SwapOffer> orders;
    if (!g_swapindex->GetOpenOrders(tokenID, orders)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to retrieve orders from swap index");
    }

    // TODO: Add UTXO validation to filter spent orders
    // This requires fixing byte order handling between the index and UTXO set

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

        UniValue::Object obj;
        obj.reserve(6);
        obj.emplace_back("version", offer.version);
        obj.emplace_back("type", offer.type);
        obj.emplace_back("tokenid", offer.tokenID.GetHex());
        
        UniValue::Object utxoObj;
        utxoObj.reserve(2);
        utxoObj.emplace_back("txid", offer.offeredUTXOHash.GetHex());
        utxoObj.emplace_back("vout", (uint64_t)offer.offeredUTXOIndex);
        obj.emplace_back("utxo", utxoObj);

        obj.emplace_back("price_terms", HexStr(offer.priceTerms));
        obj.emplace_back("signature", HexStr(offer.signature));

        result.emplace_back(obj);
    }

    return result;
}

static UniValue getswaphistory(const Config &config,
                               const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            RPCHelpMan{"getswaphistory",
                "\nReturns a list of executed (or cancelled) swap advertisements for a given token reference.\n"
                "These are advertisements where the offered UTXO has been spent.\n",
                {
                    {"token_ref", RPCArg::Type::STR_HEX, /* opt */ false, /* default_val */ "", "The token reference (TXID) to filter by"},
                }}
                .ToString() +
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"version\": n,       (numeric) Protocol version\n"
            "    \"type\": n,          (numeric) Swap type (1=Sell, 2=Buy)\n"
            "    \"tokenid\": \"hex\",   (string) Token ID\n"
            "    \"utxo\": {\n"
            "      \"txid\": \"hex\",    (string) Offered UTXO TXID\n"
            "      \"vout\": n         (numeric) Offered UTXO index\n"
            "    },\n"
            "    \"price_terms\": \"hex\", (string) Serialized requested output\n"
            "    \"signature\": \"hex\"    (string) Partial signature\n"
            "  },\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n" +
            HelpExampleCli("getswaphistory", "\"<token_ref>\"") +
            HelpExampleRpc("getswaphistory", "\"<token_ref>\""));
    }

    if (!g_swapindex) {
        throw JSONRPCError(RPC_MISC_ERROR, "Swap index not enabled. Use -swapindex=1 to enable.");
    }

    uint256 tokenID = ParseHashV(request.params[0], "token_ref");

    std::vector<SwapOffer> orders;
    if (!g_swapindex->GetOpenOrders(tokenID, orders)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to retrieve orders from swap index");
    }

    // Filter for SPENT orders
    LOCK(cs_main);
    CCoinsViewCache &view = *pcoinsTip;
    CCoinsViewMemPool viewMemPool(&view, g_mempool);

    UniValue::Array result;

    for (const auto &offer : orders) {
        COutPoint outpoint(TxId(offer.offeredUTXOHash), offer.offeredUTXOIndex);
        Coin coin;
        
        // If the coin is NOT in the view, it is spent.
        if (g_mempool.isSpent(outpoint) || !viewMemPool.GetCoin(outpoint, coin) || coin.IsSpent()) {
            UniValue::Object obj;
            obj.reserve(6);
            obj.emplace_back("version", offer.version);
            obj.emplace_back("type", offer.type);
            obj.emplace_back("tokenid", offer.tokenID.GetHex());
            
            UniValue::Object utxoObj;
            utxoObj.reserve(2);
            utxoObj.emplace_back("txid", offer.offeredUTXOHash.GetHex());
            utxoObj.emplace_back("vout", (uint64_t)offer.offeredUTXOIndex);
            obj.emplace_back("utxo", utxoObj);

            obj.emplace_back("price_terms", HexStr(offer.priceTerms));
            obj.emplace_back("signature", HexStr(offer.signature));

            result.emplace_back(obj);
        }
    }

    return result;
}

// clang-format off
static const ContextFreeRPCCommand commands[] = {
    //  category            name                      actor (function)        argNames
    //  ------------------- ------------------------  ----------------------  ----------
    { "blockchain",         "getopenorders",          getopenorders,          {"token_ref"} },
    { "blockchain",         "getswaphistory",         getswaphistory,         {"token_ref"} },
};
// clang-format on

void RegisterSwapRPCCommands(CRPCTable &t) {
    for (size_t i = 0; i < std::size(commands); i++) {
        t.appendCommand(commands[i].name, &commands[i]);
    }
}
