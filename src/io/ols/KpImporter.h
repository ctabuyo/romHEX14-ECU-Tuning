/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "../../romdata.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QVector>
#include <cstdint>

namespace ols {

struct KpImportResult {
    uint32_t       formatVersion = 0;
    uint32_t       declaredFileSize = 0;
    uint32_t       mapCount = 0;
    QVector<MapInfo> maps;
    QString        error;
    QStringList    warnings;
};

class KpImporter {
    Q_DECLARE_TR_FUNCTIONS(ols::KpImporter)
public:
    static KpImportResult importFromBytes(const QByteArray &fileData,
                                           uint32_t baseAddress = 0,
                                           uint32_t romSize = 0);

private:
    static bool extractInternEntry(const QByteArray &fileData,
                                    QByteArray &compressed,
                                    uint32_t &uncompressedSize,
                                    uint16_t &method,
                                    QString &err);
};

}
