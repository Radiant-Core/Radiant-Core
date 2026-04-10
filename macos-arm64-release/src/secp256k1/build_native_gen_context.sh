#!/bin/sh

cd "/Users/main/Downloads/Radiant-Core-main/macos-arm64-release/native"
"/opt/homebrew/bin/cmake" --build "/Users/main/Downloads/Radiant-Core-main/macos-arm64-release/native" --target "src/secp256k1/gen_context"
"/opt/homebrew/bin/cmake" -E create_symlink "/Users/main/Downloads/Radiant-Core-main/macos-arm64-release/native/src/secp256k1/gen_context" "/Users/main/Downloads/Radiant-Core-main/macos-arm64-release/src/secp256k1/native-gen_context"

# Ok let's generate a depfile if we can.
if test "xNinja" = "xNinja"; then
    "/Users/main/Downloads/Radiant-Core-main/cmake/utils/gen-ninja-deps.py" \
        --build-dir "/Users/main/Downloads/Radiant-Core-main/macos-arm64-release/native" \
        --base-dir "/Users/main/Downloads/Radiant-Core-main/macos-arm64-release" \
        --ninja "/opt/homebrew/bin/ninja" \
        "src/secp256k1/native-gen_context" "src/secp256k1/gen_context" > "/Users/main/Downloads/Radiant-Core-main/macos-arm64-release/src/secp256k1/native-gen_context.d"
fi
