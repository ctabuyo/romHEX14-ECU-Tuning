/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// Tests for OLS map-pack JSON import/export (src/io/OlsMapJson.cpp).
//
// Usage:  test_ols_mapjson            -> synthetic fixture + round-trip checks
//         test_ols_mapjson <file.json> -> also imports the given real pack and
//                                         prints a summary (no assertions).
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <cmath>
#include "../src/io/OlsMapJson.h"

static int failures = 0, passes = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { qCritical() << "FAIL:" << msg; ++failures; } \
    else { qDebug() << "  OK:" << msg; ++passes; } \
} while(0)

static QString findFixture(const QString &rel)
{
    for (const QString &c : { rel, QStringLiteral("../") + rel,
                              QCoreApplication::applicationDirPath() + QStringLiteral("/../") + rel })
        if (QFileInfo::exists(c)) return c;
    return {};
}
static const MapInfo *find(const QVector<MapInfo> &v, const QString &n)
{
    for (const auto &m : v) if (m.name == n) return &m;
    return nullptr;
}
static bool near(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }

static QByteArray readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

static void testSniff()
{
    qDebug() << "\n=== OLS JSON sniff ===";
    CHECK(olsjson::looksLikeOlsMapJson("{\"maps\":[{\"Fieldvalues.StartAddr\":\"$10\"}]}"), "pack recognised");
    CHECK(olsjson::looksLikeOlsMapJson("{\"maps\":[]}"), "empty pack recognised");
    CHECK(!olsjson::looksLikeOlsMapJson("{\"version\":1,\"maps\":[{\"name\":\"x\"}]}"), "rxpack-style rejected");
    CHECK(!olsjson::looksLikeOlsMapJson("[1,2,3]"), "array rejected");
    CHECK(!olsjson::looksLikeOlsMapJson("not json"), "garbage rejected");
}

