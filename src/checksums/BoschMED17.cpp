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

struct Med17Block {
    size_t startOffset;
    size_t endOffset;
    size_t trailerOffset;
    uint32_t storedCrc;
    uint32_t calculatedCrc;
    bool valid;
};

static std::vector<Med17Block> scanMed17Blocks(const uint8_t* udata, size_t size) {
    std::vector<Med17Block> blocks;
    if (size < 0x80000) return blocks; // TriCore minimum 512KB

    // Search for 0xDEADBEEF block trailer footers
    for (size_t i = 0x8000; i + 4 <= size; i += 4) {
        uint32_t val = udata[i] | (uint32_t(udata[i+1]) << 8) | (uint32_t(udata[i+2]) << 16) | (uint32_t(udata[i+3]) << 24);
        if (val == 0xDEADBEEF && i >= 16) {
            Med17Block b;
            b.trailerOffset = i;
            b.startOffset = (i >= 0x20000) ? (i - 0x20000) : 0;
            b.endOffset = i - 16;
            
            // Read stored CRC at i - 16
            b.storedCrc = udata[i - 16] | (uint32_t(udata[i - 15]) << 8) | (uint32_t(udata[i - 14]) << 16) | (uint32_t(udata[i - 13]) << 24);
            b.calculatedCrc = BoschMED17::calculateCrc32(udata, b.startOffset, b.endOffset);
            b.valid = (b.storedCrc == b.calculatedCrc);
            blocks.push_back(b);
        }
    }
    return blocks;
}

BoschMED17::Status BoschMED17::verify(const QByteArray& rom, QString& errorMsg) {
    if (rom.size() < 0x80000) {
        errorMsg = "ROM binary size too small for Bosch MED17/EDC17";
        return Status::InvalidFormat;
    }
    initCrcTable();

    const uint8_t* udata = reinterpret_cast<const uint8_t*>(rom.constData());
    size_t size = rom.size();

    std::vector<Med17Block> blocks = scanMed17Blocks(udata, size);
    if (blocks.empty()) {
        errorMsg = "No MED17 block trailer markers (0xDEADBEEF) found in ROM";
        return Status::OK; // Valid ROM without block trailers
    }

    bool allValid = true;
    int mismatches = 0;
    for (const auto& b : blocks) {
        if (!b.valid) {
            allValid = false;
            mismatches++;
        }
    }

    if (!allValid) {
        errorMsg = QString("%1 MED17 checksum block(s) mismatched").arg(mismatches);
        return Status::Mismatch;
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

    std::vector<Med17Block> blocks = scanMed17Blocks(udata, size);
    if (blocks.empty()) {
        errorMsg.clear();
        return Status::OK;
    }

    int corrections = 0;
    for (auto& b : blocks) {
        if (!b.valid) {
            // Recalculate CRC over block
            uint32_t newCrc = calculateCrc32(udata, b.startOffset, b.endOffset);
            
            // Write recalculated CRC back to ROM at trailerOffset - 16
            size_t off = b.trailerOffset - 16;
            udata[off]     = static_cast<uint8_t>(newCrc & 0xFF);
            udata[off + 1] = static_cast<uint8_t>((newCrc >> 8) & 0xFF);
            udata[off + 2] = static_cast<uint8_t>((newCrc >> 16) & 0xFF);
            udata[off + 3] = static_cast<uint8_t>((newCrc >> 24) & 0xFF);

            corrections++;
        }
    }

    errorMsg.clear();
    return Status::OK;
}

} // namespace Checksum
