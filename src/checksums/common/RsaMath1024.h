/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QByteArray>
#include <array>
#include <cstddef>
#include <cstdint>

namespace Checksum::Common {

/** Status code resulting from 1024-bit RSA verification */
enum class RsaVerifyStatus {
    Valid,
    InvalidLength,
    InvalidModulus,
    InvalidPadding,
    ShaMismatch
};

/** Result containing decoded 128-byte block and metadata */
struct RsaVerifyResult {
    RsaVerifyStatus status = RsaVerifyStatus::InvalidLength;
    std::array<uint8_t, 128> decodedBlock{};
    uint8_t metadataByte0 = 0;
    uint8_t metadataByte1 = 0;
};

class RsaMath1024 {
public:
    static constexpr size_t kModulusWords = 32;       // 32 * 32-bit = 1024-bit
    static constexpr size_t kModulusBytes = 128;

    using Words1024 = std::array<uint32_t, kModulusWords>;
    using Words2048 = std::array<uint32_t, kModulusWords * 2>;
    using Words3072 = std::array<uint32_t, kModulusWords * 3>;

    /// Import 128 bytes in Big-Endian representation into 1024-bit word array
    static Words1024 importBigEndian(const uint8_t* bytes);

    /// Import 128 bytes in Little-Endian representation into 1024-bit word array
    static Words1024 importLittleEndian(const uint8_t* bytes);

    /// Export 1024-bit word array into 128 bytes Big-Endian
    static std::array<uint8_t, 128> exportBigEndian(const Words1024& words);

    /// Computes (value^3) mod modulus (1024-bit modular cubing for e=3)
    static Words1024 modCube(const Words1024& base, const Words1024& modulus);

    /// Full verification of a 128-byte signature block against 128-byte public key modulus
    static RsaVerifyResult verifySignature(
        const uint8_t* signatureBytes,
        const uint8_t* modulusLittleEndian);
};

} // namespace Checksum::Common
