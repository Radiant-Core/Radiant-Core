// Copyright (c) 2025-present The Avian Core developers
// Copyright (c) 2025-present The Radiant Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#pragma once

#include <cstdint>
#include <string>

static constexpr bool DEFAULT_WEBUI_ENABLE{false};
inline const std::string WEBUI_COOKIE_FILE{"webui.cookie"};

/** Start the Web UI HTTP endpoint. Registers routes on the existing HTTP server.
 *  Requires the HTTP server to already be initialised (-server=1 implied). */
void StartWebUI();

/** Signal Web UI shutdown (stops accepting new SSE clients). */
void InterruptWebUI();

/** Stop the Web UI HTTP endpoint and remove the session cookie file. */
void StopWebUI();
