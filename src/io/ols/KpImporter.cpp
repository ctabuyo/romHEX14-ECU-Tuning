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
bool attachKpCarriedData(const QByteArray &fileData, QVector<MapInfo> *maps,
                         QByteArray *carriedData, QStringList *warnings,
                         bool zeroRangeUsesCell, bool includeSingletonYAxis)
{
    if (!maps || !carriedData || maps->isEmpty()) return false;

    struct Span { int value = 0, x = 0, y = 0; };
    QVector<Span> spans;
    spans.reserve(maps->size());
    qsizetype total = 0;
    for (const MapInfo &map : *maps) {
        Span span;
        const uint32_t mapStart = map.getSideProp(QStringLiteral("kpMapStart")).toUInt();
        const uint32_t mapEnd = map.getSideProp(QStringLiteral("kpMapEnd")).toUInt();
        const uint32_t kind = map.getSideProp(QStringLiteral("kpKind")).toUInt();
        span.value = mapEnd >= mapStart
            && uint64_t(mapEnd - mapStart) <= uint64_t(std::numeric_limits<int>::max())
            ? int(mapEnd - mapStart) : 0;
        // Schema-750's scalar zero range still occupies one cell in the
        // relocation vector. Earlier schema-597 uses its serialized span.
        if (zeroRangeUsesCell && span.value == 0 && mapStart != 0)
            span.value = qMax(0, map.length);
        if (kind >= 3 && map.getSideProp(QStringLiteral("kpXAxisRawAddress")).toUInt() != 0)
            span.x = qMax(0, map.dimensions.x) * qMax(0, map.xAxis.ptsDataSize);
        if (kind == 4 && map.getSideProp(QStringLiteral("kpYAxisRawAddress")).toUInt() != 0
            && (includeSingletonYAxis || map.dimensions.y > 1))
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
            "KP map-value payload was not found; importing structure only"));
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
    int32_t  wireRecordType = 10;
    int32_t  nativeRecordType = 10;
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

// Before gate 439 a positive simple-string marker is followed by that many
// CP-1252 bytes *and* an encoded NUL.  The NUL is part of the wire grammar,
// not a delimiter the importer is allowed to search for.  Zero and negative
// markers retain their native no-inline-bytes behavior.
bool readLegacyKpString(const QByteArray &data, qsizetype pos, qsizetype limit,
                        Kp750SerializedString *out)
{
    if (!out || pos < 0 || pos + 4 > limit || limit > data.size())
        return false;
    Kp750SerializedString value;
    value.start = pos;
    value.marker = peekI32(data, pos);
    value.end = pos + 4;
    if (value.marker > 0) {
        if (value.end + qsizetype(value.marker) + 1 > limit
            || data.at(int(value.end + value.marker)) != '\0')
            return false;
        value.text = decodeKpText(data.mid(int(value.end), value.marker));
        value.end += value.marker + 1;
    }
    *out = value;
    return true;
}

bool readKpStringForVersion(const QByteArray &data, qsizetype pos, qsizetype limit,
                            uint32_t schemaVersion, Kp750SerializedString *out)
{
    return schemaVersion < 439
        ? readLegacyKpString(data, pos, limit, out)
        : readSchema750String(data, pos, limit, out);
}

bool readKpStructuredStringForVersion(const QByteArray &data, qsizetype start,
                                      qsizetype limit, uint32_t schemaVersion,
                                      Kp750SerializedString *primary, qsizetype *end)
{
    if (!primary || !end) return false;
    if (schemaVersion < 330) {
        if (!readKpStringForVersion(data, start, limit, schemaVersion, primary)) return false;
        *end = primary->end;
        return true;
    }
    qsizetype cursor = 0;
    if (!readKpStringForVersion(data, start, limit, schemaVersion, primary)) return false;
    cursor = primary->end;
    // CFolderRef's schema-330 branch adds only its count.  Schema 345 then
    // adds the two scalar members and materializes each string/int32 entry.
    if (schemaVersion >= 345) {
        if (cursor + 8 > limit) return false;
        cursor += 8;
    }
    if (cursor + 4 > limit) return false;
    const int32_t count = peekI32(data, cursor);
    cursor += 4;
    if (count < 0 || qsizetype(count) > (limit - cursor) / 5) return false;
    if (schemaVersion < 345) {
        *end = cursor;
        return true;
    }
    for (int32_t index = 0; index < count; ++index) {
        Kp750SerializedString entry;
        if (!readKpStringForVersion(data, cursor, limit, schemaVersion, &entry)
            || entry.end + 4 > limit)
            return false;
        cursor = entry.end + 4;
    }
    *end = cursor;
    return true;
}

// Exact schema-750 wire reader for serializer calls whose only type
// information is the call-site/member offset in the WinOLS binary.  This is
// intentionally a sequential reader: callers may not search for a marker or
// recover after an invalid field, because that would hide a changed grammar.
class Kp750WireReader {
public:
    Kp750WireReader(const QByteArray &data, qsizetype start, qsizetype limit,
                    QVector<KpSchema750Field> *fields)
        : m_data(data), m_pos(start), m_limit(limit), m_fields(fields) {}

    qsizetype position() const { return m_pos; }
    bool good() const { return m_good; }
    QString lastPath() const { return m_lastPath; }

    bool string(const QString &path)
    {
        m_lastPath = path;
        const qsizetype start = m_pos;
        Kp750SerializedString value;
        if (!readSchema750String(m_data, m_pos, m_limit, &value))
            return fail();
        m_pos = value.end;
        add(path, KpSchema750Field::Type::String, start, m_pos,
            value.text, value.marker, 0, 0.0);
        return true;
    }

    // DecodeOrEncodeVersionedStructuredString (schema >= 345): a primary
    // simple string, the native +0x0c/+0x08 integers, and a vector of
    // simple-string/int32 entries.  The primary text is returned solely for
    // the MapInfo projection; all nested bytes remain represented in fields.
    bool structuredString(const QString &path, QString *primaryText = nullptr)
    {
        const auto member = [&path](uint32_t offset) {
            return path + QStringLiteral("+0x") + QString::number(offset, 16);
        };
        const int object = beginVector(path);
        const qsizetype primaryIndex = m_fields ? m_fields->size() : -1;
        if (!string(member(4))
            || !int32(member(0xc))
            || !int32(member(8)))
            return false;
        if (primaryText && m_fields && primaryIndex >= 0)
            *primaryText = (*m_fields)[primaryIndex].text;

        const QString vectorPath = member(0x10);
        const int vector = beginVector(vectorPath);
        int32_t count = 0;
        if (!this->count(vectorPath + QStringLiteral(".count"), &count))
            return false;
        for (int32_t index = 0; index < count; ++index) {
            const QString entry = vectorPath + QStringLiteral("[%1]").arg(index);
            if (!string(entry + QStringLiteral("+0x0"))
                || !int32(entry + QStringLiteral("+0x8")))
                return false;
        }
        endVector(vector);
        endVector(object);
        return true;
    }

    bool boolean(const QString &path)
    {
        m_lastPath = path;
        const qsizetype start = m_pos;
        if (!need(1)) return false;
        const uint8_t value = uint8_t(m_data.at(int(m_pos++)));
        add(path, KpSchema750Field::Type::Bool, start, m_pos,
            {}, 0, value, 0.0);
        return true;
    }

    bool int32(const QString &path)
    {
        m_lastPath = path;
        const qsizetype start = m_pos;
        if (!need(4)) return false;
        const int32_t value = peekI32(m_data, m_pos);
        m_pos += 4;
        add(path, KpSchema750Field::Type::Int32, start, m_pos,
            {}, value, uint32_t(value), 0.0);
        return true;
    }

    bool uint32(const QString &path, KpSchema750Field::Type type = KpSchema750Field::Type::UInt32)
    {
        m_lastPath = path;
        const qsizetype start = m_pos;
        if (!need(4)) return false;
        const uint32_t value = peekU32(m_data, m_pos);
        m_pos += 4;
        add(path, type, start, m_pos, {}, int32_t(value), value, 0.0);
        return true;
    }

    bool literalU32(uint32_t expected, const QString &path)
    {
        m_lastPath = path;
        const qsizetype start = m_pos;
        if (!need(4) || peekU32(m_data, m_pos) != expected)
            return fail();
        m_pos += 4;
        add(path, KpSchema750Field::Type::UInt32, start, m_pos,
            {}, int32_t(expected), expected, 0.0);
        return true;
    }

    bool float64(const QString &path)
    {
        m_lastPath = path;
        const qsizetype start = m_pos;
        if (!need(8)) return false;
        const double value = peekF64(m_data, m_pos);
        m_pos += 8;
        add(path, KpSchema750Field::Type::Float64, start, m_pos,
            {}, 0, 0, value);
        return true;
    }

    bool raw(const QString &path, qsizetype length)
    {
        m_lastPath = path;
        const qsizetype start = m_pos;
        if (length < 0 || !need(length)) return false;
        m_pos += length;
        add(path, KpSchema750Field::Type::RawBytes, start, m_pos,
            {}, 0, 0, 0.0);
        return true;
    }

    bool sentinel(uint32_t expected, const QString &path)
    {
        return literalU32(expected, path);
    }

    // `count` is always a signed Int32 in the native vector helpers.  The
    // lower-bound check follows from the count field itself (each entry must
    // consume at least one byte); it is not a format heuristic.
    bool count(const QString &path, int32_t *out)
    {
        m_lastPath = path;
        const qsizetype start = m_pos;
        if (!need(4)) return false;
        const int32_t value = peekI32(m_data, m_pos);
        m_pos += 4;
        add(path, KpSchema750Field::Type::Int32, start, m_pos,
            {}, value, uint32_t(value), 0.0);
        if (value < 0 || qsizetype(value) > m_limit - m_pos)
            return fail();
        if (out) *out = value;
        return true;
    }

    int beginVector(const QString &path)
    {
        m_lastPath = path;
        if (!m_fields) return -1;
        KpSchema750Field field;
        field.path = path;
        field.type = KpSchema750Field::Type::Vector;
        field.streamOffset = m_pos;
        field.signedValue = m_pos;
        m_fields->append(field);
        return m_fields->size() - 1;
    }

    void endVector(int index)
    {
        if (!m_fields || index < 0 || index >= m_fields->size()) return;
        KpSchema750Field &field = (*m_fields)[index];
        const qsizetype start = qsizetype(field.signedValue);
        field.serialized = m_data.mid(int(start), int(m_pos - start));
        field.signedValue = 0;
    }

private:
    bool need(qsizetype length)
    {
        if (length < 0 || m_pos < 0 || m_pos + length > m_limit)
            return fail();
        return true;
    }

    bool fail() { m_good = false; return false; }

    void add(const QString &path, KpSchema750Field::Type type,
             qsizetype start, qsizetype end, const QString &text,
             int64_t signedValue, uint64_t unsignedValue, double floatValue)
    {
        if (!m_fields) return;
        KpSchema750Field field;
        field.path = path;
        field.type = type;
        field.streamOffset = start;
        field.serialized = m_data.mid(int(start), int(end - start));
        field.text = text;
        field.signedValue = signedValue;
        field.unsignedValue = unsignedValue;
        field.floatValue = floatValue;
        m_fields->append(field);
    }

    const QByteArray &m_data;
    qsizetype m_pos = 0;
    qsizetype m_limit = 0;
    QVector<KpSchema750Field> *m_fields = nullptr;
    bool m_good = true;
    QString m_lastPath;
};

QString kp750Path(const QString &base, uint32_t offset)
{
    return base + QStringLiteral("+0x") + QString::number(offset, 16);
}

bool readKp750StringVector(Kp750WireReader &reader, const QString &path)
{
    const int vector = reader.beginVector(path);
    int32_t count = 0;
    if (!reader.count(path + QStringLiteral(".count"), &count)) return false;
    for (int32_t i = 0; i < count; ++i) {
        if (!reader.string(path + QStringLiteral("[%1]").arg(i))) return false;
    }
    reader.endVector(vector);
    return true;
}

