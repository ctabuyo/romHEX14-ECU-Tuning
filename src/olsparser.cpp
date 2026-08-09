/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "olsparser.h"
#include <QDataStream>
#include <QMap>
#include <QSet>
#include <QtEndian>
#include <cstring>
#include <cmath>
#include <QDebug>

// ═══════════════════════════════════════════════════════════════════════════════
//  OLS Parser — Clean rewrite based on RE of 60+ OLS files
//
//  PROVEN FACTS (verified on EDC16, ME7, ME17, MEVD17, BMW DME, Delphi, etc.):
//
//  1. ROM extraction:
//     romStart = Original[-20] + 4, where Original[-16] == virtualRomSize
//     remaining = fileSize - romStart = N × virtualRomSize (EXACT)
//
//  2. Map record: [type u32][ds u32][...][0x0A000000][groupId u32][nameLen u32][name]
//     Address triple in body: [addr][endAddr][romSize][...][addr repeat]
//     addr = direct offset into virtual ROM block
//     endAddr - addr = map body byte length
//
//  3. Dimensions: FIRST uint32 pair (a,b) after name where a*b*ds == bodyLen
//     stores (ROWS, COLS) — must SWAP for MapInfo {x=cols, y=rows}
//
//  4. Data layout: column-major (first column stored first)
//
//  5. Byte order: Hexdump marker, scan for [1][2] then field at +8: 2=BE, 3=LE
//
//  6. Scaling/units/labels in post-name region as length-prefixed strings + doubles
// ═══════════════════════════════════════════════════════════════════════════════

OLSParser::OLSParser(QObject *parent)
    : QObject(parent)
{
}

// ── Helpers ──────────────────────────────────────────────────────────────────

static inline uint32_t u32(const char *d, int pos) {
    return qFromLittleEndian<uint32_t>(d + pos);
}

static inline bool isMarker(const char *d, int pos) {
    return (unsigned char)d[pos] == 0x0A && d[pos+1] == 0 && d[pos+2] == 0 && d[pos+3] == 0;
}

static bool isText(const char *p, int len) {
    if (len <= 0) return false;
    for (int i = 0; i < len; ++i) {
        unsigned char c = static_cast<unsigned char>(p[i]);
        if (c < 32 || c > 126) return false;
    }
    return true;
}

static inline bool isValidNameChar(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '[' || c == ']' || c == '-';
}

QString OLSParser::readString(const QByteArray &data, int &pos)
{
    if (pos + 4 > data.size()) return {};
    uint32_t len = u32(data.constData(), pos);
    pos += 4;
    if (len == 0 || len > 500 || pos + (int)len > data.size()) { pos -= 4; return {}; }
    QString s = QString::fromLatin1(data.mid(pos, len)).trimmed();
    pos += len;
    while (pos < data.size() && data[pos] == '\0') pos++;
    while (s.endsWith('\0')) s.chop(1);
    return s;
}

// ── Header parsing ──────────────────────────────────────────────────────────

