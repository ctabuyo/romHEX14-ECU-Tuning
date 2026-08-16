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

std::vector<Med17BlockDescriptor> BoschMED17::scanBlocks(const uint8_t* data, size_t size) {
    std::vector<Med17BlockDescriptor> blocks;
    if (size < 0x80000) return blocks;

    initCrcTable();

    // 1. Dynamic Header Scan (0xFADECAFE + 0xCAFEAFFE)
    for (size_t i = 0x40; i + 8 <= size; i += 0x20) {
        uint32_t sig1 = data[i] | (uint32_t(data[i+1]) << 8) | (uint32_t(data[i+2]) << 16) | (uint32_t(data[i+3]) << 24);
        uint32_t sig2 = data[i+4] | (uint32_t(data[i+5]) << 8) | (uint32_t(data[i+6]) << 16) | (uint32_t(data[i+7]) << 24);
        
        if (sig1 == 0xFADECAFE && sig2 == 0xCAFEAFFE && i >= 0x40) {
            size_t hdr_off = i - 0x40;
            if (hdr_off + 64 <= size) {
                const uint32_t* w = reinterpret_cast<const uint32_t*>(data + hdr_off);
                uint32_t b_type = w[0] & 0xFF;
                uint32_t b_trailer = w[3] & 0x7FFFFFFF;
                const uint32_t* w2 = reinterpret_cast<const uint32_t*>(data + hdr_off + 32);
                uint32_t b_start = w2[6] & 0x7FFFFFFF;
                uint32_t b_end = w2[7] & 0x7FFFFFFF;

                if (b_start < size && b_trailer <= size && b_start < b_trailer && b_trailer >= 16) {
                    // Verify 0xDEADBEEF trailer footer
                    uint32_t magic = data[b_trailer] | (uint32_t(data[b_trailer+1]) << 8) |
                                     (uint32_t(data[b_trailer+2]) << 16) | (uint32_t(data[b_trailer+3]) << 24);
                    if (magic == 0xDEADBEEF) {
                        Med17BlockDescriptor blk;
                        blk.type = b_type;
                        blk.startOffset = b_start;
                        blk.trailerOffset = b_trailer;
                        blk.endOffset = b_trailer - 17;

                        size_t off = b_trailer - 16;
                        blk.storedCrc = data[off] | (uint32_t(data[off+1]) << 8) |
                                        (uint32_t(data[off+2]) << 16) | (uint32_t(data[off+3]) << 24);
                        blk.calculatedCrc = calculateCrc32(data, blk.startOffset, blk.endOffset);
                        blk.valid = (blk.storedCrc == blk.calculatedCrc);
                        blocks.push_back(blk);
                    }
                }
            }
        }
    }

    // 2. Fallback Partition Trailer Scan (0xDEADBEEF) if dynamic header table was not present
    if (blocks.empty()) {
        static const struct { size_t start; size_t trailer; } kStandardPartitions[] = {
            {0x020000, 0x1FFF00}, // 2MB / 4MB Calibration Flash
            {0x200000, 0x3FFF00}, // 4MB Total Flash
            {0, 0}
        };
        for (int p = 0; kStandardPartitions[p].trailer != 0; ++p) {
            size_t tr = kStandardPartitions[p].trailer;
            if (tr + 4 <= size && tr >= 16) {
                uint32_t magic = data[tr] | (uint32_t(data[tr+1]) << 8) |
                                 (uint32_t(data[tr+2]) << 16) | (uint32_t(data[tr+3]) << 24);
                if (magic == 0xDEADBEEF) {
                    Med17BlockDescriptor blk;
                    blk.type = 0x40;
                    blk.startOffset = kStandardPartitions[p].start;
                    blk.trailerOffset = tr;
                    blk.endOffset = tr - 17;

                    size_t off = tr - 16;
                    blk.storedCrc = data[off] | (uint32_t(data[off+1]) << 8) |
                                    (uint32_t(data[off+2]) << 16) | (uint32_t(data[off+3]) << 24);
                    blk.calculatedCrc = calculateCrc32(data, blk.startOffset, blk.endOffset);
                    blk.valid = (blk.storedCrc == blk.calculatedCrc);
                    blocks.push_back(blk);
                }
            }
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

    std::vector<Med17BlockDescriptor> blocks = scanBlocks(udata, size);
    if (blocks.empty()) {
        errorMsg = "No MED17 TriCore block headers or trailers found in ROM";
        return Status::OK;
    }

    int mismatches = 0;
    for (const auto& b : blocks) {
        if (!b.valid) {
            mismatches++;
        }
    }

    if (mismatches > 0) {
        errorMsg = QString("%1 MED17 block checksum(s) mismatched").arg(mismatches);
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

    std::vector<Med17BlockDescriptor> blocks = scanBlocks(udata, size);
    if (blocks.empty()) {
        errorMsg.clear();
        return Status::OK;
    }

    for (const auto& b : blocks) {
        if (b.trailerOffset + 4 <= size && b.trailerOffset >= 16) {
            uint32_t newCrc = calculateCrc32(udata, b.startOffset, b.endOffset);
            size_t off = b.trailerOffset - 16;
            udata[off]     = static_cast<uint8_t>(newCrc & 0xFF);
            udata[off + 1] = static_cast<uint8_t>((newCrc >> 8) & 0xFF);
            udata[off + 2] = static_cast<uint8_t>((newCrc >> 16) & 0xFF);
            udata[off + 3] = static_cast<uint8_t>((newCrc >> 24) & 0xFF);
        }
    }

    errorMsg.clear();
    return Status::OK;
}

} // namespace Checksum
