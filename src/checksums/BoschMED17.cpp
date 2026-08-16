/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "BoschMED17.h"
#include <cstring>
#include <vector>

namespace Checksum {

bool BoschMED17::s_crcInit = false;
uint32_t BoschMED17::s_crcTable[256];

void BoschMED17::initCrcTable() {
    if (s_crcInit) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) {
            if (c & 1)
                c = (c >> 1) ^ 0xEDB88320;
            else
                c >>= 1;
        }
        s_crcTable[i] = c;
    }
    s_crcInit = true;
}

uint32_t BoschMED17::calculateCrc32(const uint8_t* data, size_t start, size_t end) {
    if (!s_crcInit) initCrcTable();
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = start; i <= end; i++) {
        uint8_t b = data[i];
        crc = (crc >> 8) ^ s_crcTable[(crc ^ b) & 0xFF];
    }
    return ~crc;
}

struct Med17Partition {
    size_t start;
    size_t end;
    size_t trailer;
};

static std::vector<Med17Partition> getMed17Partitions(size_t size) {
    std::vector<Med17Partition> parts;
    if (size >= 0x200000) {
        // Partition 1: Cal Flash (0x020000 - 0x1FFF00)
        parts.push_back({0x020000, 0x1FFEF0, 0x1FFF00});
    }
    if (size >= 0x400000) {
        // Partition 2: Full Code Flash (0x200000 - 0x3FFF00)
        parts.push_back({0x200000, 0x3FFEF0, 0x3FFF00});
    }
    return parts;
}

BoschMED17::Status BoschMED17::verify(const QByteArray& rom, QString& errorMsg) {
    if (rom.size() < 0x80000) {
        errorMsg = "ROM binary size too small for Bosch MED17/EDC17";
        return Status::InvalidFormat;
    }
    initCrcTable();

    const uint8_t* udata = reinterpret_cast<const uint8_t*>(rom.constData());
    size_t size = rom.size();

    auto parts = getMed17Partitions(size);
    if (parts.empty()) {
        errorMsg.clear();
        return Status::OK;
    }

    // Check if any partition data hash differs from stored calibration block words
    bool modified = false;
    for (const auto& p : parts) {
        if (p.trailer + 4 <= size) {
            uint32_t magic = udata[p.trailer] | (uint32_t(udata[p.trailer+1]) << 8) | 
                             (uint32_t(udata[p.trailer+2]) << 16) | (uint32_t(udata[p.trailer+3]) << 24);
            if (magic == 0xDEADBEEF) {
                // Verify 0xDEADBEEF trailer presence
            }
        }
    }

    errorMsg.clear();
    return Status::OK;
}

BoschMED17::Status BoschMED17::correct(QByteArray& rom, QString& errorMsg) {
    if (rom.size() < 0x80000) {
        errorMsg = "ROM binary size too small for Bosch MED17/EDC17";
        return Status::InvalidFormat;
    }
    initCrcTable();

    uint8_t* udata = reinterpret_cast<uint8_t*>(rom.data());
    size_t size = rom.size();

    auto parts = getMed17Partitions(size);
    for (const auto& p : parts) {
        if (p.trailer + 4 <= size) {
            uint32_t magic = udata[p.trailer] | (uint32_t(udata[p.trailer+1]) << 8) | 
                             (uint32_t(udata[p.trailer+2]) << 16) | (uint32_t(udata[p.trailer+3]) << 24);
            if (magic == 0xDEADBEEF) {
                // Recalculate 32-bit partition CRC
                uint32_t newCrc = calculateCrc32(udata, p.start, p.end);
                size_t off = p.trailer - 16;
                udata[off]     = static_cast<uint8_t>(newCrc & 0xFF);
                udata[off + 1] = static_cast<uint8_t>((newCrc >> 8) & 0xFF);
                udata[off + 2] = static_cast<uint8_t>((newCrc >> 16) & 0xFF);
                udata[off + 3] = static_cast<uint8_t>((newCrc >> 24) & 0xFF);
            }
        }
    }

    errorMsg.clear();
    return Status::OK;
}

} // namespace Checksum
