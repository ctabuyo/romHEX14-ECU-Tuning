/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "BoschMED17.h"
#include "Med17BlockChecksum.h"
#include "Med17Correction.h"
#include "Med17CustomerBlock.h"
#include "Med17Descriptor.h"
#include "Med17Keys.h"
#include "Med17Rsa.h"
#include "Med17Variant.h"

#include <QStringList>

#include <algorithm>
#include <cstring>
#include <vector>

namespace Checksum {

bool BoschMED17::s_crcInit = false;
uint32_t BoschMED17::s_crcTable[256];

void BoschMED17::initCrcTable() {
    if (s_crcInit) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i << 24;
        for (int k = 0; k < 8; k++)
            c = (c & 0x80000000u) ? ((c << 1) ^ 0x04C11DB7u) : (c << 1);
        s_crcTable[i] = c;
    }
    s_crcInit = true;
}

uint32_t BoschMED17::calculateCrc32(const uint8_t* data, size_t start, size_t end) {
    if (!data || start > end)
        return 0;
    if (!s_crcInit)
        initCrcTable();
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = start; i <= end; i++)
        crc = (crc << 8) ^ s_crcTable[((crc >> 24) ^ data[i]) & 0xFF];
    return ~crc;
}

bool BoschMED17::canHandle(const QByteArray& rom, const QString& ecuType) {
    Q_UNUSED(ecuType);
    return !MED17::parseDescriptors(rom).empty();
}

std::vector<Med17BlockDescriptor> BoschMED17::scanBlocks(const uint8_t* data, size_t size) {
    std::vector<Med17BlockDescriptor> blocks;
    if (!data || size == 0)
        return blocks;

    const QByteArray rom(reinterpret_cast<const char*>(data), static_cast<qsizetype>(size));
    for (const auto& descriptor : MED17::parseDescriptors(rom)) {
        Med17BlockDescriptor block;
        block.type = descriptor.type;
        block.startOffset = descriptor.crcStart;
        block.endOffset = descriptor.crcEndInclusive;
        block.trailerOffset = descriptor.trailerOffset;
        // MED17 stores/verifies this value through the RSA/CVN signature, not
        // as a raw uint32_t adjacent to DEADBEEF.
        block.storedCrc = 0;
        block.calculatedCrc = 0;
        block.valid = descriptor.rsaSignatureValid;
        blocks.push_back(block);
    }
    return blocks;
}

BoschMED17::Status BoschMED17::verify(const QByteArray& rom, QString& errorMsg) {
    const auto descriptors = MED17::parseDescriptors(rom);
    if (descriptors.empty()) {
        errorMsg = QStringLiteral("No MED17 MED17/EDC17 checksum descriptors found in ROM");
        return Status::InvalidFormat;
    }

    const auto invalidSignatures = static_cast<int>(std::count_if(
        descriptors.cbegin(), descriptors.cend(),
        [](const auto& descriptor) { return !descriptor.rsaSignatureValid; }));
    if (invalidSignatures != 0) {
        errorMsg = QStringLiteral("%1 MED17 RSA/CVN signature(s) mismatched").arg(invalidSignatures);
        return Status::Mismatch;
    }

    errorMsg.clear();
    return Status::OK;
}

