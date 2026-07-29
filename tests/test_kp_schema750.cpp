/*
 * Schema-750 WinOLS KP codec regression test.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "io/ols/KpImporter.h"

#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

namespace {

bool require(bool condition, const char *message)
{
    if (!condition)
        QTextStream(stderr) << "FAIL: " << message << '\n';
    return condition;
}

ols::KpImportResult importFixture(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return ols::KpImporter::importFromBytes(file.readAll(), 0, 4u * 1024u * 1024u);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString primary = QStringLiteral(KP_SCHEMA750_PRIMARY_FIXTURE);
    if (!QFile::exists(primary))
        return 0; // Fixtures are deliberately local and not part of the source tree.

    const auto result = importFixture(primary);
    bool ok = true;
    ok &= require(result.error.isEmpty(), "primary fixture rejected");
    ok &= require(result.formatVersion == 750, "wrong schema version");
    ok &= require(result.maps.size() == 332, "wrong primary map count");
    ok &= require(result.carriedData.size() == 0xFC65, "wrong carried-data length");
    ok &= require(result.carriedData.left(16).toHex()
                      == QByteArrayLiteral("0005ffff0082000a0032003200328025"),
                  "wrong carried-data prefix");
    if (result.maps.size() == 332) {
        const auto &compressor = result.maps.at(39);
        const auto &correction = result.maps.at(50);
        const auto &knock = result.maps.at(204);
        ok &= require(!compressor.dataSigned, "compressor signedness changed");
        ok &= require(correction.dataSigned, "correction signedness changed");
        ok &= require(knock.dataSigned, "knock signedness changed");
        ok &= require(!correction.columnMajor && !knock.columnMajor,
                      "schema-750 cell ordering changed");
        for (const auto &map : result.maps) {
            ok &= require(map.getSideProp(QStringLiteral("kpSerializedTail"))
                              .toByteArray().size() == 142,
                          "map tail no longer frames exactly");
            ok &= require(!map.getSideProp(QStringLiteral("kpSerializedRecord"))
                               .toByteArray().isEmpty(),
                          "raw schema record was not retained");
        }
    }

    QFile file(primary);
    if (file.open(QIODevice::ReadOnly)) {
        const QByteArray source = file.readAll();
        QByteArray corrupted = source;
        corrupted[0] = char(uint8_t(corrupted.at(0)) ^ 0x01);
        const auto bad = ols::KpImporter::importFromBytes(
            corrupted, 0, 4u * 1024u * 1024u);
        ok &= require(!bad.error.isEmpty(), "invalid container was accepted");

        // Version-gated fields are not interchangeable. A stream whose
        // wrapper declares an untraced schema must never be decoded with the
        // schema-750 layout merely because it looks structurally similar.
        QByteArray unsupported = source;
        unsupported[16] = char(0xEF); // 0x000002EF == schema 751, LE
        unsupported[17] = char(0x02);
        unsupported[18] = '\0';
        unsupported[19] = '\0';
        const auto unknown = ols::KpImporter::importFromBytes(
            unsupported, 0, 4u * 1024u * 1024u);
        ok &= require(!unknown.error.isEmpty()
                          && unknown.error.contains(QStringLiteral("Unsupported KP schema 751")),
                      "untraced schema was decoded as schema 750");
    }

    const QString secondary = QStringLiteral(KP_SCHEMA750_SECONDARY_FIXTURE);
    if (QFile::exists(secondary)) {
        const auto alternate = importFixture(secondary);
        ok &= require(alternate.error.isEmpty(), "secondary fixture rejected");
        ok &= require(alternate.maps.size() == 1, "wrong secondary map count");
        ok &= require(alternate.carriedData.size() == 0x90,
                      "wrong secondary carried-data length");
    }
    return ok ? 0 : 1;
}
