/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KpImporter.h"
#include "ZipDecompressor.h"

#include <QtEndian>
#include <QHash>
#include <QStringDecoder>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <zlib.h>

namespace ols {

namespace {

uint32_t peekU32(const QByteArray &data, qsizetype off)
{
    if (off < 0 || off + 4 > data.size()) return 0;
    return qFromLittleEndian<uint32_t>(
        reinterpret_cast<const uchar *>(data.constData() + off));
}

int32_t peekI32(const QByteArray &data, qsizetype off)
{
    return static_cast<int32_t>(peekU32(data, off));
}

double peekF64(const QByteArray &data, qsizetype off)
{
    if (off < 0 || off + 8 > data.size())
        return std::numeric_limits<double>::quiet_NaN();
    const uint64_t bits = qFromLittleEndian<uint64_t>(
        reinterpret_cast<const uchar *>(data.constData() + off));
    double v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

QString decodeKpText(const QByteArray &bytes)
{
    const auto encoding = QStringConverter::encodingForName("Windows-1252");
    if (!encoding)
        return QString::fromLatin1(bytes).trimmed();
    QStringDecoder decoder(*encoding);
    return QString(decoder.decode(bytes)).trimmed();
}

QString typeFromKpKind(uint32_t kind, int x, int y)
{
    if (kind == 2) return QStringLiteral("VALUE");
    if (x <= 1 && y <= 1) return QStringLiteral("VALUE");
    if (kind == 3) return QStringLiteral("CURVE");
    if (y <= 1) return QStringLiteral("CURVE");
    return QStringLiteral("MAP");
}

bool normalizeKpAddress(uint32_t raw, uint32_t end, uint32_t universalBase,
                        uint32_t projectBase, uint32_t romSize,
                        uint32_t *fileOffset)
{
    if (raw < 0x80 || end <= raw || universalBase == 0
        || universalBase == 0xFFFFFFFF)
        return false;

    const uint64_t dataBytes = uint64_t(end) - uint64_t(raw);
    if (dataBytes == 0 || dataBytes > 64ull * 1024ull * 1024ull)
        return false;

    if (romSize > 0) {
        if (raw < romSize && end <= romSize) {
            *fileOffset = raw;
            return true;
        }
        if (projectBase != 0
            && raw >= projectBase
            && end <= projectBase + uint64_t(romSize)) {
            *fileOffset = raw - projectBase;
            return true;
        }
        if (universalBase == romSize && raw < romSize && end <= romSize) {
            *fileOffset = raw;
            return true;
        }
        if (projectBase != 0
            && universalBase == projectBase + romSize
            && raw >= projectBase
            && end <= universalBase) {
            *fileOffset = raw - projectBase;
            return true;
        }
        return false;
    }

    // Debug/import without a ROM loaded: KP files often carry the ROM size as
    // universalBase and store addresses as file offsets.
    if (raw < universalBase && end <= universalBase) {
        *fileOffset = raw;
        return true;
    }
    if (projectBase != 0 && raw >= projectBase && end <= universalBase) {
        *fileOffset = raw - projectBase;
        return true;
    }
    return false;
}

// Axis records use the same address convention as their parent map, but do
// not carry an end/base triplet.  Normalise them directly instead of deriving
// an offset from MapInfo::rawAddress: that field is presentation-oriented and
// may include the project's ECU base even when the KP stores file offsets.
bool normalizeKpAxisAddress(uint32_t raw, int count, int dataSize,
                            uint32_t projectBase, uint32_t romSize,
                            uint32_t *fileOffset)
{
    if (raw == 0 || count <= 0 || dataSize <= 0)
        return false;
    const uint64_t end = uint64_t(raw) + uint64_t(count) * uint64_t(dataSize);
    if (end > 0x100000000ull)
        return false;

    if (romSize == 0) {
        *fileOffset = raw >= projectBase && projectBase != 0
            ? raw - projectBase : raw;
        return true;
    }
    if (end <= romSize) {
        *fileOffset = raw;
        return true;
    }
    if (projectBase != 0 && raw >= projectBase
        && end <= uint64_t(projectBase) + romSize) {
        *fileOffset = raw - projectBase;
        return true;
    }
    return false;
}

// Schema-750 stores its relocation byte stream after the 0x98728833 sentinel
// in the outer KP file. This is native code, not a discovered marker:
// LoadMapPackIntoImportModel at 0x7ff6e29401d2 calls
// CheckSerializedSentinelConstant(0x98728833) immediately before consuming
// the relocation vectors. Each map contributes its value bytes, followed by
// its X-axis bytes and then its Y-axis bytes for genuine two-dimensional
// maps. Preserve offsets on parsed maps so the caller can apply selected data
// without reconstructing these ranges from visible values.
bool attachSchema750CarriedData(const QByteArray &fileData,
                                QVector<MapInfo> *maps,
                                QByteArray *carriedData,
                                QStringList *warnings)
{
    if (!maps || !carriedData || maps->isEmpty()) return false;

    struct Span { int value = 0, x = 0, y = 0; };
    QVector<Span> spans;
    spans.reserve(maps->size());
    qsizetype total = 0;
    for (const MapInfo &map : *maps) {
        Span span;
        // WinOLS retains a cell-sized payload for the one zero-range scalar
        // in the reference pack. Its display address remains usable even
        // though the serialized end address equals the start address.
        span.value = map.rawAddress == 0 ? 0 : qMax(0, map.length);
        if (map.xAxis.hasPtsAddress)
            span.x = qMax(0, map.dimensions.x) * qMax(0, map.xAxis.ptsDataSize);
        // A kind-4 record with one row still serializes a Y descriptor, but
        // WinOLS omits that singleton from the packed relocation vector.
        if (map.yAxis.hasPtsAddress && map.dimensions.y > 1)
            span.y = qMax(0, map.dimensions.y) * qMax(0, map.yAxis.ptsDataSize);
        total += qsizetype(span.value) + span.x + span.y;
        spans.append(span);
    }
    if (total <= 0 || total > fileData.size()) return false;

    static const QByteArray sentinel = QByteArray::fromHex("33887298");
    qsizetype payloadStart = -1;
    for (qsizetype at = fileData.indexOf(sentinel); at >= 0;
         at = fileData.indexOf(sentinel, at + 1)) {
        const qsizetype candidate = at + sentinel.size();
        // The validated schema-750 payload is the complete trailing block.
        // Require this exact boundary so an accidental sentinel never results
        // in writes to the target project.
        if (candidate + total == fileData.size()) {
            payloadStart = candidate;
            break;
        }
    }
    if (payloadStart < 0) {
        if (warnings) warnings->append(KpImporter::tr(
            "Schema-750 map-value payload was not found; importing structure only"));
        return false;
    }

    *carriedData = fileData.mid(payloadStart, total);
    auto chooseNativeAxisSignedness = [&](AxisInfo &axis, int count, qsizetype source) {
        if (!axis.hasPtsAddress || count < 2
            || (axis.ptsDataSize != 1 && axis.ptsDataSize != 2
                && axis.ptsDataSize != 4)
            || source < 0
            || source + qsizetype(count) * axis.ptsDataSize > carriedData->size())
            return;

        const auto *bytes = reinterpret_cast<const uchar *>(carriedData->constData());
        auto readValue = [&](int index, bool isSigned) -> double {
            const auto *p = bytes + source + index * axis.ptsDataSize;
            qint64 raw = 0;
            switch (axis.ptsDataSize) {
            case 1: raw = isSigned ? qint64(int8_t(p[0])) : qint64(uint8_t(p[0])); break;
            case 2: {
                const uint16_t v = qFromLittleEndian<uint16_t>(p);
                raw = isSigned ? qint64(int16_t(v)) : qint64(v);
                break;
            }
            case 4: {
                const uint32_t v = qFromLittleEndian<uint32_t>(p);
                raw = isSigned ? qint64(int32_t(v)) : qint64(v);
                break;
            }
            }
            return axis.hasScaling ? raw * axis.scaling.linA + axis.scaling.linB
                                   : double(raw);
        };

        // KpMapObjectCodec calls FUN_7ff6e2427d24 after deserialization. It
        // evaluates both interpretations, sums abs(int(delta)) across each
        // adjacent pair, and selects one only when its total is less than half
        // the other's. Preserve the serialized flag for a tie.
        qint64 variation[2] = {0, 0};
        for (int signedCandidate = 0; signedCandidate <= 1; ++signedCandidate) {
            for (int i = 0; i + 1 < count; ++i) {
                const double delta = readValue(i, signedCandidate)
                    - readValue(i + 1, signedCandidate);
                // Native code truncates the floating-point delta to a 32-bit
                // int before taking its absolute value.
                const int truncatedDelta = int(delta);
                variation[signedCandidate] += std::llabs(qint64(truncatedDelta));
            }
        }
        if (variation[0] * 2 < variation[1])
            axis.ptsSigned = false;
        else if (variation[1] * 2 < variation[0])
            axis.ptsSigned = true;
    };
    qsizetype offset = 0;
    for (int i = 0; i < maps->size(); ++i) {
        MapInfo &map = (*maps)[i];
        const Span &span = spans[i];
        map.setSideProp(QStringLiteral("kpValueOffset"), uint(offset));
        map.setSideProp(QStringLiteral("kpValueLength"), span.value);
        offset += span.value;
        map.setSideProp(QStringLiteral("kpXAxisOffset"), uint(offset));
        map.setSideProp(QStringLiteral("kpXAxisLength"), span.x);
        chooseNativeAxisSignedness(map.xAxis, map.dimensions.x, offset);
        offset += span.x;
        map.setSideProp(QStringLiteral("kpYAxisOffset"), uint(offset));
        map.setSideProp(QStringLiteral("kpYAxisLength"), span.y);
        chooseNativeAxisSignedness(map.yAxis, map.dimensions.y, offset);
        offset += span.y;
    }
    return true;
}

// ── Schema-750 axis sub-blocks ────────────────────────────────────────────
// After the main address triplet, kind-3 records carry one axis sub-block
// (X) and kind-4/5 records carry two (X = columns first, then Y = rows).
// Each sub-block is, in order:
//   [u32 len][axis name]            (NOT NUL-terminated in all files)
//   ... padding/flags ...
//   [u32 len][unit]                 (optional; absent on some axes)
//   [double factor][double offset]  (offset pair only when offset != 0)
//   [u32 0|1][u32 axisAddr][u32 1|3][u32 dataSize][u32 0x0A]   <- anchor
//   ... [u32 len][axis id slug][NUL]
// Validated against the issue-#32 TunerPro XDF: 61/61 axis addresses and
// element sizes, 79/79 scale factors once the factor/offset pair rule is
// applied.
struct Kp750Axis {
    uint32_t rawAddr = 0;
    uint32_t dataType = 0;
    int      dataSize = 2;
    int      precision = -1;
    QString  name;
    QString  unit;
    double   factor = 0.0;
    double   offset = 0.0;
    bool     hasFactor = false;
    bool     pointsSigned = false;
};

// Schema-750 uses a signed 32-bit string marker. Non-negative markers carry
// exactly that many CP-1252 bytes; negative values are serialized references
// or absent values and carry no inline bytes. This is the native
// DecodeOrEncodeStringField contract, not a printable-text scan.
struct Kp750SerializedString {
    qsizetype start = 0;
    qsizetype end = 0;
    int32_t marker = 0;
    QString text;
};

bool readSchema750String(const QByteArray &data, qsizetype pos, qsizetype limit,
                         Kp750SerializedString *out)
{
    if (!out || pos < 0 || pos + 4 > limit || limit > data.size())
        return false;
    Kp750SerializedString value;
    value.start = pos;
    value.marker = peekI32(data, pos);
    value.end = pos + 4;
    if (value.marker >= 0) {
        // The native string codec uses the signed marker verbatim.  Bound it
        // only by the enclosing serialized object, not by a guessed text
        // length, so valid long labels and identifiers remain representable.
        if (value.end + qsizetype(value.marker) > limit)
            return false;
        value.text = decodeKpText(data.mid(int(value.end), value.marker));
        value.end += value.marker;
    }
    *out = value;
    return true;
}

// This is the exact schema-750 axis stream framed by KpAxisDescriptorCodec.
// The fixed 20-byte anchor is followed by a 31-byte descriptor tail, then a
// serialized identifier, an inline-record count/reserved pair, and 16-byte
// records. The parser returns raw inline data as well as map-facing fields so
// no boundaries are recovered by searching for text or plausible doubles.
struct Kp750AxisBlock {
    Kp750Axis descriptor;
    Kp750SerializedString identifier;
    uint32_t inlineCount = 0;
    QByteArray inlineData;
    qsizetype nextAxis = 0;
    qsizetype tailStart = 0;
};

bool parseSchema750AxisBlock(const QByteArray &data, qsizetype start,
                             qsizetype limit, Kp750AxisBlock *out)
{
    if (!out || start < 0 || limit > data.size())
        return false;

    Kp750SerializedString name, unit, identifier;
    if (!readSchema750String(data, start, limit, &name))
        return false;
    const qsizetype padding = name.end;
    if (padding + 12 > limit
        || data.mid(int(padding), 12) != QByteArray(12, '\0'))
        return false;
    if (!readSchema750String(data, padding + 12, limit, &unit))
        return false;

    const qsizetype factorPos = unit.end;
    const qsizetype anchor = factorPos + 16;
    if (anchor + 51 > limit)
        return false;
    const uint32_t prefix = peekU32(data, anchor);
    const uint32_t address = peekU32(data, anchor + 4);
    const uint32_t type = peekU32(data, anchor + 8);
    const uint32_t size = peekU32(data, anchor + 12);
    const uint32_t marker = peekU32(data, anchor + 16);
    if ((prefix != 0 && prefix != 1 && prefix != 8)
        || type < 1 || type > 8
        || (size != 1 && size != 2 && size != 4)
        || marker != 10)
        return false;
    if (!readSchema750String(data, anchor + 51, limit, &identifier))
        return false;
    if (identifier.end + 8 > limit)
        return false;
    const uint32_t inlineCount = peekU32(data, identifier.end);
    const qsizetype inlineStart = identifier.end + 8;
    if (inlineCount > uint32_t((limit - inlineStart) / 16))
        return false;
    const qsizetype inlineEnd = inlineStart + qsizetype(inlineCount) * 16;
    if (inlineEnd > limit)
        return false;

    Kp750AxisBlock block;
    block.descriptor.rawAddr = address;
    block.descriptor.dataType = type;
    block.descriptor.dataSize = int(size);
    block.descriptor.name = name.text;
    block.descriptor.unit = unit.text;
    block.descriptor.factor = peekF64(data, factorPos);
    block.descriptor.offset = peekF64(data, factorPos + 8);
    block.descriptor.hasFactor = true;
    // These two fields are accessed by KpAxisDescriptorCodec at fixed offsets
    // within the 31-byte post-anchor descriptor tail.
    block.descriptor.precision = int(peekU32(data, anchor + 34));
    block.descriptor.pointsSigned = data.at(int(anchor + 46)) != 0;
    block.identifier = identifier;
    block.inlineCount = inlineCount;
    block.inlineData = data.mid(int(inlineStart), int(inlineEnd - inlineStart));
    block.nextAxis = inlineEnd;
    // Six bytes after inline records precede the duplicated dimension pair.
    block.tailStart = inlineEnd + 6;
    if (block.tailStart > limit)
        return false;
    *out = block;
    return true;
}

// ── Folder table (schema-750) ─────────────────────────────────────────────
// WinOLS stores the project's folder tree in the trailing metadata after the
// embedded ZIP, not in the `intern` stream. Each schema-750 map record carries
// a folder id at metadata +0x1F; resolving it against this table lets maps that
// share a display name land in their own (sub)folder instead of collapsing
// into an ambiguous flat list. Format per the community RE notes: a double
// 0x98638811 marker, a u32 folder count, then one entry each:
//   u32 id · u32 parentId · u32 nameLen · char[nameLen] name · <fixed suffix>
// The suffix size is variant-dependent (~46–47 bytes), so rather than trust a
// single byte count we deterministically read each entry header and re-sync to
// the next valid [id][parent][nameLen][text] header within a bounded window.
struct KpFolder {
    uint32_t parentId = 0;
    QString  name;
};

QHash<uint32_t, KpFolder> parseKpFolderTable(const QByteArray &fileData,
                                             qsizetype archiveEnd,
                                             uint32_t formatVersion,
                                             QVector<KpFolderRecord> *records,
                                             QStringList *warnings)
{
    QHash<uint32_t, KpFolder> folders;
    if (formatVersion != 750 || archiveEnd < 0 || archiveEnd + 16 > fileData.size())
        return folders;
    const uint32_t marker1 = peekU32(fileData, archiveEnd);
    const uint32_t reserved = peekU32(fileData, archiveEnd + 4);
    const uint32_t marker2 = peekU32(fileData, archiveEnd + 8);
    const uint32_t count = peekU32(fileData, archiveEnd + 12);
    // A schema-750 folder record has at least 58 bytes after the table's
    // count (12-byte header and a 46-byte version-gated suffix). This derives
    // the only count bound from the stream itself rather than an invented
    // maximum number of folders.
    if (marker1 != marker2 || reserved != 0
        || count > uint32_t((fileData.size() - (archiveEnd + 16)) / 58))
        return folders;

    qsizetype cursor = archiveEnd + 16;
    for (uint32_t index = 0; index < count; ++index) {
        const qsizetype recordStart = cursor;
        if (cursor + 12 > fileData.size()) {
            folders.clear();
            break;
        }
        const uint32_t id = peekU32(fileData, cursor);
        const uint32_t parent = peekU32(fileData, cursor + 4);
        const uint32_t nameLength = peekU32(fileData, cursor + 8);
        cursor += 12;
        if (cursor + qsizetype(nameLength) > fileData.size()) {
            folders.clear();
            break;
        }
        const QString name = decodeKpText(fileData.mid(int(cursor), int(nameLength)));
        cursor += nameLength;

        // SerializeMapPackFolderRecord at schema 750: a 13-byte zero prefix,
        // a byte-vector, v131 fields, v291/v302 fields, then the v750 u32.
        if (cursor + 17 > fileData.size()
            || fileData.mid(int(cursor), 13) != QByteArray(13, '\0')) {
            folders.clear();
            break;
        }
        cursor += 13;
        const uint32_t variantLength = peekU32(fileData, cursor);
        cursor += 4;
        if (cursor + qsizetype(variantLength) > fileData.size()) {
            folders.clear();
            break;
        }
        cursor += variantLength;
        if (cursor + 14 > fileData.size()) {
            folders.clear();
            break;
        }
        const bool flag131 = fileData.at(int(cursor)) != 0;
        Q_UNUSED(flag131);
        cursor += 10; // bool, enum32, enum32, u8
        const int32_t auxiliaryStringLength = peekI32(fileData, cursor);
        cursor += 4;
        if (auxiliaryStringLength >= 0) {
            if (cursor + qsizetype(auxiliaryStringLength) > fileData.size()) {
                folders.clear();
                break;
            }
            cursor += auxiliaryStringLength;
        }
        if (cursor + 15 > fileData.size()) { // v291 bool + v302 fields + v750 u32
            folders.clear();
            break;
        }
        const bool flag291 = fileData.at(int(cursor)) != 0;
        const bool flag302a = fileData.at(int(cursor + 1)) != 0;
        const bool flag302b = fileData.at(int(cursor + 2)) != 0;
        const int32_t value302a = peekI32(fileData, cursor + 3);
        const int32_t value302b = peekI32(fileData, cursor + 7);
        const int32_t value750 = peekI32(fileData, cursor + 11);
        cursor += 15;
        folders.insert(id, { parent, name });
        if (records) {
            KpFolderRecord record;
            record.id = id;
            record.parentId = parent;
            record.name = name;
            record.byteVector = fileData.mid(int(recordStart + 12 + nameLength + 17),
                                             int(variantLength));
            record.auxiliaryStringMarker = auxiliaryStringLength;
            if (auxiliaryStringLength >= 0) {
                const qsizetype auxiliaryStart = recordStart + 12 + nameLength + 17
                    + variantLength + 10 + 4;
                record.auxiliaryString = decodeKpText(fileData.mid(
                    int(auxiliaryStart), auxiliaryStringLength));
            }
            record.flag291 = flag291;
            record.flag302a = flag302a;
            record.flag302b = flag302b;
            record.value302a = value302a;
            record.value302b = value302b;
            record.value750 = value750;
            record.serializedRecord = fileData.mid(int(recordStart),
                                                   int(cursor - recordStart));
            records->append(record);
        }
    }
    if (folders.size() != int(count) && warnings)
        warnings->append(KpImporter::tr("schema-750 folder table is incomplete"));
    if (folders.size() != int(count) && records)
        records->clear();
    return folders;
}

// Resolve each folder id to a "/"-delimited path (no leading slash), matching
// the convention MapInfo::folderPath uses for A2L groups and XDF categories.
QHash<uint32_t, QString> resolveKpFolderPaths(
    const QHash<uint32_t, KpFolder> &folders)
{
    QHash<uint32_t, QString> paths;
    for (auto it = folders.cbegin(); it != folders.cend(); ++it) {
        QStringList parts;
        uint32_t cur = it.key();
        for (int depth = 0; depth < 64 && folders.contains(cur); ++depth) {
            const KpFolder &f = folders.value(cur);
            // A folder whose parent is not itself a folder is a WinOLS system
            // root ("My maps" / "Hexdump"); treat it as the tree root and don't
            // emit its name, so real folders sit at the top level.
            if (f.parentId == cur || !folders.contains(f.parentId)) break;
            if (!f.name.isEmpty()) parts.prepend(f.name);
            cur = f.parentId;
        }
        paths.insert(it.key(), parts.join(QLatin1Char('/')));
    }
    return paths;
}

// ── Deterministic schema-750 (OLS 5.x) object walk ─────────────────────────
// The .kp intern grammar is self-describing: every object begins with the
// fixed marker  00 FF FF FF FF  + 17 zero bytes, and every variable field
// carries its own length/sentinel. So instead of scanning for record starts
// and pattern-matching an address triplet (the fragile heuristic below), we
// delimit objects by the marker and read each field from its exact grammar
// offset — name, folder id, kind, element size, the stored [cols][rows], and
// the map address (reached by walking the identifier + unit strings and the
// factor/offset doubles). Confirmed against the Cayenne/Civic corpus: the map
// data start/end and dimensions match the TunerPro XDF for every addressable
// object (mapEnd-mapStart == cols*rows*elem holds for 331/332; the one
// exception carries no address in the file, exactly as WinOLS reports).
QVector<MapInfo> parseSchema750Deterministic(
    const QByteArray &payload, uint32_t baseAddress, uint32_t romSize,
    const QHash<uint32_t, QString> &folderPaths, QStringList *warnings)
{
    QVector<MapInfo> maps;
    const qsizetype sz = payload.size();
    if (sz < 5 || payload.at(0) != '\0')
        return maps;
    const uint32_t mapCount = peekU32(payload, 1);
    // Every object consumes at least its fixed prefix and display-name length
    // field, so this is a structural bound rather than a policy limit.
    if (mapCount == 0 || mapCount > uint32_t((sz - 5) / 27))
        return maps;

    static const QByteArray objectPrefix =
        QByteArray::fromHex("00FFFFFFFF") + QByteArray(17, '\0');
    auto fail = [&](uint32_t index, const QString &reason) {
        if (warnings) {
            warnings->append(KpImporter::tr(
                "schema-750 object %1: %2").arg(index).arg(reason));
        }
        maps.clear();
    };

    qsizetype cursor = 5;
    maps.reserve(int(mapCount));
    for (uint32_t index = 0; index < mapCount; ++index) {
        const qsizetype objectStart = cursor;
        if (cursor + 27 > sz || payload.mid(int(cursor), objectPrefix.size()) != objectPrefix) {
            fail(index, QStringLiteral("invalid object prefix"));
            return maps;
        }
        const uint32_t nameLength = peekU32(payload, cursor + 22);
        const qsizetype nameStart = cursor + 26;
        const qsizetype nameEnd = nameStart + nameLength;
        if (nameEnd >= sz
            || payload.at(int(nameEnd)) != '\0') {
            fail(index, QStringLiteral("invalid display name"));
            return maps;
        }
        const QString name = decodeKpText(payload.mid(int(nameStart), int(nameLength)));
        const qsizetype meta = nameEnd + 1;
        if (meta + 40 > sz || payload.mid(int(meta), 11) != QByteArray(11, '\0')) {
            fail(index, QStringLiteral("invalid metadata prefix"));
            return maps;
        }
        const uint32_t kind = peekU32(payload, meta + 11);
        const uint32_t constant2 = peekU32(payload, meta + 15);
        const uint32_t subtype = peekU32(payload, meta + 19);
        const uint32_t elementSize = peekU32(payload, meta + 23);
        const uint32_t recordType = peekU32(payload, meta + 27);
        const uint32_t folderId = peekU32(payload, meta + 31);
        const uint32_t idLength = peekU32(payload, meta + 35);
        const qsizetype idStart = meta + 39;
        const qsizetype idEnd = idStart + idLength;
        if ((kind != 2 && kind != 3 && kind != 4) || constant2 != 2
            || (subtype != 1 && subtype != 3)
            || (elementSize != 1 && elementSize != 2)
            || recordType != 10
            || idEnd >= sz || payload.at(int(idEnd)) != '\0') {
            fail(index, QStringLiteral("unsupported schema-750 metadata record"));
            return maps;
        }
        const QString technicalId = decodeKpText(payload.mid(int(idStart), int(idLength)));
        const qsizetype fixed = idEnd + 1;
        if (fixed + 136 > sz) {
            fail(index, QStringLiteral("truncated fixed map descriptor"));
            return maps;
        }
        const uint32_t columns = peekU32(payload, fixed + 116);
        const uint32_t rows = peekU32(payload, fixed + 120);
        if (columns == 0 || columns > 999 || rows == 0 || rows > 999) {
            fail(index, QStringLiteral("invalid dimensions"));
            return maps;
        }

        Kp750SerializedString identifier, unit;
        if (!readSchema750String(payload, fixed + 136, sz, &identifier)
            || !readSchema750String(payload, identifier.end, sz, &unit)) {
            fail(index, QStringLiteral("invalid map string field"));
            return maps;
        }
        const qsizetype values = unit.end;
        const qsizetype addressField = values + 16;
        const qsizetype axisXStart = addressField + 40;
        if (axisXStart > sz) {
            fail(index, QStringLiteral("truncated map value block"));
            return maps;
        }
        const double factor = peekF64(payload, values);
        const double offset = peekF64(payload, values + 8);
        const uint32_t mapStart = peekU32(payload, addressField);
        const uint32_t mapEnd = peekU32(payload, addressField + 4);
        const uint32_t mapBase = peekU32(payload, addressField + 8);
        Kp750AxisBlock axisX, axisY;
        if (!parseSchema750AxisBlock(payload, axisXStart, sz, &axisX)
            || !parseSchema750AxisBlock(payload, axisX.nextAxis, sz, &axisY)) {
            fail(index, QStringLiteral("invalid axis descriptor"));
            return maps;
        }
        const qsizetype tailStart = axisY.tailStart;
        const qsizetype objectEnd = tailStart + 142;
        if (objectEnd > sz
            || peekU32(payload, tailStart) != columns
            || peekU32(payload, tailStart + 4) != rows) {
            fail(index, QStringLiteral("invalid fixed object tail"));
            return maps;
        }

        const int dx = kind == 2 ? 1 : int(columns);
        const int dy = kind == 3 ? 1 : (kind == 2 ? 1 : int(rows));
        const uint64_t logicalLength = uint64_t(dx) * uint64_t(dy) * elementSize;
        const bool positiveSpan = mapEnd > mapStart
            && uint64_t(mapEnd - mapStart) == logicalLength;
        const bool zeroSpan = mapStart != 0 && mapEnd == mapStart;
        if (!positiveSpan && !zeroSpan) {
            fail(index, QStringLiteral("map range does not match descriptor"));
            return maps;
        }

        MapInfo map;
        map.name = name;
        map.description = name;
        map.type = typeFromKpKind(kind, dx, dy);
        map.dataSize = int(elementSize);
        map.dimensions = { dx, dy };
        map.linkConfidence = 100;
        map.columnMajor = false;
        map.olsUniversalBase = mapBase;
        map.dataSigned = payload.at(int(fixed + 113)) != 0;
        map.length = positiveSpan ? int(mapEnd - mapStart) : int(logicalLength);
        map.setSideProp(QStringLiteral("kpSchemaVersion"), 750);
        map.setSideProp(QStringLiteral("kpKind"), kind);
        map.setSideProp(QStringLiteral("kpSubtype"), subtype);
        map.setSideProp(QStringLiteral("kpRecordType"), recordType);
        map.setSideProp(QStringLiteral("kpTechnicalId"), technicalId);
        map.setSideProp(QStringLiteral("kpMapIdentifier"), identifier.text);
        map.setSideProp(QStringLiteral("kpUnit"), unit.text);
        map.setSideProp(QStringLiteral("kpMapFactor"), factor);
        map.setSideProp(QStringLiteral("kpMapOffset"), offset);
        map.setSideProp(QStringLiteral("kpMapPrecisionRaw"), peekU32(payload, fixed + 132));
        map.setSideProp(QStringLiteral("kpSerializedTail"),
                        payload.mid(int(tailStart), int(objectEnd - tailStart)));
        map.setSideProp(QStringLiteral("kpSerializedRecord"),
                        payload.mid(int(objectStart), int(objectEnd - objectStart)));
        if (!technicalId.isEmpty())
            map.setSideProp(QStringLiteral("kpIdName"), technicalId);
        if (!folderPaths.isEmpty())
            map.folderPath = folderPaths.value(folderId);

        if (std::isfinite(factor) && std::isfinite(offset)) {
            map.scaling.type = CompuMethod::Type::Linear;
            map.scaling.linA = factor;
            map.scaling.linB = offset;
            map.hasScaling = (factor != 1.0 || offset != 0.0);
            const uint32_t precision = peekU32(payload, fixed + 132);
            if (precision <= 6)
                map.scaling.format = QStringLiteral("1.%1f").arg(precision);
        }

        uint32_t fileOffset = 0;
        if (positiveSpan && normalizeKpAddress(mapStart, mapEnd, mapBase,
                                                baseAddress, romSize, &fileOffset)) {
            map.rawAddress = (baseAddress != 0 && fileOffset == mapStart)
                ? baseAddress + fileOffset : mapStart;
            map.address = fileOffset;
        } else if (zeroSpan && normalizeKpAxisAddress(mapStart, dx * dy,
                                                        int(elementSize), baseAddress,
                                                        romSize, &fileOffset)) {
            map.rawAddress = (baseAddress != 0 && fileOffset == mapStart)
                ? baseAddress + fileOffset : mapStart;
            map.address = fileOffset;
        }

        auto fillAxis = [&](AxisInfo &destination, const Kp750AxisBlock &source,
                            int pointCount, const QString &prefix) {
            const Kp750Axis &axis = source.descriptor;
            destination.inputName = axis.unit.isEmpty()
                ? axis.name : QStringLiteral("%1 [%2]").arg(axis.name, axis.unit);
            destination.scaling.type = CompuMethod::Type::Linear;
            destination.scaling.linA = axis.factor;
            destination.scaling.linB = axis.offset;
            destination.hasScaling = std::isfinite(axis.factor)
                && std::isfinite(axis.offset)
                && (axis.factor != 1.0 || axis.offset != 0.0);
            if (axis.precision >= 0 && axis.precision <= 6)
                destination.scaling.format = QStringLiteral("1.%1f").arg(axis.precision);
            destination.ptsDataSize = axis.dataSize;
            destination.ptsDataType = axis.dataType;
            destination.ptsBigEndian = false;
            destination.ptsSigned = axis.pointsSigned;
            destination.ptsCount = pointCount;
            map.setSideProp(prefix + QStringLiteral("Identifier"), source.identifier.text);
            map.setSideProp(prefix + QStringLiteral("InlineData"), source.inlineData);
            map.setSideProp(prefix + QStringLiteral("InlineCount"), source.inlineCount);
            map.setSideProp(prefix + QStringLiteral("PrecisionRaw"), axis.precision);
            uint32_t pointOffset = 0;
            if (axis.rawAddr != 0
                && normalizeKpAxisAddress(axis.rawAddr, pointCount, axis.dataSize,
                                          baseAddress, romSize, &pointOffset)) {
                destination.ptsAddress = pointOffset;
                destination.hasPtsAddress = true;
            }
        };
        if (kind != 2)
            fillAxis(map.xAxis, axisX, dx, QStringLiteral("kpXAxis"));
        if (kind == 4)
            fillAxis(map.yAxis, axisY, dy, QStringLiteral("kpYAxis"));

        maps.append(map);
        cursor = objectEnd;
    }
    if (cursor != sz) {
        if (warnings) {
            warnings->append(KpImporter::tr(
                "schema-750 objects end at 0x%1, intern ends at 0x%2")
                    .arg(cursor, 0, 16).arg(sz, 0, 16));
        }
        maps.clear();
    }
    return maps;
}

} // namespace


bool KpImporter::extractInternEntry(const QByteArray &fileData,
                                     QByteArray &compressed,
                                     uint32_t &uncompressedSize,
                                     uint16_t &method,
                                     uint32_t &expectedCrc,
                                     qsizetype &archiveStart,
                                     qsizetype &archiveEnd,
                                     QString &err)
{
    static const char pkSig[] = { 'P', 'K', '\x03', '\x04' };
    static const char eocdSig[] = { 'P', 'K', '\x05', '\x06' };
    for (qsizetype localHeader = 0; localHeader + 30 <= fileData.size(); ) {
        const int found = fileData.indexOf(QByteArray(pkSig, 4), localHeader);
        if (found < 0)
            break;
        localHeader = found;
        const auto *h = reinterpret_cast<const uchar *>(fileData.constData() + localHeader);
        const uint16_t entryMethod = qFromLittleEndian<uint16_t>(h + 8);
        const uint32_t entryCrc = qFromLittleEndian<uint32_t>(h + 14);
        const uint32_t csize = qFromLittleEndian<uint32_t>(h + 18);
        const uint32_t usize = qFromLittleEndian<uint32_t>(h + 22);
        const uint16_t fnLen = qFromLittleEndian<uint16_t>(h + 26);
        const uint16_t extraLen = qFromLittleEndian<uint16_t>(h + 28);
        const qsizetype fnStart = localHeader + 30;
        const qsizetype dataStart = fnStart + fnLen + extraLen;
        const qsizetype dataEnd = dataStart + qsizetype(csize);
        if (fnStart + fnLen > fileData.size() || dataEnd > fileData.size()) {
            ++localHeader;
            continue;
        }
        if (QString::fromLatin1(fileData.constData() + fnStart, fnLen)
            != QStringLiteral("intern")) {
            ++localHeader;
            continue;
        }

        // Validate the embedded ZIP through its EOCD and central-directory
        // relation. This identifies the folder-table boundary exactly instead
        // of finding a later sentinel that merely looks like one.
        for (qsizetype eocd = dataEnd; eocd + 22 <= fileData.size(); ) {
            const int eocdFound = fileData.indexOf(QByteArray(eocdSig, 4), eocd);
            if (eocdFound < 0)
                break;
            eocd = eocdFound;
            const uint16_t commentLength = qFromLittleEndian<uint16_t>(
                reinterpret_cast<const uchar *>(fileData.constData() + eocd + 20));
            const qsizetype end = eocd + 22 + commentLength;
            if (end <= fileData.size()) {
                const uint32_t directorySize = peekU32(fileData, eocd + 12);
                const uint32_t directoryOffset = peekU32(fileData, eocd + 16);
                if (localHeader + qsizetype(directoryOffset) + directorySize == eocd) {
                    compressed = fileData.mid(int(dataStart), int(csize));
                    uncompressedSize = usize;
                    method = entryMethod;
                    expectedCrc = entryCrc;
                    archiveStart = localHeader;
                    archiveEnd = end;
                    return true;
                }
            }
            ++eocd;
        }
        ++localHeader;
    }
    err = KpImporter::tr("No validated embedded ZIP entry named 'intern' found");
    return false;
}


KpImportResult KpImporter::importFromBytes(const QByteArray &fileData,
                                            uint32_t baseAddress,
                                            uint32_t romSize)
{
    KpImportResult result;

    if (fileData.size() < 24) {
        result.error = KpImporter::tr("File too small for KP header (%1 bytes)")
                           .arg(fileData.size());
        return result;
    }

    static const char magic[] = "WinOLS File";
    uint32_t magicLen = qFromLittleEndian<uint32_t>(
        reinterpret_cast<const uchar *>(fileData.constData()));
    if (magicLen != 11
        || std::memcmp(fileData.constData() + 4, magic, 11) != 0) {
        result.error = KpImporter::tr("Invalid OLS magic header");
        return result;
    }

    result.formatVersion = qFromLittleEndian<uint32_t>(
        reinterpret_cast<const uchar *>(fileData.constData() + 16));
    result.declaredFileSize = qFromLittleEndian<uint32_t>(
        reinterpret_cast<const uchar *>(fileData.constData() + 20));

    if (result.declaredFileSize != static_cast<uint32_t>(fileData.size())) {
        result.warnings.append(
            KpImporter::tr("Header declares file size %1 but actual is %2")
                .arg(result.declaredFileSize)
                .arg(fileData.size()));
    }

    QByteArray compressed;
    uint32_t uncompressedSize = 0;
    uint16_t method = 0;
    uint32_t expectedCrc = 0;
    qsizetype archiveStart = 0;
    qsizetype archiveEnd = 0;
    QString extractErr;

    if (!extractInternEntry(fileData, compressed, uncompressedSize,
                            method, expectedCrc, archiveStart, archiveEnd, extractErr)) {
        result.error = extractErr;
        return result;
    }

    result.outerEnvelope = fileData.left(int(archiveStart));
    result.trailingMetadata = fileData.mid(int(archiveEnd));

    QByteArray intern;
    if (method == 8) {
        QString inflateErr;
        intern = ZipDecompressor::decompress(compressed,
                                             static_cast<qsizetype>(uncompressedSize),
                                             &inflateErr);
        if (intern.isEmpty()) {
            result.error = KpImporter::tr("Failed to inflate intern: %1")
                               .arg(inflateErr);
            return result;
        }
    } else if (method == 0) {
        intern = compressed;
    } else {
        result.error = KpImporter::tr("Unsupported ZIP compression method %1")
                           .arg(method);
        return result;
    }

    if (static_cast<uint32_t>(intern.size()) != uncompressedSize) {
        result.error = KpImporter::tr("Decompressed intern size %1 != declared %2")
                           .arg(intern.size()).arg(uncompressedSize);
        return result;
    }
    const uint32_t actualCrc = ::crc32(
        0L, reinterpret_cast<const Bytef *>(intern.constData()), uInt(intern.size()));
    if (actualCrc != expectedCrc) {
        result.error = KpImporter::tr("Intern CRC-32 0x%1 != declared 0x%2")
                           .arg(actualCrc, 8, 16, QLatin1Char('0'))
                           .arg(expectedCrc, 8, 16, QLatin1Char('0'));
        return result;
    }

    // Folder tree lives in the trailing metadata (after the ZIP), not in
    // `intern` — parse it so maps can be grouped like WinOLS shows them.
    const QHash<uint32_t, KpFolder> folders =
        parseKpFolderTable(fileData, archiveEnd, result.formatVersion,
                           &result.folders, &result.warnings);
    const QHash<uint32_t, QString> folderPaths = resolveKpFolderPaths(folders);

    // Select a codec by its exact native serialization version. Adjacent
    // WinOLS versions add gated fields, so treating an arbitrary newer stream
    // as schema 750 can move every following field while still producing
    // plausible-looking maps. A version without a traced codec is rejected;
    // it must never fall back to a scanner.
    const uint32_t internMapCount = intern.size() >= 5 ? peekU32(intern, 1) : 0;
    switch (result.formatVersion) {
    case 750:
        result.maps = parseSchema750Deterministic(intern, baseAddress, romSize,
                                                  folderPaths, &result.warnings);
        if (internMapCount > 0 && result.maps.size() != int(internMapCount)) {
            result.error = KpImporter::tr(
                "Schema-750 parser recognized %1 of %2 maps; refusing an "
                "incomplete import rather than guessing the remaining layout")
                .arg(result.maps.size()).arg(internMapCount);
            return result;
        }
        attachSchema750CarriedData(fileData, &result.maps, &result.carriedData,
                                   &result.warnings);
        break;
    default:
        result.error = KpImporter::tr(
            "Unsupported KP schema %1: no deterministic native codec is "
            "implemented for this version")
            .arg(result.formatVersion);
        return result;
    }
    result.mapCount = static_cast<uint32_t>(result.maps.size());

    return result;
}

}
