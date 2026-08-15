/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "mapvaluecodec.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <QObject>
#include <QLocale>

namespace {

int cellWidth(const MapInfo &map)
{
    // OLS datatypes 10–13 are 64-bit integers/doubles.  A2L imports use
    // dataSize directly, so retain that width too instead of truncating it.
    if (map.cellDataType >= 10 && map.cellDataType <= 13) return 8;
    return qBound(1, map.dataSize, 8);
}

QString statusMessage(CodecStatus status)
{
    switch (status) {
    case CodecStatus::Clamped: return QObject::tr("Value was clamped to the cell range");
    case CodecStatus::InvalidRange: return QObject::tr("Cell lies outside the ROM data");
    case CodecStatus::NonFinite: return QObject::tr("NaN and infinity cannot be written");
    case CodecStatus::NonInvertible: return QObject::tr("Scaling cannot convert this value");
    case CodecStatus::Ok: break;
    }
    return {};
}

quint64 readUnsigned(const QByteArray &data, uint32_t offset, int width, ByteOrder order)
{
    quint64 value = 0;
    if (order == ByteOrder::BigEndian) {
        for (int i = 0; i < width; ++i)
            value = (value << 8) | quint8(data.at(int(offset) + i));
    } else {
        for (int i = width - 1; i >= 0; --i)
            value = (value << 8) | quint8(data.at(int(offset) + i));
    }
    return value;
}

QByteArray writeUnsigned(quint64 value, int width, ByteOrder order)
{
    QByteArray bytes(width, Qt::Uninitialized);
    for (int i = 0; i < width; ++i) {
        const int shiftIndex = order == ByteOrder::BigEndian ? width - 1 - i : i;
        bytes[i] = char((value >> (shiftIndex * 8)) & 0xff);
    }
    return bytes;
}

qint64 signExtend(quint64 value, int width)
{
    if (width >= 8) return qint64(value);
    const int bits = width * 8;
    const quint64 signBit = quint64(1) << (bits - 1);
    return (value & signBit) ? qint64(value | (~quint64(0) << bits)) : qint64(value);
}

bool isScaled(const MapInfo &map)
{
    return map.hasScaling && map.scaling.type != CompuMethod::Type::Identical;
}

// Keep the inverse conversion in this boundary rather than delegating an
// invalid coefficient set to CompuMethod's legacy fallback value.  A zero
// denominator or zero linear factor is not a writable physical-to-raw map.
bool physicalToRaw(double physical, const MapInfo &map, double *raw)
{
    if (!isScaled(map)) {
        *raw = physical;
        return true;
    }

    const CompuMethod &scaling = map.scaling;
    switch (scaling.type) {
    case CompuMethod::Type::Linear:
        if (scaling.linA == 0.0) return false;
        *raw = (physical - scaling.linB) / scaling.linA;
        return true;
    case CompuMethod::Type::RationalFunction: {
        const double numerator = scaling.rfA * physical * physical
            + scaling.rfB * physical + scaling.rfC;
        const double denominator = scaling.rfD * physical * physical
            + scaling.rfE * physical + scaling.rfF;
        if (denominator == 0.0) return false;
        *raw = numerator / denominator;
        return true;
    }
    case CompuMethod::Type::Identical:
        *raw = physical;
        return true;
    }
    return false;
}

bool rawToPhysical(double raw, const MapInfo &map, double *physical)
{
    if (!isScaled(map)) {
        *physical = raw;
        return true;
    }

    const CompuMethod &scaling = map.scaling;
    switch (scaling.type) {
    case CompuMethod::Type::Linear:
        *physical = scaling.linA * raw + scaling.linB;
        return true;
    case CompuMethod::Type::RationalFunction:
        if (scaling.rfA == 0.0 && scaling.rfD == 0.0) {
            const double denominator = raw * scaling.rfE - scaling.rfB;
            if (denominator == 0.0) return false;
            *physical = (scaling.rfC - raw * scaling.rfF) / denominator;
            return true;
        }
        {
            const double numerator = scaling.rfA * raw * raw
                + scaling.rfB * raw + scaling.rfC;
            const double denominator = scaling.rfD * raw * raw
                + scaling.rfE * raw + scaling.rfF;
            if (denominator == 0.0) return false;
            *physical = numerator / denominator;
            return true;
        }
    case CompuMethod::Type::Identical:
        *physical = raw;
        return true;
    }
    return false;
}

} // namespace