static void testSynthetic()
{
    qDebug() << "\n=== OLS JSON import (synthetic fixture) ===";
    const QString path = findFixture("testdata/olsjson/synthetic.json");
    if (path.isEmpty()) { qCritical() << "FAIL: fixture missing"; ++failures; return; }

    auto res = olsjson::importFromJson(readAll(path));
    CHECK(res.error.isEmpty(), QString("no error: %1").arg(res.error));
    CHECK(res.maps.size() == 3, QString("3 maps (got %1)").arg(res.maps.size()));

    const MapInfo *egt = find(res.maps, "Exhaust gas temperature");
    CHECK(egt && egt->type == "MAP" && egt->dimensions.x == 4 && egt->dimensions.y == 3,
          QString("EGT MAP 4x3 (got %1 %2x%3)").arg(egt?egt->type:"?")
              .arg(egt?egt->dimensions.x:0).arg(egt?egt->dimensions.y:0));
    CHECK(egt && egt->address == 0x2100 && egt->length == 4*3*2,
          QString("EGT @0x2100 len 24 (got 0x%1 len %2)").arg(egt?egt->address:0,0,16).arg(egt?egt->length:0));
    CHECK(egt && egt->dataSize == 2 && egt->cellBigEndian && egt->cellDataType == 2 && egt->dataSigned,
          "EGT eHiLo -> 16-bit BE signed");
    CHECK(egt && egt->id == "AirCtl_EGT" && egt->folderPath == "Air Control" && egt->userNotes == "test note",
          "EGT id / folder / comment");
    CHECK(egt && egt->hasScaling && egt->scaling.type == CompuMethod::Type::Linear
              && near(egt->scaling.linA, 0.023436) && near(egt->scaling.linB, -273.128693)
              && egt->scaling.unit == "C",
          QString("EGT scaling 0.023436 / -273.128693 C (got %1 / %2 %3)")
              .arg(egt?egt->scaling.linA:0).arg(egt?egt->scaling.linB:0).arg(egt?egt->scaling.unit:"?"));
    CHECK(egt && egt->scaling.formatValue(8.1) == "8.10",
          QString("EGT precision 2 (formatValue -> %1)").arg(egt?egt->scaling.formatValue(8.1):"?"));
    CHECK(egt && egt->xAxis.hasPtsAddress && egt->xAxis.ptsAddress == 0x2000 && egt->xAxis.ptsCount == 4
              && egt->xAxis.ptsDataSize == 2 && egt->xAxis.ptsBigEndian && egt->xAxis.ptsDataType == 2,
          "EGT X axis @0x2000 n=4 u16 BE");
    CHECK(egt && egt->xAxis.hasScaling && near(egt->xAxis.scaling.linA, 0.046882)
              && egt->xAxis.scaling.unit == "% Air",
          "EGT X axis scaling");
    CHECK(egt && egt->yAxis.hasPtsAddress && egt->yAxis.ptsAddress == 0x2010 && egt->yAxis.ptsCount == 3
              && egt->yAxis.ptsDataSize == 1 && egt->yAxis.ptsDataType == 1,
          "EGT Y axis @0x2010 n=3 u8");
    CHECK(egt && egt->yAxis.inputName == "n_mot" && near(egt->yAxis.scaling.linA, 40.0)
              && egt->yAxis.scaling.unit == "RPM",
          "EGT Y axis name/scaling");

    const MapInfo *lim = find(res.maps, "RPM Limiter");
    CHECK(lim && lim->type == "CURVE" && lim->dimensions.x == 1 && lim->dimensions.y == 5,
          QString("Limiter eZweiInv -> CURVE 1x5 (got %1 %2x%3)").arg(lim?lim->type:"?")
              .arg(lim?lim->dimensions.x:0).arg(lim?lim->dimensions.y:0));
    CHECK(lim && lim->dataSize == 2 && !lim->cellBigEndian && lim->cellDataType == 3,
          "Limiter eLoHi -> 16-bit LE");
    CHECK(lim && !lim->yAxis.hasPtsAddress && lim->yAxis.fixedValues.size() == 5
              && near(lim->yAxis.fixedValues[4], 4.0),
          QString("Limiter Y axis user-defined 5 values (got %1)").arg(lim?lim->yAxis.fixedValues.size():0));
    CHECK(lim && lim->yAxis.inputName.isEmpty(), "Limiter Y axis name not polluted by value list");
    CHECK(lim && lim->yAxis.scaling.unit.isEmpty(), "Limiter Y axis unit '--' -> empty");

    const MapInfo *spd = find(res.maps, "Maximum speed of vehicle");
    CHECK(spd && spd->type == "VALUE" && spd->dimensions.x == 1 && spd->dimensions.y == 1
              && spd->address == 0x4000 && spd->dataSize == 1,
          "Speed eEinzel -> VALUE 1x1 u8 @0x4000");
    CHECK(spd && spd->hasScaling && spd->scaling.type == CompuMethod::Type::RationalFunction
              && near(spd->scaling.toPhysical(4.0), 1.0 / (4.0 * 0.0625)),
          QString("Speed reciprocal scaling (toPhysical(4) = %1)").arg(spd?spd->scaling.toPhysical(4.0):0));
    CHECK(spd && spd->scaling.unit == "km/h", "Speed unit km/h (dot-decimal factor parsed)");
}

