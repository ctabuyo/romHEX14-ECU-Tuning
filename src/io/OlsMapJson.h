/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>
#include "romdata.h"

// OLS map-pack JSON import/export.
//
// The OLS "OLS-Files" map-pack exporter writes a plain JSON document of the
// form  { "maps": [ { "Name": ..., "Fieldvalues.StartAddr": "$39C818", ... } ] }
// where every property is a string and the keys mirror the OLS map-property
// names (AxisX.*, AxisY.*, Fieldvalues.*, Type, DataOrg, ...).  It carries map
// *definitions* only (geometry, address, scaling, axes, folder) — no ROM bytes.
//
// Conventions of the format that this codec honours on both sides:
//   * Addresses are file offsets written as "$" + upper-case hex ("$39C818"),
//     or "0" when unset.
//   * Decimal numbers use the exporting machine's locale, so "0,046882" and
//     "0.046882" are both accepted; export defaults to the comma form the
//     reference files use.
//   * Type: eEinzel (1x1), eEindim (1 x N curve), eZweidim (N x M map),
//     eZweiInv (N x 1 curve laid out vertically).
//   * DataOrg: eByte / eLoHi / eHiLo / eLoHiLoHi / eHiLoHiLo / eFloatLoHi /
//     eFloatHiLo / eDoubleLoHi / eDoubleHiLo.
//   * Axis DataSrc: eRom (points live in the ROM at AxisX.DataAddr) or
//     eUserdef (fixed points listed in AxisX.Values, '.'-decimal, space-separated).
namespace olsjson {

struct ImportResult {
    QVector<MapInfo> maps;
    QString          error;      // set on malformed JSON / wrong document shape
    QStringList      warnings;   // per-map notes about unsupported properties
};

// Quick structural sniff: true when the bytes look like an OLS map-pack JSON
// (top-level "maps" array whose entries carry OLS property keys). Used to
// route dropped .json files without fully parsing them into maps.
bool looksLikeOlsMapJson(const QByteArray &json);

// Parse an OLS map-pack JSON document. On failure `error` is set and `maps`
// is empty.
ImportResult importFromJson(const QByteArray &json);

struct ExportOptions {
    ByteOrder projectByteOrder = ByteOrder::BigEndian; // for maps without an explicit cell type
    bool      decimalComma     = true;                 // "0,046882" (OLS default) vs "0.046882"
};

// Serialize map definitions to an OLS map-pack JSON document.
QByteArray exportToJson(const QVector<MapInfo> &maps, const ExportOptions &opt);

} // namespace olsjson