bool MapValueCodec::isFloatingPoint(const MapInfo &map)
{
    return map.cellDataType == 6 || map.cellDataType == 7
        || map.cellDataType == 12 || map.cellDataType == 13;
}

CodecResult MapValueCodec::decode(const QByteArray &data, uint32_t offset,
                                  const MapInfo &map, ByteOrder projectByteOrder)
{
    CodecResult result;
    const int width = cellWidth(map);
    if (offset > uint32_t(data.size()) || width > data.size() - int(offset)) {
        result.status = CodecStatus::InvalidRange;
        result.message = statusMessage(result.status);
        return result;
    }

    const ByteOrder order = cellByteOrder(map, projectByteOrder);
    const quint64 bits = readUnsigned(data, offset, width, order);
    result.rawBytes = data.mid(int(offset), width);
    result.rawBits = bits;

    if (isFloatingPoint(map)) {
        if (width == 4) {
            const quint32 rawBits = quint32(bits);
            float value = 0.0f;
            std::memcpy(&value, &rawBits, sizeof(value));
            result.rawValue = value;
        } else {
            double value = 0.0;
            std::memcpy(&value, &bits, sizeof(value));
            result.rawValue = value;
        }
    } else {
        result.rawValue = map.dataSigned ? double(signExtend(bits, width)) : double(bits);
    }

    if (!std::isfinite(result.rawValue)) {
        result.status = CodecStatus::NonFinite;
        result.message = statusMessage(result.status);
        result.physicalValue = result.rawValue;
        return result;
    }
    if (!rawToPhysical(result.rawValue, map, &result.physicalValue)) {
        result.status = CodecStatus::NonInvertible;
        result.message = statusMessage(result.status);
        return result;
    }
    if (!std::isfinite(result.physicalValue)) {
        result.status = CodecStatus::NonFinite;
        result.message = statusMessage(result.status);
    }
    return result;
}

CodecResult MapValueCodec::physicalToBytes(double physical, const MapInfo &map,
                                            ByteOrder projectByteOrder)
{
    if (!std::isfinite(physical)) {
        CodecResult result;
        result.physicalValue = physical;
        result.status = CodecStatus::NonFinite;
        result.message = statusMessage(result.status);
        return result;
    }

    double raw = 0.0;
    if (!physicalToRaw(physical, map, &raw) || !std::isfinite(raw)) {
        CodecResult result;
        result.physicalValue = physical;
        result.status = CodecStatus::NonInvertible;
        result.message = statusMessage(result.status);
        return result;
    }

    CodecResult result = rawToBytes(raw, map, projectByteOrder);
    result.physicalValue = physical;
    return result;
}

CodecResult MapValueCodec::rawToBytes(double raw, const MapInfo &map,
                                       ByteOrder projectByteOrder)
{
    CodecResult result;
    result.rawValue = raw;
    if (!std::isfinite(raw)) {
        result.status = CodecStatus::NonFinite;
        result.message = statusMessage(result.status);
        return result;
    }

    const int width = cellWidth(map);
    const ByteOrder order = cellByteOrder(map, projectByteOrder);
    if (isFloatingPoint(map)) {
        if (width == 4) {
            const double bounded = qBound(-double(std::numeric_limits<float>::max()), raw,
                                          double(std::numeric_limits<float>::max()));
            const float value = float(bounded);
            quint32 bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            result.rawBytes = writeUnsigned(bits, width, order);
            result.rawBits = bits;
            result.rawValue = value;
            if (bounded != raw) result.status = CodecStatus::Clamped;
        } else {
            const double value = raw;
            quint64 bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            result.rawBytes = writeUnsigned(bits, width, order);
            result.rawBits = bits;
            result.rawValue = value;
        }
    } else {
        const int bits = width * 8;
        const double rounded = std::round(raw);
        if (map.dataSigned) {
            const double minimum = -std::ldexp(1.0, bits - 1);
            const double maximumExclusive = std::ldexp(1.0, bits - 1);
            const bool clampedLow = rounded < minimum;
            // For 64-bit signed cells, qint64's largest value rounds to the
            // exclusive upper bound as a double.  Handle that endpoint before
            // casting so a user value can never invoke an out-of-range cast.
            const bool clampedHigh = rounded >= maximumExclusive;
            const qint64 signedValue = clampedLow
                ? (bits == 64 ? std::numeric_limits<qint64>::min()
                              : qint64(minimum))
                : (clampedHigh
                    ? (bits == 64 ? std::numeric_limits<qint64>::max()
                                  : qint64(maximumExclusive - 1.0))
                    : qint64(rounded));
            if (clampedLow || clampedHigh) result.status = CodecStatus::Clamped;
            result.rawBytes = writeUnsigned(quint64(signedValue), width, order);
            result.rawBits = quint64(signedValue);
            result.rawValue = signedValue;
        } else {
            const double maximumExclusive = std::ldexp(1.0, bits);
            const bool clampedLow = rounded < 0.0;
            const bool clampedHigh = rounded >= maximumExclusive;
            const quint64 unsignedValue = clampedLow ? 0
                : (clampedHigh
                    ? (bits == 64 ? std::numeric_limits<quint64>::max()
                                  : quint64(maximumExclusive - 1.0))
                    : quint64(rounded));
            if (clampedLow || clampedHigh) result.status = CodecStatus::Clamped;
            result.rawBytes = writeUnsigned(unsignedValue, width, order);
            result.rawBits = unsignedValue;
            result.rawValue = double(unsignedValue);
        }
    }

    if (result.status != CodecStatus::Ok)
        result.message = statusMessage(result.status);
    return result;
}

