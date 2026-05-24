/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KpImporter.h"
#include "ZipDecompressor.h"

#include <QtEndian>
#include <algorithm>
#include <cmath>
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

QVector<MapInfo> parseKpIntern(const QByteArray &payload,
                               uint32_t baseAddress,
                               uint32_t romSize,
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
        m.description    = name;
        m.type           = typeFromKpKind(hdr.kind, 1, 1);
        m.rawAddress     = (baseAddress != 0 && addr.fileOffset == addr.raw)
            ? baseAddress + addr.fileOffset
            : addr.raw;
        m.address        = addr.fileOffset;
        m.olsUniversalBase = addr.universalBase;
        m.dataSize       = dataSize;
        if (hdr.legacyLayout && hdr.kind != 2 && hdr.kind != 3)
            m.dimensions = dimensionsFromLegacyRecord(record, addr, cells);
        if (m.dimensions.x <= 1 && m.dimensions.y <= 1) {
            uint32_t hintX = hdr.hintX;
            if (hintX == 0)
                hintX = dimensionHintFromName(name, cells);
            m.dimensions = dimensionsFromRecord(hdr.kind, hintX, cells);
        }
        m.type           = typeFromKpKind(hdr.kind, m.dimensions.x, m.dimensions.y);
        m.length         = qMax(1, addr.dataBytes);
        m.linkConfidence = 100;
        m.columnMajor    = true;

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

    result.maps = parseKpIntern(intern, baseAddress, romSize,
                                &result.warnings);
    result.mapCount = static_cast<uint32_t>(result.maps.size());

    return result;
}

}
