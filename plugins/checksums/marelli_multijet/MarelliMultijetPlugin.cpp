/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "MarelliMultijetPlugin.h"

bool MarelliMultijetPlugin::canHandle(const QByteArray& rom, const QString& ecuType) const {
    Q_UNUSED(rom);
    const QString q = ecuType.toUpper();
    return (q.contains("MJD") || q.contains("6JF") || q.contains("6F3") || q.contains("8DF") || q.contains("8F2") || q.contains("8F3") || q.contains("9DF"));
}

int MarelliMultijetPlugin::verify(const QByteArray& rom, QString& errorMsg) {
    Q_UNUSED(rom);
    errorMsg = QStringLiteral("Native open-source checksum verification for %1 is currently in development. On Windows, the DLL bridge is active.").arg(name());
    return 3; // Status::InDevelopment
}

int MarelliMultijetPlugin::correct(QByteArray& rom, QString& errorMsg) {
    Q_UNUSED(rom);
    errorMsg = QStringLiteral("Native open-source checksum calculation for %1 is currently in development. On Windows, the DLL bridge is active.").arg(name());
    return 3; // Status::InDevelopment
}