CodecResult MapValueCodec::parseRaw(const QString &text, const MapInfo &map,
                                    ByteOrder projectByteOrder, bool hexadecimal)
{
    const QString input = text.trimmed();
    CodecResult result;
    if (input.isEmpty()) {
        result.status = CodecStatus::InvalidRange;
        result.message = QObject::tr("Enter a raw value");
        return result;
    }

    if (!hexadecimal) {
        bool ok = false;
        const double raw = QLocale::c().toDouble(input, &ok);
        if (!ok) {
            result.status = CodecStatus::InvalidRange;
            result.message = QObject::tr("Invalid raw decimal value");
            return result;
        }
        return rawToBytes(raw, map, projectByteOrder);
    }

    QString digits = input;
    if (digits.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
        digits.remove(0, 2);
    bool ok = false;
    const quint64 bits = digits.toULongLong(&ok, 16);
    const int width = cellWidth(map);
    const quint64 maxBits = width == 8 ? std::numeric_limits<quint64>::max()
                                       : (quint64(1) << (width * 8)) - 1;
    if (!ok || bits > maxBits) {
        result.status = CodecStatus::InvalidRange;
        result.message = QObject::tr("Raw hexadecimal value does not fit this cell");
        return result;
    }

    result.rawBytes = writeUnsigned(bits, width, cellByteOrder(map, projectByteOrder));
    // Decode the bit pattern to obtain signed/float rawValue, while retaining
    // the original bytes and bypassing all physical scaling on the write path.
    CodecResult decoded = decode(result.rawBytes, 0, map, projectByteOrder);
    decoded.rawBytes = result.rawBytes;
    decoded.rawBits = bits;
    return decoded;
}

QString MapValueCodec::formatPhysical(const CodecResult &value, const MapInfo &map)
{
    if (std::isnan(value.physicalValue)) return QStringLiteral("NaN");
    if (std::isinf(value.physicalValue))
        return value.physicalValue < 0.0 ? QStringLiteral("-Inf") : QStringLiteral("+Inf");
    return isScaled(map) ? map.scaling.formatValue(value.physicalValue)
                         : QString::number(value.physicalValue, 'g', 10);
}

QString MapValueCodec::formatRaw(const CodecResult &value, const MapInfo &map,
                                 bool hexadecimal)
{
    const int width = cellWidth(map);
    if (hexadecimal) {
        return QStringLiteral("0x%1").arg(value.rawBits, width * 2, 16,
                                             QChar('0')).toUpper();
    }
    if (isFloatingPoint(map)) {
        if (std::isnan(value.rawValue)) return QStringLiteral("NaN");
        if (std::isinf(value.rawValue))
            return value.rawValue < 0.0 ? QStringLiteral("-Inf") : QStringLiteral("+Inf");
        return QString::number(value.rawValue, 'g', 10);
    }

    return map.dataSigned
        ? QString::number(signExtend(value.rawBits, width))
        : QString::number(value.rawBits);
}
