# Copyright (c) 2017-2019 The Bitcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

project(radiant-seeder)

add_library(seeder bitcoin.cpp db.cpp dns.cpp)
target_link_libraries(seeder common radiantconsensus)

add_executable(radiant-seeder main.cpp)
target_link_libraries(radiant-seeder common radiantconsensus seeder)

include(BinaryTest)
add_to_symbols_check(radiant-seeder)
add_to_security_check(radiant-seeder)

include(InstallationHelper)
install_target(radiant-seeder)

add_subdirectory(test)