bool readKp750Int32Vector(Kp750WireReader &reader, const QString &path)
{
    const int vector = reader.beginVector(path);
    int32_t count = 0;
    if (!reader.count(path + QStringLiteral(".count"), &count)) return false;
    for (int32_t i = 0; i < count; ++i) {
        if (!reader.int32(path + QStringLiteral("[%1]").arg(i))) return false;
    }
    reader.endVector(vector);
    return true;
}

bool readKp750ByteVector(Kp750WireReader &reader, const QString &path)
{
    const int vector = reader.beginVector(path);
    int32_t count = 0;
    if (!reader.count(path + QStringLiteral(".count"), &count)
        || !reader.raw(path + QStringLiteral(".bytes"), count))
        return false;
    reader.endVector(vector);
    return true;
}

bool readKp750Nested2c(Kp750WireReader &reader, const QString &path)
{
    const int object = reader.beginVector(path);
    if (!reader.boolean(kp750Path(path, 0))
        || !reader.raw(kp750Path(path, 2), 0x28)
        || !reader.raw(kp750Path(path, 0x2a), 0x20)
        || !reader.raw(kp750Path(path, 0x4a), 0x28)
        || !reader.raw(kp750Path(path, 0x72), 0x20))
        return false;
    // FUN_7ff6e2339b9c writes +0x94 only for its two non-import serializer
    // modes.  LoadMapPackIntoImportModel uses the input mode, so an importer
    // must not consume those four bytes from the KP stream.
    reader.endVector(object);
    return true;
}

bool readKp750Metadata2f8(Kp750WireReader &reader, const QString &path)
{
    const int object = reader.beginVector(path);
    if (!reader.string(kp750Path(path, 0x70))
        || !reader.uint32(kp750Path(path, 0), KpSchema750Field::Type::Enum32)
        || !reader.int32(kp750Path(path, 4))
        || !reader.int32(kp750Path(path, 0x14))
        || !reader.int32(kp750Path(path, 0x18)))
        return false;
    for (const uint32_t offset : {0x28u, 0x30u, 0x38u, 0x40u, 0x48u,
                                  0x50u, 0x58u, 0x60u, 0x68u, 0x70u, 0x78u}) {
        if (!reader.string(kp750Path(path, offset))) return false;
    }
    for (uint32_t offset = 0x90; offset <= 0xd0; offset += 4) {
        if (!reader.uint32(kp750Path(path, offset), KpSchema750Field::Type::Enum32))
            return false;
    }
    reader.endVector(object);
    return true;
}

bool readKp750MetadataC0(Kp750WireReader &reader, const QString &path)
{
    const int vector = reader.beginVector(path);
    int32_t count = 0;
    if (!reader.count(path + QStringLiteral(".count"), &count)) return false;
    for (int32_t i = 0; i < count; ++i) {
        const QString entry = path + QStringLiteral("[%1]").arg(i);
        const int object = reader.beginVector(entry);
        if (!reader.uint32(kp750Path(entry, 8), KpSchema750Field::Type::Enum32)
            || !reader.uint32(kp750Path(entry, 0xc), KpSchema750Field::Type::Enum32)
            || !reader.string(kp750Path(entry, 0x18))
            || !reader.int32(kp750Path(entry, 0x28))
            || !reader.uint32(kp750Path(entry, 4))
            || !reader.uint32(kp750Path(entry, 0x10), KpSchema750Field::Type::Enum32)
            || !readKp750Nested2c(reader, kp750Path(entry, 0x2c))
            || !reader.uint32(kp750Path(entry, 0))
            || !reader.uint32(kp750Path(entry, 0x14), KpSchema750Field::Type::Enum32)
            || !reader.string(kp750Path(entry, 0x20)))
            return false;
        reader.endVector(object);
    }
    reader.endVector(vector);
    return true;
}

bool readKp750Metadata110(Kp750WireReader &reader, const QString &path)
{
    const int vector = reader.beginVector(path);
    int32_t count = 0;
    if (!reader.count(path + QStringLiteral(".count"), &count)) return false;
    for (int32_t i = 0; i < count; ++i) {
        const QString entry = path + QStringLiteral("[%1]").arg(i);
        const int object = reader.beginVector(entry);
        if (!reader.int32(kp750Path(entry, 0x48))
            || !reader.uint32(kp750Path(entry, 0), KpSchema750Field::Type::Enum32)
            || !reader.raw(kp750Path(entry, 4), 0x40)
            || !reader.int32(kp750Path(entry, 0x44)))
            return false;
        reader.endVector(object);
    }
    reader.endVector(vector);
    return true;
}

bool readKp750ImportModelMetadata(const QByteArray &data, qsizetype start,
                                  qsizetype limit, KpSchema750Metadata *metadata,
                                  qsizetype *failedAt = nullptr,
                                  QString *failedPath = nullptr)
{
    if (!metadata || start < 0 || start >= limit || limit > data.size())
        return false;
    metadata->streamOffset = start;
    metadata->fields.clear();
    Kp750WireReader reader(data, start, limit, &metadata->fields);
    struct FailurePosition {
        Kp750WireReader &reader;
        qsizetype *out;
        QString *path;
        ~FailurePosition()
        {
            if (out) *out = reader.position();
            if (path) *path = reader.lastPath();
        }
    } failurePosition { reader, failedAt, failedPath };
    const QString model = QStringLiteral("importModel");
    for (const uint32_t offset : {0x40u, 0x48u, 0x58u, 0x68u, 0x78u, 0x80u,
                                  0x88u, 0x98u, 0xb8u, 0x140u, 0x148u, 0x150u,
                                  0x158u, 0x160u, 0x18u, 0x28u}) {
        if (!reader.string(kp750Path(model, offset))) {
            if (failedAt) *failedAt = reader.position();
            return false;
        }
    }
    if (!reader.uint32(kp750Path(model, 0x200), KpSchema750Field::Type::Enum32)
        || !reader.uint32(kp750Path(model, 0x208), KpSchema750Field::Type::Enum32)) {
        if (failedAt) *failedAt = reader.position();
        return false;
    }
    for (const uint32_t offset : {0x228u, 0x250u, 0x258u, 0x260u, 0x30u, 0x70u,
                                  0x170u, 0x178u}) {
        if (!reader.string(kp750Path(model, offset))) {
            if (failedAt) *failedAt = reader.position();
            return false;
        }
    }
    if (!readKp750Int32Vector(reader, kp750Path(model, 0x2a8))
        || !reader.uint32(kp750Path(model, 0x27c), KpSchema750Field::Type::Enum32)
        || !reader.int32(kp750Path(model, 0x234))
        || !reader.string(kp750Path(model, 0x3d0))
        || !reader.int32(kp750Path(model, 0x180))
        || !reader.string(kp750Path(model, 0x138))
        || !reader.string(kp750Path(model, 0x240))
        || !readKp750Metadata2f8(reader, kp750Path(model, 0x2f8))
        || !reader.int32(kp750Path(model, 0x268))
        || !reader.string(kp750Path(model, 0x270))
        || !reader.boolean(kp750Path(model, 0x190))
        || !reader.boolean(kp750Path(model, 0x191))
        || !reader.boolean(kp750Path(model, 0x192))
        || !reader.int32(kp750Path(model, 0x198))
        || !reader.boolean(QStringLiteral("importModel.local+0x0"))
        || !readKp750StringVector(reader, kp750Path(model, 0x2d0))
        || !reader.string(kp750Path(model, 0x38))
        || !reader.string(kp750Path(model, 0xb0))
        || !reader.uint32(kp750Path(model, 0x280), KpSchema750Field::Type::Enum32)
        || !reader.uint32(kp750Path(model, 0x284), KpSchema750Field::Type::Enum32)) {
        if (failedAt) *failedAt = reader.position();
        return false;
    }
    for (const uint32_t offset : {0x288u, 0x290u, 0x298u, 0x2a0u}) {
        if (!reader.float64(kp750Path(model, offset))) {
            if (failedAt) *failedAt = reader.position();
            return false;
        }
    }
    if (!reader.int32(kp750Path(model, 0x1bc))
        || !reader.string(kp750Path(model, 0x1c0))
        || !reader.string(kp750Path(model, 0x1c8))
        || !reader.string(kp750Path(model, 0x218))
        || !reader.string(kp750Path(model, 0x220))
        || !reader.boolean(kp750Path(model, 0x194))
        || !reader.uint32(kp750Path(model, 0x1e0), KpSchema750Field::Type::Enum32)) {
        if (failedAt) *failedAt = reader.position();
        return false;
    }
    for (const uint32_t offset : {0x1e4u, 0x1e8u, 0x1ecu, 0x1f0u, 0x1f4u, 0x1f8u}) {
        if (!reader.uint32(kp750Path(model, offset))) return false;
    }
    if (!readKp750StringVector(reader, kp750Path(model, 0x3d8))
        || !reader.string(kp750Path(model, 0x1a0))
        || !reader.string(kp750Path(model, 0x168))
        || !reader.string(kp750Path(model, 0xa0))
        || !reader.string(kp750Path(model, 0x1a8))
        || !readKp750MetadataC0(reader, kp750Path(model, 0xc0))
        || !reader.string(kp750Path(model, 0x20))
        || !reader.string(kp750Path(model, 0x50))
        || !reader.string(kp750Path(model, 0x60))
        || !reader.boolean(kp750Path(model, 0x278))
        || !reader.string(kp750Path(model, 0x238))
        || !readKp750Metadata110(reader, kp750Path(model, 0x110))
        || !reader.uint32(kp750Path(model, 4), KpSchema750Field::Type::Enum32)
        || !reader.string(kp750Path(model, 0x90))
        || !reader.int32(kp750Path(model, 0x26c))
        || !reader.uint32(kp750Path(model, 0x1d0), KpSchema750Field::Type::Enum32)
        || !reader.string(kp750Path(model, 0xa8))
        || !reader.string(kp750Path(model, 0x1d8))
        || !reader.uint32(kp750Path(model, 0x210), KpSchema750Field::Type::Enum32)
        || !reader.string(kp750Path(model, 0x1b0))
        || !reader.string(kp750Path(model, 0x10))
        || !reader.string(kp750Path(model, 8)))
        return false;

    if (!reader.good()) {
        if (failedAt) *failedAt = reader.position();
        return false;
    }
    metadata->streamEnd = reader.position();
    metadata->serialized = data.mid(int(start), int(metadata->streamEnd - start));
    return true;
}

// The bytes between ReadWriteKpImportModelMetadata and the ZIP local header
// are not metadata padding. LoadMapPackIntoImportModel serializes three root
// fields, marker 0x42007899, then calls FUN_7ff6e294fb04 for a vector of
// FUN_7ff6e2942910 records. The native backing stride is 0x100; wire length
// remains variable because it contains strings and vectors.
bool readKp750RootRecord(Kp750WireReader &reader, const QString &path)
{
    const int record = reader.beginVector(path);
    if (!reader.string(kp750Path(path, 0))
        || !reader.string(kp750Path(path, 8))
        || !reader.literalU32(0x93bc9201u, kp750Path(path, 0x10))
        || !reader.uint32(kp750Path(path, 0x10 + 4), KpSchema750Field::Type::Enum32)
        || !reader.uint32(kp750Path(path, 0x18), KpSchema750Field::Type::Enum32)
        || !readKp750StringVector(reader, kp750Path(path, 0x28))
        || !reader.string(kp750Path(path, 0x20)))
        return false;

    const int vector78 = reader.beginVector(kp750Path(path, 0x78));
    int32_t count = 0;
    if (!reader.count(kp750Path(path, 0x78) + QStringLiteral(".count"), &count))
        return false;
    for (int32_t index = 0; index < count; ++index) {
        const QString entry = kp750Path(path, 0x78) + QStringLiteral("[%1]").arg(index);
        const int element = reader.beginVector(entry);
        if (!reader.string(kp750Path(entry, 0))
            || !readKp750ByteVector(reader, kp750Path(entry, 8)))
            return false;
        reader.endVector(element);
    }
    reader.endVector(vector78);

    if (!reader.string(kp750Path(path, 0xa0))
        || !reader.int32(kp750Path(path, 0xa8))
        || !reader.raw(kp750Path(path, 0xac), 1)
        || !reader.uint32(kp750Path(path, 0xb0), KpSchema750Field::Type::Enum32)
        || !reader.string(kp750Path(path, 0xd8))
        || !reader.string(kp750Path(path, 0xe0)))
        return false;
    for (const uint32_t offset : {0xb4u, 0xb8u, 0xbcu, 0xc0u, 0xc4u, 0xc8u,
                                  0xccu, 0xd0u, 0xd4u}) {
        if (!reader.uint32(kp750Path(path, offset), KpSchema750Field::Type::Enum32))
            return false;
    }
    if (!reader.int32(kp750Path(path, 0xf0))
        || !reader.boolean(kp750Path(path, 0xe8))
        || !reader.uint32(kp750Path(path, 0xec), KpSchema750Field::Type::Enum32)
        || !reader.uint32(kp750Path(path, 0xf8), KpSchema750Field::Type::Enum32)
        || !reader.int32(kp750Path(path, 0xf4)))
        return false;
    reader.endVector(record);
    return true;
}

