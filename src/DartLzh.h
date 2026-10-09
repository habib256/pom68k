// SPDX-License-Identifier: Apache-2.0
// Adaptive Huffman tree adapted from CiderPress2 LZHufStream.cs.
// Copyright 2025 faddenSoft; modifications Copyright 2026 Arnaud Verhille.
// See extern/ciderpress2/LICENSE and POM68K_VENDOR.md for provenance.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

// DART variant: no length prefix, zero-filled 4096-byte window, maximum
// match 60 bytes. Each independently coded chunk expands to 20960 bytes.
class DartLzh {
    static constexpr int kSymbols = 314, kNodes = 627, kRoot = 626;
    std::array<unsigned, kNodes + 1> frequency_{};
    std::array<int, kNodes + kSymbols> parent_{};
    std::array<int, kNodes> child_{};
    std::span<const uint8_t> input_;
    size_t bit_ = 0;

    int bit() {
        if (bit_ / 8 >= input_.size()) return -1;
        const int value = (input_[bit_ / 8] >> (7 - bit_ % 8)) & 1;
        ++bit_;
        return value;
    }
    void update(int symbol) {
        // At most 20960 symbols per chunk: root frequency remains below
        // 0x8000, so LZHUF's frequency-halving reconstruction is unreachable.
        int node = parent_[symbol + kNodes];
        do {
            const unsigned weight = ++frequency_[node];
            int other = node + 1;
            if (weight > frequency_[other]) {
                while (weight > frequency_[++other]) {}
                --other;
                frequency_[node] = frequency_[other];
                frequency_[other] = weight;
                const int a = child_[node], b = child_[other];
                parent_[a] = other;
                if (a < kNodes) parent_[a + 1] = other;
                parent_[b] = node;
                if (b < kNodes) parent_[b + 1] = node;
                child_[other] = a;
                child_[node] = b;
                node = other;
            }
        } while ((node = parent_[node]) != 0);
    }
    int symbol() {
        int node = child_[kRoot];
        while (node < kNodes) {
            const int value = bit();
            if (value < 0) return -1;
            node = child_[node + value];
        }
        node -= kNodes;
        update(node);
        return node;
    }
    int distance() {
        // Canonical Huffman code for the upper six distance bits.
        constexpr int counts[] = {0, 0, 0, 1, 3, 8, 12, 24, 16};
        int code = 0, first = 0, base = 0;
        for (int length = 1; length <= 8; ++length) {
            const int value = bit();
            if (value < 0) return -1;
            code = code * 2 + value;
            if (code >= first && code < first + counts[length]) {
                int low = 0;
                for (int i = 0; i < 6; ++i) {
                    const int next = bit();
                    if (next < 0) return -1;
                    low = low * 2 + next;
                }
                return (base + code - first) * 64 + low;
            }
            first = (first + counts[length]) * 2;
            base += counts[length];
        }
        return -1;
    }
public:
    explicit DartLzh(std::span<const uint8_t> input) : input_(input) {
        for (int i = 0; i < kSymbols; ++i) {
            frequency_[i] = 1;
            child_[i] = i + kNodes;
            parent_[i + kNodes] = i;
        }
        for (int i = 0, j = kSymbols; j <= kRoot; i += 2, ++j) {
            frequency_[j] = frequency_[i] + frequency_[i + 1];
            child_[j] = i;
            parent_[i] = parent_[i + 1] = j;
        }
        frequency_[kNodes] = 0xffff;
    }
    bool expand(std::span<uint8_t> output) {
        if (output.size() != 20960) return false;
        std::array<uint8_t, 4096> window{};
        size_t produced = 0, head = 4096 - 60;
        while (produced < output.size()) {
            const int value = symbol();
            if (value < 0) return false;
            if (value < 256) {
                output[produced++] = window[head] = uint8_t(value);
                head = (head + 1) & 4095;
            } else {
                const int offset = distance();
                if (offset < 0) return false;
                const size_t length = size_t(value - 253);
                // Some historical encoders finish with a match crossing the
                // fixed chunk boundary; consume only the expected output.
                size_t source = (head - size_t(offset) - 1) & 4095;
                for (size_t i = 0; i < length && produced < output.size(); ++i) {
                    const uint8_t byte = window[source];
                    source = (source + 1) & 4095;
                    output[produced++] = window[head] = byte;
                    head = (head + 1) & 4095;
                }
            }
        }
        return true;
    }
};
