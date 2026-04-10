#!/bin/sh

cd "/Users/main/Downloads/Radiant-Core-main/macos-arm64-release/native"
"/opt/homebrew/bin/cmake" --build "/Users/main/Downloads/Radiant-Core-main/macos-arm64-release/native" --target "src/univalue/univalue_gen"
"/opt/homebrew/bin/cmake" -E create_symlink "/Users/main/Downloads/Radiant-Core-main/macos-arm64-release/native/src/univalue/univalue_gen" "/Users/main/Downloads/Radiant-Core-main/macos-arm64-release/src/univalue/native-univalue_gen"

# Ok let's generate a depfile if we can.
if test "xNinja" = "xNinja"; then
    "/Users/main/Downloads/Radiant-Core-main/cmake/utils/gen-ninja-deps.py" \
        --build-dir "/Users/main/Downloads/Radiant-Core-main/macos-arm64-release/native" \
        --base-dir "/Users/main/Downloads/Radiant-Core-main/macos-arm64-release" \
        --ninja "/opt/homebrew/bin/ninja" \
        "src/univalue/native-univalue_gen" "src/univalue/univalue_gen" > "/Users/main/Downloads/Radiant-Core-main/macos-arm64-release/src/univalue/native-univalue_gen.d"
fi
