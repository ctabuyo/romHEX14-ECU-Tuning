/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ecudetector.h"
#include <cstring>
#include <cctype>

namespace {

struct Pattern {
    const char *needle;
    const char *family;
    uint32_t    base;
    ByteOrder   bo;
    int         score; // higher = more specific match
};

// Ordered most-specific first so the highest score wins
static const Pattern kPatterns[] = {
    // ── Bosch MED17 / ME17 (TriCore TC179x, LE, 0x80000000) ──────────────
    { "MED17.1.6",  "MED17", 0x80000000, ByteOrder::LittleEndian, 100 },
    { "MED17.3.4",  "MED17", 0x80000000, ByteOrder::LittleEndian, 100 },
    { "MED17.1.1",  "MED17", 0x80000000, ByteOrder::LittleEndian, 100 },
    { "MED17.1",    "MED17", 0x80000000, ByteOrder::LittleEndian,  90 },
    { "MED17",      "MED17", 0x80000000, ByteOrder::LittleEndian,  80 },
    { "ME17.9",     "ME17",  0x80000000, ByteOrder::LittleEndian, 100 },
    { "ME17",       "ME17",  0x80000000, ByteOrder::LittleEndian,  80 },
    { "MEVD17.2.G", "MG1",   0x80000000, ByteOrder::LittleEndian, 100 },
    { "MEVD17.2.4", "MG1",   0x80000000, ByteOrder::LittleEndian, 100 },
    { "MEVD17.2.5", "MG1",   0x80000000, ByteOrder::LittleEndian, 100 },
    { "MEVD17.2",   "MG1",   0x80000000, ByteOrder::LittleEndian,  90 },
    { "MEVD17",     "MG1",   0x80000000, ByteOrder::LittleEndian,  80 },
    // ── Bosch EDC17 (TriCore TC179x, LE, 0x80000000) ─────────────────────
    { "EDC17C46",   "EDC17", 0x80000000, ByteOrder::LittleEndian, 100 },
    { "EDC17C64",   "EDC17", 0x80000000, ByteOrder::LittleEndian, 100 },
    { "EDC17C",     "EDC17", 0x80000000, ByteOrder::LittleEndian,  90 },
    { "EDC17",      "EDC17", 0x80000000, ByteOrder::LittleEndian,  80 },
    // ── Bosch ME9 / MED9 (TriCore or SH, LE, 0x80000000) ────────────────
    { "MED9.7",     "MED9",  0x80000000, ByteOrder::LittleEndian, 100 },
    { "MED9.1",     "MED9",  0x80000000, ByteOrder::LittleEndian, 100 },
    { "ME9.7",      "ME9",   0x80000000, ByteOrder::LittleEndian, 100 },
    { "ME9.6",      "ME9",   0x80000000, ByteOrder::LittleEndian, 100 },
    // ── Bosch ME7 (ST10 / Motorola HC12, BE, 0x800000) ───────────────────
    { "ME7.5.20",   "ME7",   0x800000,   ByteOrder::BigEndian,    100 },
    { "ME7.5.10",   "ME7",   0x800000,   ByteOrder::BigEndian,    100 },
    { "ME7.5",      "ME7",   0x800000,   ByteOrder::BigEndian,    100 },
    { "ME7.4.5",    "ME7",   0x800000,   ByteOrder::BigEndian,    100 },
    { "ME7.4",      "ME7",   0x800000,   ByteOrder::BigEndian,    100 },
    { "ME7.6",      "ME7",   0x800000,   ByteOrder::BigEndian,    100 },
    { "ME7.3",      "ME7",   0x800000,   ByteOrder::BigEndian,    100 },
    { "ME7.2",      "ME7",   0x800000,   ByteOrder::BigEndian,    100 },
    { "ME7.1",      "ME7",   0x800000,   ByteOrder::BigEndian,    100 },
    { "ME7",        "ME7",   0x800000,   ByteOrder::BigEndian,     70 },
    { "MED7",       "MED7",  0x800000,   ByteOrder::BigEndian,     70 },
    // ── Bosch EDC16 (ST10 / PPC, BE, 0x400000) ───────────────────────────
    { "EDC16C34",   "EDC16", 0x400000,   ByteOrder::BigEndian,    100 },
    { "EDC16C",     "EDC16", 0x400000,   ByteOrder::BigEndian,     90 },
    { "EDC16",      "EDC16", 0x400000,   ByteOrder::BigEndian,     80 },
    // ── Bosch EDC15 (ST10, BE, 0x400000) ─────────────────────────────────
    { "EDC15VM",    "EDC15", 0x400000,   ByteOrder::BigEndian,    100 },
    { "EDC15V",     "EDC15", 0x400000,   ByteOrder::BigEndian,     95 },
    { "EDC15",      "EDC15", 0x400000,   ByteOrder::BigEndian,     80 },
    // ── Siemens / Continental ─────────────────────────────────────────────
    { "SDI7.5",     "SDI7",  0xA00000,   ByteOrder::BigEndian,    100 },
    { "SDI7",       "SDI7",  0xA00000,   ByteOrder::BigEndian,     80 },
    { "SIM90",      "SIM90", 0x000000,   ByteOrder::BigEndian,     80 },
    { "SIM266",     "SIM266",0x000000,   ByteOrder::BigEndian,     80 },
    // ── Magneti Marelli ───────────────────────────────────────────────────
    { "4SM",        "4SM",   0x200000,   ByteOrder::BigEndian,     70 },
    { nullptr, nullptr, 0, ByteOrder::BigEndian, 0 }
};

// Extract a printable identifier string starting at position i in data
static QString extractIdent(const char *data, int dataLen, int i, int maxLen = 32)
{
    int end = i;
    while (end < dataLen && end < i + maxLen) {
        char c = data[end];
        if (c >= 0x20 && c < 0x7F) ++end;
        else break;
    }
    return QString::fromLatin1(data + i, end - i).trimmed();
}

} // namespace

