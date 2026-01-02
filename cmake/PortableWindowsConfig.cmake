# Portable Windows CMake configuration for Radiant Core
# This script automatically finds dependencies on Windows

cmake_minimum_required(VERSION 3.22)

# Function to find OpenSSL on Windows
function(find_portable_openssl)
    set(OPENSSL_FOUND FALSE)
    
    # Try vcpkg first
    if(DEFINED ENV{VCPKG_ROOT})
        set(VCPKG_OPENSSL "$ENV{VCPKG_ROOT}/installed/x64-windows")
        if(EXISTS "${VCPKG_OPENSSL}/include/openssl/ssl.h")
            set(OPENSSL_ROOT_DIR "${VCPKG_OPENSSL}" CACHE PATH "OpenSSL root directory")
            set(OPENSSL_FOUND TRUE)
            message(STATUS "Found OpenSSL via vcpkg: ${OPENSSL_ROOT_DIR}")
        endif()
    endif()
    
    # Try MSYS2
    if(NOT OPENSSL_FOUND AND EXISTS "C:/msys64/mingw64")
        if(EXISTS "C:/msys64/mingw64/include/openssl/ssl.h")
            set(OPENSSL_ROOT_DIR "C:/msys64/mingw64" CACHE PATH "OpenSSL root directory")
            set(OPENSSL_FOUND TRUE)
            message(STATUS "Found OpenSSL via MSYS2: ${OPENSSL_ROOT_DIR}")
        endif()
    endif()
    
    # Try Strawberry Perl
    if(NOT OPENSSL_FOUND AND EXISTS "C:/Strawberry/c")
        if(EXISTS "C:/Strawberry/c/include/openssl/ssl.h")
            set(OPENSSL_ROOT_DIR "C:/Strawberry/c" CACHE PATH "OpenSSL root directory")
            set(OPENSSL_FOUND TRUE)
            message(STATUS "Found OpenSSL via Strawberry Perl: ${OPENSSL_ROOT_DIR}")
        endif()
    endif()
    
    # Try system paths
    if(NOT OPENSSL_FOUND)
        set(SYSTEM_PATHS
            "C:/Program Files/OpenSSL"
            "C:/Program Files (x86)/OpenSSL"
            "C:/OpenSSL"
            "C:/OpenSSL-Win64"
        )
        foreach(PATH ${SYSTEM_PATHS})
            if(EXISTS "${PATH}/include/openssl/ssl.h")
                set(OPENSSL_ROOT_DIR "${PATH}" CACHE PATH "OpenSSL root directory")
                set(OPENSSL_FOUND TRUE)
                message(STATUS "Found OpenSSL in system path: ${OPENSSL_ROOT_DIR}")
                break()
            endif()
        endforeach()
    endif()
    
    if(OPENSSL_FOUND)
        # Set the required CMake variables
        set(OPENSSL_INCLUDE_DIR "${OPENSSL_ROOT_DIR}/include" CACHE PATH "OpenSSL include directory")
        set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_ROOT_DIR}/lib/libcrypto.a" CACHE PATH "OpenSSL crypto library")
        set(OPENSSL_SSL_LIBRARY "${OPENSSL_ROOT_DIR}/lib/libssl.a" CACHE PATH "OpenSSL SSL library")
        
        # Also try .dll files if .a files don't exist
        if(NOT EXISTS "${OPENSSL_CRYPTO_LIBRARY}")
            set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_ROOT_DIR}/lib/libcrypto.dll.a" CACHE PATH "OpenSSL crypto library")
        endif()
        if(NOT EXISTS "${OPENSSL_SSL_LIBRARY}")
            set(OPENSSL_SSL_LIBRARY "${OPENSSL_ROOT_DIR}/lib/libssl.dll.a" CACHE PATH "OpenSSL SSL library")
        endif()
        
        message(STATUS "OpenSSL libraries: ${OPENSSL_CRYPTO_LIBRARY}, ${OPENSSL_SSL_LIBRARY}")
    else()
        message(FATAL_ERROR "OpenSSL not found. Please install OpenSSL via vcpkg, MSYS2, or Strawberry Perl")
    endif()
endfunction()

