/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "BoschBMSPlugin.h"

bool BoschBMSPlugin::canHandle(const QByteArray& rom, const QString& ecuType) const {
    Q_UNUSED(rom);
    const QString q = ecuType.toUpper();
    return (q.contains("BMS-X") || q.contains("BMS-MP") || q.contains("BMS-KP") || q.contains("BMS-O") || q.contains("BMS-E"));
}

int BoschBMSPlugin::verify(const QByteArray& rom, QString& errorMsg) {
    Q_UNUSED(rom);
    errorMsg = QStringLiteral("Native open-source checksum verification for %1 is currently in development. On Windows, the DLL bridge is active.").arg(name());
    return 3; // Status::InDevelopment
}

int BoschBMSPlugin::correct(QByteArray& rom, QString& errorMsg) {
    Q_UNUSED(rom);
    errorMsg = QStringLiteral("Native open-source checksum calculation for %1 is currently in development. On Windows, the DLL bridge is active.").arg(name());
    return 3; // Status::InDevelopment
}
