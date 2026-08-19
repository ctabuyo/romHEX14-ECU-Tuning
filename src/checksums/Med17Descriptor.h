/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QByteArray>

#include <array>
#include <cstdint>
#include <vector>

namespace Checksum::MED17 {

/**
 * One 0xb0-byte runtime record reconstructed from MED17's
 * ParseChecksumDescriptors().  ROM offsets are always file-relative.
 *
 * Field layout mirrors the DLL's DAT_10060b20 (0xb0-stride) array:
 *   +0x00 type, +0x02 subtype, +0x04 crcStartRaw, +0x08 headerOffset,
 *   +0x0c addressBias, +0x10 blankWordFlag, +0x14 keyIndex,
 *   +0x18 crcStart, +0x1c crcEnd, +0xa0 storedCrc32,
 *   +0xa8 subTableOffset, +0xac subTableCount.
 */
struct Descriptor {
    uint16_t type = 0;
    uint16_t subtype = 0;
    uint32_t crcStartRaw = 0;
    uint32_t headerOffset = 0;
    uint32_t trailerOffset = 0;
    uint32_t addressBias = 0;
    uint32_t crcStart = 0;
    uint32_t crcEndInclusive = 0;
    uint32_t signatureOffset = 0;
    uint32_t signatureKeyIndex = UINT32_MAX;
    uint32_t storedCrc32 = 0;
    bool hasSubTable = false;
    uint32_t subTableOffset = 0;
    uint32_t subTableCount = 0;
    bool hasNonBlankWord = false; // true if signature region contains non-0xAFAFAFAF data (not blank)
    bool rsaSignatureValid = false;
};

/** Parse MED17 FADECAFE/CAFEAFFE checksum descriptor headers. */
std::vector<Descriptor> parseDescriptors(const QByteArray& rom);

/** MED17 CalculateDescriptorCrc32: non-reflected 0x04c11db7, init/final xor FFFFFFFF. */
uint32_t calculateDescriptorCrc32(const uint8_t* data, size_t start, size_t endInclusive);

/**
 * MED17 CalculateDescriptorSha1.  ROM words and the final partial block are
 * packed little-endian before the SHA-1 compression transform, so this is not
 * interchangeable with QCryptographicHash::Sha1 over the raw byte range.
 */
std::array<uint8_t, 20> calculateDescriptorSha1(const uint8_t* data,
                                                 size_t start,
                                                 size_t endInclusive);

} // namespace Checksum::MED17
