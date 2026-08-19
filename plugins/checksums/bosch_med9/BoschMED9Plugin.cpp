/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "BoschMED9Plugin.h"

bool BoschMED9Plugin::canHandle(const QByteArray& rom, const QString& ecuType) const {
    Q_UNUSED(rom);
    const QString q = ecuType.toUpper();
    return (q.contains("MED9") || q.contains("MED9.1") || q.contains("MED9.5") || q.contains("EDC7") || q.contains("EDC7C") || q.contains("EDC7C4"));
}

int BoschMED9Plugin::verify(const QByteArray& rom, QString& errorMsg) {
    Q_UNUSED(rom);
    errorMsg = QStringLiteral("Native open-source checksum verification for %1 is currently in development. On Windows, the DLL bridge is active.").arg(name());
    return 3; // Status::InDevelopment
}

int BoschMED9Plugin::correct(QByteArray& rom, QString& errorMsg) {
    Q_UNUSED(rom);
    errorMsg = QStringLiteral("Native open-source checksum calculation for %1 is currently in development. On Windows, the DLL bridge is active.").arg(name());
    return 3; // Status::InDevelopment
}