static void testRoundTrip()
{
    qDebug() << "\n=== OLS JSON export -> import round-trip ===";
    const QString path = findFixture("testdata/olsjson/synthetic.json");
    if (path.isEmpty()) return;
    auto res = olsjson::importFromJson(readAll(path));

    olsjson::ExportOptions opt;
    const QByteArray out = olsjson::exportToJson(res.maps, opt);

    // Shape: same keys as the reference file, in the same (sorted) order.
    const QJsonDocument src = QJsonDocument::fromJson(readAll(path));
    const QJsonDocument dst = QJsonDocument::fromJson(out);
    CHECK(dst.isObject() && dst.object().value("maps").isArray(), "export has top-level maps array");
    const QJsonObject s0 = src.object().value("maps").toArray().first().toObject();
    const QJsonObject d0 = dst.object().value("maps").toArray().first().toObject();
    CHECK(s0.keys() == d0.keys(), QString("map 0 key set identical (%1 vs %2 keys)")
                                       .arg(s0.keys().size()).arg(d0.keys().size()));
    const QJsonObject s1 = src.object().value("maps").toArray().at(1).toObject();
    const QJsonObject d1 = dst.object().value("maps").toArray().at(1).toObject();
    CHECK(s1.keys() == d1.keys(), "map 1 (user-defined axes) key set identical incl. AxisX.Values");
    CHECK(d0.value("Fieldvalues.StartAddr").toString() == "$2100",
          QString("address written as $hex (got %1)").arg(d0.value("Fieldvalues.StartAddr").toString()));
    CHECK(d0.value("Fieldvalues.Factor").toString() == "0,023436",
          QString("factor written with decimal comma (got %1)").arg(d0.value("Fieldvalues.Factor").toString()));
    CHECK(d0.value("Type").toString() == "eZweidim" && d1.value("Type").toString() == "eZweiInv",
          "types preserved");
    CHECK(d1.value("AxisY.Values").toString() == "0.0 1.0 2.0 3.0 4.0",
          QString("user-defined axis values (got %1)").arg(d1.value("AxisY.Values").toString()));
    CHECK(d0.value("bSigned").toString() == "1" && d0.value("DataOrg").toString() == "eHiLo",
          "signed + DataOrg preserved");
    const QJsonObject d2 = dst.object().value("maps").toArray().at(2).toObject();
    CHECK(d2.value("bReciprocal").toString() == "1" && d2.value("Fieldvalues.Factor").toString() == "0,062500",
          "reciprocal scaling preserved");

    // Semantic: re-import equals import.
    auto res2 = olsjson::importFromJson(out);
    CHECK(res2.error.isEmpty() && res2.maps.size() == res.maps.size(), "re-import ok, same count");
    for (int i = 0; i < res.maps.size() && i < res2.maps.size(); ++i) {
        const MapInfo &a = res.maps[i], &b = res2.maps[i];
        const bool same = a.name == b.name && a.id == b.id && a.address == b.address
            && a.dimensions.x == b.dimensions.x && a.dimensions.y == b.dimensions.y
            && a.dataSize == b.dataSize && a.cellBigEndian == b.cellBigEndian
            && a.dataSigned == b.dataSigned && a.folderPath == b.folderPath
            && a.userNotes == b.userNotes
            && near(a.scaling.toPhysical(1000), b.scaling.toPhysical(1000))
            && a.xAxis.ptsAddress == b.xAxis.ptsAddress && a.yAxis.ptsAddress == b.yAxis.ptsAddress
            && a.xAxis.fixedValues == b.xAxis.fixedValues && a.yAxis.fixedValues == b.yAxis.fixedValues
            && a.xAxis.inputName == b.xAxis.inputName && a.yAxis.inputName == b.yAxis.inputName;
        CHECK(same, QString("map %1 '%2' stable through round-trip").arg(i).arg(a.name));
    }

    // Dot-decimal export variant.
    opt.decimalComma = false;
    const QJsonDocument dot = QJsonDocument::fromJson(olsjson::exportToJson(res.maps, opt));
    CHECK(dot.object().value("maps").toArray().first().toObject().value("Fieldvalues.Offset").toString()
              == "-273.128693", "dot-decimal export variant");
}