bool readKp750Root(const QByteArray &data, qsizetype start, qsizetype limit,
                   qsizetype expectedArchiveBytes,
                   KpSchema750Root *root, qsizetype *failedAt = nullptr,
                   QString *failedPath = nullptr)
{
    if (!root || start < 0 || start >= limit || limit > data.size()
        || expectedArchiveBytes < 0)
        return false;
    root->streamOffset = start;
    root->fields.clear();
    root->records.clear();
    Kp750WireReader reader(data, start, limit, &root->fields);
    struct FailurePosition {
        Kp750WireReader &reader;
        qsizetype *out;
        QString *path;
        ~FailurePosition()
        {
            if (out) *out = reader.position();
            if (path) *path = reader.lastPath();
        }
    } failurePosition { reader, failedAt, failedPath };

    const QString rootPath = QStringLiteral("importRoot");
    if (!reader.int32(kp750Path(rootPath, 0))
        || !reader.int32(kp750Path(rootPath, 0x4b8))
        || !reader.int32(kp750Path(rootPath, 0xbe0))
        || !reader.literalU32(0x42007899u, rootPath + QStringLiteral(".marker")))
        return false;

    const int vector = reader.beginVector(rootPath + QStringLiteral("+0x4c8"));
    int32_t count = 0;
    if (!reader.count(rootPath + QStringLiteral("+0x4c8.count"), &count))
        return false;
    for (int32_t index = 0; index < count; ++index) {
        const qsizetype recordStart = reader.position();
        const QString recordPath = rootPath + QStringLiteral("+0x4c8[%1]").arg(index);
        const int firstField = root->fields.size();
        if (!readKp750RootRecord(reader, recordPath))
            return false;
        KpSchema750RootRecord record;
        record.serialized = data.mid(int(recordStart), int(reader.position() - recordStart));
        for (int field = firstField; field < root->fields.size(); ++field)
            record.fields.append(root->fields.at(field));
        root->records.append(record);
    }
    reader.endVector(vector);
    if (!reader.literalU32(0x11883377u,
                           rootPath + QStringLiteral(".archiveMarker"))
        || !reader.boolean(rootPath + QStringLiteral(".archiveFlag")))
        return false;
    const qsizetype archiveSizeField = reader.position();
    if (!reader.uint32(rootPath + QStringLiteral(".archiveByteCount")))
        return false;
    if (peekU32(data, archiveSizeField) != uint32_t(expectedArchiveBytes))
        return false;
    if (!reader.good())
        return false;
    root->streamEnd = reader.position();
    root->serialized = data.mid(int(start), int(root->streamEnd - start));
    return true;
}

// Schema 750 selects every KpAxisDescriptorCodec gate through 750.  This is a
// strictly ordered stream of structured/simple strings, scalar fields, a byte
// vector, and a vector of structured strings; it is not a tagged record.
struct Kp750AxisBlock {
    Kp750Axis descriptor;
    Kp750SerializedString identifier;
    uint32_t structuredNameCount = 0;
    QByteArray byteVector;
    bool flag80 = false;
    bool flag81 = false;
    qsizetype nextAxis = 0;
};

// FUN_21861d0: a simple string plus its schema-345 integer.  The enclosing
// structured string serializes a second integer and a vector of these entry
// records.  All bounds are derived from native count/length fields.
bool readSchema750StructuredString(const QByteArray &data, qsizetype start,
                                   qsizetype limit,
                                   Kp750SerializedString *base,
                                   qsizetype *end)
{
    if (!base || !end || !readSchema750String(data, start, limit, base))
        return false;
    qsizetype cursor = base->end;
    if (cursor + 12 > limit)
        return false;
    cursor += 8; // entry +0x08 and enclosing string +0x08
    const int32_t count = peekI32(data, cursor);
    cursor += 4;
    if (count < 0 || qsizetype(count) > (limit - cursor) / 8)
        return false;
    for (int32_t index = 0; index < count; ++index) {
        Kp750SerializedString entry;
        if (!readSchema750String(data, cursor, limit, &entry)
            || entry.end + 4 > limit)
            return false;
        cursor = entry.end + 4; // FUN_21861d0 entry +0x08
    }
    *end = cursor;
    return true;
}

bool readSchema750StructuredStringVector(const QByteArray &data, qsizetype start,
                                         qsizetype limit, uint32_t *countOut,
                                         qsizetype *end)
{
    if (!countOut || !end || start < 0 || start + 4 > limit)
        return false;
    const int32_t count = peekI32(data, start);
    qsizetype cursor = start + 4;
    if (count < 0 || qsizetype(count) > (limit - cursor) / 16)
        return false;
    for (int32_t index = 0; index < count; ++index) {
        Kp750SerializedString entry;
        if (!readSchema750StructuredString(data, cursor, limit, &entry, &cursor))
            return false;
    }
    *countOut = uint32_t(count);
    *end = cursor;
    return true;
}

bool parseSchema750AxisBlock(const QByteArray &data, qsizetype start,
                             qsizetype limit, Kp750AxisBlock *out,
                             uint32_t schemaVersion = 750)
{
    if (!out || start < 0 || limit > data.size())
        return false;

    Kp750SerializedString name, unit, identifier;
    qsizetype cursor = 0;
    if (!readKpStructuredStringForVersion(data, start, limit, schemaVersion, &name, &cursor))
        return false;
    if (!readKpStringForVersion(data, cursor, limit, schemaVersion, &unit))
        return false;

    const qsizetype factorPos = unit.end;
    cursor = factorPos + 16;
    if (cursor + 20 + 2 + 8 + 12 + 1 + 4 > limit)
        return false;
    // Native members +0x64/+0x70/+0x74/+0x78/+0x7c.  Only the latter
    // three are projected below; the first and +0x7c are retained in the
    // serialized record rather than restricted to fixture-observed enums.
    const uint32_t address = peekU32(data, cursor + 4);
    const uint32_t type = peekU32(data, cursor + 8);
    const int32_t size = peekI32(data, cursor + 12);
    const int32_t wireRecordType = peekI32(data, cursor + 16);
    cursor += 20;
    const bool flag80 = data.at(int(cursor)) != 0;     // bool +0x80
    const bool flag81 = data.at(int(cursor + 1)) != 0; // bool +0x81
    cursor += 2;
    if (schemaVersion >= 264) {
        if (cursor + 8 > limit) return false;
        cursor += 8; // raw64 +0x58
    }
    int32_t precision = -1;
    if (schemaVersion >= 241) {
        if (cursor + 4 > limit) return false;
        precision = peekI32(data, cursor); // +0x68
        cursor += 4;
    }
    // Schema 834 adds +0x8c before the older +0x90 member.
    if (schemaVersion >= 834) {
        if (cursor + 4 > limit) return false;
        cursor += 4; // int +0x8c
    }
    // +0x90 starts at schema 8.
    if (schemaVersion >= 8) {
        if (cursor + 4 > limit) return false;
        cursor += 4;
    }
    bool pointsSigned = false;
    if (schemaVersion >= 12) {
        if (cursor + 1 > limit) return false;
        pointsSigned = data.at(int(cursor)) != 0; // bool +0x82
        ++cursor;
    }
    QByteArray byteVector;
    if (schemaVersion >= 73) {
        if (cursor + 4 > limit) return false;
        const int32_t byteCount = peekI32(data, cursor);
        cursor += 4;
        if (byteCount < 0 || qsizetype(byteCount) > limit - cursor)
            return false;
        byteVector = data.mid(int(cursor), byteCount);
        cursor += byteCount;
    }
    if (schemaVersion >= 77) {
        if (cursor + 4 > limit) return false;
        cursor += 4; // int +0x6c
    }
    if (schemaVersion >= 91) {
        if (cursor + 4 > limit) return false;
        cursor += 4; // enum +0x60
    }
    // Member +0x40 was introduced at serializer gate 354.  Earlier axis
    // descriptors end the fixed part at +0x60 and must not consume an
    // imaginary empty string before the subsequent gated fields.
    if (schemaVersion >= 354) {
        if (!readKpStringForVersion(data, cursor, limit, schemaVersion, &identifier))
            return false;
        cursor = identifier.end;
    } else {
        identifier.start = cursor;
        identifier.end = cursor;
    }
    uint32_t structuredNameCount = 0;
    if (schemaVersion >= 372) {
        if (cursor + 4 > limit) return false;
        const int32_t count = peekI32(data, cursor);
        cursor += 4;
        if (count < 0 || qsizetype(count) > (limit - cursor) / 5) return false;
        for (int32_t index = 0; index < count; ++index) {
            Kp750SerializedString entry;
            if (!readKpStructuredStringForVersion(data, cursor, limit, schemaVersion,
                                                   &entry, &cursor))
                return false;
        }
        structuredNameCount = uint32_t(count);
    }
    if (schemaVersion >= 440) {
        if (cursor + 4 > limit) return false;
        cursor += 4; // +0x88 enum
    }
    if (schemaVersion >= 834) {
        if (cursor + 8 > limit) return false;
        cursor += 8; // int +0x94, int +0x98

        const int32_t wordCount = peekI32(data, cursor);
        cursor += 4;
        if (wordCount < 0 || qsizetype(wordCount) > (limit - cursor) / 4)
            return false;
        cursor += qsizetype(wordCount) * 4; // int vector +0xa0

        const int32_t stringCount = peekI32(data, cursor);
        cursor += 4;
        if (stringCount < 0 || qsizetype(stringCount) > (limit - cursor) / 4)
            return false;
        for (int32_t index = 0; index < stringCount; ++index) {
            Kp750SerializedString entry;
            if (!readKpStringForVersion(data, cursor, limit, schemaVersion, &entry))
                return false;
            cursor = entry.end; // object-pointer vector +0xc8
        }
    }

    Kp750AxisBlock block;
    block.descriptor.rawAddr = address;
    block.descriptor.dataType = type;
    block.descriptor.dataSize = size;
    block.descriptor.wireRecordType = wireRecordType;
    block.descriptor.nativeRecordType = (wireRecordType == 2 || wireRecordType == 10
                                         || wireRecordType == 16)
        ? wireRecordType : 10;
    block.descriptor.name = name.text;
    block.descriptor.unit = unit.text;
    block.descriptor.factor = peekF64(data, factorPos);
    block.descriptor.offset = peekF64(data, factorPos + 8);
    block.descriptor.hasFactor = true;
    block.descriptor.precision = precision;
    block.descriptor.pointsSigned = pointsSigned;
    block.identifier = identifier;
    block.structuredNameCount = structuredNameCount;
    block.byteVector = byteVector;
    block.flag80 = flag80;
    block.flag81 = flag81;
    block.nextAxis = cursor;
    *out = block;
    return true;
}

