/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "RsaMath1024.h"
#include <algorithm>
#include <cstring>

namespace Checksum::Common {

namespace {

using Remainder = std::array<uint32_t, RsaMath1024::kModulusWords + 1>;

bool isZero(const RsaMath1024::Words1024& value) {
    return std::all_of(value.begin(), value.end(), [](uint32_t w) { return w == 0; });
}

int compare(const Remainder& lhs, const RsaMath1024::Words1024& rhs) {
    if (lhs.back() != 0)
        return 1;
    for (size_t i = RsaMath1024::kModulusWords; i-- > 0;) {
        if (lhs[i] < rhs[i])
            return -1;
        if (lhs[i] > rhs[i])
            return 1;
    }
    return 0;
}

void subtract(Remainder& lhs, const RsaMath1024::Words1024& rhs) {
    uint64_t borrow = 0;
    for (size_t i = 0; i < RsaMath1024::kModulusWords; ++i) {
        const uint64_t subtrahend = uint64_t(rhs[i]) + borrow;
        const uint64_t current = lhs[i];
        lhs[i] = static_cast<uint32_t>(current - subtrahend);
        borrow = current < subtrahend ? 1 : 0;
    }
    lhs[RsaMath1024::kModulusWords] = static_cast<uint32_t>(lhs[RsaMath1024::kModulusWords] - borrow);
}

void shiftLeftOne(Remainder& value) {
    uint32_t carry = 0;
    for (uint32_t& word : value) {
        const uint32_t nextCarry = word >> 31;
        word = (word << 1) | carry;
        carry = nextCarry;
    }
}

RsaMath1024::Words1024 reduce(const uint32_t* numerator, size_t wordCount, const RsaMath1024::Words1024& modulus) {
    Remainder remainder{};
    for (size_t wordIndex = wordCount; wordIndex-- > 0;) {
        const uint32_t word = numerator[wordIndex];
        for (int bit = 31; bit >= 0; --bit) {
            shiftLeftOne(remainder);
            remainder[0] |= (word >> bit) & 1u;
            if (compare(remainder, modulus) >= 0)
                subtract(remainder, modulus);
        }
    }

    RsaMath1024::Words1024 result{};
    std::copy_n(remainder.begin(), RsaMath1024::kModulusWords, result.begin());
    return result;
}

RsaMath1024::Words1024 multiplyMod(const RsaMath1024::Words1024& lhs, const RsaMath1024::Words1024& rhs, const RsaMath1024::Words1024& modulus) {
    RsaMath1024::Words2048 product{};
    for (size_t i = 0; i < RsaMath1024::kModulusWords; ++i) {
        uint64_t carry = 0;
        for (size_t j = 0; j < RsaMath1024::kModulusWords; ++j) {
            const uint64_t accumulated = uint64_t(product[i + j]) + uint64_t(lhs[i]) * rhs[j] + carry;
            product[i + j] = static_cast<uint32_t>(accumulated);
            carry = accumulated >> 32;
        }
        for (size_t k = i + RsaMath1024::kModulusWords; carry != 0 && k < product.size(); ++k) {
            const uint64_t accumulated = uint64_t(product[k]) + carry;
            product[k] = static_cast<uint32_t>(accumulated);
            carry = accumulated >> 32;
        }
    }
    return reduce(product.data(), product.size(), modulus);
}

} // namespace

RsaMath1024::Words1024 RsaMath1024::importBigEndian(const uint8_t* bytes) {
    Words1024 result{};
    if (!bytes) return result;
    for (size_t i = 0; i < 128; ++i)
        result[(127 - i) / 4] |= uint32_t(bytes[i]) << (((127 - i) % 4) * 8);
    return result;
}

RsaMath1024::Words1024 RsaMath1024::importLittleEndian(const uint8_t* bytes) {
    Words1024 result{};
    if (!bytes) return result;
    for (size_t i = 0; i < 128; ++i)
        result[i / 4] |= uint32_t(bytes[i]) << ((i % 4) * 8);
    return result;
}

std::array<uint8_t, 128> RsaMath1024::exportBigEndian(const Words1024& value) {
    std::array<uint8_t, 128> result{};
    for (size_t i = 0; i < result.size(); ++i)
        result[i] = static_cast<uint8_t>(value[(127 - i) / 4] >> (((127 - i) % 4) * 8));
    return result;
}

RsaMath1024::Words1024 RsaMath1024::modCube(const Words1024& base, const Words1024& modulus) {
    return multiplyMod(multiplyMod(base, base, modulus), base, modulus);
}

RsaVerifyResult RsaMath1024::verifySignature(
    const uint8_t* signatureBytes,
    const uint8_t* modulusLittleEndian)
{
    RsaVerifyResult result;
    if (!signatureBytes || !modulusLittleEndian)
        return result;

    const Words1024 modulus = importLittleEndian(modulusLittleEndian);
    if (isZero(modulus)) {
        result.status = RsaVerifyStatus::InvalidModulus;
        return result;
    }

    const Words1024 signature = reduce(importBigEndian(signatureBytes).data(), kModulusWords, modulus);
    const Words1024 decoded = modCube(signature, modulus);
    result.decodedBlock = exportBigEndian(decoded);
    result.status = RsaVerifyStatus::InvalidPadding;

    // Standard PKCS#1 padding verification: 00 01 FF FF FF FF FF FF FF FF 00
    if (result.decodedBlock[0] != 0 || result.decodedBlock[1] != 1)
        return result;
    for (size_t i = 2; i < 10; ++i) {
        if (result.decodedBlock[i] != 0xff)
            return result;
    }
    if (result.decodedBlock[10] != 0)
        return result;

    result.metadataByte0 = result.decodedBlock[11];
    result.metadataByte1 = result.decodedBlock[12];
    result.status = RsaVerifyStatus::Valid;
    return result;
}

} // namespace Checksum::Common
