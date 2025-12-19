// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2022-2026 The Radiant developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <support/cleanse.h>

#include <memory>
#include <vector>

template <typename T>
struct zero_after_free_allocator {
    using value_type = T;

    zero_after_free_allocator() noexcept {}
    zero_after_free_allocator(const zero_after_free_allocator &a) noexcept {}
    template <typename U>
    zero_after_free_allocator(const zero_after_free_allocator<U> &a) noexcept {}
    ~zero_after_free_allocator() noexcept {}

    template <typename _Other> struct rebind {
        typedef zero_after_free_allocator<_Other> other;
    };

    T *allocate(std::size_t n) {
        return std::allocator<T>().allocate(n);
    }

    void deallocate(T *p, std::size_t n) {
        if (p != nullptr) memory_cleanse(p, sizeof(T) * n);
        std::allocator<T>().deallocate(p, n);
    }

    friend bool operator==(const zero_after_free_allocator &, const zero_after_free_allocator &) noexcept {
        return true;
    }
    friend bool operator!=(const zero_after_free_allocator &, const zero_after_free_allocator &) noexcept {
        return false;
    }
};

// Byte-vector that clears its contents before deletion.
typedef std::vector<char, zero_after_free_allocator<char>> CSerializeData;