// Field-complete ledger for KpMapObjectCodec at effective serializer version
// 750.  This follows the same call order as the native routine; the separate
// MapInfo projection below deliberately stays small, while this record keeps
// every field required to reproduce later WinOLS import decisions.
bool readKp750MapRecord(const QByteArray &data, qsizetype start, qsizetype limit,
                        KpSchema750MapRecord *record, uint32_t schemaVersion = 750)
{
    if (!record || start < 0 || start >= limit || limit > data.size())
        return false;
    record->streamOffset = start;
    record->fields.clear();
    Kp750WireReader reader(data, start, limit, &record->fields);
    const QString map = QStringLiteral("mapObject");
    auto axis = [&](const QString &path) -> bool {
        if (!reader.structuredString(kp750Path(path, 0))
            || !reader.string(kp750Path(path, 0x38))
            || !reader.raw(kp750Path(path, 0x48), 8)
            || !reader.raw(kp750Path(path, 0x50), 8)
            || !reader.uint32(kp750Path(path, 0x64), KpSchema750Field::Type::Enum32)
            || !reader.int32(kp750Path(path, 0x70))
            || !reader.uint32(kp750Path(path, 0x74), KpSchema750Field::Type::Enum32)
            || !reader.int32(kp750Path(path, 0x78))
            || !reader.int32(kp750Path(path, 0x7c))
            || !reader.boolean(kp750Path(path, 0x80))
            || !reader.boolean(kp750Path(path, 0x81))
            || !reader.raw(kp750Path(path, 0x58), 8)
            || !reader.int32(kp750Path(path, 0x68))
            || !reader.int32(kp750Path(path, 0x90))
            || !reader.boolean(kp750Path(path, 0x82))
            || !readKp750ByteVector(reader, kp750Path(path, 0x168))
            || !reader.int32(kp750Path(path, 0x6c))
            || !reader.uint32(kp750Path(path, 0x60), KpSchema750Field::Type::Enum32)
            || !reader.string(kp750Path(path, 0x40)))
            return false;

        if (schemaVersion >= 372) {
            const QString vectorPath = kp750Path(path, 0x190);
            const int vector = reader.beginVector(vectorPath);
            int32_t count = 0;
            if (!reader.count(vectorPath + QStringLiteral(".count"), &count))
                return false;
            for (int32_t index = 0; index < count; ++index) {
                if (!reader.structuredString(vectorPath + QStringLiteral("[%1]").arg(index)))
                    return false;
            }
            reader.endVector(vector);
        }
        return schemaVersion < 440
            || reader.uint32(kp750Path(path, 0x88), KpSchema750Field::Type::Enum32);
    };

    if (!reader.raw(kp750Path(map, 0x1a0), 1)
        || !reader.int32(kp750Path(map, 0x90))
        || !reader.structuredString(kp750Path(map, 0x208))
        || !reader.raw(kp750Path(map, 0), 1)
        || !reader.structuredString(kp750Path(map, 8))
        || !reader.uint32(kp750Path(map, 0x48), KpSchema750Field::Type::Enum32)
        || !reader.uint32(kp750Path(map, 0x94), KpSchema750Field::Type::Enum32)
        || !reader.uint32(kp750Path(map, 0x98), KpSchema750Field::Type::Enum32)
        || !reader.int32(kp750Path(map, 0xa0))
        || !reader.int32(kp750Path(map, 0xa4))
        || !reader.int32(kp750Path(map, 0x54))
        || !reader.string(kp750Path(map, 0x40))
        || (schemaVersion >= 298 && !reader.int32(kp750Path(map, 0x58)))
        || (schemaVersion >= 299 && !reader.int32(kp750Path(map, 0x11c)))
        || !reader.int32(kp750Path(map, 0x5c))
        || !reader.boolean(kp750Path(map, 0x50))
        || !reader.uint32(kp750Path(map, 0xc0)))
        return false;
    if (schemaVersion >= 300) {
        for (uint32_t offset = 0xd0; offset <= 0xf8; offset += 8) {
            if (!reader.raw(kp750Path(map, offset), 8)) return false;
        }
    }
    for (uint32_t index = 0; index < 6; ++index) {
        if (!reader.float64(map + QStringLiteral(".floatPlaceholder[%1]").arg(index)))
            return false;
    }
    if (!reader.boolean(kp750Path(map, 0x104))
        || !reader.boolean(kp750Path(map, 0x105))
        || !reader.boolean(kp750Path(map, 0x106))
        || !reader.boolean(kp750Path(map, 0x107))
        || !reader.int32(kp750Path(map, 0x114))
        || !reader.int32(kp750Path(map, 0x118))
        || !reader.raw(kp750Path(map, 0x18c), 8)
        || !reader.int32(kp750Path(map, 0x198)))
        return false;

    const QString properties = kp750Path(map, 0x240);
    if (!reader.string(kp750Path(properties, 0))
        || !reader.string(kp750Path(properties, 8))
        || !reader.raw(kp750Path(properties, 0x10), 8)
        || !reader.raw(kp750Path(properties, 0x18), 8)
        || !reader.int32(kp750Path(properties, 0x38))
        || !reader.int32(kp750Path(properties, 0x48))
        || !reader.int32(kp750Path(properties, 0x4c))
        || !reader.raw(kp750Path(properties, 0x20), 8)
        || !reader.int32(kp750Path(properties, 0x3c))
        || !reader.int32(kp750Path(properties, 0x28))
        || !reader.int32(kp750Path(properties, 0x2c))
        || !reader.int32(kp750Path(properties, 0x30))
        || !reader.uint32(kp750Path(properties, 0x34), KpSchema750Field::Type::Enum32)
        || !axis(kp750Path(map, 0x290))
        || !axis(kp750Path(map, 0x450)))
        return false;

    if (!reader.boolean(kp750Path(map, 0x109))
        || !reader.boolean(kp750Path(map, 0x184))
        || !reader.uint32(kp750Path(map, 0x188))
        || !reader.int32(kp750Path(map, 0x17c))
        || !reader.int32(kp750Path(map, 0x180))
        || !reader.int32(kp750Path(map, 0x4c))
        || !reader.boolean(kp750Path(map, 0x120))
        || !reader.boolean(kp750Path(map, 0x121))
        || !reader.raw(kp750Path(map, 0x128), 8)
        || !reader.raw(kp750Path(map, 0x130), 8)
        || !reader.int32(kp750Path(map, 0x138))
        || !reader.boolean(kp750Path(map, 0x13c))
        || !reader.raw(kp750Path(map, 0x140), 8)
        || !reader.raw(kp750Path(map, 0x148), 8)
        || !reader.raw(kp750Path(map, 0x150), 8)
        || !reader.int32(kp750Path(map, 0x160))
        || !reader.raw(kp750Path(map, 0x158), 8)
        || !reader.boolean(kp750Path(map, 0x164))
        || !reader.boolean(kp750Path(map, 0x165))
        || !reader.boolean(kp750Path(map, 0x166))
        || !reader.int32(kp750Path(map, 0x194))
        || !reader.uint32(kp750Path(map, 0x168), KpSchema750Field::Type::Enum32)
        || !reader.raw(kp750Path(map, 0x16c), 16)
        || !reader.int32(kp750Path(map, 0xa8))
        || !reader.uint32(kp750Path(map, 0xc4))
        || !reader.uint32(kp750Path(map, 200))
        || !reader.int32(kp750Path(map, 0xac))
        || (schemaVersion >= 476 && !reader.int32(kp750Path(map, 0x1ac))))
        return false;
    if (schemaVersion >= 476) {
        if (!readKp750Int32Vector(reader, kp750Path(map, 0x1b8)))
            return false;
        const QString objectVector = kp750Path(map, 0x1e0);
        const int vector = reader.beginVector(objectVector);
        int32_t count = 0;
        if (!reader.count(objectVector + QStringLiteral(".count"), &count)) return false;
        for (int32_t index = 0; index < count; ++index) {
            if (!reader.string(objectVector + QStringLiteral("[%1]").arg(index))) return false;
        }
        reader.endVector(vector);
    }
    if (schemaVersion >= 834 && !reader.int32(kp750Path(map, 0x1b0)))
        return false;
    if (schemaVersion >= 503
        && (!reader.uint32(kp750Path(map, 0xb0))
            || !reader.raw(kp750Path(map, 0xb8), 8)))
        return false;
    if (schemaVersion >= 596 && !reader.uint32(kp750Path(map, 0x100)))
        return false;
    if (!reader.good())
        return false;

    record->streamEnd = reader.position();
    record->serialized = data.mid(int(start), int(record->streamEnd - start));
    return true;
}

