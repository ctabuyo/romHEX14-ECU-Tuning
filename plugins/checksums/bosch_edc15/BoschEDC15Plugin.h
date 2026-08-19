/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#include <QObject>
#include "checksums/IChecksumPlugin.h"

class BoschEDC15Plugin : public QObject, public Checksum::IChecksumPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID RX14_CHECKSUM_PLUGIN_IID)
    Q_INTERFACES(Checksum::IChecksumPlugin)

public:
    uint32_t pluginVersion() const override { return 1; }
    uint32_t devNum() const override { return 1; }
    QVector<uint32_t> supportedDevNums() const override { return { 1, 2, 3, 5, 10, 13 }; }
    QString pluginId() const override { return QStringLiteral("bosch_edc15"); }
    QString name() const override { return QStringLiteral("Bosch EDC15 (V3.1 / PSOP / CR1 / CR2 / EDC15P / EDC15V)"); }
    QString description() const override { return QStringLiteral("BOSCH EDC15 V3.1/PSOP/CR1/CR2/P/V - ALL BRAND"); }
    QString author() const override { return QStringLiteral("romHEX Community"); }
    QString versionString() const override { return QStringLiteral("0.1.0-wip"); }

    bool canHandle(const QByteArray& rom, const QString& ecuType) const override;
    int verify(const QByteArray& rom, QString& errorMsg) override;
    int correct(QByteArray& rom, QString& errorMsg) override;
};
