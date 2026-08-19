/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "BoschEDC16Plugin.h"

bool BoschEDC16Plugin::canHandle(const QByteArray& rom, const QString& ecuType) const {
    Q_UNUSED(rom);
    const QString q = ecuType.toUpper();
    return (q.contains("EDC16") || q.contains("EDC16C") || q.contains("EDC16CP") || q.contains("EDC16U") || q.contains("MPC555") || q.contains("MPC556") || q.contains("MPC561") || q.contains("MPC562") || q.contains("MPC563") || q.contains("MPC564"));
}

int BoschEDC16Plugin::verify(const QByteArray& rom, QString& errorMsg) {
    Q_UNUSED(rom);
    errorMsg = QStringLiteral("Native open-source checksum verification for %1 is currently in development. On Windows, the DLL bridge is active.").arg(name());
    return 3; // Status::InDevelopment
}

int BoschEDC16Plugin::correct(QByteArray& rom, QString& errorMsg) {
    Q_UNUSED(rom);
    errorMsg = QStringLiteral("Native open-source checksum calculation for %1 is currently in development. On Windows, the DLL bridge is active.").arg(name());
    return 3; // Status::InDevelopment
}