static void testErrors()
{
    qDebug() << "\n=== OLS JSON error handling ===";
    auto r1 = olsjson::importFromJson("{ broken");
    CHECK(!r1.error.isEmpty() && r1.maps.isEmpty(), "malformed JSON -> error");
    auto r2 = olsjson::importFromJson("{\"foo\":1}");
    CHECK(!r2.error.isEmpty(), "missing maps array -> error");
    auto r3 = olsjson::importFromJson("{\"maps\":[{\"Name\":\"x\",\"Fieldvalues.StartAddr\":\"zz\"}]}");
    CHECK(r3.error.isEmpty() && r3.maps.isEmpty() && r3.warnings.size() == 1, "bad address -> skipped with warning");
}

static void importRealFile(const QString &path)
{
    qDebug() << "\n=== Import real pack:" << path;
    auto res = olsjson::importFromJson(readAll(path));
    if (!res.error.isEmpty()) { qCritical() << "error:" << res.error; ++failures; return; }
    int values = 0, curves = 0, maps = 0, romAxes = 0, userAxes = 0;
    QSet<QString> folders;
    for (const auto &m : res.maps) {
        if (m.type == "VALUE") ++values; else if (m.type == "CURVE") ++curves; else ++maps;
        for (const AxisInfo *ax : { &m.xAxis, &m.yAxis }) {
            if (ax->hasPtsAddress) ++romAxes;
            else if (!ax->fixedValues.isEmpty()) ++userAxes;
        }
        folders.insert(m.folderPath);
    }
    qDebug() << " maps:" << res.maps.size() << " VALUE:" << values << " CURVE:" << curves << " MAP:" << maps;
    qDebug() << " ROM axes:" << romAxes << " user-defined axes:" << userAxes << " folders:" << folders.size();
    qDebug() << " warnings:" << res.warnings.size();
    for (int i = 0; i < qMin(5, res.warnings.size()); ++i) qDebug() << "   " << res.warnings[i];

    // Round-trip: export and compare key sets + a handful of literal values.
    olsjson::ExportOptions opt;
    const QByteArray out = olsjson::exportToJson(res.maps, opt);
    const QJsonArray src = QJsonDocument::fromJson(readAll(path)).object().value("maps").toArray();
    const QJsonArray dst = QJsonDocument::fromJson(out).object().value("maps").toArray();
    int keyMismatch = 0, valueMismatch = 0;
    const QStringList literal = { "Name", "IdName", "FolderName", "Type", "DataOrg", "Rows", "Columns",
                                  "Fieldvalues.StartAddr", "Fieldvalues.Factor", "Fieldvalues.Offset",
                                  "Fieldvalues.Unit", "Precision", "bSigned", "AxisX.DataSrc", "AxisY.DataSrc",
                                  "AxisX.DataAddr", "AxisY.DataAddr", "AxisX.Factor", "AxisY.Factor",
                                  "AxisX.Values", "AxisY.Values", "AxisX.Unit", "AxisY.Unit" };
    for (int i = 0; i < src.size() && i < dst.size(); ++i) {
        const QJsonObject a = src[i].toObject(), b = dst[i].toObject();
        if (a.keys() != b.keys()) { ++keyMismatch; if (keyMismatch <= 3) qDebug() << "  key mismatch in" << a.value("Name").toString() << a.keys().size() << b.keys().size(); }
        for (const QString &k : literal) {
            if (a.contains(k) && a.value(k).toString() != b.value(k).toString()) {
                ++valueMismatch;
                if (valueMismatch <= 8)
                    qDebug() << "  value mismatch" << a.value("Name").toString() << k
                             << a.value(k).toString() << "->" << b.value(k).toString();
            }
        }
    }
    qDebug() << " round-trip: key-set mismatches:" << keyMismatch << " literal value mismatches:" << valueMismatch
             << "over" << literal.size() * src.size() << "checks";
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    testSniff();
    testSynthetic();
    testRoundTrip();
    testErrors();
    for (int i = 1; i < argc; ++i) importRealFile(QString::fromLocal8Bit(argv[i]));
    qDebug() << "\n" << passes << "passed," << failures << "failed";
    return failures ? 1 : 0;
}
