/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#include <QObject>
#include "checksums/IChecksumPlugin.h"

class SiemensSIDPlugin : public QObject, public Checksum::IChecksumPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID RX14_CHECKSUM_PLUGIN_IID)
    Q_INTERFACES(Checksum::IChecksumPlugin)

public:
    uint32_t pluginVersion() const override { return 1; }
    uint32_t devNum() const override { return 83; }
    QVector<uint32_t> supportedDevNums() const override { return { 83, 121, 122, 128, 129, 130, 131, 132, 133, 134, 135, 136, 139, 140, 141, 142 }; }
    QString pluginId() const override { return QStringLiteral("siemens_sid"); }
    QString name() const override { return QStringLiteral("Siemens / Continental SID (SID801 / SID803 / SID208 / SID305)"); }
    QString description() const override { return QStringLiteral("SIEMENS / CONTINENTAL SID MPC5xx/TC17xx - ALL BRAND"); }
    QString author() const override { return QStringLiteral("romHEX Community"); }
    QString versionString() const override { return QStringLiteral("0.1.0-wip"); }

    bool canHandle(const QByteArray& rom, const QString& ecuType) const override;
    int verify(const QByteArray& rom, QString& errorMsg) override;
    int correct(QByteArray& rom, QString& errorMsg) override;
};
