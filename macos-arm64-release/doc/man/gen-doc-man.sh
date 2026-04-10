#!/usr/bin/env bash
# Copyright (c) 2016-2019 The Bitcoin Core developers
# Copyright (c) 2020-2021 The Bitcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C
set -e

if [ "/Users/main/Downloads/Radiant-Core-main" != "/Users/main/Downloads/Radiant-Core-main/macos-arm64-release" ] && [ -f "/Users/main/Downloads/Radiant-Core-main/doc/man/${1##*/}.1" ]; then
  # If the manpage is available in the source dir, simply copy that.
  # Used for cross-compilation where help2man cannot execute the compiled binaries.
  cp -f "/Users/main/Downloads/Radiant-Core-main/doc/man/${1##*/}.1" "${1##*/}.1"
else
  # If the Git repository is available, set the date shown in help2man output to the date of the current commit.
  if [ -d "/Users/main/Downloads/Radiant-Core-main/.git" ]; then
    export SOURCE_DATE_EPOCH="$(git show -s --format=%ct HEAD)"
  fi
  # Generate the manpage with help2man.
  if [ "Darwin" == "Darwin" ] && [ "$1" == "qt/radiant-qt" ]; then
    help2man --include=footer.h2m -o "${1##*/}.1" "../../src/qt/RadiantCore-Qt.app/Contents/MacOS/RadiantCore-Qt"
  else
    help2man --include=footer.h2m -o "${1##*/}.1" "../../src/$1"
  fi
fi
