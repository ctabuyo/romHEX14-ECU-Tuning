/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "BleichenbacherForge.h"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace Checksum::Common {

namespace {

constexpr size_t kWordCount = 32;
constexpr size_t kWideWordCount = kWordCount * 3;
constexpr size_t kWorkerWordCount = 0x17;
constexpr uint32_t kWorkerStride = 0x30000;
constexpr uint32_t kCandidatesPerBlock = 0x10001;

using Words = std::array<uint32_t, kWordCount>;
using WideWords = std::array<uint32_t, kWideWordCount>;

uint32_t readLe32(const uint8_t* bytes) {
    return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8)
        | (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
}

WideWords cube(const Words& value) {
    std::array<uint32_t, kWordCount * 2> squared{};
    for (size_t left = 0; left < kWordCount; ++left) {
        uint64_t carry = 0;
        for (size_t right = 0; right < kWordCount; ++right) {
            const uint64_t total = uint64_t(squared[left + right])
                + uint64_t(value[left]) * value[right] + carry;
            squared[left + right] = static_cast<uint32_t>(total);
            carry = total >> 32;
        }
        for (size_t pos = left + kWordCount; carry != 0 && pos < squared.size(); ++pos) {
            const uint64_t total = uint64_t(squared[pos]) + carry;
            squared[pos] = static_cast<uint32_t>(total);
            carry = total >> 32;
        }
    }

    WideWords result{};
    for (size_t left = 0; left < squared.size(); ++left) {
        uint64_t carry = 0;
        for (size_t right = 0; right < kWordCount && left + right < result.size(); ++right) {
            const uint64_t total = uint64_t(result[left + right])
                + uint64_t(squared[left]) * value[right] + carry;
            result[left + right] = static_cast<uint32_t>(total);
            carry = total >> 32;
        }
        for (size_t pos = left + kWordCount; carry != 0 && pos < result.size(); ++pos) {
            const uint64_t total = uint64_t(result[pos]) + carry;
            result[pos] = static_cast<uint32_t>(total);
            carry = total >> 32;
        }
    }
    return result;
}

int compareWithTemplate(const WideWords& lhs, const Words& rhs) {
    for (size_t i = lhs.size(); i-- > kWordCount;) {
        if (lhs[i] != 0) return 1;
    }
    for (size_t i = kWordCount; i-- > 0;) {
        if (lhs[i] < rhs[i]) return -1;
        if (lhs[i] > rhs[i]) return 1;
    }
    return 0;
}

void setBit(Words& value, unsigned int bit) {
    value[bit / 32] |= uint32_t(1) << (bit % 32);
}

void increment(Words& value) {
    for (uint32_t& w : value) {
        ++w;
        if (w != 0) return;
    }
}

Words ceilingCubeRoot(const Words& templateValue) {
    Words root{};
    for (int bit = 341; bit >= 0; --bit) {
        Words candidate = root;
        setBit(candidate, static_cast<unsigned int>(bit));
        if (compareWithTemplate(cube(candidate), templateValue) <= 0)
            root = candidate;
    }
    increment(root);
    return root;
}

Words squareTimesThree(const Words& root) {
    Words squaredTimesThree{};
    uint64_t carry = 0;
    for (size_t left = 0; left < kWordCount; ++left) {
        for (size_t right = 0; left + right < kWordCount; ++right) {
            const uint64_t product = uint64_t(root[left]) * root[right] * (left == right ? 3u : 6u);
            const uint64_t total = uint64_t(squaredTimesThree[left + right]) + product + carry;
            squaredTimesThree[left + right] = static_cast<uint32_t>(total);
            carry = total >> 32;
        }
    }
    return squaredTimesThree;
}

Words multiplySmall(const Words& lhs, uint32_t rhs) {
    Words result{};
    uint64_t carry = 0;
    for (size_t i = 0; i < kWordCount; ++i) {
        const uint64_t total = uint64_t(lhs[i]) * rhs + carry;
        result[i] = static_cast<uint32_t>(total);
        carry = total >> 32;
    }
    return result;
}

void addWords(Words& lhs, const Words& rhs, size_t wordCount) {
    uint64_t carry = 0;
    for (size_t i = 0; i < wordCount; ++i) {
        const uint64_t total = uint64_t(lhs[i]) + rhs[i] + carry;
        lhs[i] = static_cast<uint32_t>(total);
        carry = total >> 32;
    }
}

const std::array<uint32_t, 256>& reflectedCrcTable() {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> res{};
        for (uint32_t index = 0; index < res.size(); ++index) {
            uint32_t val = index;
            for (int bit = 0; bit < 8; ++bit)
                val = (val & 1u) ? ((val >> 1) ^ 0xedb88320u) : (val >> 1);
            res[index] = val;
        }
        return res;
    }();
    return table;
}

uint32_t initializeTailCrc(const std::array<uint8_t, 128>& candidate) {
    uint32_t crc = 0xffffffffu;
    const auto& table = reflectedCrcTable();
    for (size_t index = 0x0b; index <= 0x25; ++index)
        crc = (crc >> 8) ^ table[(crc ^ candidate[index]) & 0xffu];
    return crc;
}

bool matchesTailCrc(std::array<uint8_t, 128>& candidate, uint32_t initialCrc) {
    uint32_t crc = initialCrc;
    const auto& table = reflectedCrcTable();
    for (size_t index = 0x26; index <= 0x3f; ++index)
        crc = (crc >> 8) ^ table[(crc ^ candidate[index]) & 0xffu];
    candidate[0x43] &= 0xfcu;
    return readLe32(candidate.data() + 0x43) == ((~crc) & 0xfffffffcu);
}

void writeWorkerWords(std::array<uint8_t, 128>& candidate, const Words& sum) {
    size_t offset = 0x24;
    for (size_t word = 23; word-- > 14;) {
        const uint32_t val = sum[word];
        candidate[offset++] = static_cast<uint8_t>(val >> 24);
        candidate[offset++] = static_cast<uint8_t>(val >> 16);
        candidate[offset++] = static_cast<uint8_t>(val >> 8);
        candidate[offset++] = static_cast<uint8_t>(val);
    }
}

} // namespace

