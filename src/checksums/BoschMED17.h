/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#include <QByteArray>
#include <QString>
#include <cstdint>
#include <vector>

namespace Checksum {

struct Med17BlockDescriptor {
    uint32_t type;
    size_t startOffset;
    size_t endOffset;
    size_t trailerOffset;
    uint32_t storedCrc;
    uint32_t calculatedCrc;
    bool valid;
};

class BoschMED17 {
public:
    enum class Status {
        OK,
        Mismatch,
        InvalidFormat,
        Error
    };

    /// Initialize global IEEE 802.3 CRC-32 table (0xEDB88320 polynomial)
    static void initCrcTable();

    /// Calculate 32-bit IEEE 802.3 CRC-32 over a byte range
    static uint32_t calculateCrc32(const uint8_t* data, size_t start, size_t end);

    /// Scan and parse all TriCore MED17/EDC17 dynamic block descriptors (0xFADECAFE / 0xCAFEAFFE / 0xDEADBEEF)
    static std::vector<Med17BlockDescriptor> scanBlocks(const uint8_t* data, size_t size);

    /// Verify Bosch MED17 / EDC17 checksums natively in ROM buffer
    static Status verify(const QByteArray& rom, QString& errorMsg);

    /// Correct Bosch MED17 / EDC17 checksums in-place in ROM buffer
    static Status correct(QByteArray& rom, QString& errorMsg);

private:
    static bool s_crcInit;
    static uint32_t s_crcTable[256];
};

} // namespace Checksum
