/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#include <QObject>
#include "checksums/IChecksumPlugin.h"

class BoschME7Plugin : public QObject, public Checksum::IChecksumPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID RX14_CHECKSUM_PLUGIN_IID)
    Q_INTERFACES(Checksum::IChecksumPlugin)

public:
    uint32_t pluginVersion() const override { return 1; }
    uint32_t devNum() const override { return 42; }
    QVector<uint32_t> supportedDevNums() const override { return { 42, 14, 16, 21, 23, 31, 35, 36, 44, 46 }; }
    QString pluginId() const override { return QStringLiteral("bosch_me7"); }
    QString name() const override { return QStringLiteral("Bosch ME7 / M7 (ME7.1 / ME7.5 / ME7.9 / ME2.8)"); }
    QString description() const override { return QStringLiteral("BOSCH ME7.x/M7.x/ME2.x - ALL BRAND"); }
    QString author() const override { return QStringLiteral("romHEX Community"); }
    QString versionString() const override { return QStringLiteral("0.1.0-wip"); }

    bool canHandle(const QByteArray& rom, const QString& ecuType) const override;
    int verify(const QByteArray& rom, QString& errorMsg) override;
    int correct(QByteArray& rom, QString& errorMsg) override;
};
