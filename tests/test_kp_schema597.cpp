/* Schema-597 WinOLS KP codec regression test. SPDX-License-Identifier: GPL-3.0-or-later */

#include "io/ols/KpImporter.h"

#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

namespace {
bool require(bool condition, const char *message)
{
    if (!condition) QTextStream(stderr) << "FAIL: " << message << '\n';
    return condition;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QFile file(QStringLiteral(KP_SCHEMA597_FIXTURE));
    if (!file.open(QIODevice::ReadOnly)) return 0;
    const auto result = ols::KpImporter::importFromBytes(
        file.readAll(), 0, 4u * 1024u * 1024u);
    bool ok = true;
    ok &= require(result.error.isEmpty(), "schema-597 fixture rejected");
    ok &= require(result.formatVersion == 597, "wrong schema version");
    ok &= require(result.maps.size() == 303, "wrong schema-597 map count");
    ok &= require(result.folders.size() == 22, "wrong schema-597 folder count");
    ok &= require(result.carriedData.size() == 0x14952,
                  "wrong schema-597 carried-data span");
    if (result.maps.size() == 303) {
        ok &= require(result.maps.constLast()
                          .getSideProp(QStringLiteral("kpInternRecordEnd")).toLongLong()
                          == result.maps.constLast()
                                 .getSideProp(QStringLiteral("kpInternRecordStart")).toLongLong()
                              + result.maps.constLast()
                                    .getSideProp(QStringLiteral("kpSerializedRecord"))
                                    .toByteArray().size(),
                      "schema-597 final record boundary is inconsistent");
    }
    return ok ? 0 : 1;
}
