/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "Med17Correction.h"

#include "Med17Keys.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace Checksum::MED17 {
namespace {

// GMP is only used by the DLL for fixed-width unsigned arithmetic.  Keeping
// the implementation here avoids another runtime dependency while preserving
// the exact 1024-bit byte ordering of MED17's correction worker.
constexpr size_t kWordCount = 32;
constexpr size_t kWideWordCount = kWordCount * 3;
constexpr size_t kWorkerWordCount = 0x17;
constexpr uint32_t kWorkerStride = 0x30000;
constexpr uint32_t kCandidatesPerBlock = 0x10001;
using Words = std::array<uint32_t, kWordCount>;
using WideWords = std::array<uint32_t, kWideWordCount>;

uint32_t readLe32(const uint8_t* bytes)
{
    return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8)
        | (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
}

Words importBigEndian(const std::array<uint8_t, 128>& bytes)
{
    Words result{};
    for (size_t index = 0; index < bytes.size(); ++index)
        result[(bytes.size() - 1 - index) / 4] |= uint32_t(bytes[index])
            << (((bytes.size() - 1 - index) % 4) * 8);
    return result;
}

std::array<uint8_t, 128> exportBigEndian(const Words& words)
{
    std::array<uint8_t, 128> result{};
    for (size_t index = 0; index < result.size(); ++index)
        result[index] = static_cast<uint8_t>(words[(result.size() - 1 - index) / 4]
            >> (((result.size() - 1 - index) % 4) * 8));
    return result;
}

WideWords cube(const Words& value)
{
    std::array<uint32_t, kWordCount * 2> squared{};
    for (size_t left = 0; left < kWordCount; ++left) {
        uint64_t carry = 0;
        for (size_t right = 0; right < kWordCount; ++right) {
            const uint64_t total = uint64_t(squared[left + right])
                + uint64_t(value[left]) * value[right] + carry;
            squared[left + right] = static_cast<uint32_t>(total);
            carry = total >> 32;
        }
        for (size_t position = left + kWordCount; carry != 0 && position < squared.size(); ++position) {
            const uint64_t total = uint64_t(squared[position]) + carry;
            squared[position] = static_cast<uint32_t>(total);
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
        for (size_t position = left + kWordCount; carry != 0 && position < result.size(); ++position) {
            const uint64_t total = uint64_t(result[position]) + carry;
            result[position] = static_cast<uint32_t>(total);
            carry = total >> 32;
        }
    }
    return result;
}

int compareWithTemplate(const WideWords& lhs, const Words& rhs)
{
    for (size_t index = lhs.size(); index-- > kWordCount;) {
        if (lhs[index] != 0)
            return 1;
    }
    for (size_t index = kWordCount; index-- > 0;) {
        if (lhs[index] < rhs[index])
            return -1;
        if (lhs[index] > rhs[index])
            return 1;
    }
    return 0;
}

void setBit(Words& value, unsigned int bit)
{
    value[bit / 32] |= uint32_t(1) << (bit % 32);
}

void increment(Words& value)
{
    for (uint32_t& word : value) {
        ++word;
        if (word != 0)
            return;
    }
}

Words ceilingCubeRoot(const Words& templateValue)
{
    // MED17 calls mpz_root(template, 3), then unconditionally adds one.
    // The highest possible root bit for a 1024-bit template is bit 341.
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

Words squareTimesThree(const Words& root)
{
    std::array<uint32_t, kWordCount * 2> square{};
    for (size_t left = 0; left < kWordCount; ++left) {
        uint64_t carry = 0;
        for (size_t right = 0; right < kWordCount; ++right) {
            const uint64_t total = uint64_t(square[left + right])
                + uint64_t(root[left]) * root[right] + carry;
            square[left + right] = static_cast<uint32_t>(total);
            carry = total >> 32;
        }
        for (size_t position = left + kWordCount; carry != 0 && position < square.size(); ++position) {
            const uint64_t total = uint64_t(square[position]) + carry;
            square[position] = static_cast<uint32_t>(total);
            carry = total >> 32;
        }
    }

    Words result{};
    uint64_t carry = 0;
    for (size_t index = 0; index < result.size(); ++index) {
        const uint64_t value = uint64_t(square[index]) * 3 + carry;
        result[index] = static_cast<uint32_t>(value);
        carry = value >> 32;
    }
    return result;
}

Words multiplySmall(const Words& value, uint32_t multiplier)
{
    Words result{};
    uint64_t carry = 0;
    for (size_t index = 0; index < value.size(); ++index) {
        const uint64_t product = uint64_t(value[index]) * multiplier + carry;
        result[index] = static_cast<uint32_t>(product);
        carry = product >> 32;
    }
    return result;
}

void addWords(Words& sum, const Words& incrementValue, size_t wordCount)
{
    uint64_t carry = 0;
    for (size_t index = 0; index < wordCount; ++index) {
        const uint64_t total = uint64_t(sum[index]) + incrementValue[index] + carry;
        sum[index] = static_cast<uint32_t>(total);
        carry = total >> 32;
    }
}

std::array<uint8_t, 128> exportWords(const Words& words)
{
    return exportBigEndian(words);
}

void writeWorkerWords(std::array<uint8_t, 128>& candidate, const Words& sum)
{
    // SearchForgedSignatureWorker serializes words 22 down to 14 as big
    // endian bytes at offsets 0x24..0x47.  Other bytes remain from root^3.
    size_t offset = 0x24;
    for (size_t word = 23; word-- > 14;) {
        const uint32_t value = sum[word];
        candidate[offset++] = static_cast<uint8_t>(value >> 24);
        candidate[offset++] = static_cast<uint8_t>(value >> 16);
        candidate[offset++] = static_cast<uint8_t>(value >> 8);
        candidate[offset++] = static_cast<uint8_t>(value);
    }
}

const std::array<uint32_t, 256>& reflectedCrcTable()
{
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> result{};
        for (uint32_t index = 0; index < result.size(); ++index) {
            uint32_t value = index;
            for (int bit = 0; bit < 8; ++bit)
                value = (value & 1u) ? ((value >> 1) ^ 0xedb88320u) : (value >> 1);
            result[index] = value;
        }
        return result;
    }();
    return table;
}

uint32_t initializeTailCrc(const std::array<uint8_t, 128>& candidate)
{
    uint32_t crc = 0xffffffffu;
    const auto& table = reflectedCrcTable();
    for (size_t index = 0x0b; index <= 0x25; ++index)
        crc = (crc >> 8) ^ table[(crc ^ candidate[index]) & 0xffu];
    return crc;
}

bool matchesTailCrc(std::array<uint8_t, 128>& candidate, uint32_t initialCrc)
{
    uint32_t crc = initialCrc;
    const auto& table = reflectedCrcTable();
    for (size_t index = 0x26; index <= 0x3f; ++index)
        crc = (crc >> 8) ^ table[(crc ^ candidate[index]) & 0xffu];
    candidate[0x43] &= 0xfcu;
    return readLe32(candidate.data() + 0x43) == ((~crc) & 0xfffffffcu);
}

std::array<uint8_t, 128> makeTemplate(const QByteArray& rom,
                                      const Descriptor& descriptor,
                                      const RsaSignatureResult& existingSignature)
{
    std::array<uint8_t, 128> result{};
    result[0] = 0x00;
    result[1] = 0x01;
    std::fill(result.begin() + 2, result.begin() + 10, 0xff);

    const std::array<uint8_t, 20> sha1 = calculateDescriptorSha1(
        reinterpret_cast<const uint8_t*>(rom.constData()), descriptor.crcStart,
        descriptor.crcEndInclusive);
    std::copy(sha1.cbegin(), sha1.cend(), result.begin() + 11);

    // VerifyRsaSignatureBlock records decoded bytes 11 and 12. MED17 copies
    // those values to template bytes 31 and 32 before its tail search.
    result[31] = existingSignature.metadataByte0;
    result[32] = existingSignature.metadataByte1;
    result[35] = 0xbf;
    return result;
}

} // namespace

CorrectionResult forgeCorrectedSignature(const QByteArray& rom,
                                         const Descriptor& descriptor,
                                         const RsaSignatureResult& existingSignature,
                                         CorrectionLimits limits)
{
    CorrectionResult result;
    if (descriptor.signatureKeyIndex > 0x8b || descriptor.crcStart > descriptor.crcEndInclusive
        || descriptor.signatureOffset > static_cast<uint32_t>(rom.size())
        || rom.size() - static_cast<qsizetype>(descriptor.signatureOffset) < 128
        || descriptor.crcEndInclusive >= static_cast<uint32_t>(rom.size())
        || existingSignature.status != RsaSignatureStatus::Valid) {
        return result;
    }

    const std::array<uint8_t, 128> signatureTemplate = makeTemplate(rom, descriptor, existingSignature);
    const Words templateValue = importBigEndian(signatureTemplate);
    const Words root = ceilingCubeRoot(templateValue);
    const WideWords rootCubeWide = cube(root);
    Words rootCube{};
    std::copy_n(rootCubeWide.begin(), rootCube.size(), rootCube.begin());
    const Words threeRootSquared = squareTimesThree(root);
    const Words blockStride = multiplySmall(threeRootSquared, kWorkerStride);
    const std::array<uint8_t, 128> initialCandidate = exportWords(rootCube);
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
    for (auto& worker : workers)
        worker.join();
    result.attempts = attempts.load(std::memory_order_relaxed);

    if (timedOut.load(std::memory_order_relaxed)) {
        result.status = CorrectionStatus::TimedOut;
        return result;
    }
    if (attemptLimitReached.load(std::memory_order_relaxed)) {
        result.status = CorrectionStatus::AttemptLimitReached;
        return result;
    }
    if (!stop.load(std::memory_order_relaxed)) {
        result.status = CorrectionStatus::TimedOut;
        return result;
    }

    std::array<uint8_t, 128> finalTemplate = signatureTemplate;
    for (size_t offset : {size_t(31), size_t(35), size_t(39), size_t(43)})
        finalTemplate[offset] = winningCandidate[offset];
    const std::array<uint8_t, 128> signatureBytes = exportWords(
        ceilingCubeRoot(importBigEndian(finalTemplate)));
    result.signature = QByteArray(reinterpret_cast<const char*>(signatureBytes.data()),
                                  static_cast<qsizetype>(signatureBytes.size()));

    const auto key = publicKeyForIndex(descriptor.signatureKeyIndex);
    if (!key) {
        result.status = CorrectionStatus::InvalidDescriptor;
        result.signature.clear();
        return result;
    }
    const RsaSignatureResult verified = verifyRsaSignatureBlock(result.signature, *key);
    const std::array<uint8_t, 20> expectedSha1 = calculateDescriptorSha1(
        reinterpret_cast<const uint8_t*>(rom.constData()), descriptor.crcStart,
        descriptor.crcEndInclusive);
    if (verified.status != RsaSignatureStatus::Valid
        || !std::equal(expectedSha1.cbegin(), expectedSha1.cend(), verified.decodedBlock.cbegin() + 11)) {
        result.status = CorrectionStatus::VerificationFailed;
        result.signature.clear();
        return result;
    }

    result.status = CorrectionStatus::Corrected;
    return result;
}

} // namespace Checksum::MED17
