/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QByteArray>
#include <QColor>
#include <QMetaType>
#include <cmath>
#include <QString>
#include <QVector>
#include <QSet>
#include <QHash>
#include <QVariant>
#include <cstdint>

// Shared ROM data model
struct MapDimensions {
    int x = 1;
    int y = 1;
};

// Physical-unit conversion from A2L COMPU_METHOD
struct CompuMethod {
    enum class Type { Identical, Linear, RationalFunction };
    Type    type   = Type::Identical;
    QString unit;
    QString format; // e.g. "%6.2f"

    // LINEAR: physical = a * raw + b
    double linA = 1.0, linB = 0.0;

    // RATIONAL_FUNCTION: physical = (a*x² + b*x + c) / (d*x² + e*x + f)
    double rfA = 0, rfB = 0, rfC = 0;
    double rfD = 0, rfE = 1, rfF = 0;

    // Inverse: physical → raw (for write-back)
    double toRaw(double phys) const {
        switch (type) {
        case Type::Linear:
            return (linA != 0.0) ? (phys - linB) / linA : phys;
        case Type::RationalFunction:
            // ASAP2 COEFFS a b c d e f is defined as physical-to-internal:
            //   raw = (rfA*phys² + rfB*phys + rfC) / (rfD*phys² + rfE*phys + rfF)
            // so this direction is direct.
            {
                const double num = rfA*phys*phys + rfB*phys + rfC;
                const double den = rfD*phys*phys + rfE*phys + rfF;
                return (den != 0.0) ? num / den : 0.0;
            }
        default: return phys;
        }
    }

    double toPhysical(double raw) const {
        switch (type) {
        case Type::Linear:
            return linA * raw + linB;
        case Type::RationalFunction: {
            // ASAP2 COEFFS a b c d e f defines the physical-to-internal formula:
            //   raw = (a·phys² + b·phys + c) / (d·phys² + e·phys + f)
            // We need the inverse (raw → phys).
            // For the linear case (a = d = 0):
            //   phys = (c - raw·f) / (raw·e - b)
            if (rfA == 0.0 && rfD == 0.0) {
                const double denom = raw * rfE - rfB;
                return (denom != 0.0) ? (rfC - raw * rfF) / denom : 0.0;
            }
            // Full quadratic case (very rare in ECU A2Ls): fall back to direct eval.
            const double num = rfA*raw*raw + rfB*raw + rfC;
            const double den = rfD*raw*raw + rfE*raw + rfF;
            return (den != 0.0) ? num / den : 0.0;
        }
        default:
            return raw;
        }
    }

    QString formatValue(double phys) const {
        int prec = 2;
        bool isFloat = true;
        if (!format.isEmpty()) {
            int dot = format.indexOf('.');
            if (dot >= 0) {
                int end = dot + 1;
                while (end < format.size() && format[end].isDigit()) ++end;
                prec = format.mid(dot+1, end-dot-1).toInt();
            }
            isFloat = format.contains('f') || format.contains('e');
        } else if (type == Type::Linear && linA != 0.0 && linA != 1.0) {
            // WinOLS automatic decimal precision heuristic:
            // Derives precision using floor(-log10(|factor|)) capped at 3 decimal places.
            double a = std::abs(linA);
            int p = int(std::floor(-std::log10(a)));
            prec = qBound(0, p, 3);
        }
        if (!isFloat && prec == 0)
            return QString::number((long long)phys);
        return QString::number(phys, 'f', qBound(0, prec, 100));
    }
};

// Axis descriptor — holds labels/values for one axis of a map
struct AxisInfo {
    QString     inputName;          // e.g. "n_mot", "v_fahrzeug"
    CompuMethod scaling;            // unit conversion for axis tick labels
    bool        hasScaling = false;

    // Fixed axis values computed from FIX_AXIS_PAR / FIX_AXIS_PAR_DIST in A2L
    // (no ROM read required — values fully known at parse time)
    QVector<double> fixedValues;

    // Axis values stored in ROM (from AXIS_PTS or STD_AXIS / OLS axis sub-rec)
    uint32_t    ptsAddress  = 0;    // file offset into ROM
    int         ptsCount    = 0;    // number of axis points
    int         ptsDataSize = 2;    // bytes per axis value (derived from ptsDataType for OLS format)
    bool        ptsSigned   = false; // true for SBYTE/SWORD/SLONG (or OLS flag3)
    bool        hasPtsAddress = false;

    // OLS format: drives the byte layoutub_7FF72EDDDED0.
    // See AXIS_CELLWIDTH_SCHEMA.md.
    // Values:  1/8/9 = u8;  2 = u16 BE;  3 = u16 LE;  4 = u32 BE;  5 = u32 LE;
    //          6 = float BE;  7 = float LE;  10 = i64;  11 = u64 LE;
    //          12 = double BE;  13 = double LE.
    // 0 = "unknown / not from OLS format" (A2L imports leave this at 0 and rely on
    // the ptsDataSize/ptsSigned pair instead).
    uint32_t    ptsDataType = 0;
    bool        ptsBigEndian = false; // true for BE variants of ptsDataType

