/*
 * Schema-750 KP importer regression test.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "../src/io/ols/KpImporter.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <cstdio>

namespace {

bool require(bool condition, const char *message)
{
    if (!condition)
        std::fprintf(stderr, "FAIL: %s\n", message ? message : "(no message)");
    return condition;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QFile fixture(QStringLiteral(KP_SCHEMA750_FIXTURE));
    if (!fixture.exists()) {
        qInfo() << "SKIP: schema-750 KP fixture unavailable";
        return 0;
    }
    if (!fixture.open(QIODevice::ReadOnly)) {
        qCritical() << fixture.errorString();
        return 1;
    }
    const QByteArray bytes = fixture.readAll();
    const auto result = ols::KpImporter::importFromBytes(bytes, 0, 4u * 1024u * 1024u);

    bool ok = true;
    ok &= require(result.error.isEmpty(), qPrintable(result.error));
    ok &= require(result.formatVersion == 750, "schema mismatch");
    ok &= require(result.mapCount == 332, "map count mismatch");
    ok &= require(result.maps.size() == 332, "map vector size mismatch");
    ok &= require(result.mapFolderIds.size() == 332, "folder reference count mismatch");
    ok &= require(result.folderNames.size() == 30, "folder count mismatch");
    ok &= require(result.valueSpans.size() == 332, "value span count mismatch");
    ok &= require(result.carriedData.size() == 0xFC65,
                  "carried-data length mismatch");
    ok &= require(result.carriedDataFileOffset == 0x1225E,
                  "carried-data file offset mismatch");
    ok &= require(result.carriedData.left(16).toHex()
                      == QByteArrayLiteral("0005ffff0082000a0032003200328025"),
                  "carried-data prefix mismatch");
    if (result.valueSpans.size() == 332) {
        const auto &first = result.valueSpans.constFirst();
        ok &= require(first.valueOffset == 0 && first.valueLength == 2,
                      "first packed value span mismatch");
    }
    if (result.maps.size() == 332) {
        const MapInfo &known = result.maps.at(266);
        ok &= require(known.name == QStringLiteral("Engine torque request in monitoring"),
                      "known map name mismatch");
        ok &= require(known.address == 0x34115A, "known map address mismatch");
        ok &= require(known.dimensions.x == 8 && known.dimensions.y == 8,
                      "known map dimensions mismatch");
        ok &= require(known.dataSize == 2, "known map element size mismatch");
        ok &= require(known.hasScaling && known.scaling.linA == 0.1
                      && known.scaling.linB == 0.0,
                      "known map scaling mismatch");
        ok &= require(known.xAxis.hasPtsAddress
                      && known.xAxis.ptsAddress == 0x34114A
                      && known.xAxis.scaling.linA == 0.390625,
                      "known X axis mismatch");
        ok &= require(known.yAxis.hasPtsAddress
                      && known.yAxis.ptsAddress == 0x34113A
                      && known.yAxis.scaling.linA == 40.0,
                      "known Y axis mismatch");
    }

    QByteArray corrupted = bytes;
    if (corrupted.size() > 0x500)
        corrupted[0x500] = char(uint8_t(corrupted.at(0x500)) ^ 0x01);
    const auto bad = ols::KpImporter::importFromBytes(
        corrupted, 0, 4u * 1024u * 1024u);
    ok &= require(!bad.error.isEmpty(), "corrupted intern was accepted");
    return ok ? 0 : 1;
}
