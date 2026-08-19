/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QByteArray>

#include <array>
#include <cstdint>

namespace Checksum::MED17 {

/** Result of MED17's 1024-bit RSA/CVN signature decoding operation. */
enum class RsaSignatureStatus {
    Valid,
    InvalidLength,
    InvalidModulus,
    InvalidPadding,
};

struct RsaSignatureResult {
    RsaSignatureStatus status = RsaSignatureStatus::InvalidLength;

    // The exact 128-byte, big-endian result of signature^3 mod modulus.
    // It is retained to make the native implementation fixture-testable.
    std::array<uint8_t, 128> decodedBlock{};

    // MED17 copies these bytes only after accepting its PKCS#1-style prefix.
    uint8_t metadataByte0 = 0;
    uint8_t metadataByte1 = 0;
};

/**
 * Decode a MED17 RSA signature block exactly as VerifyRsaSignatureBlock does.
 *
 * The signature is a 128-byte big-endian integer.  MED17's selected public
 * modulus is stored as 128 little-endian one-byte GMP "words" (mpz_import
 * order == -1), hence the deliberately explicit modulusLittleEndian argument.
 * The function performs the observed public operation signature^3 mod modulus
 * and accepts only 00 01 FF FF FF FF FF FF FF FF 00 xx yy ... padding.
 *
 * This primitive is intentionally separate from checksum correction: no MED17
 * correction route is enabled until the descriptor parser, key selection, and
 * write transaction have been reconstructed against golden ROM fixtures.
 */
RsaSignatureResult verifyRsaSignatureBlock(
    const QByteArray& signatureBlock,
    const std::array<uint8_t, 128>& modulusLittleEndian);

} // namespace Checksum::MED17
