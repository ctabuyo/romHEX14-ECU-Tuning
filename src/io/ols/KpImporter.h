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

// A field as it appears on the schema-750 wire. `path` is derived directly
// from the WinOLS serializer's native member offset (and vector index for a
// repeated member); it deliberately does not assign a speculative semantic
// name. `serialized` retains the exact bytes read for future write support.
struct KpSchema750Field {
    enum class Type : uint8_t {
        String,
        Bool,
        Int32,
        UInt32,
        Enum32,
        Float64,
        RawBytes,
        Vector
    };

    QString path;
    Type type = Type::RawBytes;
    qsizetype streamOffset = -1;
    QByteArray serialized;
    QString text;
    int64_t signedValue = 0;
    uint64_t unsignedValue = 0;
    double floatValue = 0.0;
};

// The import-model metadata stream is serialized before the root version
// records and the embedded ZIP. The stream has no field tags: its exact
// grammar is the version-gated WinOLS routine, so fields are identified by
// their native member offset path rather than guessed display names.
struct KpSchema750Metadata {
    qsizetype streamOffset = -1;
    qsizetype streamEnd = -1;
    QByteArray serialized;
    QVector<KpSchema750Field> fields;
};

struct KpSchema750RootRecord {
    QByteArray serialized;
    QVector<KpSchema750Field> fields;
};

struct KpSchema750Root {
    qsizetype streamOffset = -1;
    qsizetype streamEnd = -1;
    QByteArray serialized;
    QVector<KpSchema750Field> fields;
    QVector<KpSchema750RootRecord> records;
};

// One complete KpMapObjectCodec record from the decompressed `intern` entry.
// The native object has no tagged members: `fields` is consequently the
// authoritative, ordered decode ledger.  `serialized` keeps the corresponding
// bytes available to callers that need fields not projected into MapInfo.
struct KpSchema750MapRecord {
    qsizetype streamOffset = -1;
    qsizetype streamEnd = -1;
    QByteArray serialized;
    QVector<KpSchema750Field> fields;
};

// `SerializeMapPackFolderRecord` stores this vector immediately after the
// folder's primary name. The member names intentionally retain the native
// layout relationship rather than assigning an unproven UI meaning.
struct KpFolderNameEntry {
    QString text;
    int32_t value = 0;
};

struct KpFolderRecord {
    uint32_t id = 0;
    uint32_t parentId = 0;
    QString name;
    int32_t nameValueAt0c = 0;
    int32_t valueAt08 = 0;
    QVector<KpFolderNameEntry> nameEntries;
    bool flagBeforeByteVector = false;
    QByteArray byteVector;
    bool flag131 = false;
    uint32_t enum131a = 0;
    uint32_t enum131b = 0;
    uint8_t value131 = 0;
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
    KpSchema750Metadata metadata;
    KpSchema750Root root;
    QVector<KpSchema750MapRecord> mapRecords;
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