void OLSParser::parseHeader(const QByteArray &data)
{
    int pos = 0;
    QString magic = readString(data, pos);
    if (!magic.startsWith("WinOLS")) {
        m_error = "Not a valid OLS file";
        return;
    }

    // Extract metadata strings from header blob
    QStringList fields;
    const char *d = data.constData();
    if (pos + 4 <= data.size()) {
        uint32_t blobLen = u32(d, pos);
        int blobStart = pos + 4;
        int blobEnd = (blobLen > 0 && blobLen < 0x2000 && blobStart + (int)blobLen <= data.size())
                      ? blobStart + blobLen
                      : qMin((int)data.size(), pos + 0x400);

        for (int p = blobStart; p < blobEnd - 4; p++) {
            uint32_t slen = u32(d, p);
            if (slen == 0) continue;
            if (slen > 200 || p + 4 + (int)slen > blobEnd) continue;
            bool valid = true;
            for (int i = 0; i < (int)slen; i++) {
                char c = d[p + 4 + i];
                if (c == '\0') break;
                if (c < 32 || c > 126) { valid = false; break; }
            }
            if (!valid) continue;

            int sp = p;
            while (sp + 4 < blobEnd && fields.size() < 25) {
                uint32_t sl = u32(d, sp);
                if (sl == 0) { sp++; continue; }
                if (sl > 200 || sp + 4 + (int)sl > blobEnd) break;
                QString s = QString::fromLatin1(data.mid(sp + 4, sl)).trimmed();
                while (s.endsWith('\0')) s.chop(1);
                if (!s.isEmpty()) fields.append(s);
                sp += 4 + sl;
                while (sp < blobEnd && d[sp] == '\0') sp++;
            }
            break;
        }
        pos = blobEnd;
    }

    // Assign fields to project info
    int idx = 0;
    auto next = [&]() -> QString { return idx < fields.size() ? fields[idx++].trimmed() : QString(); };
    m_info.brand        = next();
    m_info.model        = next();
    m_info.engine       = next();
    m_info.year         = next();
    m_info.fuelType     = next();
    next(); // sometimes empty
    m_info.transmission = next();
    next(); // category
    m_info.ecuFamily    = next();
    m_info.ecuType      = next();
    next(); // empty
    m_info.partNumber   = next();
    m_info.swVersion    = next();
    m_info.description  = next();

    for (const auto &f : fields) {
        if (f.startsWith("OLS ")) { m_info.olsVersion = f; break; }
    }

    // Byte order from "Hexdump" marker
    int hexdumpIdx = data.indexOf("Hexdump");
    if (hexdumpIdx > 0 && hexdumpIdx + 40 < data.size()) {
        for (int scan = hexdumpIdx + 8; scan < hexdumpIdx + 36; scan++) {
            if (u32(d, scan) == 1 && u32(d, scan + 4) == 2) {
                uint32_t boField = u32(d, scan + 8);
                m_byteOrder = (boField == 2) ? ByteOrder::BigEndian : ByteOrder::LittleEndian;
                break;
            }
        }
    }

    // NOTE: we do NOT set m_romSize here. It must come from address triples
    // in parseMapRecords, because the header sometimes contains the physical
    // ROM size which differs from the virtual ROM size used by address triples.
}

// ── Map record parsing ──────────────────────────────────────────────────────

