/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#include <QObject>
#include "checksums/IChecksumPlugin.h"

class BoschMED17Plugin : public QObject, public Checksum::IChecksumPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID RX14_CHECKSUM_PLUGIN_IID)
    Q_INTERFACES(Checksum::IChecksumPlugin)

public:
    uint32_t pluginVersion() const override { return 1; }
    uint32_t devNum() const override { return 94; }
    QString pluginId() const override { return QStringLiteral("bosch_med17"); }
    QString name() const override { return QStringLiteral("Bosch MED17 / EDC17 (TriCore TC176x/179x/172x)"); }
    QString description() const override { return QStringLiteral("BOSCH MEDVC17 TC176x/TC179x/TC172x - ALL BRAND"); }
    QString author() const override { return QStringLiteral("romHEX Community"); }
    QString versionString() const override { return QStringLiteral("1.0.0"); }

    bool canHandle(const QByteArray& rom, const QString& ecuType) const override;
    int verify(const QByteArray& rom, QString& errorMsg) override;
    int correct(QByteArray& rom, QString& errorMsg) override;
};
