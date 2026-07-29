/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KpImporter.h"
#include "ZipDecompressor.h"

#include <QtEndian>
#include <QHash>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace ols {

namespace {

uint32_t peekU32(const QByteArray &data, qsizetype off)
{
    if (off < 0 || off + 4 > data.size()) return 0;
    return qFromLittleEndian<uint32_t>(
        reinterpret_cast<const uchar *>(data.constData() + off));
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

bool isText(const char *data, int length)
{
    if (length <= 0) return false;
    int printable = 0;
    for (int i = 0; i < length; ++i) {
        const auto b = static_cast<uint8_t>(data[i]);
        if ((b >= 0x20 && b < 0x7F) || b == 0x09 || b >= 0x80)
            ++printable;
    }
    return printable >= std::max(1, length - 1);
}

QString decodeKpText(const QByteArray &bytes)
{
    return QString::fromLatin1(bytes).trimmed();
}

int bytesFromCellBits(uint32_t bits)
{
    switch (bits) {
    case 2:  return 1;
    case 8:  return 1;
    case 10: return 2;
    case 16: return 2;
    case 32: return 4;
    default: return 2;
    }
}

QString typeFromKpKind(uint32_t kind, int x, int y)
{
    if (kind == 2) return QStringLiteral("VALUE");
    if (x <= 1 && y <= 1) return QStringLiteral("VALUE");
    if (kind == 3) return QStringLiteral("CURVE");
    if (y <= 1) return QStringLiteral("CURVE");
    return QStringLiteral("MAP");
}

struct KpRecordStart {
    qsizetype offset = -1;
    uint32_t nameLen = 0;
    qsizetype metaOffset = -1;
};

struct KpHeader {
    uint32_t kind = 0;
    uint32_t cellBits = 10;
    int dataSizeBytes = 0;
    uint32_t hintY = 0;
    uint32_t hintX = 0;
    bool legacyLayout = false;
    bool schema750 = false;
};

bool readKpHeader(const QByteArray &payload, qsizetype metaOff, KpHeader *out)
{
    if (metaOff < 0 || metaOff + 48 > payload.size()) return false;

    uint32_t v[12] = {};
    for (int i = 0; i < 12; ++i)
        v[i] = peekU32(payload, metaOff + i * 4);

    // Newer .kp map records start with a compact header, not the richer .ols
    // Kennfeld record.  This gate deliberately rejects axis labels/units that
    // are also length-prefixed strings inside the same record.
    if (v[0] == 0 && v[1] == 0 && v[2] == 0
        && v[3] >= 1 && v[3] <= 5
        && (v[7] == 2 || v[7] == 8 || v[7] == 10
            || v[7] == 16 || v[7] == 32)
        && v[8] <= 256 && v[11] <= 256) {
        if (out) {
            out->kind = v[3];
            out->cellBits = v[7];
            out->dataSizeBytes = 0;
            out->hintY = v[8];
            out->hintX = v[11];
            out->legacyLayout = false;
        }
        return true;
    }

    // OLS 5.x .kp records (schema/format version 750, "OLS 5.0 (Windows)"
    // creator string): the name's NUL is followed by 11 zero padding bytes
    // (folder references), then kind at +11, cell size in bytes at +23 and a
    // constant 0x0A marker at +27. A length-prefixed id string follows at
    // +35. Dimensions live near the end of the record as a [cols][rows]
    // uint32 pair, so only kind + data size come from this header.
    // Cannot collide with the compact gate above: these records always have
    // v[2] != 0 (kind's low byte lands in v[2]'s top byte).
    if (v[0] == 0 && peekU32(payload, metaOff + 27) == 0x0A) {
        const uint32_t kind = peekU32(payload, metaOff + 11);
        const uint32_t cellBytes = peekU32(payload, metaOff + 23);
        if (kind >= 1 && kind <= 10
            && (cellBytes == 1 || cellBytes == 2 || cellBytes == 4)) {
            if (out) {
                out->kind = kind;
                out->cellBits = 10;
                out->dataSizeBytes = int(cellBytes);
                out->hintY = 0;
                out->hintX = 0;
                out->legacyLayout = false;
                out->schema750 = true;
            }
            return true;
        }
    }

    // Older WinOLS 4.x .kp intern records are length-prefixed, NUL-terminated
    // names followed directly by: kind, data-size, dim hints and a group id.
    if (v[0] >= 1 && v[0] <= 10
        && (v[1] == 0 || v[1] == 1 || v[1] == 2 || v[1] == 4)
        && v[2] >= 1 && v[2] <= 999
        && v[3] >= 1 && v[3] <= 999
        && v[4] <= 9999) {
        if (out) {
            out->kind = v[0];
            out->cellBits = 10;
            out->dataSizeBytes = (v[1] == 0) ? 1 : int(v[1]);
            out->hintY = 0;
            out->hintX = 0;
            out->legacyLayout = true;
        }
        return true;
    }

    return false;
}

QVector<KpRecordStart> findKpRecordStarts(const QByteArray &payload,
                                          uint32_t expectedCount)
{
    QVector<KpRecordStart> starts;
    const qsizetype sz = payload.size();
    for (qsizetype pos = 14; pos + 52 < sz; ++pos) {
        const uint32_t len = peekU32(payload, pos);
        if (len == 0 || len > 240) continue;
        const qsizetype textOff = pos + 4;
        if (!isText(payload.constData() + textOff, static_cast<int>(len)))
            continue;
        const qsizetype textEnd = textOff + static_cast<qsizetype>(len);
        qsizetype metaOff = textEnd;
        if (!readKpHeader(payload, metaOff, nullptr)) {
            if (textEnd >= sz || payload.at(textEnd) != '\0')
                continue;
            metaOff = textEnd + 1;
            if (!readKpHeader(payload, metaOff, nullptr))
                continue;
        }
        starts.append({pos, len, metaOff});
        if (expectedCount > 0 && starts.size() >= static_cast<int>(expectedCount))
            break;
    }
    return starts;
}

struct AddressCandidate {
    qsizetype off = -1;
    uint32_t raw = 0;
    uint32_t end = 0;
    uint32_t universalBase = 0;
    uint32_t fileOffset = 0;
    int dataBytes = 0;
    int score = 0;
};

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

// Schema-750 stores a single packed byte stream after a 0x98728833 sentinel
// in the outer KP file. Each map contributes its value bytes, followed by its
// X-axis bytes and then its Y-axis bytes for genuine two-dimensional maps.
// Preserve offsets on the parsed maps; the caller can then honour the dialog's
// map selection and address relocation while applying values safely.
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

bool hasRepeatedAddress(const QByteArray &record, qsizetype off, uint32_t raw)
{
    const qsizetype end = qMin(record.size() - 4, off + 64);
    for (qsizetype p = off + 12; p <= end; ++p) {
        if (peekU32(record, p) == raw)
            return true;
    }
    return false;
}

bool chooseAddress(const QByteArray &record, uint32_t projectBase,
                   uint32_t romSize, int dataSize, AddressCandidate *out)
{
    AddressCandidate best;
    for (qsizetype off = 0; off + 12 <= record.size(); ++off) {
        if (off < 0x40)
            continue;

        const uint32_t raw = peekU32(record, off);
        const uint32_t end = peekU32(record, off + 4);
        const uint32_t base = peekU32(record, off + 8);
        uint32_t fileOffset = 0;
        if (!normalizeKpAddress(raw, end, base, projectBase, romSize, &fileOffset))
            continue;

        const uint32_t dataBytes = end - raw;
        if (dataBytes == 0) continue;

        int score = 0;
        const bool repeated = hasRepeatedAddress(record, off, raw);
        if (repeated) score += 35;
        if (romSize > 0 && base == romSize) score += 80;
        if (projectBase != 0 && romSize > 0 && base == projectBase + romSize)
            score += 80;
        if (romSize > 0 && base <= romSize && base >= raw) score += 15;
        if (romSize > 0 && base <= romSize && base + 0x20000u >= romSize)
            score += 10;
        if (romSize == 0 && base > raw) score += 40;
        if (dataSize > 0 && dataBytes % uint32_t(dataSize) == 0) score += 20;
        if (dataBytes <= 0x1000) score += 12;
        else if (dataBytes <= 0x10000) score += 6;
        else if (dataBytes > 0x40000) score -= 30;
        if (fileOffset == raw) score += 5; // common KP layout: offsets, not absolute flash addrs
        if (off >= 16) score += 2;         // avoids very early false positives

        if (best.off < 0 || score > best.score) {
            best.off = off;
            best.raw = raw;
            best.end = end;
            best.universalBase = base;
            best.fileOffset = fileOffset;
            best.dataBytes = static_cast<int>(dataBytes);
            best.score = score;
        }
    }
    if (best.off < 0) return false;
    if (best.score < 45) return false;
    if (out) *out = best;
    return true;
}

uint32_t dimensionHintFromName(const QString &name, int cells)
{
    if (cells <= 0) return 0;
    const QString lower = name.toLower();
    const uint32_t hints[] = { 32, 24, 20, 16, 12, 10, 8, 6, 4 };
    for (uint32_t h : hints) {
        if (cells % int(h) != 0)
            continue;
        if (lower.contains(QString::number(h)))
            return h;
    }
    return 0;
}

MapDimensions dimensionsFromLegacyRecord(const QByteArray &record,
                                         const AddressCandidate &addr,
                                         int cells)
{
    MapDimensions dims;
    if (cells <= 1 || addr.off <= 8)
        return dims;

    for (qsizetype off = 0; off + 8 <= addr.off; ++off) {
        const uint32_t x = peekU32(record, off);
        const uint32_t y = peekU32(record, off + 4);
        if (x >= 2 && x <= 256 && y >= 2 && y <= 256
            && uint64_t(x) * uint64_t(y) == uint64_t(cells)) {
            dims.x = int(x);
            dims.y = int(y);
            return dims;
        }
    }
    return dims;
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

// A length-prefixed string candidate: printable Latin-1 with at least one
// ASCII-printable byte (rejects the FF FF FF FF sentinel read as "ÿÿÿÿ").
static bool readKpString(const QByteArray &rec, qsizetype pos, qsizetype limit,
                         bool requireNul, QString *out, qsizetype *end)
{
    const uint32_t len = peekU32(rec, pos);
    if (len < 1 || len > 100) return false;
    const qsizetype textEnd = pos + 4 + qsizetype(len);
    if (textEnd > limit || textEnd > rec.size()) return false;
    if (requireNul && (textEnd >= rec.size() || rec.at(int(textEnd)) != '\0'))
        return false;
    bool hasAscii = false;
    for (qsizetype i = pos + 4; i < textEnd; ++i) {
        const auto b = static_cast<uint8_t>(rec.at(int(i)));
        if (b < 0x20 || b == 0x7F) return false;
        if (b >= 0x21 && b <= 0x7E) hasAscii = true;
    }
    if (!hasAscii) return false;
    if (out) *out = decodeKpText(rec.mid(int(pos + 4), int(len)));
    if (end) *end = textEnd + (requireNul ? 1 : 0);
    return true;
}

QVector<Kp750Axis> parseSchema750Axes(const QByteArray &record, qsizetype start)
{
    QVector<Kp750Axis> axes;
    qsizetype segStart = start;
    qsizetype q = start;
    while (q + 47 <= record.size() && axes.size() < 2) {
        const uint32_t pre = peekU32(record, q);
        if (pre <= 1) {
            const uint32_t addr = peekU32(record, q + 4);
            const uint32_t f3   = peekU32(record, q + 8);
            const uint32_t ds   = peekU32(record, q + 12);
            const uint32_t mark = peekU32(record, q + 16);
            if (addr >= 0x1000 && addr < 0x10000000 && mark == 0x0A
                && (f3 == 1 || f3 == 3 || f3 == 5 || f3 == 7)
                && (ds == 1 || ds == 2 || ds == 4)) {
                Kp750Axis ax;
                ax.rawAddr  = addr;
                ax.dataType = f3;
                ax.dataSize = int(ds);
                // KpAxisDescriptorCodec serializes its source signed flag
                // after the two post-anchor integers, raw64, and two more
                // integer fields: anchor + 46 for schema 750.
                ax.pointsSigned = record.at(int(q + 46)) != 0;
                // The axis display precision is an unaligned u32 in the
                // otherwise undocumented 31-byte area following the anchor.
                // Correlated against WinOLS: 3 / 1 gives X / Y labels such
                // as 1.600 and 45.0 for the compressor-efficiency map.
                const uint32_t precision = peekU32(record, q + 34);
                if (precision <= 6)
                    ax.precision = int(precision);

                // Strings between the previous sub-block and this anchor:
                // first is the axis name, second (if any) the unit.
                for (qsizetype b = segStart; b + 5 < q; ) {
                    QString s;
                    qsizetype e = 0;
                    if (readKpString(record, b, q, false, &s, &e)) {
                        if (ax.name.isEmpty())      ax.name = s;
                        else if (ax.unit.isEmpty()) ax.unit = s;
                        b = e;
                    } else {
                        ++b;
                    }
                }

                // Scaling: the last plausible double before the anchor. When
                // the axis has an offset, [factor][offset] are adjacent — the
                // double 8 bytes earlier is then the factor.
                for (qsizetype fb = q - 8; fb >= q - 40 && fb >= segStart; --fb) {
                    const double d1 = peekF64(record, fb);
                    if (std::isfinite(d1) && d1 != 0.0
                        && std::abs(d1) > 1e-12 && std::abs(d1) < 1e10) {
                        const double d0 = peekF64(record, fb - 8);
                        if (fb - 8 >= segStart && std::isfinite(d0) && d0 != 0.0
                            && std::abs(d0) > 1e-12 && std::abs(d0) < 1e10) {
                            ax.factor = d0;
                            ax.offset = d1;
                        } else {
                            ax.factor = d1;
                            ax.offset = 0.0;
                        }
                        ax.hasFactor = true;
                        break;
                    }
                }

                axes.append(ax);

                // Skip past this axis' trailing NUL-terminated id slug so the
                // next segment's string search starts cleanly after it.
                qsizetype next = q + 20;
                for (qsizetype s2 = q + 20;
                     s2 < qMin(record.size() - 5, q + 120); ++s2) {
                    qsizetype e = 0;
                    if (readKpString(record, s2, record.size(), true, nullptr, &e)) {
                        next = e;
                        break;
                    }
                }
                segStart = next;
                q = next;
                continue;
            }
        }
        ++q;
    }
    return axes;
}

// Schema-750 records store exact dimensions as a [cols][rows] uint32 pair
// after the address triplet (usually near the end of the record, following
// the axis sub-records). The product must equal the cell count.
MapDimensions dimensionsFromSchema750Record(const QByteArray &record,
                                            const AddressCandidate &addr,
                                            int cells)
{
    MapDimensions dims;
    if (cells <= 1 || addr.off < 0)
        return dims;

    for (qsizetype off = addr.off + 12; off + 8 <= record.size(); ++off) {
        const uint32_t x = peekU32(record, off);
        const uint32_t y = peekU32(record, off + 4);
        if (x >= 2 && x <= 999 && y >= 2 && y <= 999
            && uint64_t(x) * uint64_t(y) == uint64_t(cells)) {
            dims.x = int(x);
            dims.y = int(y);
            return dims;
        }
    }
    return dims;
}

MapDimensions dimensionsFromRecord(uint32_t kind, uint32_t hintX,
                                   int cells)
{
    MapDimensions dims;
    if (cells <= 1 || kind == 2) {
        dims.x = 1;
        dims.y = 1;
        return dims;
    }
    if (kind == 3) {
        dims.x = qBound(1, cells, 4096);
        dims.y = 1;
        return dims;
    }

    if (hintX > 1 && hintX <= 256 && cells % int(hintX) == 0) {
        const int y = cells / int(hintX);
        if (y >= 1 && y <= 256) {
            dims.x = int(hintX);
            dims.y = y;
            return dims;
        }
    }

    const uint32_t standardHints[] = { 16, 12, 10, 8 };
    for (uint32_t h : standardHints) {
        if (cells >= int(h) * 2 && cells % int(h) == 0) {
            const int y = cells / int(h);
            if (y >= 2 && y <= 256) {
                dims.x = int(h);
                dims.y = y;
                return dims;
            }
        }
    }

    int bestX = cells;
    int bestY = 1;
    int bestDelta = std::numeric_limits<int>::max();
    for (int x = 1; x <= 256; ++x) {
        if (cells % x != 0) continue;
        const int y = cells / x;
        if (y < 1 || y > 256) continue;
        const int delta = std::abs(x - y);
        if (delta < bestDelta) {
            bestDelta = delta;
            bestX = x;
            bestY = y;
        }
    }
    dims.x = qBound(1, bestX, 4096);
    dims.y = qBound(1, bestY, 4096);
    return dims;
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
                                             QStringList *warnings)
{
    QHash<uint32_t, KpFolder> folders;
    // This folder-table grammar is the OLS 5.x layout (schema >= 700). Older
    // schemas embed a folder table too but with a different entry format; their
    // map records also never carry a schema-750 folder id, so skip (and don't
    // warn) on them rather than mis-parsing an unsupported layout.
    if (peekU32(fileData, 16) < 700)
        return folders;
    static const uchar sig[12] = { 0x11, 0x88, 0x63, 0x98, 0, 0, 0, 0,
                                   0x11, 0x88, 0x63, 0x98 };
    qsizetype tblOff = -1;
    for (qsizetype i = 0; i + 16 <= fileData.size(); ++i) {
        if (std::memcmp(fileData.constData() + i, sig, 12) == 0) {
            tblOff = i;
            break;
        }
    }
    if (tblOff < 0) return folders;              // legacy/no folder table

    const uint32_t count = peekU32(fileData, tblOff + 12);
    if (count == 0 || count > 100000) return folders;

    // Parse the fixed [id][parentId][nameLen][name] header at `q`.
    auto readEntry = [&](qsizetype q, uint32_t *id, uint32_t *parent,
                         QString *name, qsizetype *nameEnd) -> bool {
        if (q + 12 > fileData.size()) return false;
        const uint32_t fid = peekU32(fileData, q);
        const uint32_t par = peekU32(fileData, q + 4);
        const uint32_t nl  = peekU32(fileData, q + 8);
        // id may be 0 (WinOLS root "My maps"); the name gate below prevents
        // the all-zero suffix from validating as a spurious entry.
        if (fid > 0x100000 || par > 0x100000) return false;
        if (nl < 1 || nl > 200 || q + 12 + qsizetype(nl) > fileData.size())
            return false;
        if (!isText(fileData.constData() + q + 12, int(nl))) return false;
        if (id)      *id     = fid;
        if (parent)  *parent = par;
        if (name)    *name   = decodeKpText(fileData.mid(int(q + 12), int(nl)));
        if (nameEnd) *nameEnd = q + 12 + qsizetype(nl);
        return true;
    };

    qsizetype q = tblOff + 16;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t id = 0, parent = 0;
        QString name;
        qsizetype nameEnd = 0;
        if (!readEntry(q, &id, &parent, &name, &nameEnd)) break;
        folders.insert(id, { parent, name });
        if (i + 1 >= count) break;
        // Each entry ends with a fixed 48-byte suffix (variant 2) or 47 bytes
        // (variant 1). Check exactly those two deterministic positions rather
        // than scanning a window, so a stray printable pair inside the binary
        // suffix can never derail the walk.
        if (readEntry(nameEnd + 48, nullptr, nullptr, nullptr, nullptr))
            q = nameEnd + 48;
        else if (readEntry(nameEnd + 47, nullptr, nullptr, nullptr, nullptr))
            q = nameEnd + 47;
        else
            break;
    }

    if (warnings && folders.size() != int(count))
        warnings->append(KpImporter::tr(
            "KP folder table: parsed %1 of %2 folders")
                .arg(folders.size()).arg(count));
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

QVector<MapInfo> parseKpIntern(const QByteArray &payload,
                               uint32_t baseAddress,
                               uint32_t romSize,
                               const QHash<uint32_t, QString> &folderPaths,
                               QStringList *warnings)
{
    QVector<MapInfo> maps;
    if (payload.size() < 14) {
        if (warnings)
            warnings->append(KpImporter::tr("intern payload too small (%1 bytes)")
                                 .arg(payload.size()));
        return maps;
    }

    const uint32_t mapCount = peekU32(payload, 1);
    if (mapCount == 0 || mapCount > 100000) {
        if (warnings)
            warnings->append(KpImporter::tr("intern payload map count %1 out of range")
                                 .arg(mapCount));
        return maps;
    }

    const QVector<KpRecordStart> starts = findKpRecordStarts(payload, mapCount);
    maps.reserve(starts.size());

    for (int idx = 0; idx < starts.size(); ++idx) {
        const KpRecordStart &rs = starts[idx];
        const qsizetype nameOff = rs.offset + 4;
        const qsizetype metaOff = rs.metaOffset;
        const qsizetype recordEnd = (idx + 1 < starts.size())
            ? starts[idx + 1].offset
            : payload.size();
        if (recordEnd <= metaOff) continue;

        KpHeader hdr;
        if (!readKpHeader(payload, metaOff, &hdr)) continue;

        const QByteArray nameBytes = payload.mid(
            static_cast<int>(nameOff), static_cast<int>(rs.nameLen));
        const QString name = decodeKpText(nameBytes);
        if (name.isEmpty()) continue;

        const QByteArray record = payload.mid(
            static_cast<int>(metaOff), static_cast<int>(recordEnd - metaOff));

        int dataSize = (hdr.dataSizeBytes > 0)
            ? hdr.dataSizeBytes
            : bytesFromCellBits(hdr.cellBits);
        AddressCandidate addr;
        if (!chooseAddress(record, baseAddress, romSize, dataSize, &addr))
            continue;

        if (addr.dataBytes > 0 && dataSize > 0
            && addr.dataBytes % dataSize != 0
            && hdr.kind == 2 && addr.dataBytes <= 4)
            dataSize = addr.dataBytes;

        int cells = dataSize > 0 ? addr.dataBytes / dataSize : 1;
        if (cells <= 0) cells = 1;

        MapInfo m;
        m.name           = name;
        // Like WinOLS: the user-friendly name is what's displayed, so it is
        // both the name and the description. The internal id slug at +35
        // (e.g. "med17.9_dwell-time-map_16L-0_0") is kept as a side property.
        m.description    = name;
        if (hdr.schema750) {
            const uint32_t idLen = peekU32(record, 35);
            if (idLen >= 1 && idLen <= 200
                && 39 + qsizetype(idLen) <= record.size()) {
                const QByteArray idBytes = record.mid(39, int(idLen));
                if (isText(idBytes.constData(), idBytes.size())) {
                    const QString idStr = decodeKpText(idBytes);
                    if (!idStr.isEmpty())
                        m.setSideProp(QStringLiteral("kpIdName"), idStr);
                }
            }
            // Folder id lives at metadata +0x1F; resolve it to the project-tree
            // path so identically-named maps land in their own (sub)folder.
            if (!folderPaths.isEmpty()) {
                const uint32_t folderId = peekU32(record, 31);
                const QString fp = folderPaths.value(folderId);
                if (!fp.isEmpty()) m.folderPath = fp;
            }
        }
        m.type           = typeFromKpKind(hdr.kind, 1, 1);
        m.rawAddress     = (baseAddress != 0 && addr.fileOffset == addr.raw)
            ? baseAddress + addr.fileOffset
            : addr.raw;
        m.address        = addr.fileOffset;
        m.olsUniversalBase = addr.universalBase;
        m.dataSize       = dataSize;
        if (hdr.legacyLayout && hdr.kind != 2 && hdr.kind != 3)
            m.dimensions = dimensionsFromLegacyRecord(record, addr, cells);
        if (hdr.schema750 && hdr.kind != 2 && hdr.kind != 3)
            m.dimensions = dimensionsFromSchema750Record(record, addr, cells);
        if (m.dimensions.x <= 1 && m.dimensions.y <= 1) {
            uint32_t hintX = hdr.hintX;
            if (hintX == 0)
                hintX = dimensionHintFromName(name, cells);
            m.dimensions = dimensionsFromRecord(hdr.kind, hintX, cells);
        }
        m.type           = typeFromKpKind(hdr.kind, m.dimensions.x, m.dimensions.y);
        m.length         = qMax(1, addr.dataBytes);
        m.linkConfidence = 100;
        // KP carries cells in row-major order: the consecutive first cells
        // are the first displayed WinOLS row, not its first column.
        m.columnMajor    = false;

        // Schema-750: import the axis sub-blocks (X = columns, then Y = rows).
        // Do not use m.rawAddress - m.address as an axis delta here: rawAddress
        // can be displayed in ECU space while the KP axis entries stay in file
        // space, which would silently discard every axis on a based project.
        if (hdr.schema750 && hdr.kind != 2) {
            const QVector<Kp750Axis> axes =
                parseSchema750Axes(record, addr.off + 12);
            auto fillAxis = [&](AxisInfo &dst, const Kp750Axis &src, int count) {
                dst.inputName = src.unit.isEmpty()
                    ? src.name
                    : QStringLiteral("%1 [%2]").arg(src.name, src.unit);
                if (src.hasFactor) {
                    dst.hasScaling   = true;
                    dst.scaling.type = CompuMethod::Type::Linear;
                    dst.scaling.linA = src.factor;
                    dst.scaling.linB = src.offset;
                    if (src.precision >= 0)
                        dst.scaling.format = QStringLiteral("1.%1f").arg(src.precision);
                }
                dst.ptsDataSize = src.dataSize;
                dst.ptsDataType = src.dataType;
                dst.ptsBigEndian = false;
                dst.ptsSigned   = src.pointsSigned;
                dst.ptsCount    = count;
                uint32_t fileOff = 0;
                if (normalizeKpAxisAddress(src.rawAddr, count, src.dataSize,
                                           baseAddress, romSize, &fileOff)) {
                    dst.ptsAddress    = fileOff;
                    dst.hasPtsAddress = true;
                }
            };
            if (axes.size() >= 1)
                fillAxis(m.xAxis, axes[0], m.dimensions.x);
            if (axes.size() >= 2 && m.dimensions.y > 1)
                fillAxis(m.yAxis, axes[1], m.dimensions.y);
        }

        if (addr.off >= 16) {
            const double scale = peekF64(record, addr.off - 16);
            const double offset = peekF64(record, addr.off - 8);
            if (std::isfinite(scale) && std::isfinite(offset)
                && std::abs(scale) < 1e12 && std::abs(offset) < 1e12
                && (scale != 0.0 && scale != 1.0 || offset != 0.0)) {
                m.hasScaling = true;
                m.scaling.type = CompuMethod::Type::Linear;
                m.scaling.linA = scale;
                m.scaling.linB = offset;
            }
        }

        maps.append(m);
    }

    if (maps.size() != int(mapCount) && warnings) {
        warnings->append(KpImporter::tr(
            "intern map_count = %1 but parser decoded %2 records")
                .arg(mapCount).arg(maps.size()));
    }
    return maps;
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
    const uint32_t mapCount = peekU32(payload, 1);

    // Object marker: reserved 0, u32 0xFFFFFFFF, 17 zero bytes, then a valid
    // length-prefixed display name. This 22-byte structural signature does not
    // occur inside map data, so it delimits objects deterministically.
    auto isObjectStart = [&](qsizetype p) -> bool {
        if (p + 0x1a > sz) return false;
        if (static_cast<uchar>(payload[p]) != 0x00) return false;
        if (peekU32(payload, p + 1) != 0xFFFFFFFFu) return false;
        for (int k = 0; k < 17; ++k)
            if (static_cast<uchar>(payload[p + 5 + k]) != 0x00) return false;
        const uint32_t nl = peekU32(payload, p + 0x16);
        if (nl < 1 || nl > 200 || p + 0x1a + qsizetype(nl) > sz) return false;
        return isText(payload.constData() + p + 0x1a,
                      int(qMin<uint32_t>(nl, 8)));
    };

    QVector<qsizetype> starts;
    for (qsizetype p = 5; p + 0x1a < sz; ++p)
        if (isObjectStart(p)) starts.append(p);

    // Serialized string/reference: i32 marker; >=0 means that many inline text
    // bytes follow, <0 is a reference/absent sentinel with no inline bytes.
    auto walkString = [&](qsizetype pos) -> qsizetype {
        if (pos + 4 > sz) return sz;
        const int32_t m = static_cast<int32_t>(peekU32(payload, pos));
        return (m >= 0 && pos + 4 + qsizetype(m) <= sz) ? pos + 4 + m : pos + 4;
    };

    maps.reserve(starts.size());
    for (int idx = 0; idx < starts.size(); ++idx) {
        const qsizetype s = starts[idx];
        const qsizetype objEnd = (idx + 1 < starts.size()) ? starts[idx + 1] : sz;

        const uint32_t nl = peekU32(payload, s + 0x16);
        const QString name = decodeKpText(payload.mid(int(s + 0x1a), int(nl)));
        if (name.isEmpty()) continue;

        const qsizetype meta = s + 0x1a + qsizetype(nl) + 1;
        if (meta + 0x27 > sz) continue;
        const uint32_t kind     = peekU32(payload, meta + 0x0b);
        const uint32_t elem     = peekU32(payload, meta + 0x17);
        const uint32_t folderId = peekU32(payload, meta + 0x1f);
        const uint32_t idLen    = peekU32(payload, meta + 0x23);
        const qsizetype P = meta + 0x27 + qsizetype(idLen) + 1;
        if (P + 124 > sz) continue;

        const uint32_t cols = peekU32(payload, P + 116);
        const uint32_t rows = peekU32(payload, P + 120);

        // Deterministic address: identifier string, unit string, factor(f64),
        // offset(f64), then [start][end][romBase] u32s.
        qsizetype pos = P + 136;
        pos = walkString(pos);      // optional identifier
        pos = walkString(pos);      // engineering unit
        const double factor = (pos + 8 <= sz) ? peekF64(payload, pos) : 0.0;
        pos += 8;
        const double offset = (pos + 8 <= sz) ? peekF64(payload, pos) : 0.0;
        pos += 8;
        uint32_t mapStart = 0, mapEnd = 0, romBase = 0;
        if (pos + 12 <= sz) {
            mapStart = peekU32(payload, pos);
            mapEnd   = peekU32(payload, pos + 4);
            romBase  = peekU32(payload, pos + 8);
        }

        const int ds = elem > 0 ? int(elem) : 1;
        int dx = 1, dy = 1;
        if (kind != 2) {
            dx = (cols >= 1 && cols <= 4096) ? int(cols) : 1;
            dy = (kind == 3) ? 1 : ((rows >= 1 && rows <= 4096) ? int(rows) : 1);
        }

        MapInfo m;
        m.name           = name;
        m.description    = name;
        m.dataSize       = ds;
        m.dimensions     = { dx, dy };
        m.type           = typeFromKpKind(kind, dx, dy);
        m.linkConfidence = 100;
        // Schema-750 packed value spans are row-major (X changes fastest).
        m.columnMajor    = false;

        if (idLen >= 1 && idLen <= 200 && meta + 0x27 + qsizetype(idLen) <= sz) {
            const QByteArray idb = payload.mid(int(meta + 0x27), int(idLen));
            if (isText(idb.constData(), idb.size())) {
                const QString id = decodeKpText(idb);
                if (!id.isEmpty()) m.setSideProp(QStringLiteral("kpIdName"), id);
            }
        }
        if (!folderPaths.isEmpty()) {
            const QString fp = folderPaths.value(folderId);
            if (!fp.isEmpty()) m.folderPath = fp;
        }

        // Trust the address only when the self-check holds; the lone object
        // without one keeps address 0 (it still imports, like WinOLS shows it).
        const bool sizeOk = mapEnd > mapStart
            && uint64_t(mapEnd - mapStart)
                   == uint64_t(dx) * uint64_t(dy) * uint64_t(ds);
        const bool zeroSpan = mapStart != 0 && mapEnd == mapStart;
        uint32_t fileOffset = 0;
        if (sizeOk && normalizeKpAddress(mapStart, mapEnd, romBase,
                                         baseAddress, romSize, &fileOffset)) {
            m.rawAddress = (baseAddress != 0 && fileOffset == mapStart)
                ? baseAddress + fileOffset : mapStart;
            m.address          = fileOffset;
            m.olsUniversalBase = romBase;
            m.length           = int(mapEnd - mapStart);
        } else if (zeroSpan && normalizeKpAxisAddress(
                       mapStart, qMax(1, dx * dy), ds,
                       baseAddress, romSize, &fileOffset)) {
            // KpMapObjectCodec's packed relocation vector still carries the
            // logical cell for this explicit zero-range scalar.
            m.rawAddress = (baseAddress != 0 && fileOffset == mapStart)
                ? baseAddress + fileOffset : mapStart;
            m.address          = fileOffset;
            m.olsUniversalBase = romBase;
            m.length           = qMax(1, dx * dy * ds);
        } else {
            m.address = 0;
            m.rawAddress = 0;
            m.length = qMax(1, dx * dy * ds);
        }

        if (std::isfinite(factor) && std::abs(factor) < 1e12
            && std::isfinite(offset) && std::abs(offset) < 1e12
            && ((factor != 0.0 && factor != 1.0) || offset != 0.0)) {
            m.hasScaling   = true;
            m.scaling.type = CompuMethod::Type::Linear;
            m.scaling.linA = factor;
            m.scaling.linB = offset;
            // The last u32 of the fixed main header is the map's displayed
            // decimal precision.  It is distinct from the X/Y fields stored
            // in the respective axis blocks.
            const uint32_t precision = peekU32(payload, P + 132);
            if (precision <= 6)
                m.scaling.format = QStringLiteral("1.%1f").arg(precision);
        }

        // KpMapObjectCodec serializes map flags directly before [columns]
        // [rows]. The second byte is the signed-cell flag (model +0x105).
        // It is independent of the displayed value range and must not be
        // inferred from the payload values.
        m.dataSigned = payload.at(int(P + 113)) != 0;

        // Axis sub-blocks (X = columns, then Y = rows) via the shared parser.
        // See the compact-layout path above: map display addresses and KP axis
        // addresses need independent normalisation when the project has a base.
        if (kind != 2 && sizeOk) {
            const QByteArray record = payload.mid(int(meta), int(objEnd - meta));
            const qsizetype addrOffInRec = pos - meta;   // offset of mapStart
            const QVector<Kp750Axis> axes =
                parseSchema750Axes(record, addrOffInRec + 12);
            auto fillAxis = [&](AxisInfo &dst, const Kp750Axis &src, int count) {
                dst.inputName = src.unit.isEmpty()
                    ? src.name
                    : QStringLiteral("%1 [%2]").arg(src.name, src.unit);
                if (src.hasFactor) {
                    dst.hasScaling   = true;
                    dst.scaling.type = CompuMethod::Type::Linear;
                    dst.scaling.linA = src.factor;
                    dst.scaling.linB = src.offset;
                    if (src.precision >= 0)
                        dst.scaling.format = QStringLiteral("1.%1f").arg(src.precision);
                }
                dst.ptsDataSize = src.dataSize;
                dst.ptsDataType = src.dataType;
                dst.ptsBigEndian = false;
                dst.ptsSigned   = src.pointsSigned;
                dst.ptsCount    = count;
                uint32_t fileOff = 0;
                if (normalizeKpAxisAddress(src.rawAddr, count, src.dataSize,
                                           baseAddress, romSize, &fileOff)) {
                    dst.ptsAddress    = fileOff;
                    dst.hasPtsAddress = true;
                }
            };
            if (axes.size() >= 1)
                fillAxis(m.xAxis, axes[0], m.dimensions.x);
            if (axes.size() >= 2 && m.dimensions.y > 1)
                fillAxis(m.yAxis, axes[1], m.dimensions.y);
        }

        maps.append(m);
    }

    if (warnings && maps.size() != int(mapCount))
        warnings->append(KpImporter::tr(
            "schema-750 deterministic walk: map_count %1, decoded %2 objects")
                .arg(mapCount).arg(maps.size()));
    return maps;
}

} // namespace


bool KpImporter::extractInternEntry(const QByteArray &fileData,
                                     QByteArray &compressed,
                                     uint32_t &uncompressedSize,
                                     uint16_t &method,
                                     QString &err)
{
    static const char pkSig[] = { 'P', 'K', '\x03', '\x04' };
    int lfhOff = -1;
    for (qsizetype i = 0; i + 4 <= fileData.size(); ++i) {
        if (std::memcmp(fileData.constData() + i, pkSig, 4) == 0) {
            lfhOff = static_cast<int>(i);
            break;
        }
    }
    if (lfhOff < 0) {
        err = KpImporter::tr("No PKZIP local file header (PK\\x03\\x04) found");
        return false;
    }

    if (lfhOff + 30 > fileData.size()) {
        err = KpImporter::tr("Truncated ZIP local file header");
        return false;
    }

    const auto *h = reinterpret_cast<const uchar *>(
        fileData.constData() + lfhOff);

    method = qFromLittleEndian<uint16_t>(h + 8);
    uint32_t csize = qFromLittleEndian<uint32_t>(h + 18);
    uncompressedSize = qFromLittleEndian<uint32_t>(h + 22);
    uint16_t fnLen = qFromLittleEndian<uint16_t>(h + 26);
    uint16_t extraLen = qFromLittleEndian<uint16_t>(h + 28);

    qsizetype fnStart = lfhOff + 30;
    if (fnStart + fnLen > fileData.size()) {
        err = KpImporter::tr("Truncated ZIP filename");
        return false;
    }
    QString filename = QString::fromLatin1(
        fileData.constData() + fnStart, fnLen);
    if (filename != QStringLiteral("intern")) {
    }

    qsizetype dataStart = fnStart + fnLen + extraLen;
    if (dataStart + static_cast<qsizetype>(csize) > fileData.size()) {
        err = KpImporter::tr("ZIP compressed data extends beyond file end "
                             "(offset 0x%1, size %2, file %3)")
                  .arg(dataStart, 0, 16)
                  .arg(csize)
                  .arg(fileData.size());
        return false;
    }

    compressed = fileData.mid(static_cast<int>(dataStart),
                               static_cast<int>(csize));
    return true;
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
    QString extractErr;

    if (!extractInternEntry(fileData, compressed, uncompressedSize,
                            method, extractErr)) {
        result.error = extractErr;
        return result;
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
        result.warnings.append(
            KpImporter::tr("Decompressed size %1 != declared %2")
                .arg(intern.size())
                .arg(uncompressedSize));
    }

    // Folder tree lives in the trailing metadata (after the ZIP), not in
    // `intern` — parse it so maps can be grouped like WinOLS shows them.
    const QHash<uint32_t, KpFolder> folders =
        parseKpFolderTable(fileData, &result.warnings);
    const QHash<uint32_t, QString> folderPaths = resolveKpFolderPaths(folders);

    // OLS 5.x (schema >= 700): use only the schema-specific object walk.
    // Do not substitute the legacy recovery parser: it can produce plausible
    // but wrong maps when the schema has not been fully characterized.
    const uint32_t internMapCount = intern.size() >= 5 ? peekU32(intern, 1) : 0;
    if (result.formatVersion >= 700) {
        result.maps = parseSchema750Deterministic(intern, baseAddress, romSize,
                                                  folderPaths, &result.warnings);
        if (internMapCount > 0 && result.maps.size() != int(internMapCount)) {
            result.error = KpImporter::tr(
                "Schema-750 parser recognized %1 of %2 maps; refusing an "
                "incomplete import rather than guessing the remaining layout")
                .arg(result.maps.size()).arg(internMapCount);
            return result;
        }
    } else {
        result.maps = parseKpIntern(intern, baseAddress, romSize,
                                    folderPaths, &result.warnings);
    }
    result.mapCount = static_cast<uint32_t>(result.maps.size());
    if (result.formatVersion >= 700)
        attachSchema750CarriedData(fileData, &result.maps, &result.carriedData,
                                   &result.warnings);

    return result;
}

}