ForgeResult BleichenbacherForge::forgeSignature(
    const std::array<uint8_t, 128>& targetTemplate,
    ForgeLimits limits)
{
    ForgeResult result;
    const Words templateValue = RsaMath1024::importBigEndian(targetTemplate.data());
    const Words root = ceilingCubeRoot(templateValue);
    const WideWords rootCubeWide = cube(root);
    Words rootCube{};
    std::copy_n(rootCubeWide.begin(), rootCube.size(), rootCube.begin());
    const Words threeRootSquared = squareTimesThree(root);
    const Words blockStride = multiplySmall(threeRootSquared, kWorkerStride);
    const std::array<uint8_t, 128> initialCandidate = RsaMath1024::exportBigEndian(rootCube);
    const uint32_t initialTailCrc = initializeTailCrc(initialCandidate);

    const unsigned int requestedWorkers = limits.workerCount == 0
        ? std::thread::hardware_concurrency() : limits.workerCount;
    const unsigned int workerCount = std::clamp(requestedWorkers == 0 ? 1u : requestedWorkers, 1u, 16u);
    const auto deadline = std::chrono::steady_clock::now() + limits.timeout;
    std::atomic<bool> stop{false};
    std::atomic<bool> timedOut{false};
    std::atomic<bool> attemptLimitReached{false};
    std::atomic<uint64_t> blockCounter{0};
    std::atomic<uint64_t> attempts{0};
    std::array<uint8_t, 128> winningCandidate{};
    std::mutex winningCandidateMutex;
    std::vector<std::thread> workers;
    workers.reserve(workerCount);

    const bool enforceAttemptLimit = limits.maximumAttempts != 0;
    for (unsigned int worker = 0; worker < workerCount; ++worker) {
        workers.emplace_back([&] {
            uint64_t localBlock = 0;
            uint64_t localAttempts = 0;
            Words sum = rootCube;
            std::array<uint8_t, 128> candidate = initialCandidate;
            while (!stop.load(std::memory_order_relaxed)) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    timedOut.store(true, std::memory_order_relaxed);
                    stop.store(true, std::memory_order_relaxed);
                    break;
                }

                const uint64_t claimedBlock = blockCounter.fetch_add(1, std::memory_order_relaxed) + 1;
                while (localBlock < claimedBlock) {
                    ++localBlock;
                    if (localBlock > 1)
                        addWords(sum, blockStride, kWorkerWordCount);
                }

                for (uint32_t candidateIndex = 0; candidateIndex < kCandidatesPerBlock; ++candidateIndex) {
                    ++localAttempts;
                    if (enforceAttemptLimit && (localAttempts & 0xfffu) == 0) {
                        const uint64_t total = attempts.load(std::memory_order_relaxed) + localAttempts;
                        if (total >= limits.maximumAttempts) {
                            attemptLimitReached.store(true, std::memory_order_relaxed);
                            stop.store(true, std::memory_order_relaxed);
                            break;
                        }
                    }
                    if (stop.load(std::memory_order_relaxed))
                        break;
                    if (matchesTailCrc(candidate, initialTailCrc)) {
                        bool expected = false;
                        if (stop.compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
                            std::lock_guard lock(winningCandidateMutex);
                            winningCandidate = candidate;
                        }
                        break;
                    }
                    addWords(sum, threeRootSquared, kWorkerWordCount);
                    writeWorkerWords(candidate, sum);
                }
            }
            attempts.fetch_add(localAttempts, std::memory_order_relaxed);
        });
    }

    for (auto& worker : workers) {
        if (worker.joinable())
            worker.join();
    }

    result.attempts = attempts.load(std::memory_order_relaxed);
    if (stop.load(std::memory_order_relaxed) && !timedOut.load(std::memory_order_relaxed)
        && !attemptLimitReached.load(std::memory_order_relaxed)) {
        result.status = ForgeStatus::Corrected;
        result.signature = winningCandidate;
    } else if (timedOut.load(std::memory_order_relaxed)) {
        result.status = ForgeStatus::TimedOut;
    } else if (attemptLimitReached.load(std::memory_order_relaxed)) {
        result.status = ForgeStatus::AttemptLimitReached;
    } else {
        result.status = ForgeStatus::Uncorrected;
    }
    return result;
}

} // namespace Checksum::Common
