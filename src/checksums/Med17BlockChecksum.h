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
 * MED17 block checksum algorithms (the MEDC17-style CRC32/ADD16/ADD32 layer,
 * distinct from the RSA signature scheme).  These are the reflected CRC-32
 * (zlib) and the 16/32-bit additive checksums the DLL's ProcessBlockChecksums
 * recomputes over block regions.
 */
uint32_t calculateReflectedCrc32(const uint8_t* data, size_t start, size_t endInclusive);
uint32_t calculateAdd16(const uint8_t* data, size_t start, size_t endExclusive);
uint32_t calculateAdd32(const uint8_t* data, size_t start, size_t endExclusive);

/**
 * Reproduce MED17 ProcessBlockChecksums (opcode 0x80) plus its sub-block table
 * construction, translated 1:1 from the decompiled three-pass engine.
 *
 * hasChecksums is the DLL's DAT_100581f8 variant flag (pass 0x1 gate).  The
 * reference buffer (DAT_10058208) is a snapshot of the input ROM, matching the
 * single-buffer correct() flow.
 *
 * Mutable ROM is written in place.  Returns true on success.
 */
bool processBlockChecksums(QByteArray& rom, const std::vector<Descriptor>& descriptors,
                           bool hasChecksums, const QByteArray* originalRom = nullptr);

/**
 * MED17 CVN (Calibration Verification Number) configuration, from
 * LocateCvnConfig: regionTable is the flash address of the {start,end} region
 * table (DAT_10062240) and storedCrc the CompTest CRC location (DAT_1006224c).
 */
struct CvnConfig {
    uint32_t regionTable = 0;
    uint32_t storedCrc = 0;
    bool valid = false;
};

/** MED17 LocateCvnConfig (opcode 0x80 CVN path). */
bool locateCvnConfig(const QByteArray& rom, const std::vector<Descriptor>& descriptors,
                     CvnConfig& cfg);

/** MED17 ComputeOrCorrectCvn (opcode 0x80 CVN path).  Returns the region bitmask. */
uint32_t computeOrCorrectCvn(QByteArray& rom, const std::vector<Descriptor>& descriptors,
                             const CvnConfig& cfg, bool write);

/** MED17 0x82 signature-marker record (FindSignatureMarker). */
struct SignatureMarker {
    uint32_t offset = 0;
    uint16_t value = 0;
    bool found = false;
};

/** MED17 FindSignatureMarker (opcode 0x82). */
void findSignatureMarker(const QByteArray& rom, uint16_t ecuType, SignatureMarker& marker);

/** MED17 ClearSignatureMarker (opcode 0x82). */
void clearSignatureMarker(QByteArray& rom, const SignatureMarker& marker);

/** MED17 HasEdc17CpVariant. */
bool hasEdc17CpVariant(bool cp48, bool cp68, bool cp22);

} // namespace Checksum::MED17
