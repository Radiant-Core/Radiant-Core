# Copyright (c) 2025-present The Radiant Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

add_library(bitcoin_webui STATIC
  auth.cpp
  json.cpp
  node_api.cpp
  settings.cpp
  static.cpp
  wallet_api.cpp
  webui.cpp
)

target_compile_definitions(bitcoin_webui
  PUBLIC
    ENABLE_WEBUI=1
)

target_link_libraries(bitcoin_webui
  PUBLIC
    util
    univalue
  PRIVATE
    crypto
    Event::event
    $<TARGET_NAME_IF_EXISTS:bitcoin_wallet>
)