    bool hasValues() const {
        return !fixedValues.isEmpty() || hasPtsAddress;
    }
};

struct MapInfo {
    QString name;
    QString id;            // WinOLS Technical Identifier / Map ID (e.g. "med17.9_compress")
    QString description;
    QString type;          // MAP, CURVE, VALUE, VAL_BLK
    int markerPos = 0;     // OLS record marker file offset
    uint32_t rawAddress = 0;
    uint32_t address = 0;  // file offset (rawAddress - baseAddress)
    int length = 0;
    MapDimensions dimensions;
    int dataSize = 2;      // 1, 2, or 4 bytes per element (derived from cellDataType for OLS format)
    bool dataSigned = false; // true for SBYTE/SWORD/SLONG datatypes (or OLS cell flag at +261)
    CompuMethod scaling;
    bool hasScaling = false;
    AxisInfo xAxis;             // column axis (first  AXIS_DESCR)
    AxisInfo yAxis;             // row    axis (second AXIS_DESCR)
    uint32_t mapDataOffset = 0; // bytes to skip past inline axis data (STD_AXIS)
    int  linkConfidence = 0;    // 0 = original (no link), 40/60/80/95/100 = linked
    bool columnMajor = false;  // true = COLUMN_DIR (x fastest), false = ROW_DIR (y fastest, Bosch default)
    QString userNotes;          // user-written comments, persisted in project file
    QString folderPath;         // "/"-delimited group path for the project tree
                                // (e.g. "Boost/Limits"); empty = project root.
                                // Populated from A2L GROUPs / OLS folders on import.

    // OLS format: virtual base address used by newer file versions to remap
    // rom_addr -> file offset via file_pos = file_size - (base - rom_addr).
    // 0 = unset.
    uint32_t olsUniversalBase = 0;

    // OLS format: byte layout enum for CELL data (analogue of
    // AxisInfo::ptsDataType for cells). Values:
    //   1/8/9 = u8; 2 = u16 BE; 3 = u16 LE; 4 = u32 BE; 5 = u32 LE;
    //   6 = float BE; 7 = float LE; 10 = i64; 11 = u64 LE;
    //   12 = double BE; 13 = double LE.
    // 0 = unknown / not from OLS (A2L imports rely on dataSize/dataSigned).
    uint32_t cellDataType = 0;
    bool     cellBigEndian = false;  // true for BE variants of cellDataType

    // Storage for WinOLS Lua map properties that have no direct MapInfo field.
    // Used by windowSetMapProperties / windowGetMapProperties for the
    // "accepted no-op" property bag (Spalten with no axis, StuetzX.bRueckwaerts,
    // etc).  Sprint L spec §5.0.2.  Not persisted to .rx14proj — purely a
    // per-session compatibility shim so WinOLS scripts that touch many
    // properties don't fail with "unknown property".
    QHash<QString, QVariant> sideProps;

    QVariant getSideProp(const QString &name) const {
        return sideProps.value(name);
    }
    void setSideProp(const QString &name, const QVariant &v) {
        sideProps.insert(name, v);
    }

    bool operator==(const MapInfo &o) const {
        return name == o.name && address == o.address;
    }
};

// WinOLS map-type enum ↔ romHEX14 string-typed MapInfo::type bridge.
// WinOLS uses integer enum (eEindim=curve, eZweidim=map, eAchse=axis);
// our MapInfo::type is a string ("MAP"/"CURVE"/"VAL_BLK"/"AXIS"/"VALUE").
// These free functions translate between them — Sprint L spec §5.0.3.
inline QString mapTypeFromWinOlsEnum(int e) {
    switch (e) {
    case 1: return QStringLiteral("CURVE");   // eEindim
    case 2: return QStringLiteral("MAP");     // eZweidim
    case 3: return QStringLiteral("AXIS");    // eAchse
    default: return QStringLiteral("VALUE");
    }
}

inline int mapTypeToWinOlsEnum(const QString &t) {
    if (t == QStringLiteral("CURVE"))   return 1;
    if (t == QStringLiteral("MAP"))     return 2;
    if (t == QStringLiteral("AXIS"))    return 3;
    return 0;
}

struct MapRegion {
    uint32_t start = 0;
    int length = 0;
    QString name;
};

Q_DECLARE_METATYPE(MapInfo)

enum class ByteOrder { BigEndian, LittleEndian };

inline ByteOrder cellByteOrder(const MapInfo &m, ByteOrder projectBO) {
    return m.cellDataType != 0
        ? (m.cellBigEndian ? ByteOrder::BigEndian : ByteOrder::LittleEndian)
        : projectBO;
}