# Function to find Boost on Windows
function(find_portable_boost)
    # Try bundled boost first
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/boost-1.83.0")
        set(BOOST_INCLUDEDIR "${CMAKE_CURRENT_SOURCE_DIR}/boost-1.83.0" CACHE PATH "Boost include directory")
        set(BOOST_LIBRARYDIR "${CMAKE_CURRENT_SOURCE_DIR}/boost-1.83.0/stage/lib" CACHE PATH "Boost library directory")
        message(STATUS "Using bundled Boost: ${BOOST_INCLUDEDIR}")
        return()
    endif()
    
    # Try vcpkg
    if(DEFINED ENV{VCPKG_ROOT})
        set(BOOST_INCLUDEDIR "$ENV{VCPKG_ROOT}/installed/x64-windows/include" CACHE PATH "Boost include directory")
        set(BOOST_LIBRARYDIR "$ENV{VCPKG_ROOT}/installed/x64-windows/lib" CACHE PATH "Boost library directory")
        message(STATUS "Found Boost via vcpkg: ${BOOST_INCLUDEDIR}")
        return()
    endif()
    
    # Try MSYS2
    if(EXISTS "C:/msys64/mingw64")
        set(BOOST_INCLUDEDIR "C:/msys64/mingw64/include" CACHE PATH "Boost include directory")
        set(BOOST_LIBRARYDIR "C:/msys64/mingw64/lib" CACHE PATH "Boost library directory")
        message(STATUS "Found Boost via MSYS2: ${BOOST_INCLUDEDIR}")
        return()
    endif()
    
    # Try system paths
    find_path(BOOST_ROOT
        NAMES boost/version.hpp
        PATHS
            "C:/local/boost_*"
            "C:/boost"
            "C:/Program Files/boost"
            "C:/Program Files (x86)/boost"
        DOC "Boost root directory"
    )
    
    if(BOOST_ROOT)
        set(BOOST_INCLUDEDIR "${BOOST_ROOT}" CACHE PATH "Boost include directory")
        set(BOOST_LIBRARYDIR "${BOOST_ROOT}/lib" CACHE PATH "Boost library directory")
        message(STATUS "Found Boost: ${BOOST_ROOT}")
    else()
        message(FATAL_ERROR "Boost not found. Please install Boost via vcpkg, MSYS2, or bundle it with the source")
    endif()
endfunction()

# Function to find libevent on Windows
function(find_portable_libevent)
    # Try bundled libevent first
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libevent-install")
        set(CMAKE_INCLUDE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/libevent-install/include" ${CMAKE_INCLUDE_PATH})
        set(CMAKE_LIBRARY_PATH "${CMAKE_CURRENT_SOURCE_DIR}/libevent-install/lib" ${CMAKE_LIBRARY_PATH})
        message(STATUS "Using bundled libevent: ${CMAKE_CURRENT_SOURCE_DIR}/libevent-install")
        return()
    endif()
    
    # Try vcpkg
    if(DEFINED ENV{VCPKG_ROOT})
        set(CMAKE_INCLUDE_PATH "$ENV{VCPKG_ROOT}/installed/x64-windows/include" ${CMAKE_INCLUDE_PATH})
        set(CMAKE_LIBRARY_PATH "$ENV{VCPKG_ROOT}/installed/x64-windows/lib" ${CMAKE_LIBRARY_PATH})
        message(STATUS "Found libevent via vcpkg")
        return()
    endif()
    
    # Try MSYS2
    if(EXISTS "C:/msys64/mingw64")
        set(CMAKE_INCLUDE_PATH "C:/msys64/mingw64/include" ${CMAKE_INCLUDE_PATH})
        set(CMAKE_LIBRARY_PATH "C:/msys64/mingw64/lib" ${CMAKE_LIBRARY_PATH})
        message(STATUS "Found libevent via MSYS2")
        return()
    endif()
    
    message(STATUS "libevent not found, using bundled stubs")
endfunction()

# Apply portable configuration
find_portable_openssl()
find_portable_boost()
find_portable_libevent()

# Apply portable configuration for Windows
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    # Use static linking for better portability
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".lib")
    
    # Add common Windows flags
    add_compile_definitions(
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0601  # Windows 7
    )
    
    # Set Windows SDK paths for MinGW
    if(MINGW)
        # Add MinGW include paths
        set(CMAKE_SYSTEM_INCLUDE_PATH 
            "C:/msys64/mingw64/include"
            "C:/Strawberry/c/x86_64-w64-mingw32/include"
            ${CMAKE_SYSTEM_INCLUDE_PATH}
        )
        
        # Add MinGW library paths
        set(CMAKE_SYSTEM_LIBRARY_PATH
            "C:/msys64/mingw64/lib"
            "C:/Strawberry/c/x86_64-w64-mingw32/lib"
            ${CMAKE_SYSTEM_LIBRARY_PATH}
        )
        
        # Find shlwapi.h in MinGW paths
        find_path(SHLWAPI_INCLUDE_DIR 
            NAMES shlwapi.h
            PATHS 
                "C:/msys64/mingw64/include"
                "C:/Strawberry/c/x86_64-w64-mingw32/include"
                "C:/msys64/ucrt64/include"
        )
        
        if(SHLWAPI_INCLUDE_DIR)
            set(SHLWAPI_INCLUDE_DIRS "${SHLWAPI_INCLUDE_DIR}")
            message(STATUS "Found shlwapi.h at: ${SHLWAPI_INCLUDE_DIR}")
        endif()
        
        # Find Windows libraries
        find_library(WS2_32_LIBRARY
            NAMES ws2_32
            PATHS
                "C:/msys64/mingw64/lib"
                "C:/Strawberry/c/x86_64-w64-mingw32/lib"
                "C:/msys64/mingw64/lib/gcc/x86_64-w64-mingw32/13.2.0"
        )
        
        if(WS2_32_LIBRARY)
            message(STATUS "Found ws2_32 library: ${WS2_32_LIBRARY}")
        endif()
        
        add_compile_options(-Wno-unused-parameter)
        add_link_options(-static-libgcc -static-libstdc++)
    endif()
    
    # If using MSVC
    if(MSVC)
        add_compile_options(/W3 /MP)
        add_link_options(/INCREMENTAL:NO)
    endif()
endif()

message(STATUS "Portable Windows configuration applied")
