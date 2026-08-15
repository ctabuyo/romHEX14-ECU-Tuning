/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "romdata.h"

enum class CodecStatus {
    Ok,
    Clamped,
    InvalidRange,
    NonFinite,
    NonInvertible,
};

struct CodecResult {
    QByteArray rawBytes;
    quint64 rawBits = 0;
    double rawValue = 0.0;
    double physicalValue = 0.0;
    CodecStatus status = CodecStatus::Ok;
    QString message;

    bool isUsable() const {
        return status == CodecStatus::Ok || status == CodecStatus::Clamped;
    }
    bool needsConfirmation() const { return status == CodecStatus::Clamped; }
};

// Central conversion boundary for map cell reads and writes.  It owns byte
// order, signed integer limits, IEEE float handling, scaling and write-time
// overflow detection so map views never silently wrap a user-entered value.
class MapValueCodec {
public:
    static CodecResult decode(const QByteArray &data, uint32_t offset,
                              const MapInfo &map, ByteOrder projectByteOrder);
    static CodecResult physicalToBytes(double physical, const MapInfo &map,
                                       ByteOrder projectByteOrder);
    // Raw editing deliberately bypasses CompuMethod scaling. Hex input is a
    // storage bit pattern; decimal input is a raw numeric value.
    static CodecResult parseRaw(const QString &text, const MapInfo &map,
                                ByteOrder projectByteOrder, bool hexadecimal);
    static CodecResult rawToBytes(double raw, const MapInfo &map,
                                  ByteOrder projectByteOrder);
    static QString formatPhysical(const CodecResult &value, const MapInfo &map);
    static QString formatRaw(const CodecResult &value, const MapInfo &map,
                             bool hexadecimal);
    static bool isFloatingPoint(const MapInfo &map);
};