inline ByteOrder axisByteOrder(const AxisInfo &a, ByteOrder projectBO) {
    return a.ptsDataType != 0
        ? (a.ptsBigEndian ? ByteOrder::BigEndian : ByteOrder::LittleEndian)
        : projectBO;
}

// Read an unsigned value from ROM data at given offset
inline uint32_t readRomValue(const uint8_t *data, int dataLen, uint32_t offset, int cellSize, ByteOrder bo) {
    if (!data || (int)(offset + cellSize) > dataLen) return 0;
    if (cellSize == 1) return data[offset];
    if (cellSize == 2) {
        return (bo == ByteOrder::BigEndian)
            ? ((uint32_t)data[offset] << 8) | data[offset + 1]
            : data[offset] | ((uint32_t)data[offset + 1] << 8);
    }
    if (cellSize == 4) {
        if (bo == ByteOrder::BigEndian) {
            return ((uint32_t)data[offset] << 24) | ((uint32_t)data[offset+1] << 16)
                 | ((uint32_t)data[offset+2] << 8) | data[offset+3];
        } else {
            return data[offset] | ((uint32_t)data[offset+1] << 8)
                 | ((uint32_t)data[offset+2] << 16) | ((uint32_t)data[offset+3] << 24);
        }
    }
    return 0;
}

// Sign-extend a raw uint32_t based on cell size (for SBYTE/SWORD/SLONG)
inline double signExtendRaw(uint32_t raw, int cellSize, bool isSigned) {
    if (!isSigned) return (double)raw;
    switch (cellSize) {
    case 1: return (double)(int8_t)(uint8_t)raw;
    case 2: return (double)(int16_t)(uint16_t)raw;
    case 4: return (double)(int32_t)raw;
    default: return (double)raw;
    }
}

// Read a value as double, handling signed types (SBYTE/SWORD/SLONG) correctly
inline double readRomValueAsDouble(const uint8_t *data, int dataLen, uint32_t offset,
                                    int cellSize, ByteOrder bo, bool isSigned) {
    uint32_t raw = readRomValue(data, dataLen, offset, cellSize, bo);
    return signExtendRaw(raw, cellSize, isSigned);
}

inline void writeRomValue(uint8_t *data, int dataLen, uint32_t offset, int cellSize, ByteOrder bo, uint32_t val) {
    if (!data || (int)(offset + cellSize) > dataLen) return;
    if (cellSize == 1) {
        data[offset] = val & 0xFF;
    } else if (cellSize == 2) {
        if (bo == ByteOrder::BigEndian) {
            data[offset] = (val >> 8) & 0xFF;
            data[offset + 1] = val & 0xFF;
        } else {
            data[offset] = val & 0xFF;
            data[offset + 1] = (val >> 8) & 0xFF;
        }
    } else if (cellSize == 4) {
        if (bo == ByteOrder::BigEndian) {
            data[offset]     = (val >> 24) & 0xFF;
            data[offset + 1] = (val >> 16) & 0xFF;
            data[offset + 2] = (val >> 8) & 0xFF;
            data[offset + 3] = val & 0xFF;
        } else {
            data[offset]     = val & 0xFF;
            data[offset + 1] = (val >> 8) & 0xFF;
            data[offset + 2] = (val >> 16) & 0xFF;
            data[offset + 3] = (val >> 24) & 0xFF;
        }
    }
}

// Heat color v2: modern perceptual gradient
// Deep navy → teal → emerald → amber → hot coral
// Softer and more readable than the classic fully-saturated OLS palette
inline QColor heatColor(double pct) {
    pct = qBound(0.0, pct, 1.0);

    // 6-stop gradient for smoother transitions
    struct Stop { double pos; int r, g, b; };
    static const Stop stops[] = {
        {0.00,  20,  50, 120},  // deep navy
        {0.20,  15, 120, 150},  // teal
        {0.40,  30, 170,  90},  // emerald
        {0.60, 140, 190,  40},  // lime-amber
        {0.80, 230, 150,  30},  // warm amber
        {1.00, 230,  60,  50},  // hot coral
    };
    static const int N = 6;

    // Find the two stops to interpolate between
    int i = 0;
    for (i = 0; i < N - 2; ++i)
        if (pct < stops[i + 1].pos) break;

    double t = (pct - stops[i].pos) / (stops[i + 1].pos - stops[i].pos);
    t = qBound(0.0, t, 1.0);

    int r = (int)(stops[i].r + t * (stops[i+1].r - stops[i].r));
    int g = (int)(stops[i].g + t * (stops[i+1].g - stops[i].g));
    int b = (int)(stops[i].b + t * (stops[i+1].b - stops[i].b));
    return QColor(r, g, b);
}