void OLSParser::parseMapRecords(const QByteArray &data)
{
    const char *d = data.constData();
    const int fileSize = data.size();

    emit progress(tr("Finding map records..."), 20);

    // ── Phase 0: Detect virtualRomSize from address triples ─────────────────
    // The most frequent page-aligned a2 value across all [a0][a1][a2][...][a0]
    // patterns is the virtualRomSize. This is 100% reliable.
    {
        QMap<uint32_t, int> romSizeCounts;
        int found = 0;
        for (int p = 0x100; p < fileSize - 20 && found < 500; p++) {
            if (!isMarker(d, p)) continue;
            uint32_t nl = u32(d, p + 8);
            if (nl < 3 || nl > 80 || p + 12 + (int)nl > fileSize) continue;
            if (!((d[p+12] >= 'A' && d[p+12] <= 'Z') || (d[p+12] >= 'a' && d[p+12] <= 'z'))) continue;
            int ne = p + 12 + nl;

            for (int s = ne; s < qMin(ne + 800, fileSize - 44); s++) {
                uint32_t a0 = u32(d, s);
                uint32_t a1 = u32(d, s + 4);
                uint32_t a2 = u32(d, s + 8);
                if (a0 < 0x100 || a0 > 0x4000000) continue;
                if (a1 <= a0 || a1 > 0x4000000 || (a1 - a0) > 0x100000) continue;
                if (a2 < 0x10000 || a2 > 0x4000000 || (a2 & 0xFFF) != 0) continue;
                if (a0 >= a2) continue;
                for (int gap = 12; gap <= 40; gap += 4) {
                    if (s + gap + 4 > fileSize) break;
                    if (u32(d, s + gap) == a0) {
                        romSizeCounts[a2]++;
                        found++;
                        break;
                    }
                }
                break; // one triple per record
            }
        }

        uint32_t bestSz = 0;
        int bestCount = 0;
        for (auto it = romSizeCounts.begin(); it != romSizeCounts.end(); ++it) {
            if (it.value() > bestCount) { bestCount = it.value(); bestSz = it.key(); }
        }
        if (bestCount >= 3) {
            m_romSize = bestSz;
            qDebug() << "OLS: virtualRomSize = 0x" + QString::number(m_romSize, 16)
                     << "from" << bestCount << "triples";
        } else {
            qDebug() << "OLS: could not detect ROM size from triples";
        }
    }

    emit progress(tr("Parsing map records..."), 30);

    // ── Phase 1: Parse all map records ──────────────────────────────────────
    QSet<QString> seen;
    QVector<int> markerPositions;
    QVector<int> allMarkerPositions;

    for (int pos = 0x100; pos < fileSize - 20; pos++) {
        if (!isMarker(d, pos)) continue;

        uint32_t groupId = u32(d, pos + 4);
        if (groupId > 50000) continue;

        uint32_t nameLen = u32(d, pos + 8);
        if (nameLen < 3 || nameLen > 80 || pos + 12 + (int)nameLen > fileSize) continue;

        QByteArray raw = data.mid(pos + 12, nameLen);
        bool valid = true;
        for (int i = 0; i < raw.size(); i++) {
            char c = raw[i];
            if (c == '\0') { raw.truncate(i); break; }
            if (!isValidNameChar(c)) { valid = false; break; }
        }
        if (!valid || raw.size() < 3) continue;
        if (!((raw[0] >= 'A' && raw[0] <= 'Z') || (raw[0] >= 'a' && raw[0] <= 'z'))) continue;

        allMarkerPositions.append(pos);

        QString name = QString::fromLatin1(raw);
        if (seen.contains(name)) continue;
        seen.insert(name);

        int nameEnd = pos + 12 + nameLen;

        QString displayName;
        for (int p = pos - 8; p >= pos - 100; --p) {
            if (p < 0) continue;
            uint32_t l = u32(d, p);
            if (l >= 3 && l <= 200 && p + 4 + (int)l <= pos) {
                QByteArray preBytes = data.mid(p + 4, (int)l);
                bool txt = isText(preBytes.constData(), preBytes.size());
                if (pos == 0x805b) {
                    qDebug() << "  p=0x" + QString::number(p, 16) << "l=" << l << "txt=" << txt << "str:" << preBytes;
                }
                if (txt) {
                    displayName = QString::fromLatin1(preBytes);
                    break;
                }
            }
        }

        MapInfo m;
        if (!displayName.isEmpty()) {
            m.name = displayName;
            m.id   = name;
            seen.insert(displayName);
        } else {
            m.name = name;
            m.id   = name;
        }
        m.markerPos = pos;
        m.linkConfidence = 100;
        m.columnMajor = false;

        // ── Type & DataSize from pre-marker fields ──
        uint32_t recType = 0, recDS = 0;
        static const int typeOffsets[] = { 8, 16, 12, 4, 20, 24 };
        for (int back : typeOffsets) {
            if (pos < back + 4) continue;
            uint32_t t  = u32(d, pos - back);
            uint32_t ds = u32(d, pos - back + 4);
            if ((t >= 2 && t <= 5) && (ds == 1 || ds == 2 || ds == 4)) {
                recType = t; recDS = ds; break;
            }
        }

        // ── Address triple: [addr][endAddr][romSize][...][addr repeat] ──
        // romSize must match m_romSize EXACTLY — no fuzzy matching.
        if (m_romSize > 0) {
            int scanEnd = qMin(nameEnd + 800, fileSize - 16);
            for (int s = nameEnd; s < scanEnd; s++) {
                uint32_t a0 = u32(d, s);
                uint32_t a1 = u32(d, s + 4);
                uint32_t a2 = u32(d, s + 8);

                if (a2 != m_romSize) continue;
                if (a0 < 0x100 || a0 >= m_romSize) continue;
                if (a1 <= a0 || a1 > m_romSize) continue;
                if ((a1 - a0) > 0x100000) continue;

                bool repeat = false;
                for (int gap = 12; gap <= 40; gap += 4) {
                    if (s + gap + 4 > fileSize) break;
                    if (u32(d, s + gap) == a0) { repeat = true; break; }
                }
                if (!repeat) continue;

                m.rawAddress = a0;
                m.address    = a0;
                m.length     = (int)(a1 - a0);
                break;
            }
        }

        int byteLen = m.length;

        // ── Dimensions: FIRST (a,b) pair where a*b*ds == byteLen ──
        // CRITICAL: OLS stores (ROWS, COLS) → swap for MapInfo {x=cols, y=rows}
        int cols = 1, rows = 1;
        int dataSize = (recDS >= 1 && recDS <= 4) ? (int)recDS : 2;

        if (byteLen > 4) {
            bool foundDims = false;
            for (int doff = 0; doff < 0x100 && !foundDims; doff++) {
                int dp = nameEnd + doff;
                if (dp + 8 > fileSize) break;
                uint32_t a = u32(d, dp);
                uint32_t b = u32(d, dp + 4);
                if (a < 2 || a > 999 || b < 2 || b > 999) continue;
                for (int ds : {1, 2, 4}) {
                    if ((int)(a * b * ds) == byteLen) {
                        cols = (int)b;  // SWAP: a=rows, b=cols
                        rows = (int)a;
                        dataSize = ds;
                        foundDims = true;
                        break;
                    }
                }
            }

            // Fallback for 1D curves
            if (!foundDims) {
                for (int doff = 0; doff < 0x100; doff++) {
                    int dp = nameEnd + doff;
                    if (dp + 4 > fileSize) break;
                    uint32_t v = u32(d, dp);
                    if (v < 2 || v > 999) continue;
                    for (int ds : {1, 2, 4}) {
                        if (byteLen == (int)(v * ds)) {
                            cols = (int)v; rows = 1; dataSize = ds;
                            foundDims = true; break;
                        }
                    }
                    if (foundDims) break;
                }
            }

            if (!foundDims && byteLen > 0) {
                if (recDS >= 1 && recDS <= 4 && byteLen % (int)recDS == 0)
                    dataSize = (int)recDS;
                cols = byteLen / dataSize;
                rows = 1;
            }
        } else if (byteLen > 0) {
            dataSize = byteLen;
            cols = 1;
            rows = 1;
        }

        m.dataSize = dataSize;
        m.dimensions = {cols, rows};

        // ── Type from dimensions ──
        if (cols > 1 && rows > 1)      m.type = "MAP";
        else if (cols > 1 || rows > 1) m.type = "CURVE";
        else                            m.type = "VALUE";

        // ── Axis addresses (stored before map body in ROM) ──
        if (m.address > 0 && (cols > 1 || rows > 1)) {
            int xSz = cols * dataSize;
            int ySz = rows * dataSize;
            if (rows > 1 && cols > 1) {
                m.yAxis.ptsAddress = m.address - ySz;
                m.yAxis.ptsCount = rows;
                m.yAxis.ptsDataSize = dataSize;
                m.yAxis.hasPtsAddress = (m.yAxis.ptsAddress > 0 && m.yAxis.ptsAddress < m_romSize);
                m.xAxis.ptsAddress = m.address - ySz - xSz;
                m.xAxis.ptsCount = cols;
                m.xAxis.ptsDataSize = dataSize;
                m.xAxis.hasPtsAddress = (m.xAxis.ptsAddress > 0 && m.xAxis.ptsAddress < m_romSize);
            } else {
                int n = qMax(cols, rows);
                m.xAxis.ptsAddress = m.address - n * dataSize;
                m.xAxis.ptsCount = n;
                m.xAxis.ptsDataSize = dataSize;
                m.xAxis.hasPtsAddress = (m.xAxis.ptsAddress > 0 && m.xAxis.ptsAddress < m_romSize);
            }
        }

        // Debug: log KFMIOP and first few maps
        if (name == "KFMIOP" || m_maps.size() < 3) {
            qDebug() << "OLS:" << name << m.type << cols << "x" << rows
                     << "ds=" << dataSize << "addr=0x" + QString::number(m.address, 16)
                     << "len=" << m.length;
        }

        markerPositions.append(pos);
        m_maps.append(m);

        if (m_maps.size() % 500 == 0)
            emit progress(tr("Parsing map records... (%1)").arg(m_maps.size()),
                          30 + qMin(40, m_maps.size() / 100));
    }

    // ── Phase 2: Extract metadata from record regions ───────────────────────
    emit progress(tr("Extracting metadata..."), 75);

    auto tryReadString = [&](int sp) -> QPair<QString, int> {
        if (sp < 0 || sp + 4 >= fileSize) return {{}, 0};
        uint32_t slen = u32(d, sp);
        if (slen < 1 || slen > 250 || sp + 4 + (int)slen > fileSize) return {{}, 0};
        int realLen = 0;
        bool ok = true;
        for (int j = 0; j < (int)slen; j++) {
            char c = d[sp + 4 + j];
            if (c == '\0') break;
            if ((unsigned char)c < 32 || (unsigned char)c > 126) { ok = false; break; }
            realLen++;
        }
        if (!ok || realLen < 1) return {{}, 0};
        return {QString::fromLatin1(data.mid(sp + 4, realLen)), (int)(4 + slen)};
    };

    auto tryReadDouble = [&](int off) -> double {
        if (off < 0 || off + 8 > fileSize) return 0;
        union { uint64_t u; double dv; } conv;
        conv.u = qFromLittleEndian<uint64_t>(d + off);
        if (!std::isfinite(conv.dv) || conv.dv == 0.0) return 0;
        double a = std::abs(conv.dv);
        if (a < 1e-10 || a > 1e10) return 0;
        return conv.dv;
    };

    struct FoundStr { int pos; QString text; };
    struct FoundDbl { int pos; double value; };

    for (int i = 0; i < m_maps.size(); i++) {
        auto &mp = m_maps[i];
        int mPos = mp.markerPos > 0 ? mp.markerPos : markerPositions[i];
        int nameEnd_i = mPos + 12 + mp.name.size() + 1;

        int scanTo = qMin(fileSize - 4, nameEnd_i + 0x140);

        // Collect strings and doubles from pre-marker + post-name regions
        QVector<FoundStr> strings;
        QVector<FoundDbl> doubles;

        auto collectStrings = [&](int from, int to) {
            for (int sp = from; sp < to - 4; sp++) {
                auto [text, consumed] = tryReadString(sp);
                if (!text.isEmpty() && consumed > 0) {
                    strings.append({sp, text});
                }
            }
        };

        auto collectDoubles = [&](int from, int to) {
            for (int dp = from; dp + 8 <= to; dp++) {
                double v = tryReadDouble(dp);
                if (v == 0) continue;
                uint32_t lo = u32(d, dp);
                uint32_t hi = u32(d, dp + 4);
                if (lo > 0x1000 && lo < 0x4000000 && hi > 0x1000 && hi < 0x4000000)
                    continue;
                doubles.append({dp, v});
            }
        };

        // ONLY collect from the POST-name region.
        // Pre-marker data bleeds in from adjacent records and causes wrong scaling.
        collectStrings(nameEnd_i + 0x20, scanTo);
        // Classify strings
        QString description;
        QVector<FoundStr> unitStrings;
        QVector<FoundStr> labelStrings;

        for (const auto &fs : strings) {
            const auto &t = fs.text;
            if (t == mp.name || t == mp.id) continue;
            if (seen.contains(t)) continue; // skip other maps' names/IDs
            if (t.contains(' ') && t.size() > 18) {
                // Long descriptive sentence (e.g. "this is a description for compressor efficiency map")
                if (description.isEmpty() || t.size() > description.size())
                    description = t;
            } else if (t.contains(' ') && t.size() > 5) {
                if (description.isEmpty()) description = t;
            } else if (t.size() >= 1 && t.size() <= 15 && !t.contains(' ') && !t.contains('_')) {
                unitStrings.append(fs);
            } else if (t.size() > 2 && t.size() <= 60 && !t.contains(' ')) {
                labelStrings.append(fs);
            }
        }

        if (!description.isEmpty()) mp.description = description;

        // Associate nearest double with each unit
        auto findNearestDouble = [&](int stringPos) -> double {
            double best = 0;
            int bestDist = INT_MAX;
            for (const auto &fd : doubles) {
                int dist = std::abs(fd.pos - stringPos);
                if (dist > 80 || fd.value == 1.0) continue;
                if (dist < bestDist) { best = fd.value; bestDist = dist; }
            }
            return best;
        };

        int numAxes = (mp.type == "MAP") ? 2 : (mp.type == "CURVE") ? 1 : 0;

        if (numAxes == 2 && unitStrings.size() >= 3) {
            mp.scaling.unit       = unitStrings[0].text;
            mp.yAxis.scaling.unit = unitStrings[1].text;
            mp.xAxis.scaling.unit = unitStrings[2].text;
            double vf = findNearestDouble(unitStrings[0].pos);
            double yf = findNearestDouble(unitStrings[1].pos);
            double xf = findNearestDouble(unitStrings[2].pos);
            if (vf != 0) { mp.hasScaling = true; mp.scaling.type = CompuMethod::Type::Linear; mp.scaling.linA = vf; }
            if (yf != 0) { mp.yAxis.hasScaling = true; mp.yAxis.scaling.type = CompuMethod::Type::Linear; mp.yAxis.scaling.linA = yf; }
            if (xf != 0) { mp.xAxis.hasScaling = true; mp.xAxis.scaling.type = CompuMethod::Type::Linear; mp.xAxis.scaling.linA = xf; }
        } else if (numAxes == 2 && unitStrings.size() == 2) {
            mp.yAxis.scaling.unit = unitStrings[0].text;
            mp.xAxis.scaling.unit = unitStrings[1].text;
            double yf = findNearestDouble(unitStrings[0].pos);
            double xf = findNearestDouble(unitStrings[1].pos);
            if (yf != 0) { mp.yAxis.hasScaling = true; mp.yAxis.scaling.type = CompuMethod::Type::Linear; mp.yAxis.scaling.linA = yf; }
            if (xf != 0) { mp.xAxis.hasScaling = true; mp.xAxis.scaling.type = CompuMethod::Type::Linear; mp.xAxis.scaling.linA = xf; }
        } else if (numAxes == 1 && unitStrings.size() >= 2) {
            mp.scaling.unit       = unitStrings[0].text;
            mp.xAxis.scaling.unit = unitStrings[1].text;
            double vf = findNearestDouble(unitStrings[0].pos);
            double xf = findNearestDouble(unitStrings[1].pos);
            if (vf != 0) { mp.hasScaling = true; mp.scaling.type = CompuMethod::Type::Linear; mp.scaling.linA = vf; }
            if (xf != 0) { mp.xAxis.hasScaling = true; mp.xAxis.scaling.type = CompuMethod::Type::Linear; mp.xAxis.scaling.linA = xf; }
        } else if (unitStrings.size() >= 1) {
            if (numAxes > 0) {
                mp.xAxis.scaling.unit = unitStrings[0].text;
                double f = findNearestDouble(unitStrings[0].pos);
                if (f != 0) { mp.xAxis.hasScaling = true; mp.xAxis.scaling.type = CompuMethod::Type::Linear; mp.xAxis.scaling.linA = f; }
            } else {
                mp.scaling.unit = unitStrings[0].text;
                double f = findNearestDouble(unitStrings[0].pos);
                if (f != 0) { mp.hasScaling = true; mp.scaling.type = CompuMethod::Type::Linear; mp.scaling.linA = f; }
            }
        }

        // ── Fallback: positional assignment from post-name doubles ────────
        // Post-name doubles appear in order: [value_scaling, y_axis_scaling, x_axis_scaling]
        // This is reliable when unit-string matching doesn't cover everything.
        {
            QVector<double> postDoubles;
            for (const auto &fd : doubles) {
                if (fd.pos <= nameEnd_i) continue;
                if (fd.value == 1.0 || fd.value < 0) continue;
                postDoubles.append(fd.value);
            }

            if (!mp.hasScaling && !postDoubles.isEmpty()) {
                mp.hasScaling = true;
                mp.scaling.type = CompuMethod::Type::Linear;
                mp.scaling.linA = postDoubles[0];
            }
            if (numAxes >= 1 && !mp.yAxis.hasScaling && postDoubles.size() >= 2 && numAxes == 2) {
                mp.yAxis.hasScaling = true;
                mp.yAxis.scaling.type = CompuMethod::Type::Linear;
                mp.yAxis.scaling.linA = postDoubles[1];
            }
            if (numAxes >= 1 && !mp.xAxis.hasScaling) {
                int xIdx = (numAxes == 2) ? 2 : 1;
                if (postDoubles.size() > xIdx) {
                    mp.xAxis.hasScaling = true;
                    mp.xAxis.scaling.type = CompuMethod::Type::Linear;
                    mp.xAxis.scaling.linA = postDoubles[xIdx];
                }
            }
        }

        // Debug: log scaling for KFMIOP and first few maps
        if (mp.name == "KFMIOP" || i < 3) {
            qDebug() << "OLS scaling:" << mp.name
                     << "hasScaling=" << mp.hasScaling
                     << "linA=" << mp.scaling.linA
                     << "unit=" << mp.scaling.unit
                     << "unitStrings=" << unitStrings.size()
                     << "doubles(total)=" << doubles.size()
                     << "nameEnd_i=" << Qt::hex << nameEnd_i
                     << "mPos=" << mPos;
            // Log first 5 post-name doubles
            int dbgCount = 0;
            for (const auto &fd : doubles) {
                if (fd.pos > nameEnd_i && dbgCount < 5) {
                    qDebug() << "  post-name double @" << Qt::hex << fd.pos
                             << "=" << fd.value;
                    dbgCount++;
                }
            }
        }

        // Axis labels
        if (numAxes == 2 && labelStrings.size() >= 2) {
            mp.yAxis.inputName = labelStrings[labelStrings.size() - 2].text;
            mp.xAxis.inputName = labelStrings[labelStrings.size() - 1].text;
        } else if (numAxes >= 1 && !labelStrings.isEmpty()) {
            mp.xAxis.inputName = labelStrings.last().text;
        }
    }

    // Summary
    int nVal = 0, nCur = 0, nMap = 0, nAddr = 0;
    for (const auto &mp : m_maps) {
        if (mp.type == "MAP") nMap++;
        else if (mp.type == "CURVE") nCur++;
        else nVal++;
        if (mp.address > 0) nAddr++;
    }
    qDebug() << "OLS:" << m_maps.size() << "records ("
             << nVal << "V" << nCur << "C" << nMap << "M)"
             << nAddr << "with addresses";
}

