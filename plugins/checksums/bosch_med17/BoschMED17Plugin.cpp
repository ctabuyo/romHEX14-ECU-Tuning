/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "BoschMED17Plugin.h"
#include "checksums/BoschMED17.h"

bool BoschMED17Plugin::canHandle(const QByteArray& rom, const QString& ecuType) const {
    const QString q = ecuType.toUpper();
    if (q.contains("MED17") || q.contains("EDC17") || q.contains("MEDVC17") ||
        q.contains("TC1793") || q.contains("TC1797") || q.contains("TC179") || q.contains("TC176")) {
        return true;
    }
    if (!rom.isEmpty() && rom.size() >= 0x80000) {
        const uint8_t* data = reinterpret_cast<const uint8_t*>(rom.constData());
        auto blocks = Checksum::BoschMED17::scanBlocks(data, rom.size());
        return !blocks.empty();
    }
    return false;
}

int BoschMED17Plugin::verify(const QByteArray& rom, QString& errorMsg) {
    Checksum::BoschMED17::Status st = Checksum::BoschMED17::verify(rom, errorMsg);
    switch (st) {
    case Checksum::BoschMED17::Status::OK: return 0;
    case Checksum::BoschMED17::Status::Mismatch: return 1;
    case Checksum::BoschMED17::Status::InvalidFormat: return 2;
    default: return -1;
    }
}

int BoschMED17Plugin::correct(QByteArray& rom, QString& errorMsg) {
    Checksum::BoschMED17::Status st = Checksum::BoschMED17::correct(rom, errorMsg);
    switch (st) {
    case Checksum::BoschMED17::Status::OK: return 0;
    case Checksum::BoschMED17::Status::InvalidFormat: return 2;
    default: return -1;
    }
}