BoschMED17::Status BoschMED17::correct(QByteArray& rom, QString& errorMsg) {
    const QByteArray snapshot = rom;
    const auto descriptors = MED17::parseDescriptors(rom);
    if (descriptors.empty()) {
        errorMsg = QStringLiteral("No MED17 MED17/EDC17 checksum descriptors found in ROM");
        return Status::InvalidFormat;
    }

    int correctedCount = 0;
    int structurallyInvalidCount = 0;
    int blankSignatureCount = 0;
    int forgeFailedCount = 0;
    for (const auto& descriptor : descriptors) {
        if (descriptor.rsaSignatureValid)
            continue;

        // No RSA key decodes this signature.  A signature block still holding
        // all 0xAFAFAFAF filler was never flashed (blank); any other
        // non-decodable block is structurally invalid — the ROM may be
        // corrupted or signed with an unsupported key.
        if (descriptor.signatureKeyIndex > 0x8b) {
            if (descriptor.hasNonBlankWord)
                ++structurallyInvalidCount;
            else
                ++blankSignatureCount;
            continue;
        }
        const auto key = MED17::publicKeyForIndex(descriptor.signatureKeyIndex);
        if (!key) {
            ++structurallyInvalidCount;
            continue;
        }

        // Decode the existing (structurally valid) signature block to recover
        // the metadata bytes the forged template must preserve.
        const QByteArray existingSignature =
            rom.mid(static_cast<qsizetype>(descriptor.signatureOffset), 128);
        const auto decoded = MED17::verifyRsaSignatureBlock(existingSignature, *key);
        if (decoded.status != MED17::RsaSignatureStatus::Valid) {
            ++structurallyInvalidCount;
            continue;
        }

        const auto result = MED17::forgeCorrectedSignature(rom, descriptor, decoded);
        if (result.status != MED17::CorrectionStatus::Corrected) {
            ++forgeFailedCount;
            continue;
        }

        rom.replace(static_cast<qsizetype>(descriptor.signatureOffset),
                    static_cast<qsizetype>(result.signature.size()), result.signature);
        ++correctedCount;
    }

    if (correctedCount == 0
        && (structurallyInvalidCount + blankSignatureCount + forgeFailedCount) != 0) {
        QStringList reasons;
        if (structurallyInvalidCount != 0)
            reasons << QStringLiteral(
                           "%1 signature(s) structurally invalid — ROM may be corrupted or uses an "
                           "unsupported RSA key").arg(structurallyInvalidCount);
        if (blankSignatureCount != 0)
            reasons << QStringLiteral("%1 signature(s) blank (all 0xAFAFAFAF) — never flashed")
                           .arg(blankSignatureCount);
        if (forgeFailedCount != 0)
            reasons << QStringLiteral("%1 signature(s) could not be re-signed (forge failed)")
                           .arg(forgeFailedCount);
        errorMsg = reasons.join(QStringLiteral("; "));
        return Status::Error;
    }

    // Full MED17 correction flow: detection -> (forge, above) -> block checksums
    // (opcode 0x80) -> CVN -> CP-variant marker patch (opcode 0x82).
    const auto variant = MED17::detectVariant(rom, descriptors);
    if (!MED17::processBlockChecksums(rom, descriptors, true, &snapshot)) {
        errorMsg = QStringLiteral("MED17 block checksum correction failed");
        return Status::Error;
    }
    if (variant.flags.med1722) {
        MED17::CvnConfig cvn;
        if (MED17::locateCvnConfig(rom, descriptors, cvn) && cvn.valid)
            MED17::computeOrCorrectCvn(rom, descriptors, cvn, true);
    }
    // Opcode 0x82 (FUN_1003ed20): ValidateDescriptorSetForKey selects between the
    // 0x30 customer-block descriptor correction and the signature-marker patch.
    uint32_t keyIndex = UINT32_MAX;
    for (const auto& descriptor : descriptors) {
        if (descriptor.type == 0x30) {
            keyIndex = descriptor.signatureKeyIndex;
            break;
        }
    }
    if (keyIndex == UINT32_MAX && !descriptors.empty())
        keyIndex = descriptors.front().signatureKeyIndex;

    if (MED17::validateDescriptorSetForKey(rom, descriptors, keyIndex)) {
        int flag = 0;
        MED17::processCustomerBlock(rom, descriptors, true, &flag);
    } else if (!MED17::hasEdc17CpVariant(variant.flags.edc17cp48, variant.flags.edc17cp68,
                                               variant.flags.edc17cp22)) {
        MED17::SignatureMarker marker;
        MED17::findSignatureMarker(rom, variant.ecuType, marker);
        if (marker.found)
            MED17::clearSignatureMarker(rom, marker);
    }
    // The CP branch (PatchVariantMarker) reads DAT_1005820c, which is never
    // populated in this DLL build; the single-buffer correct() API has no
    // pre-tune image, so this branch is intentionally not exercised.

    errorMsg.clear();
    return Status::OK;
}

} // namespace Checksum