// ── ROM extraction ──────────────────────────────────────────────────────────

void OLSParser::extractRomData(const QByteArray &data)
{
    const char *d = data.constData();
    const int fileSize = data.size();

    if (m_romSize < 0x10000) {
        m_error = "ROM size not detected";
        return;
    }

    const uint32_t vrs = m_romSize;
    int romStart = -1;

    // Find "Original" marker where [-16] == virtualRomSize EXACTLY
    int searchPos = 0;
    while (searchPos < fileSize) {
        int idx = data.indexOf("Original", searchPos);
        if (idx < 0 || idx < 24) break;
        searchPos = idx + 1;

        uint32_t szField = u32(d, idx - 16);
        uint32_t ofField = u32(d, idx - 20);

        if (szField != vrs) continue;
        int rs = (int)ofField + 4;
        if (rs <= 0 || rs >= fileSize) continue;

        int remaining = fileSize - rs;
        if (remaining <= 0 || remaining % (int)vrs != 0) continue;
        int copies = remaining / (int)vrs;
        if (copies < 1 || copies > 6) continue;

        romStart = rs + (copies - 1) * (int)vrs; // last copy
        qDebug() << "OLS: ROM at 0x" + QString::number(romStart, 16)
                 << "size=0x" + QString::number(vrs, 16) << "copies=" << copies;
        break;
    }

    // Fallback: end of file
    if (romStart < 0 && (int)vrs <= fileSize) {
        romStart = fileSize - vrs;
        qDebug() << "OLS: ROM from EOF fallback at 0x" + QString::number(romStart, 16);
    }

    if (romStart < 0) {
        m_error = "Could not locate ROM data";
        return;
    }

    m_romData = data.mid(romStart, vrs);
}

// ── Main parse entry point ──────────────────────────────────────────────────

bool OLSParser::parse(const QByteArray &fileData)
{
    m_maps.clear();
    m_romData.clear();
    m_error.clear();
    m_romSize = 0;

    if (fileData.size() < 0x400) {
        m_error = "File too small";
        return false;
    }

    if (!fileData.mid(4, 11).startsWith("WinOLS")) {
        m_error = "Not a valid OLS file";
        return false;
    }

    emit progress(tr("Parsing header..."), 5);
    parseHeader(fileData);

    // IMPORTANT: reset m_romSize — it must come from triples, not header
    m_romSize = 0;

    parseMapRecords(fileData);

    if (m_romSize > 0) {
        emit progress(tr("Extracting ROM data..."), 80);
        extractRomData(fileData);
    }

    if (m_maps.isEmpty()) {
        m_error = tr("Could not parse map definitions from this OLS file.\n"
                     "This may be a newer OLS format version that is not yet supported.\n\n"
                     "ROM data was loaded successfully — you can import an A2L file\n"
                     "to define maps manually.");
    }

    emit progress(tr("Done"), 100);
    return true;
}