ECUDetection detectECU(const QByteArray &romData)
{
    ECUDetection best;
    int bestScore = -1;

    if (romData.isEmpty()) return best;

    const char *data = romData.constData();
    const int   size = romData.size();

    for (int pi = 0; kPatterns[pi].needle != nullptr; ++pi) {
        const Pattern &pat  = kPatterns[pi];
        const int      nlen = (int)strlen(pat.needle);

        for (int i = 0; i <= size - nlen; ++i) {
            if (memcmp(data + i, pat.needle, nlen) != 0) continue;

            // Must be followed by non-alphanumeric (or end of data) to avoid
            // partial matches of longer strings we haven't listed
            char after = (i + nlen < size) ? data[i + nlen] : '\0';
            if (isalnum((unsigned char)after)) continue;

            if (pat.score > bestScore) {
                bestScore        = pat.score;
                best.family      = QString::fromLatin1(pat.family);
                best.identifier  = extractIdent(data, size, i);
                best.baseAddr    = pat.base;
                best.byteOrder   = pat.bo;
                best.confident   = (bestScore >= 80);
            }
            break; // first occurrence is enough per pattern
        }
    }

    // ── Size-based fallback ───────────────────────────────────────────────
    if (bestScore < 0) {
        const int mb = size / (1024 * 1024);
        if (mb >= 2) {
            best.family     = "Unknown TriCore";
            best.identifier = QString("%1 MB ROM — likely MED17/EDC17").arg(mb);
            best.baseAddr   = 0x80000000;
            best.byteOrder  = ByteOrder::LittleEndian;
        } else if (size >= 512 * 1024) {
            best.family     = "Unknown ST10/HC12";
            best.identifier = QString("%1 KB ROM — likely ME7/EDC16")
                                  .arg(size / 1024);
            best.baseAddr   = 0x800000;
            best.byteOrder  = ByteOrder::BigEndian;
        } else {
            best.family     = "Unknown";
            best.identifier = QString("%1 KB ROM").arg(size / 1024);
        }
        best.confident = false;
    }

    return best;
}
