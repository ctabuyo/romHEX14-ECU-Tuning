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

    /// Initialize the MED17 non-reflected CRC-32 table (0x04C11DB7 polynomial).
    static void initCrcTable();

    /// Calculate MED17's CRC-32/BZIP2-style checksum over an inclusive byte range.
    static uint32_t calculateCrc32(const uint8_t* data, size_t start, size_t end);

    /// Check if ROM can be handled by Bosch MED17/EDC17 engine
    static bool canHandle(const QByteArray& rom, const QString& ecuType = QString());

    /// Scan recovered MED17 TriCore descriptors (FADECAFE / CAFEAFFE / DEADBEEF).
    static std::vector<Med17BlockDescriptor> scanBlocks(const uint8_t* data, size_t size);

    /// Verify MED17 RSA/CVN signatures natively in the ROM buffer.
    static Status verify(const QByteArray& rom, QString& errorMsg);

    /// Forge and write corrected MED17 RSA/CVN signatures in-place in the ROM buffer.
    static Status correct(QByteArray& rom, QString& errorMsg);

private:
    static bool s_crcInit;
    static uint32_t s_crcTable[256];
};

} // namespace Checksum
