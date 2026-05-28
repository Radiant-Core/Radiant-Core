# Copyright (c) 2017 The Bitcoin developers
# Copyright (c) 2026 The Radiant Core developers
#
# CMake toolchain file for reproducible macOS ARM64 (Apple Silicon) builds via
# depends/. Pairs with `make -C depends HOST=aarch64-apple-darwin`. See
# doc/build-reproducibility.md for the SDK extraction prerequisite.

set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Use given TOOLCHAIN_PREFIX if specified
if(CMAKE_TOOLCHAIN_PREFIX)
  set(TOOLCHAIN_PREFIX ${CMAKE_TOOLCHAIN_PREFIX})
else()
  set(TOOLCHAIN_PREFIX aarch64-apple-darwin)
endif()

# Pin clang from the depends/ native prefix when present; fall back to the
# system clang for native builds on Apple Silicon hosts.
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

set(CMAKE_C_COMPILER_TARGET ${TOOLCHAIN_PREFIX})
set(CMAKE_CXX_COMPILER_TARGET ${TOOLCHAIN_PREFIX})

# Apple SDK — depends/ stages an extracted SDK in depends/SDKs/. The version
# tracked in depends/packages/native_macos_sdk.mk (or equivalent) is the one
# the build is reproducible against. Update this path if depends/ is rotated
# to a newer SDK.
set(OSX_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/depends/SDKs/MacOSX14.0.sdk")
if(NOT EXISTS "${OSX_SDK_PATH}")
    message(FATAL_ERROR
        "macOS SDK not found at ${OSX_SDK_PATH}. "
        "Extract it via depends/ — see doc/build-reproducibility.md.")
endif()
set(CMAKE_OSX_SYSROOT ${OSX_SDK_PATH})
set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0)
set(CMAKE_OSX_ARCHITECTURES arm64)

# Target environment on the build host system
set(CMAKE_FIND_ROOT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/depends/${TOOLCHAIN_PREFIX};${OSX_SDK_PATH}")

# Native depends prefix (host-only tools)
set(CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/depends/${TOOLCHAIN_PREFIX}/native")

# Modify default behavior of FIND_XXX() commands
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

string(APPEND CMAKE_CXX_FLAGS_INIT " -stdlib=libc++")

# Use the OSX-specific binary manipulation tools, sourced from the depends
# prefix so the build doesn't depend on host Xcode CLI tools being updated.
find_program(CMAKE_AR ${TOOLCHAIN_PREFIX}-ar)
find_program(CMAKE_INSTALL_NAME_TOOL ${TOOLCHAIN_PREFIX}-install_name_tool)
find_program(CMAKE_LINKER ${TOOLCHAIN_PREFIX}-ld)
find_program(CMAKE_NM ${TOOLCHAIN_PREFIX}-nm)
find_program(CMAKE_OBJCOPY ${TOOLCHAIN_PREFIX}-objcopy)
find_program(CMAKE_OBJDUMP ${TOOLCHAIN_PREFIX}-objdump)
find_program(CMAKE_OTOOL ${TOOLCHAIN_PREFIX}-otool)
find_program(CMAKE_RANLIB ${TOOLCHAIN_PREFIX}-ranlib)
find_program(CMAKE_STRIP ${TOOLCHAIN_PREFIX}-strip)
