/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "SiemensSIDPlugin.h"

bool SiemensSIDPlugin::canHandle(const QByteArray& rom, const QString& ecuType) const {
    Q_UNUSED(rom);
    const QString q = ecuType.toUpper();
    return (q.contains("SID") || q.contains("SID801") || q.contains("SID803") || q.contains("SID208") || q.contains("SID305") || q.contains("SID307") || q.contains("SID310"));
}

int SiemensSIDPlugin::verify(const QByteArray& rom, QString& errorMsg) {
    Q_UNUSED(rom);
    errorMsg = QStringLiteral("Native open-source checksum verification for %1 is currently in development. On Windows, the DLL bridge is active.").arg(name());
    return 3; // Status::InDevelopment
}

int SiemensSIDPlugin::correct(QByteArray& rom, QString& errorMsg) {
    Q_UNUSED(rom);
    errorMsg = QStringLiteral("Native open-source checksum calculation for %1 is currently in development. On Windows, the DLL bridge is active.").arg(name());
    return 3; // Status::InDevelopment
}