// ── Folder table (schema-750) ─────────────────────────────────────────────
// WinOLS stores the project's folder tree in the trailing metadata after the
// embedded ZIP, not in the `intern` stream. Each schema-750 map record carries
// a folder id at metadata +0x1F; resolving it against this table lets maps that
// share a display name land in their own (sub)folder instead of collapsing
// into an ambiguous flat list. Its grammar is the exact schema-750 branch of
// SerializeMapPackFolderRecord (0x7ff6e248d428), including the specialised
// string/vector field at native member +0x04. There is no record re-sync or
// inferred suffix length: every field advances the stream exactly once.
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
    // Native folder records add their final field at schema 750; no later
    // folder gate exists through the current WinOLS ceiling (834).
    if (formatVersion < 750 || formatVersion > 834
        || archiveEnd < 0 || archiveEnd + 16 > fileData.size())
        return folders;
    const uint32_t marker1 = peekU32(fileData, archiveEnd);
    const uint32_t reserved = peekU32(fileData, archiveEnd + 4);
    const uint32_t marker2 = peekU32(fileData, archiveEnd + 8);
    const uint32_t count = peekU32(fileData, archiveEnd + 12);
    // The schema-750 serializer emits at least 58 bytes for an empty folder:
    // id/parent, an empty name, its two int32 fields, empty name-entry vector,
    // boolean, empty byte vector, v131 fields, empty auxiliary string, v291/
    // v302 fields and the v750 int32. This is a native grammar minimum.
    if (marker1 != marker2 || reserved != 0
        || count > uint32_t((fileData.size() - (archiveEnd + 16)) / 58))
        return folders;

    qsizetype cursor = archiveEnd + 16;
    for (uint32_t index = 0; index < count; ++index) {
        const qsizetype recordStart = cursor;
        if (cursor + 8 > fileData.size()) {
            folders.clear();
            break;
        }
        const uint32_t id = peekU32(fileData, cursor);
        const uint32_t parent = peekU32(fileData, cursor + 4);
        cursor += 8;

        // FUN_7ff6e2186c24(+0x04), schema >= 345:
        // string +0x04, int32 +0x0c, int32 +0x08, then a count of
        // [string,int32] entries. The serializer subsequently writes its
        // local boolean before member +0x3e's byte vector.
        Kp750SerializedString nameField;
        if (!readSchema750String(fileData, cursor, fileData.size(), &nameField)) {
            folders.clear();
            break;
        }
        cursor = nameField.end;
        if (cursor + 12 > fileData.size()) {
            folders.clear();
            break;
        }
        const int32_t nameValueAt0c = peekI32(fileData, cursor);
        const int32_t valueAt08 = peekI32(fileData, cursor + 4);
        const int32_t entryCount = peekI32(fileData, cursor + 8);
        cursor += 12;
        if (entryCount < 0 || entryCount > (fileData.size() - cursor) / 8) {
            folders.clear();
            break;
        }
        QVector<KpFolderNameEntry> nameEntries;
        nameEntries.reserve(entryCount);
        for (int32_t entry = 0; entry < entryCount; ++entry) {
            Kp750SerializedString entryName;
            if (!readSchema750String(fileData, cursor, fileData.size(), &entryName)
                || entryName.end + 4 > fileData.size()) {
                folders.clear();
                break;
            }
            nameEntries.append({entryName.text, peekI32(fileData, entryName.end)});
            cursor = entryName.end + 4;
        }
        if (nameEntries.size() != entryCount || cursor + 5 > fileData.size()) {
            folders.clear();
            break;
        }
        const bool flagBeforeByteVector = fileData.at(int(cursor)) != 0;
        ++cursor;

        const int32_t variantLength = peekI32(fileData, cursor);
        cursor += 4;
        if (variantLength < 0 || cursor + qsizetype(variantLength) > fileData.size()) {
            folders.clear();
            break;
        }
        const QByteArray byteVector = fileData.mid(int(cursor), int(variantLength));
        cursor += variantLength;
        if (cursor + 14 > fileData.size()) {
            folders.clear();
            break;
        }
        const bool flag131 = fileData.at(int(cursor)) != 0;
        const uint32_t enum131a = peekU32(fileData, cursor + 1);
        const uint32_t enum131b = peekU32(fileData, cursor + 5);
        const uint8_t value131 = uint8_t(fileData.at(int(cursor + 9)));
        cursor += 10;
        Kp750SerializedString auxiliaryField;
        if (!readSchema750String(fileData, cursor, fileData.size(), &auxiliaryField)) {
            folders.clear();
            break;
        }
        cursor = auxiliaryField.end;
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
        folders.insert(id, { parent, nameField.text });
        if (records) {
            KpFolderRecord record;
            record.id = id;
            record.parentId = parent;
            record.name = nameField.text;
            record.nameValueAt0c = nameValueAt0c;
            record.valueAt08 = valueAt08;
            record.nameEntries = std::move(nameEntries);
            record.flagBeforeByteVector = flagBeforeByteVector;
            record.byteVector = byteVector;
            record.flag131 = flag131;
            record.enum131a = enum131a;
            record.enum131b = enum131b;
            record.value131 = value131;
            record.auxiliaryStringMarker = auxiliaryField.marker;
            record.auxiliaryString = auxiliaryField.text;
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

// Schemas through 503 use the pre-cumulative folder table: [id, byte-count,
// CP-1252 name, fixed version suffix].  The suffix grows at native gates 292
// and 315, then loses the legacy string NUL at 439.  It has no parent member,
// so every resolved path is a native root folder.
QHash<uint32_t, KpFolder> parseKpFolderTableLegacy(const QByteArray &fileData,
                                                    qsizetype archiveEnd,
                                                    uint32_t schemaVersion,
                                                    QVector<KpFolderRecord> *records,
                                                    QStringList *warnings)
{
    QHash<uint32_t, KpFolder> folders;
    constexpr uint32_t marker = 0x98638811u;
    qsizetype suffixBytes = schemaVersion < 292 ? 25
        : (schemaVersion < 315 ? 26 : (schemaVersion < 439 ? 36
           : (schemaVersion == 440 ? 35 : (schemaVersion == 479 ? 47 : 49))));
    const QByteArray suffixPrefix = schemaVersion < 439
        ? QByteArray::fromHex("00010100000001000000")
        : QByteArray::fromHex("010100000001000000");
    if (archiveEnd < 0 || archiveEnd + 20 > fileData.size()
        || peekU32(fileData, archiveEnd) != marker
        || peekU32(fileData, archiveEnd + 4) != 0
        || peekU32(fileData, archiveEnd + 8) != marker
        || peekU32(fileData, archiveEnd + 16) != 1)
        return folders;
    const uint32_t count = peekU32(fileData, archiveEnd + 12);
    // Schema 503 has two native folder-record profiles.  The outer schema is
    // identical; the serializer mode selects either the 49-byte suffix
    // (twelve leading zeroes) or the 47-byte suffix (fourteen).  Select from
    // their explicit first-record discriminator, never by re-synchronising.
    if (schemaVersion == 503 && count > 0) {
        const qsizetype first = archiveEnd + 20;
        if (first + 8 > fileData.size()) return folders;
        const uint32_t nameBytes = peekU32(fileData, first + 4);
        const qsizetype suffixStart = first + 8 + qsizetype(nameBytes);
        if (nameBytes > uint32_t(fileData.size() - (first + 8))
            || suffixStart + 49 > fileData.size())
            return folders;
        if (fileData.mid(int(suffixStart), 12) == QByteArray(12, '\0')
            && fileData.at(int(suffixStart + 12)) == '\x01') {
            suffixBytes = 49;
        } else if (fileData.mid(int(suffixStart), 14) == QByteArray(14, '\0')
                   && fileData.at(int(suffixStart + 14)) == '\x01') {
            suffixBytes = 47;
        } else {
            return folders;
        }
    }
    if (count > uint32_t((fileData.size() - (archiveEnd + 20)) / (8 + suffixBytes)))
        return folders;
    qsizetype cursor = archiveEnd + 20;
    for (uint32_t index = 0; index < count; ++index) {
        const qsizetype start = cursor;
        if (cursor + 8 > fileData.size()) { folders.clear(); break; }
        const uint32_t id = peekU32(fileData, cursor);
        const uint32_t nameBytes = peekU32(fileData, cursor + 4);
        cursor += 8;
        if (nameBytes > uint32_t(fileData.size() - cursor)
            || cursor + qsizetype(nameBytes) + suffixBytes > fileData.size()) {
            folders.clear();
            break;
        }
        const QString name = decodeKpText(fileData.mid(int(cursor), int(nameBytes)));
        cursor += nameBytes;
        const QByteArray suffix = fileData.mid(int(cursor), int(suffixBytes));
        const bool validPrefix = schemaVersion == 503 && suffixBytes == 47
            ? suffix.startsWith(QByteArray(14, '\0')) && suffix.at(14) == '\x01'
            : schemaVersion >= 479
            ? suffix.startsWith(QByteArray(12, '\0')) && suffix.at(12) == '\x01'
            : suffix.startsWith(suffixPrefix)
                && suffix.mid(suffixPrefix.size(), 12) == QByteArray(12, '\0');
        if (!validPrefix) {
            folders.clear();
            break;
        }
        cursor += suffixBytes;
        folders.insert(id, {0, name});
        if (records) {
            KpFolderRecord record;
            record.id = id;
            record.name = name;
            if (schemaVersion < 479) {
                const qsizetype valueOffset = suffixPrefix.size() + 12;
                record.value302a = int(uint8_t(suffix.at(int(valueOffset))))
                    | (int(uint8_t(suffix.at(int(valueOffset + 1)))) << 8)
                    | (int(uint8_t(suffix.at(int(valueOffset + 2)))) << 16);
                record.flag302a = suffixBytes > valueOffset + 3
                    && suffix.at(int(valueOffset + 3)) != 0;
            }
            record.serializedRecord = fileData.mid(int(start), int(cursor - start));
            records->append(record);
        }
    }
    if (folders.isEmpty() && count > 0 && warnings) {
        warnings->append(KpImporter::tr("legacy KP folder table is incomplete"));
    }
    return folders;
}

// Schema 597 folder records are the pre-750 cumulative folder branch.  Their
// leading name is a simple >=439 string, followed by the 13-byte legacy name
// suffix, a byte vector, the gate-131 fields, an auxiliary string, and gates
// 291/302.  Gate 750's final integer is deliberately absent.
QHash<uint32_t, KpFolder> parseKpFolderTable597(const QByteArray &fileData,
                                                qsizetype archiveEnd,
                                                uint32_t schemaVersion,
                                                QVector<KpFolderRecord> *records,
                                                QStringList *warnings)
{
    QHash<uint32_t, KpFolder> folders;
    if (archiveEnd < 0 || archiveEnd + 16 > fileData.size()
        || peekU32(fileData, archiveEnd) != peekU32(fileData, archiveEnd + 8)
        || peekU32(fileData, archiveEnd + 4) != 0)
        return folders;
    const uint32_t count = peekU32(fileData, archiveEnd + 12);
    if (count > uint32_t((fileData.size() - (archiveEnd + 16)) / 44))
        return folders;
    qsizetype cursor = archiveEnd + 16;
    for (uint32_t index = 0; index < count; ++index) {
        const qsizetype start = cursor;
        if (cursor + 8 > fileData.size()) { folders.clear(); break; }
        const uint32_t id = peekU32(fileData, cursor);
        const uint32_t parent = peekU32(fileData, cursor + 4);
        cursor += 8;
        Kp750SerializedString name;
        if (!readKpStringForVersion(fileData, cursor, fileData.size(), schemaVersion, &name)) {
            folders.clear(); break;
        }
        cursor = name.end;
        if (cursor + 13 > fileData.size()
            || fileData.mid(int(cursor), 13) != QByteArray(13, '\0')) {
            folders.clear(); break;
        }
        cursor += 13;
        if (cursor + 4 > fileData.size()) { folders.clear(); break; }
        const int32_t variantLength = peekI32(fileData, cursor);
        cursor += 4;
        if (variantLength < 0 || cursor + qsizetype(variantLength) > fileData.size()) {
            folders.clear(); break;
        }
        const QByteArray byteVector = fileData.mid(int(cursor), variantLength);
        cursor += variantLength;
        if (cursor + 10 > fileData.size()) { folders.clear(); break; }
        const bool flag131 = fileData.at(int(cursor)) != 0;
        const uint32_t enum131a = peekU32(fileData, cursor + 1);
        const uint32_t enum131b = peekU32(fileData, cursor + 5);
        const uint8_t value131 = uint8_t(fileData.at(int(cursor + 9)));
        cursor += 10;
        Kp750SerializedString auxiliary;
        if (!readKpStringForVersion(fileData, cursor, fileData.size(), schemaVersion,
                                    &auxiliary)) {
            folders.clear(); break;
        }
        cursor = auxiliary.end;
        if (cursor + 11 > fileData.size()) { folders.clear(); break; }
        const bool flag291 = fileData.at(int(cursor)) != 0;
        const bool flag302a = fileData.at(int(cursor + 1)) != 0;
        const bool flag302b = fileData.at(int(cursor + 2)) != 0;
        const int32_t value302a = peekI32(fileData, cursor + 3);
        const int32_t value302b = peekI32(fileData, cursor + 7);
        cursor += 11;
        folders.insert(id, {parent, name.text});
        if (records) {
            KpFolderRecord record;
            record.id = id;
            record.parentId = parent;
            record.name = name.text;
            record.byteVector = byteVector;
            record.flag131 = flag131;
            record.enum131a = enum131a;
            record.enum131b = enum131b;
            record.value131 = value131;
            record.auxiliaryStringMarker = auxiliary.marker;
            record.auxiliaryString = auxiliary.text;
            record.flag291 = flag291;
            record.flag302a = flag302a;
            record.flag302b = flag302b;
            record.value302a = value302a;
            record.value302b = value302b;
            record.serializedRecord = fileData.mid(int(start), int(cursor - start));
            records->append(record);
        }
    }
    if (folders.isEmpty() && count > 0 && warnings)
        warnings->append(KpImporter::tr("schema-597 folder table is incomplete"));
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

// ── Native schema-750 map-object walk ──────────────────────────────────────
// The `intern` payload starts with KpInternMapObjectArray +0xA5 (bool) and a
// count.  Each following item is the cumulative KpMapObjectCodec stream.  Do
// not use the former zero-prefix/fixed-tail framer here: those bytes happened
// to align on the original corpus but are ordinary version-gated fields.
QVector<MapInfo> parseSchema750Deterministic(
    const QByteArray &payload, uint32_t baseAddress, uint32_t romSize,
    const QHash<uint32_t, QString> &folderPaths,
    QVector<KpSchema750MapRecord> *mapRecords, QStringList *warnings,
    uint32_t schemaVersion = 750)
{
    QVector<MapInfo> maps;
    if (mapRecords) mapRecords->clear();
    const qsizetype sz = payload.size();
    if (sz < 5 || payload.at(0) != '\0')
        return maps;
    const uint32_t mapCount = peekU32(payload, 1);
    if (mapCount == 0 || mapCount > uint32_t((sz - 5) / 5))
        return maps;

    auto fail = [&](uint32_t index, const QString &reason) {
        if (warnings) {
            warnings->append(KpImporter::tr(
                "schema-750 object %1: %2").arg(index).arg(reason));
        }
        maps.clear();
        if (mapRecords) mapRecords->clear();
    };

    qsizetype cursor = 5;
    maps.reserve(int(mapCount));
    for (uint32_t index = 0; index < mapCount; ++index) {
        const qsizetype objectStart = cursor;
        auto skip = [&](qsizetype bytes, const QString &what) -> bool {
            if (bytes < 0 || cursor + bytes > sz) {
                fail(index, QStringLiteral("truncated %1").arg(what));
                return false;
            }
            cursor += bytes;
            return true;
        };
        auto readWordVector = [&](const QString &what, uint32_t *countOut) -> bool {
            if (cursor + 4 > sz) {
                fail(index, QStringLiteral("truncated %1 count").arg(what));
                return false;
            }
            const uint32_t count = peekU32(payload, cursor);
            cursor += 4;
            if (count > uint32_t((sz - cursor) / 4)) {
                fail(index, QStringLiteral("invalid %1 count").arg(what));
                return false;
            }
            cursor += qsizetype(count) * 4;
            if (countOut) *countOut = count;
            return true;
        };
        auto readObjectStringVector = [&](uint32_t *countOut) -> bool {
            if (cursor + 4 > sz) {
                fail(index, QStringLiteral("truncated object-string vector count"));
                return false;
            }
            const uint32_t count = peekU32(payload, cursor);
            cursor += 4;
            if (count > uint32_t((sz - cursor) / 4)) {
                fail(index, QStringLiteral("invalid object-string vector count"));
                return false;
            }
            for (uint32_t item = 0; item < count; ++item) {
                Kp750SerializedString value;
                if (!readSchema750String(payload, cursor, sz, &value)) {
                    fail(index, QStringLiteral("invalid object-string vector value"));
                    return false;
                }
                cursor = value.end;
            }
            if (countOut) *countOut = count;
            return true;
        };

        // Each prefix member has its own native serializer gate.  Do not
        // treat their absence in pre-288 projects as padding.
        if ((schemaVersion >= 268 && !skip(1, QStringLiteral("map +1A0")))
            || (schemaVersion >= 282 && !skip(4, QStringLiteral("map +90"))))
            return maps;
        Kp750SerializedString ignoredFirst;
        qsizetype structuredEnd = 0;
        if (schemaVersion >= 287) {
            if (!readKpStructuredStringForVersion(payload, cursor, sz, schemaVersion,
                                                  &ignoredFirst, &structuredEnd)) {
                fail(index, QStringLiteral("invalid first structured string"));
                return maps;
            }
            cursor = structuredEnd;
        }
        if (schemaVersion >= 93 && !skip(1, QStringLiteral("map raw byte")))
            return maps;
        Kp750SerializedString displayName;
        if (!readKpStructuredStringForVersion(payload, cursor, sz, schemaVersion,
                                              &displayName, &structuredEnd)) {
            fail(index, QStringLiteral("invalid display structured string"));
            return maps;
        }
        cursor = structuredEnd;
        if (cursor + 24 > sz) {
            fail(index, QStringLiteral("truncated map member prefix"));
            return maps;
        }
        const uint32_t kind = peekU32(payload, cursor);
        const uint32_t constant2 = peekU32(payload, cursor + 4);
        const uint32_t subtype = peekU32(payload, cursor + 8);
        const int32_t elementSize = peekI32(payload, cursor + 12);
        const int32_t wireRecordType = peekI32(payload, cursor + 16);
        const uint32_t folderId = peekU32(payload, cursor + 20);
        cursor += 24;
        Kp750SerializedString technicalId;
        if (!readKpStringForVersion(payload, cursor, sz, schemaVersion, &technicalId)) {
            fail(index, QStringLiteral("invalid technical identifier"));
            return maps;
        }
        cursor = technicalId.end;
        const qsizetype versionedPrefixBytes = 4 /* +0x5c */ + 1 + 4
            + (schemaVersion >= 298 ? 4 : 0) + (schemaVersion >= 299 ? 4 : 0);
        if (cursor + versionedPrefixBytes > sz) {
            fail(index, QStringLiteral("truncated map versioned prefix"));
            return maps;
        }
        if (schemaVersion >= 298) cursor += 4; // +0x58
        const int32_t precision = schemaVersion >= 299 ? peekI32(payload, cursor) : -1;
        if (schemaVersion >= 299) cursor += 4; // +0x11c
        cursor += 4; // +0x5c
        const bool boolAt50 = payload.at(int(cursor)) != 0; // native member +50
        cursor += 1 + 4; // +50 and +C0
        if ((schemaVersion >= 300 && !skip(6 * 8, QStringLiteral("map raw64 fields")))
            || !skip(6 * 8, QStringLiteral("map float64 fields"))
            || cursor + 24 > sz) {
            if (cursor <= sz) fail(index, QStringLiteral("truncated map core"));
            return maps;
        }
        cursor += 4; // bool +104..+107
        const int32_t wireColumns = peekI32(payload, cursor);
        const int32_t rows = peekI32(payload, cursor + 4);
        cursor += 8 + 8 + 4; // dimensions, raw +18C, +198
        // KpMapObjectCodec applies this sole dimension normalization after
        // deserializing both axis descriptors.  It does not impose the old
        // importer-specific 999 limit and it does not clamp the Y member.
        const int32_t columns = wireColumns > 0x4000 ? 0x4000 : wireColumns;

        Kp750SerializedString propertyName, unit;
        if (!readKpStringForVersion(payload, cursor, sz, schemaVersion, &propertyName)
            || !readKpStringForVersion(payload, propertyName.end, sz, schemaVersion, &unit)) {
            fail(index, QStringLiteral("invalid map properties strings"));
            return maps;
        }
        cursor = unit.end;
        const qsizetype propertyBytes = 16 + 12
            + (schemaVersion >= 264 ? 8 : 0)
            + (schemaVersion >= 61 ? 4 : 0)
            + (schemaVersion >= 105 ? 16 : 0);
        if (cursor + propertyBytes > sz) {
            fail(index, QStringLiteral("truncated map properties"));
            return maps;
        }
        const double factor = peekF64(payload, cursor);
        const double offset = peekF64(payload, cursor + 8);
        const uint32_t mapStart = peekU32(payload, cursor + 16);
        const uint32_t mapEnd = peekU32(payload, cursor + 20);
        const uint32_t mapBase = peekU32(payload, cursor + 24);
        // Native member +0x260: gate 264 raw64 — extra map-scaling double
        // compared by WinOLS duplicate detection alongside factor and offset.
        const double scaleExtra = schemaVersion >= 264
            ? peekF64(payload, cursor + 28) : 0.0;
        cursor += propertyBytes;
        Kp750AxisBlock axisX, axisY;
        if (!parseSchema750AxisBlock(payload, cursor, sz, &axisX, schemaVersion)
            || !parseSchema750AxisBlock(payload, axisX.nextAxis, sz, &axisY, schemaVersion)) {
            fail(index, QStringLiteral("invalid axis descriptor"));
            return maps;
        }
        const qsizetype tailStart = axisY.nextAxis;
        cursor = tailStart;
        if (!skip(14, QStringLiteral("post-axis v9-v90"))
            || !skip(22, QStringLiteral("post-axis v49"))
            || !skip(44, QStringLiteral("post-axis v51-v55"))
            // These calls occur in native source order rather than numeric
            // gate order.  Keeping the individual gates avoids shifting all
            // later records in schema 315/330/356/372 streams.
            || (schemaVersion >= 315 && !skip(4, QStringLiteral("post-axis v315")))
            || (schemaVersion >= 383 && !skip(20, QStringLiteral("post-axis v383")))
            || (schemaVersion >= 329 && !skip(4, QStringLiteral("post-axis v329")))
            || (schemaVersion >= 346 && !skip(8, QStringLiteral("post-axis v346")))
            || (schemaVersion >= 395 && !skip(4, QStringLiteral("post-axis v395")))
            || (schemaVersion >= 476
                && !skip(4, QStringLiteral("post-axis v476 integer"))))
            return maps;
        uint32_t wordVectorCount = 0;
        uint32_t stringVectorCount = 0;
        if ((schemaVersion >= 476
                && (!readWordVector(QStringLiteral("post-axis word vector"), &wordVectorCount)
                    || !readObjectStringVector(&stringVectorCount)))
            || (schemaVersion >= 834
                && !skip(4, QStringLiteral("post-axis v834 integer")))
            || (schemaVersion >= 503
                && !skip(12, QStringLiteral("post-axis v503")))
            || (schemaVersion >= 596
                && !skip(4, QStringLiteral("post-axis v596"))))
            return maps;
        const qsizetype objectEnd = cursor;

        KpSchema750MapRecord nativeRecord;
        if (schemaVersion >= 439
            && (!readKp750MapRecord(payload, objectStart, sz, &nativeRecord, schemaVersion)
                || nativeRecord.streamEnd != objectEnd)) {
            fail(index, QStringLiteral("field ledger disagrees with native object boundary"));
            return maps;
        }

        const int dx = kind == 2 ? 1 : columns;
        const int dy = kind == 3 ? 1 : (kind == 2 ? 1 : rows);
        const bool validProjectionShape = dx > 0 && dy > 0 && elementSize >= 0;
        const uint64_t logicalLength = validProjectionShape
            ? uint64_t(dx) * uint64_t(dy) * uint64_t(elementSize) : 0;
        const bool positiveSpan = mapEnd > mapStart
            && validProjectionShape && uint64_t(mapEnd - mapStart) == logicalLength;
        const bool zeroSpan = mapStart != 0 && mapEnd == mapStart;
        const int32_t nativeRecordType = (wireRecordType == 2 || wireRecordType == 10
                                          || wireRecordType == 16)
            ? wireRecordType : 10;

        MapInfo map;
        map.name = displayName.text;
        map.description = displayName.text;
        map.type = typeFromKpKind(kind, dx, dy);
        map.dataSize = elementSize;
        map.dimensions = { dx, dy };
        map.linkConfidence = 100;
        map.columnMajor = false;
        map.olsUniversalBase = mapBase;
        // Native member +0x50 is retained below. Its UI meaning has not yet
        // been established by a controlled WinOLS differential, so it must
        // not be projected as MapInfo::dataSigned.
        map.length = positiveSpan && mapEnd - mapStart <= uint32_t(std::numeric_limits<int>::max())
            ? int(mapEnd - mapStart)
            : (logicalLength <= uint64_t(std::numeric_limits<int>::max())
                ? int(logicalLength) : 0);
        map.setSideProp(QStringLiteral("kpSchemaVersion"), schemaVersion);
        map.setSideProp(QStringLiteral("kpInternRecordStart"), objectStart);
        map.setSideProp(QStringLiteral("kpInternRecordEnd"), objectEnd);
        map.setSideProp(QStringLiteral("kpKind"), kind);
        map.setSideProp(QStringLiteral("kpSubtype"), subtype);
        map.setSideProp(QStringLiteral("kpRecordTypeWire"), wireRecordType);
        map.setSideProp(QStringLiteral("kpRecordType"), nativeRecordType);
        map.setSideProp(QStringLiteral("kpWireColumns"), wireColumns);
        map.setSideProp(QStringLiteral("kpNativeColumns"), columns);
        map.setSideProp(QStringLiteral("kpTechnicalId"), technicalId.text);
        map.setSideProp(QStringLiteral("kpMapIdentifier"), technicalId.text);
        map.setSideProp(QStringLiteral("kpUnit"), unit.text);
        map.setSideProp(QStringLiteral("kpMapFactor"), factor);
        map.setSideProp(QStringLiteral("kpMapOffset"), offset);
        map.setSideProp(QStringLiteral("kpMapScaleExtra"), scaleExtra);
        map.setSideProp(QStringLiteral("kpMapStart"), mapStart);
        map.setSideProp(QStringLiteral("kpMapEnd"), mapEnd);
        map.setSideProp(QStringLiteral("kpMapPrecisionRaw"), precision);
        map.setSideProp(QStringLiteral("kpMapBool50"), boolAt50);
        map.setSideProp(QStringLiteral("kpPostAxisWordVectorCount"), wordVectorCount);
        map.setSideProp(QStringLiteral("kpPostAxisStringVectorCount"), stringVectorCount);
        map.setSideProp(QStringLiteral("kpDescriptorRangeMatches"), positiveSpan || zeroSpan);
        map.setSideProp(QStringLiteral("kpSerializedTail"),
                        payload.mid(int(tailStart), int(objectEnd - tailStart)));
        map.setSideProp(QStringLiteral("kpSerializedRecord"),
                        payload.mid(int(objectStart), int(objectEnd - objectStart)));
        if (!technicalId.text.isEmpty())
            map.setSideProp(QStringLiteral("kpIdName"), technicalId.text);
        if (!folderPaths.isEmpty())
            map.folderPath = folderPaths.value(folderId);

        if (!positiveSpan && !zeroSpan && warnings) {
            warnings->append(KpImporter::tr(
                "schema-750 object %1: descriptor range is retained but does not match its "
                "decoded dimensions").arg(index));
        }
        if (!validProjectionShape && warnings) {
            warnings->append(KpImporter::tr(
                "schema-750 object %1: non-positive dimensions or element size cannot be "
                "projected into MapInfo").arg(index));
        }

        if (std::isfinite(factor) && std::isfinite(offset)) {
            map.scaling.type = CompuMethod::Type::Linear;
            map.scaling.linA = factor;
            map.scaling.linB = offset;
            // The serialized integer is preserved as kpMapPrecisionRaw above.
            // It is not a proven display-format field.
            map.hasScaling = factor != 1.0 || offset != 0.0;
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
            destination.inputName = axis.name.isEmpty() ? axis.unit
                : (axis.unit.isEmpty() ? axis.name
                                       : QStringLiteral("%1 [%2]").arg(axis.name, axis.unit));
            destination.scaling.type = CompuMethod::Type::Linear;
            destination.scaling.linA = axis.factor;
            destination.scaling.linB = axis.offset;
            destination.hasScaling = std::isfinite(axis.factor)
                && std::isfinite(axis.offset)
                && (axis.factor != 1.0 || axis.offset != 0.0);
            destination.ptsDataSize = axis.dataSize;
            destination.ptsDataType = axis.dataType;
            destination.ptsBigEndian = false;
            destination.ptsSigned = axis.pointsSigned;
            destination.ptsCount = pointCount;
            map.setSideProp(prefix + QStringLiteral("Identifier"), source.identifier.text);
            // Kept under the established property keys for project-file
            // compatibility.  Native evidence identifies this as member
            // +0x168's byte vector and +0x400's structured-string count,
            // not an array of fixed-size inline axis records.
            map.setSideProp(prefix + QStringLiteral("InlineData"), source.byteVector);
            map.setSideProp(prefix + QStringLiteral("InlineCount"), source.structuredNameCount);
            map.setSideProp(prefix + QStringLiteral("PrecisionRaw"), axis.precision);
            map.setSideProp(prefix + QStringLiteral("RawAddress"), axis.rawAddr);
            map.setSideProp(prefix + QStringLiteral("RecordTypeWire"), axis.wireRecordType);
            map.setSideProp(prefix + QStringLiteral("RecordType"), axis.nativeRecordType);
            map.setSideProp(prefix + QStringLiteral("AxisFlag80"), source.flag80);
            map.setSideProp(prefix + QStringLiteral("AxisFlag81"), source.flag81);
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
        if (mapRecords && schemaVersion >= 439) mapRecords->append(nativeRecord);
        cursor = objectEnd;
    }
    if (cursor != sz) {
        if (warnings) {
            warnings->append(KpImporter::tr(
                "schema-750 objects end at 0x%1, intern ends at 0x%2")
                    .arg(cursor, 0, 16).arg(sz, 0, 16));
        }
        maps.clear();
        if (mapRecords) mapRecords->clear();
    }
    return maps;
}

// Native KpMapObjectCodec branch for effective schema 292.  Unlike schema
// 750 it uses the pre-439 string wire form and stops before gates 315/329/…;
// this is a separate grammar, not a 750 parser with a shorter tail.
QVector<MapInfo> parseSchema292Deterministic(
    const QByteArray &payload, uint32_t baseAddress, uint32_t romSize,
    const QHash<uint32_t, QString> &folderPaths, QStringList *warnings)
{
    QVector<MapInfo> maps;
    const qsizetype size = payload.size();
    if (size < 5 || payload.at(0) != '\0') return maps;
    const uint32_t count = peekU32(payload, 1);
    if (count > uint32_t((size - 5) / 5)) return maps;
    qsizetype cursor = 5;
    auto warn = [&](uint32_t index, const QString &message) {
        if (warnings) warnings->append(KpImporter::tr("schema-292 object %1: %2")
                                       .arg(index).arg(message));
        maps.clear();
    };
    auto readAxis = [&](Kp750Axis *axis) -> bool {
        if (!axis) return false;
        Kp750SerializedString name, unit;
        if (!readLegacyKpString(payload, cursor, size, &name)) return false;
        cursor = name.end;
        if (!readLegacyKpString(payload, cursor, size, &unit)) return false;
        cursor = unit.end;
        if (cursor + 16 + 20 + 2 + 8 + 8 + 1 + 4 > size) return false;
        axis->name = name.text;
        axis->unit = unit.text;
        axis->factor = peekF64(payload, cursor);
        axis->offset = peekF64(payload, cursor + 8);
        axis->hasFactor = true;
        const uint32_t rawAddr = peekU32(payload, cursor + 20);
        const uint32_t dataType = peekU32(payload, cursor + 24);
        const int32_t dataSize = peekI32(payload, cursor + 28);
        const int32_t wireRecordType = peekI32(payload, cursor + 32);
        cursor += 16 + 20 + 2 + 8;
        axis->precision = peekI32(payload, cursor);
        cursor += 8; // +0x68 and +0x90
        axis->pointsSigned = payload.at(int(cursor)) != 0;
        ++cursor;
        const int32_t byteCount = peekI32(payload, cursor);
        cursor += 4;
        if (byteCount < 0 || cursor + qsizetype(byteCount) > size) return false;
        cursor += byteCount;
        if (cursor + 8 > size) return false; // +0x6c, +0x60
        cursor += 8;
        axis->rawAddr = rawAddr;
        axis->dataType = dataType;
        axis->dataSize = dataSize;
        axis->wireRecordType = wireRecordType;
        axis->nativeRecordType = (wireRecordType == 2 || wireRecordType == 10
                                  || wireRecordType == 16) ? wireRecordType : 10;
        return true;
    };

    maps.reserve(int(count));
    for (uint32_t index = 0; index < count; ++index) {
        const qsizetype recordStart = cursor;
        auto need = [&](qsizetype bytes) { return bytes >= 0 && cursor + bytes <= size; };
        if (!need(5)) { warn(index, QStringLiteral("truncated prefix")); return maps; }
        cursor += 5; // +0x1a0 byte, +0x90 int
        Kp750SerializedString ignored, displayName, technicalId, propertyName, unit;
        if (!readLegacyKpString(payload, cursor, size, &ignored)) {
            warn(index, QStringLiteral("invalid first string")); return maps;
        }
        cursor = ignored.end;
        if (!need(1)) { warn(index, QStringLiteral("truncated map byte")); return maps; }
        ++cursor;
        if (!readLegacyKpString(payload, cursor, size, &displayName)) {
            warn(index, QStringLiteral("invalid display name")); return maps;
        }
        cursor = displayName.end;
        if (!need(24)) { warn(index, QStringLiteral("truncated integer prefix")); return maps; }
        const uint32_t kind = peekU32(payload, cursor);
        const uint32_t subtype = peekU32(payload, cursor + 8);
        const int32_t elementSize = peekI32(payload, cursor + 12);
        const int32_t wireRecordType = peekI32(payload, cursor + 16);
        const uint32_t folderId = peekU32(payload, cursor + 20);
        cursor += 24;
        if (!readLegacyKpString(payload, cursor, size, &technicalId)) {
            warn(index, QStringLiteral("invalid technical identifier")); return maps;
        }
        cursor = technicalId.end;
        if (!need(4 + 1 + 4 + 48 + 4 + 8 + 4)) {
            warn(index, QStringLiteral("truncated map core")); return maps;
        }
        cursor += 4; // +0x5c
        const bool boolAt50 = payload.at(int(cursor)) != 0;
        cursor += 1 + 4; // +0x50, +0xc0
        cursor += 48; // schema-292: six Float64 fields
        cursor += 4; // +0x104..+0x107
        const int32_t wireColumns = peekI32(payload, cursor);
        const int32_t rows = peekI32(payload, cursor + 4);
        cursor += 8 + 8 + 4; // dimensions, +0x18c, +0x198
        const int32_t columns = wireColumns > 0x4000 ? 0x4000 : wireColumns;

        if (!readLegacyKpString(payload, cursor, size, &propertyName)) {
            warn(index, QStringLiteral("invalid property name")); return maps;
        }
        cursor = propertyName.end;
        if (!readLegacyKpString(payload, cursor, size, &unit)) {
            warn(index, QStringLiteral("invalid property unit")); return maps;
        }
        cursor = unit.end;
        if (!need(16 + 8 + 12 + 4 + 16)) {
            warn(index, QStringLiteral("truncated map properties")); return maps;
        }
        const double factor = peekF64(payload, cursor);
        const double offset = peekF64(payload, cursor + 8);
        // At schema 292 the actual carried-data start/end pair is member
        // +0x20.  The preceding +0x38/+0x48/+0x4c integers are separate
        // map-properties fields and must not be mistaken for a range.
        const uint32_t mapStart = peekU32(payload, cursor + 28);
        const uint32_t mapEnd = peekU32(payload, cursor + 32);
        const uint32_t mapBase = peekU32(payload, cursor + 24);
        cursor += 56; // complete schema-292 KpMapPropertiesCodec

        Kp750Axis axisX, axisY;
        if (!readAxis(&axisX) || !readAxis(&axisY)) {
            warn(index, QStringLiteral("invalid axis descriptor")); return maps;
        }
        if (!need(14 + 22 + 44)) {
            warn(index, QStringLiteral("truncated post-axis fields")); return maps;
        }
        cursor += 14 + 22 + 44; // gates 9..55; gate 315 is absent
        const qsizetype recordEnd = cursor;

        const int dx = kind == 2 ? 1 : columns;
        const int dy = kind == 3 ? 1 : (kind == 2 ? 1 : rows);
        const bool validShape = dx > 0 && dy > 0 && elementSize >= 0;
        const uint64_t logicalLength = validShape
            ? uint64_t(dx) * uint64_t(dy) * uint64_t(elementSize) : 0;
        const bool positiveSpan = mapEnd > mapStart && validShape
            && uint64_t(mapEnd - mapStart) == logicalLength;
        const bool zeroSpan = mapStart != 0 && mapEnd == mapStart;
        const int32_t nativeRecordType = (wireRecordType == 2 || wireRecordType == 10
                                          || wireRecordType == 16) ? wireRecordType : 10;

        MapInfo map;
        map.name = displayName.text;
        map.description = displayName.text;
        map.type = typeFromKpKind(kind, dx, dy);
        map.dataSize = elementSize;
        map.dimensions = {dx, dy};
        map.linkConfidence = 100;
        map.columnMajor = false;
        map.olsUniversalBase = mapBase;
        map.length = positiveSpan && mapEnd - mapStart <= uint32_t(std::numeric_limits<int>::max())
            ? int(mapEnd - mapStart)
            : (logicalLength <= uint64_t(std::numeric_limits<int>::max())
                ? int(logicalLength) : 0);
        map.setSideProp(QStringLiteral("kpSchemaVersion"), 292);
        map.setSideProp(QStringLiteral("kpInternRecordStart"), recordStart);
        map.setSideProp(QStringLiteral("kpInternRecordEnd"), recordEnd);
        map.setSideProp(QStringLiteral("kpSerializedRecord"),
                        payload.mid(int(recordStart), int(recordEnd - recordStart)));
        map.setSideProp(QStringLiteral("kpKind"), kind);
        map.setSideProp(QStringLiteral("kpSubtype"), subtype);
        map.setSideProp(QStringLiteral("kpRecordTypeWire"), wireRecordType);
        map.setSideProp(QStringLiteral("kpRecordType"), nativeRecordType);
        map.setSideProp(QStringLiteral("kpTechnicalId"), technicalId.text);
        map.setSideProp(QStringLiteral("kpMapIdentifier"), technicalId.text);
        map.setSideProp(QStringLiteral("kpFolderId"), folderId);
        map.setSideProp(QStringLiteral("kpMapBool50"), boolAt50);
        map.setSideProp(QStringLiteral("kpMapFactor"), factor);
        map.setSideProp(QStringLiteral("kpMapOffset"), offset);
        map.setSideProp(QStringLiteral("kpUnit"), unit.text);
        map.setSideProp(QStringLiteral("kpWireColumns"), wireColumns);
        map.setSideProp(QStringLiteral("kpNativeColumns"), columns);
        map.setSideProp(QStringLiteral("kpDescriptorRangeMatches"), positiveSpan || zeroSpan);
        if (!folderPaths.isEmpty())
            map.folderPath = folderPaths.value(folderId);
        if (std::isfinite(factor) && std::isfinite(offset)) {
            map.scaling.type = CompuMethod::Type::Linear;
            map.scaling.linA = factor;
            map.scaling.linB = offset;
            map.hasScaling = factor != 1.0 || offset != 0.0;
        }
        uint32_t fileOffset = 0;
        if (positiveSpan && normalizeKpAddress(mapStart, mapEnd, mapBase,
                                                baseAddress, romSize, &fileOffset)) {
            map.rawAddress = (baseAddress != 0 && fileOffset == mapStart)
                ? baseAddress + fileOffset : mapStart;
            map.address = fileOffset;
        }
        auto fillAxis = [&](AxisInfo &destination, const Kp750Axis &axis,
                            int points, const QString &prefix) {
            destination.inputName = axis.name.isEmpty() ? axis.unit
                : (axis.unit.isEmpty() ? axis.name
                                        : QStringLiteral("%1 [%2]").arg(axis.name, axis.unit));
            destination.scaling.type = CompuMethod::Type::Linear;
            destination.scaling.linA = axis.factor;
            destination.scaling.linB = axis.offset;
            destination.hasScaling = std::isfinite(axis.factor) && std::isfinite(axis.offset)
                && (axis.factor != 1.0 || axis.offset != 0.0);
            destination.ptsDataSize = axis.dataSize;
            destination.ptsDataType = axis.dataType;
            destination.ptsSigned = axis.pointsSigned;
            destination.ptsBigEndian = false;
            destination.ptsCount = points;
            map.setSideProp(prefix + QStringLiteral("RecordTypeWire"), axis.wireRecordType);
            map.setSideProp(prefix + QStringLiteral("RecordType"), axis.nativeRecordType);
            uint32_t pointOffset = 0;
            if (axis.rawAddr != 0 && normalizeKpAxisAddress(axis.rawAddr, points,
                                                             axis.dataSize, baseAddress,
                                                             romSize, &pointOffset)) {
                destination.ptsAddress = pointOffset;
                destination.hasPtsAddress = true;
            }
        };
        if (kind != 2) fillAxis(map.xAxis, axisX, dx, QStringLiteral("kpXAxis"));
        if (kind == 4) fillAxis(map.yAxis, axisY, dy, QStringLiteral("kpYAxis"));
        if ((!positiveSpan && !zeroSpan) || !validShape) {
            if (warnings) warnings->append(KpImporter::tr(
                "schema-292 object %1: descriptor range or shape cannot be projected exactly")
                .arg(index));
        }
        maps.append(map);
    }
    if (cursor != size) {
        if (warnings) warnings->append(KpImporter::tr(
            "schema-292 objects end at 0x%1, intern ends at 0x%2")
            .arg(cursor, 0, 16).arg(size, 0, 16));
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

    // The file header is exactly 0x18 bytes: after the validated literal,
    // schema version and declared file size, WinOLS invokes
    // ReadWriteKpImportModelMetadata(serializer, importModel + 0x08).
    // For schema 750 its version-gated stream must be completely consumed
    // before the embedded ZIP local header.  Keeping this boundary strict is
    // important: accepting an arbitrary pre-ZIP suffix would make a changed
    // import-model grammar look like a valid map pack.
    if (result.formatVersion == 750) {
        qsizetype metadataFailure = 0;
        QString metadataFailurePath;
        if (!readKp750ImportModelMetadata(fileData, 0x18, archiveStart,
                                          &result.metadata, &metadataFailure,
                                          &metadataFailurePath)) {
            result.error = KpImporter::tr(
                "Schema-750 import-model metadata is invalid at %1 (offset 0x%2)")
                .arg(metadataFailurePath)
                .arg(metadataFailure, 0, 16, QLatin1Char('0'));
            return result;
        }
        qsizetype rootFailure = 0;
        QString rootFailurePath;
        if (!readKp750Root(fileData, result.metadata.streamEnd, archiveStart,
                           archiveEnd - archiveStart,
                           &result.root, &rootFailure, &rootFailurePath)) {
            result.error = KpImporter::tr(
                "Schema-750 root stream is invalid at %1 (offset 0x%2)")
                .arg(rootFailurePath)
                .arg(rootFailure, 0, 16, QLatin1Char('0'));
            return result;
        }
        if (result.root.streamEnd != archiveStart) {
            result.error = KpImporter::tr(
                "Schema-750 root stream ends at 0x%1, expected ZIP at 0x%2")
                .arg(result.root.streamEnd, 0, 16, QLatin1Char('0'))
                .arg(archiveStart, 0, 16, QLatin1Char('0'));
            return result;
        }
    }

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
    QHash<uint32_t, KpFolder> folders;
    if (result.formatVersion < 597) {
        folders = parseKpFolderTableLegacy(fileData, archiveEnd, result.formatVersion,
                                           &result.folders, &result.warnings);
    } else if (result.formatVersion < 750) {
        folders = parseKpFolderTable597(fileData, archiveEnd, result.formatVersion,
                                        &result.folders, &result.warnings);
    } else {
        folders = parseKpFolderTable(fileData, archiveEnd, result.formatVersion,
                                     &result.folders, &result.warnings);
    }
    const QHash<uint32_t, QString> folderPaths = resolveKpFolderPaths(folders);

    // WinOLS's schema value is cumulative, not a format-family identifier:
    // each serializer call compares its effective version to its own gate.
    // The current native reader accepts 6 through 834; the portable walk
    // mirrors those gates and verifies that every map record is consumed.
    //
    // ReadWriteKpInternMapObjectArray gate 0xf3 (243): the nested intern
    // reader inherits the parent's effective version only when the outer
    // schema is at least 243.  Below that threshold the default file-reader
    // version (0) applies, so no object-codec gates are active.
    const uint32_t internMapCount = intern.size() >= 5 ? peekU32(intern, 1) : 0;
    const uint32_t effectiveMapVersion = (result.formatVersion >= 243)
        ? result.formatVersion : 0;
    switch (result.formatVersion) {
    case 292:
        result.maps = parseSchema292Deterministic(intern, baseAddress, romSize,
                                                  folderPaths, &result.warnings);
        if (internMapCount > 0 && result.maps.size() != int(internMapCount)) {
            result.error = KpImporter::tr(
                "Schema-292 parser recognized %1 of %2 maps; refusing an "
                "incomplete import rather than guessing the remaining layout")
                .arg(result.maps.size()).arg(internMapCount);
            return result;
        }
        break;
    case 750:
        result.maps = parseSchema750Deterministic(intern, baseAddress, romSize,
                                                  folderPaths, &result.mapRecords,
                                                  &result.warnings);
        if (internMapCount > 0 && result.maps.size() != int(internMapCount)) {
            result.error = KpImporter::tr(
                "Schema-750 parser recognized %1 of %2 maps; refusing an "
                "incomplete import rather than guessing the remaining layout")
                .arg(result.maps.size()).arg(internMapCount);
            return result;
        }
        attachKpCarriedData(fileData, &result.maps, &result.carriedData,
                            &result.warnings, true, false);
        break;
    case 597:
        // The map/axis codec reaches the same cumulative gate set as schema
        // 750 through member +0x100 (gate 596).  Its outer/root and folder
        // containers remain version-specific and are intentionally handled
        // outside this shared object walk.
        result.maps = parseSchema750Deterministic(intern, baseAddress, romSize,
                                                  folderPaths, &result.mapRecords,
                                                  &result.warnings, 597);
        if (internMapCount > 0 && result.maps.size() != int(internMapCount)) {
            result.error = KpImporter::tr(
                "Schema-597 parser recognized %1 of %2 maps; refusing an "
                "incomplete import rather than guessing the remaining layout")
                .arg(result.maps.size()).arg(internMapCount);
            return result;
        }
        attachKpCarriedData(fileData, &result.maps, &result.carriedData,
                            &result.warnings, false, true);
        break;
    case 503:
    case 479:
    case 440:
    case 397:
    case 396:
    case 372:
    case 356:
    case 330:
    case 315:
    case 290:
    case 288:
    case 264:
    case 252:
    case 249:
    case 245:
        result.maps = parseSchema750Deterministic(intern, baseAddress, romSize,
                                                  folderPaths, &result.mapRecords,
                                                  &result.warnings,
                                                  result.formatVersion);
        if (internMapCount > 0 && result.maps.size() != int(internMapCount)) {
            result.error = KpImporter::tr(
                "Schema-%1 parser recognized %2 of %3 maps; refusing an incomplete import "
                "rather than guessing the remaining layout")
                .arg(result.formatVersion).arg(result.maps.size()).arg(internMapCount);
            return result;
        }
        // These versions predate schema-750's scalar/singleton relocation
        // exceptions; retain their serialized span policy.
        attachKpCarriedData(fileData, &result.maps, &result.carriedData,
                            &result.warnings, false, true);
        break;
    default:
        if (result.formatVersion < 6 || result.formatVersion > 834) {
            result.error = KpImporter::tr(
                "Unsupported KP schema %1: current WinOLS accepts schemas 6 through 834")
                .arg(result.formatVersion);
            return result;
        }
        // ReadWriteKpInternMapObjectArray gate 0xf2 (242): schemas below 242
        // use the direct (non-ZIP) path.  The embedded ZIP is absent and an
        // importer cannot reconstruct the intern stream without decompressing it.
        if (result.formatVersion < 242) {
            result.error = KpImporter::tr(
                "Schema-%1 KP files predate the ZIP container (gate 242); only schema 242+ "
                "archive-backed containers are supported")
                .arg(result.formatVersion);
            return result;
        }
        result.maps = parseSchema750Deterministic(intern, baseAddress, romSize,
                                                  folderPaths, &result.mapRecords,
                                                  &result.warnings,
                                                  effectiveMapVersion);
        if (internMapCount > 0 && result.maps.size() != int(internMapCount)) {
            result.error = KpImporter::tr(
                "Schema-%1 parser recognized %2 of %3 maps; refusing an incomplete import "
                "rather than guessing the remaining layout")
                .arg(result.formatVersion).arg(result.maps.size()).arg(internMapCount);
            return result;
        }
        attachKpCarriedData(fileData, &result.maps, &result.carriedData,
                            &result.warnings, result.formatVersion >= 750,
                            result.formatVersion < 750);
        break;
    }
    result.mapCount = static_cast<uint32_t>(result.maps.size());

    return result;
}

}
