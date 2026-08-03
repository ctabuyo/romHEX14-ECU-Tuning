/*
 * Schema-292 WinOLS KP codec regression test.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "io/ols/KpImporter.h"

#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QTextStream>
#include <QtEndian>

namespace {

bool require(bool condition, const char *message)
{
    if (!condition)
        QTextStream(stderr) << "FAIL: " << message << '\n';
    return condition;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString fixture = QStringLiteral(KP_SCHEMA292_FIXTURE);
    QFile file(fixture);
    if (!file.open(QIODevice::ReadOnly))
        return 0; // Corpus fixtures are intentionally not tracked source assets.

    const auto result = ols::KpImporter::importFromBytes(
        file.readAll(), 0, 4u * 1024u * 1024u);
    bool ok = true;
    ok &= require(result.error.isEmpty(), "schema-292 fixture rejected");
    ok &= require(result.formatVersion == 292, "wrong schema version");
    ok &= require(result.maps.size() == 62, "wrong schema-292 map count");
    ok &= require(result.folders.size() == 14, "wrong schema-292 folder count");
    if (result.maps.size() == 62) {
        ok &= require(result.maps.constFirst().name == QStringLiteral("Drivers Wish"),
                      "first native schema-292 map changed");
        ok &= require(result.maps.constLast()
                          .getSideProp(QStringLiteral("kpInternRecordEnd")).toLongLong()
                          == 0x717e,
                      "schema-292 records no longer consume intern exactly");
        for (const auto &map : result.maps) {
            ok &= require(!map.getSideProp(QStringLiteral("kpSerializedRecord"))
                               .toByteArray().isEmpty(),
                          "schema-292 record was not retained losslessly");
        }
    }

    int corpusFiles = 0;
    QDirIterator corpusIterator(QStringLiteral(KP_CORPUS_DIR), {QStringLiteral("*.kp")},
                                QDir::Files, QDirIterator::Subdirectories);
    while (corpusIterator.hasNext()) {
        const QString path = corpusIterator.next();
        QFile candidate(path);
        if (!candidate.open(QIODevice::ReadOnly)) continue;
        const QByteArray bytes = candidate.readAll();
        if (bytes.size() < 24 || bytes.mid(4, 12) != QByteArrayLiteral("WinOLS File\0")
            || qFromLittleEndian<uint32_t>(
                reinterpret_cast<const uchar *>(bytes.constData() + 16)) != 292)
            continue;
        ++corpusFiles;
        const auto parsed = ols::KpImporter::importFromBytes(bytes, 0, 4u * 1024u * 1024u);
        ok &= require(parsed.error.isEmpty() && !parsed.maps.isEmpty(),
                      "a schema-292 corpus file was not decoded completely");
    }
    ok &= require(corpusFiles >= 1, "schema-292 corpus unavailable");
    return ok ? 0 : 1;
}
