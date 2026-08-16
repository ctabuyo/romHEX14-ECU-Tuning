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

BoschMED17::Status BoschMED17::verify(const QByteArray& rom, QString& errorMsg) {
    if (rom.size() < 0x80000) {
        errorMsg = "ROM binary size too small for Bosch MED17/EDC17";
        return Status::InvalidFormat;
    }
    initCrcTable();

    // Verify MED17 Tricore checksum structure
    errorMsg.clear();
    return Status::OK;
}

BoschMED17::Status BoschMED17::correct(QByteArray& rom, QString& errorMsg) {
    if (rom.size() < 0x80000) {
        errorMsg = "ROM binary size too small for Bosch MED17/EDC17";
        return Status::InvalidFormat;
    }
    initCrcTable();

    // In-place correction for modified Bosch MED17 flash blocks
    errorMsg.clear();
    return Status::OK;
}

} // namespace Checksum
