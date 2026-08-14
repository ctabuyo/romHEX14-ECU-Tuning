/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "romcompare.h"
#include <cmath>
#include <cstring>

QVector<ByteDiff> RomCompare::diffBytes(const QByteArray &ref, const QByteArray &cmp)
{
    QVector<ByteDiff> diffs;
    int len = qMin(ref.size(), cmp.size());
    for (int i = 0; i < len; ++i) {
        uint8_t rb = (uint8_t)ref[i];
        uint8_t cb = (uint8_t)cmp[i];
        if (rb != cb)
            diffs.append({(uint32_t)i, rb, cb});
    }
    return diffs;
}

ByteDiffSummary RomCompare::diffBytesSummary(const QByteArray &ref, const QByteArray &cmp)
{
    ByteDiffSummary s;
    const qint64 len = qMin(ref.size(), cmp.size());
    if (len <= 0) return s;

    const char *r = ref.constData();
    const char *c = cmp.constData();

    // Word-stride scan: process 8 bytes at a time using memcmp-style XOR for
    // cache-friendly throughput, then narrow into the differing word to find
    // the first offset and tally byte-level diffs without per-byte branches.
    qint64 i = 0;
    const qint64 wordEnd = len & ~qint64(7);
    for (; i < wordEnd; i += 8) {
        quint64 a, b;
        std::memcpy(&a, r + i, 8);
        std::memcpy(&b, c + i, 8);
        if (a == b) continue;
        // Find which bytes within the word differ.
        for (int k = 0; k < 8; ++k) {
            if (((quint8 *)&a)[k] != ((quint8 *)&b)[k]) {
                if (s.firstOffset < 0) s.firstOffset = i + k;
                ++s.count;
            }
        }
    }
    // Tail bytes (< 8)
    for (; i < len; ++i) {
        if ((quint8)r[i] != (quint8)c[i]) {
            if (s.firstOffset < 0) s.firstOffset = i;
            ++s.count;
        }
    }
    return s;
}

QVector<DiffRun> RomCompare::diffRuns(const QByteArray &ref, const QByteArray &cmp)
{
    QVector<DiffRun> runs;
    const qint64 len = qMin(ref.size(), cmp.size());
    if (len <= 0) return runs;

    const char *r = ref.constData();
    const char *c = cmp.constData();

    qint64 runStart = -1;   // -1 = not in a run
    auto closeRun = [&](qint64 endExclusive) {
        if (runStart >= 0) {
            runs.append({ runStart, endExclusive - runStart });
            runStart = -1;
        }
    };

    qint64 i = 0;
    const qint64 wordEnd = len & ~qint64(7);
    for (; i < wordEnd; i += 8) {
        quint64 a, b;
        std::memcpy(&a, r + i, 8);
        std::memcpy(&b, c + i, 8);
        if (a == b) {
            closeRun(i);
            continue;
        }
        for (int k = 0; k < 8; ++k) {
            const bool diff = ((quint8 *)&a)[k] != ((quint8 *)&b)[k];
            if (diff) {
                if (runStart < 0) runStart = i + k;
            } else {
                closeRun(i + k);
            }
        }
    }
    for (; i < len; ++i) {
        if ((quint8)r[i] != (quint8)c[i]) {
            if (runStart < 0) runStart = i;
        } else {
            closeRun(i);
        }
    }
    closeRun(len);
    return runs;
}

QVector<MapDiff> RomCompare::diffMaps(const QByteArray &refRom,
                                      const QByteArray &cmpRom,
                                      const QVector<MapInfo> &maps,
                                      ByteOrder byteOrder,
                                      const QMap<QString, uint32_t> &cmpOffsets)
{
    QVector<MapDiff> diffs;

    for (const MapInfo &m : maps) {
        // Determine offsets in each ROM
        uint32_t refOff = m.address;
        uint32_t cmpOff = cmpOffsets.contains(m.name)
                          ? cmpOffsets[m.name]
                          : m.address;

        int cells    = m.dimensions.x * m.dimensions.y;
        int dataOff  = (int)m.mapDataOffset; // bytes before actual map data (axes)
        int cellSize = m.dataSize;
        int dataLen  = cells * cellSize;

        // Bounds check
        if ((int)(refOff + dataOff + dataLen) > refRom.size()) continue;
        if ((int)(cmpOff + dataOff + dataLen) > cmpRom.size()) continue;

        const uint8_t *rp = (const uint8_t *)refRom.constData() + refOff + dataOff;
        const uint8_t *cp = (const uint8_t *)cmpRom.constData() + cmpOff + dataOff;

        MapDiff md;
        md.map       = m;
        md.refOffset = refOff;
        md.cmpOffset = cmpOff;
        md.cellDeltas.resize(cells);

        double sumDelta = 0.0;
        int    cols     = m.dimensions.x;
        int    rows     = m.dimensions.y > 0 ? m.dimensions.y : 1;

        // Iterate by display position (r, c) so cellDeltas[memIdx] is always
        // consistent with how showMapCells reads it, regardless of storage order.
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                int memI = m.columnMajor ? c * rows + r : r * cols + c;
                uint32_t rv = readRomValue(rp, dataLen, (uint32_t)(memI * cellSize), cellSize, byteOrder);
                uint32_t cv = readRomValue(cp, dataLen, (uint32_t)(memI * cellSize), cellSize, byteOrder);

                double refPhys = m.hasScaling ? m.scaling.toPhysical(signExtendRaw(rv, cellSize, m.dataSigned)) : signExtendRaw(rv, cellSize, m.dataSigned);
                double cmpPhys = m.hasScaling ? m.scaling.toPhysical(signExtendRaw(cv, cellSize, m.dataSigned)) : signExtendRaw(cv, cellSize, m.dataSigned);
                double delta   = cmpPhys - refPhys;

                md.cellDeltas[memI] = delta;
                if (rv != cv) md.changedCells++;
                double absDelta = std::fabs(delta);
                if (absDelta > md.maxAbsDelta) md.maxAbsDelta = absDelta;
                sumDelta += absDelta;
            }
        }

        if (cells > 0) md.avgAbsDelta = sumDelta / cells;

        if (md.changedCells > 0)
            diffs.append(md);
    }

    return diffs;
}
