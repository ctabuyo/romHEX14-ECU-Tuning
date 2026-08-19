/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "Med17Descriptor.h"

#include <QByteArray>

#include <cstdint>
#include <vector>

namespace Checksum::MED17 {

/**
 * MED17's full ECU-variant flag set (the DLL's DAT_100581e0..DAT_10058300
 * globals).  Names carry the DAT offset; the gating-relevant flags have
 * descriptive aliases.
 */
struct VariantFlags {
    bool f581e0 = false;         // key 5
    bool f581e4 = false;         // key 0x29
    bool f581e8 = false;         // key 0x30
    bool f581ec = false;         // key 0x21
    bool f581f0 = false;         // key 0x42
    bool f581f4 = false;         // key 9 / 0x53
    bool hasChecksums = false;   // DAT_100581f8 (keys 0/3/0xb/0x1a/0x6a)
    bool f58210 = false;         // key 1
    bool f58214 = false;         // key 4 (MED17.4.2)
    bool ford = false;           // DAT_10058218 (Ford Motor Co)
    bool f5821c = false;         // key 9 (EDC17_C41)
    bool med1722 = false;        // DAT_10058220 (MED17/22/MEDECM -> CVN)
    bool f58224 = false;         // key 0x42 (EDC17C79_N)
    bool f58228 = false;         // key 8
    bool f5822c = false;         // key 0x28
    bool f58230 = false;
    bool f58234 = false;         // key 0x4a
    bool f58238 = false;         // key 0x4b
    bool edc17cv41 = false;      // DAT_10058240
    bool edc17cv44 = false;      // DAT_10058244
    bool f58248 = false;         // key 9 (EDC17CP49)
    bool edc17cp48 = false;      // DAT_1005824c
    bool f58250 = false;         // key 0x52
    bool f58254 = false;         // key 0x53
    bool f58258 = false;         // key 0x1e
    bool f5825c = false;         // key 0x14
    bool f58260 = false;         // key 0x12
    bool f58264 = false;         // key 0x19
    bool f58268 = false;         // key 0x1b
    bool f5826c = false;         // key 0x56
    bool f58270 = false;         // key 0x59
    bool f58274 = false;         // key 0x5a
    bool edc17cp68 = false;      // DAT_10058278
    bool edc17cp22 = false;      // DAT_1005827c
    bool f58280 = false;         // key 0x42 (EDC17_C49)
    bool f58284 = false;         // key 100
    bool f58288 = false;         // key 0x68
    bool f5828c = false;         // key 0x69
    bool f58290 = false;         // key 99
    bool f58294 = false;         // key 2 (EDC17_CP20)
    bool f58298 = false;         // key 2 (EDC17C64/C74)
    bool f5829c = false;         // key 2 (EDC17_C46)
    bool f582a0 = false;         // key 2 (EDC17_CP14)
    bool f582ac = false;         // key 0x5e
    bool f582b0 = false;         // key 2 (EDC17C64)
    bool f582b4 = false;         // key 0x16
    bool f582b8 = false;         // EDC17C49 (string)
    bool f582bc = false;         // key 0x60
    bool f582c0 = false;         // key 0x76
    bool f582c4 = false;         // key 0x26
    bool f582c8 = false;         // key 0x73
    bool f582cc = false;         // key 0x7a
    bool f582d0 = false;         // key 0x72
    bool f582d4 = false;         // key 4 (MED17.4.2 variant)
    bool f582d8 = false;         // key 0x3c
    bool f582dc = false;         // key 0x78 (ME177)
    bool f582e0 = false;         // key 0x78 (PCFG:CB/248)
    bool f582e4 = false;         // key 0x27
    bool f582e8 = false;         // EDC17C49 (string)
    bool f582ec = false;         // key 0x13
    bool needsCustomerBlock = false; // DAT_100582f0 (customer-block parser)
    bool f582f4 = false;         // key 6
    bool f582f8 = false;         // has 0x60 block
    bool f582fc = false;         // has 0x70 block
    bool f58300 = false;         // has 0x80 block
    bool isDde73 = false;        // DAT_10064790
};

struct VariantResult {
    uint16_t ecuType = 0;
    VariantFlags flags;
};

/**
 * MED17 detection phase: DetectEcuType + DetectMed17EcuLayout +
 * DetectVariantFromKey (opcode 0x70 and its callees).
 */
VariantResult detectVariant(const QByteArray& rom, const std::vector<Descriptor>& descriptors);

/** MED17 DetectEcuType (FUN_100387d0). */
uint16_t detectEcuType(const QByteArray& rom);

/** MED17 DetectEdc17Cv41Layout (FUN_100382e0). */
bool detectEdc17Cv41Layout(const QByteArray& rom, uint32_t start);

/** MED17 ValidateDescriptorSetForKey (FUN_1003d3b0). */
bool validateDescriptorSetForKey(const QByteArray& rom,
                                 const std::vector<Descriptor>& descriptors,
                                 uint32_t keyIndex);

/**
 * MED17 PatchVariantMarker (FUN_1003e090).  originalRom is the DLL's
 * DAT_1005820c buffer (the pre-tune image); current ROM is written in place.
 */
bool patchVariantMarker(QByteArray& rom, const QByteArray& originalRom,
                        const std::vector<Descriptor>& descriptors,
                        const VariantFlags& flags);

/** MED17 ComputeOverallRomChecksum (FUN_1002fcb0). */
uint32_t computeOverallRomChecksum(const QByteArray& rom);

} // namespace Checksum::MED17
