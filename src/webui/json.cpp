// Copyright (c) 2025-present The Avian Core developers
// Copyright (c) 2025-present The Radiant Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <webui/webui_internal.h>
#include <univalue.h>
#include <string>

std::string JsonError(const std::string &msg)
{
    UniValue::Object obj;
    obj.emplace_back("error", UniValue(msg));
    return UniValue::stringify(obj);
}
