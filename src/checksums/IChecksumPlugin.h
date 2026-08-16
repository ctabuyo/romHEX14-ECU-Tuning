/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#include <QByteArray>
#include <QString>
#include <QtPlugin>
#include <cstdint>

#define RX14_CHECKSUM_PLUGIN_IID "com.romhex14.ChecksumPlugin/1.0"

namespace Checksum {

class IChecksumPlugin {
public:
    virtual ~IChecksumPlugin() = default;

    /// Plugin ABI Version (default 1)
    virtual uint32_t pluginVersion() const = 0;

    /// Corresponding Alientech/WinOLS devNum identifier (e.g. 94 for Bosch MED17)
    virtual uint32_t devNum() const = 0;

    /// Plugin identifier slug (e.g. "bosch_med17")
    virtual QString pluginId() const = 0;

    /// Human-readable plugin name
    virtual QString name() const = 0;

    /// Detailed description of covered ECUs and families
    virtual QString description() const = 0;

    /// Author / Maintainer
    virtual QString author() const = 0;

    /// Plugin version string (e.g. "1.0.0")
    virtual QString versionString() const = 0;

    /// Check if this plugin recognizes and can handle the given ROM binary or ECU type
    virtual bool canHandle(const QByteArray& rom, const QString& ecuType) const = 0;

    /// Verify checksums in the ROM binary. Returns 0 for OK, 1 for Mismatch, 2 for InvalidFormat, -1 for Error.
    virtual int verify(const QByteArray& rom, QString& errorMsg) = 0;

    /// Correct checksums in-place in the ROM binary. Returns 0 for OK, 2 for InvalidFormat, -1 for Error.
    virtual int correct(QByteArray& rom, QString& errorMsg) = 0;
};

} // namespace Checksum

Q_DECLARE_INTERFACE(Checksum::IChecksumPlugin, RX14_CHECKSUM_PLUGIN_IID)
