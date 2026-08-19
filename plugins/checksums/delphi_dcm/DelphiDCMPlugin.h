/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#include <QObject>
#include "checksums/IChecksumPlugin.h"

class DelphiDCMPlugin : public QObject, public Checksum::IChecksumPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID RX14_CHECKSUM_PLUGIN_IID)
    Q_INTERFACES(Checksum::IChecksumPlugin)

public:
    uint32_t pluginVersion() const override { return 1; }
    uint32_t devNum() const override { return 123; }
    QVector<uint32_t> supportedDevNums() const override { return { 123, 124, 125, 137, 143, 151, 152, 153, 157, 158, 159 }; }
    QString pluginId() const override { return QStringLiteral("delphi_dcm"); }
    QString name() const override { return QStringLiteral("Delphi DCM (DCM3.2 / DCM3.5 / DCM3.7 / CRD2)"); }
    QString description() const override { return QStringLiteral("DELPHI DCM MPC55xx/SH705x - ALL BRAND"); }
    QString author() const override { return QStringLiteral("romHEX Community"); }
    QString versionString() const override { return QStringLiteral("0.1.0-wip"); }

    bool canHandle(const QByteArray& rom, const QString& ecuType) const override;
    int verify(const QByteArray& rom, QString& errorMsg) override;
    int correct(QByteArray& rom, QString& errorMsg) override;
};
