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

struct KpFolderRecord {
    uint32_t id = 0;
    uint32_t parentId = 0;
    QString name;
    QByteArray byteVector;
    int32_t auxiliaryStringMarker = 0;
    QString auxiliaryString;
    bool flag291 = false;
    bool flag302a = false;
    bool flag302b = false;
    int32_t value302a = 0;
    int32_t value302b = 0;
    int32_t value750 = 0;
    QByteArray serializedRecord;
};

struct KpImportResult {
    uint32_t       formatVersion = 0;
    uint32_t       declaredFileSize = 0;
    uint32_t       mapCount = 0;
    QVector<MapInfo> maps;
    QVector<KpFolderRecord> folders;
    // Complete non-ZIP portions of the source container. These are retained
    // verbatim while their schema-specific import-model and trailing metadata
    // records are decoded into typed fields.
    QByteArray      outerEnvelope;
    QByteArray      trailingMetadata;
    // Schema-750 packs can carry the source bytes for the selected map data
    // and axes. Per-map offsets are retained as MapInfo side properties.
    QByteArray      carriedData;
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
                                    uint32_t &expectedCrc,
                                    qsizetype &archiveStart,
                                    qsizetype &archiveEnd,
                                    QString &err);
};

}
