/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "Med17Rsa.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace Checksum::MED17 {
namespace {

// MED17 uses GMP for one fixed-size RSA public operation.  The small fixed
// implementation below avoids a platform-specific GMP runtime while preserving
// the binary's 1024-bit arithmetic and byte order.  Limbs are little-endian.
constexpr size_t kModulusWords = 32;
constexpr size_t kRemainderWords = kModulusWords + 1;
using Modulus = std::array<uint32_t, kModulusWords>;
using Remainder = std::array<uint32_t, kRemainderWords>;
using Product = std::array<uint32_t, kModulusWords * 2>;

bool isZero(const Modulus& value)
{
    return std::all_of(value.begin(), value.end(), [](uint32_t word) { return word == 0; });
}

int compare(const Remainder& lhs, const Modulus& rhs)
{
    if (lhs.back() != 0)
        return 1;
    for (size_t i = kModulusWords; i-- > 0;) {
        if (lhs[i] < rhs[i])
            return -1;
        if (lhs[i] > rhs[i])
            return 1;
    }
    return 0;
}

void subtract(Remainder& lhs, const Modulus& rhs)
{
    uint64_t borrow = 0;
    for (size_t i = 0; i < kModulusWords; ++i) {
        const uint64_t subtrahend = uint64_t(rhs[i]) + borrow;
        const uint64_t current = lhs[i];
        lhs[i] = static_cast<uint32_t>(current - subtrahend);
        borrow = current < subtrahend ? 1 : 0;
    }
    lhs[kModulusWords] = static_cast<uint32_t>(lhs[kModulusWords] - borrow);
}

void shiftLeftOne(Remainder& value)
{
    uint32_t carry = 0;
    for (uint32_t& word : value) {
        const uint32_t nextCarry = word >> 31;
        word = (word << 1) | carry;
        carry = nextCarry;
    }
}

Modulus reduce(const uint32_t* numerator, size_t wordCount, const Modulus& modulus)
{
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

    Modulus result{};
    std::copy_n(remainder.begin(), kModulusWords, result.begin());
    return result;
}

Modulus multiplyMod(const Modulus& lhs, const Modulus& rhs, const Modulus& modulus)
{
    Product product{};
    for (size_t i = 0; i < kModulusWords; ++i) {
        uint64_t carry = 0;
        for (size_t j = 0; j < kModulusWords; ++j) {
            const uint64_t accumulated = uint64_t(product[i + j]) + uint64_t(lhs[i]) * rhs[j] + carry;
            product[i + j] = static_cast<uint32_t>(accumulated);
            carry = accumulated >> 32;
        }
        for (size_t k = i + kModulusWords; carry != 0 && k < product.size(); ++k) {
            const uint64_t accumulated = uint64_t(product[k]) + carry;
            product[k] = static_cast<uint32_t>(accumulated);
            carry = accumulated >> 32;
        }
    }
    return reduce(product.data(), product.size(), modulus);
}

Modulus importBigEndian(const QByteArray& bytes)
{
    Modulus result{};
    for (size_t i = 0; i < 128; ++i)
        result[(127 - i) / 4] |= uint32_t(static_cast<uint8_t>(bytes[static_cast<qsizetype>(i)])) << (((127 - i) % 4) * 8);
    return result;
}

Modulus importLittleEndian(const std::array<uint8_t, 128>& bytes)
{
    Modulus result{};
    for (size_t i = 0; i < bytes.size(); ++i)
        result[i / 4] |= uint32_t(bytes[i]) << ((i % 4) * 8);
    return result;
}

std::array<uint8_t, 128> exportBigEndian(const Modulus& value)
{
    std::array<uint8_t, 128> result{};
    for (size_t i = 0; i < result.size(); ++i)
        result[i] = static_cast<uint8_t>(value[(127 - i) / 4] >> (((127 - i) % 4) * 8));
    return result;
}

} // namespace

RsaSignatureResult verifyRsaSignatureBlock(
    const QByteArray& signatureBlock,
    const std::array<uint8_t, 128>& modulusLittleEndian)
{
    RsaSignatureResult result;
    if (signatureBlock.size() != 128)
        return result;

    const Modulus modulus = importLittleEndian(modulusLittleEndian);
    if (isZero(modulus)) {
        result.status = RsaSignatureStatus::InvalidModulus;
        return result;
    }

    const Modulus signature = reduce(importBigEndian(signatureBlock).data(), kModulusWords, modulus);
    // __gmpz_powm_ui(..., 3, ...) in MED17.
    const Modulus decoded = multiplyMod(multiplyMod(signature, signature, modulus), signature, modulus);
    result.decodedBlock = exportBigEndian(decoded);
    result.status = RsaSignatureStatus::InvalidPadding;

    // Exact acceptance condition reconstructed from VerifyRsaSignatureBlock:
    // 00 01, exactly eight FF bytes at positions 2..9, then a 00 separator.
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
    result.status = RsaSignatureStatus::Valid;
    return result;
}

} // namespace Checksum::MED17
