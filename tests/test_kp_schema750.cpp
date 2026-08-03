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
    ok &= require(result.mapRecords.size() == result.maps.size(),
                  "every schema-750 map must have a native field ledger");
    ok &= require(result.outerEnvelope.size() == 0x42e,
                  "wrong outer-envelope boundary");
    ok &= require(result.metadata.streamOffset == 0x18
                      && result.metadata.streamEnd == 0x32c,
                  "schema-750 metadata stream boundary changed");
    ok &= require(result.root.streamOffset == 0x32c
                      && result.root.streamEnd == 0x42e
                      && result.root.records.size() == 2,
                  "schema-750 root stream boundary or records changed");
    ok &= require(result.trailingMetadata.size() == 0x1c432,
                  "wrong trailing-metadata boundary");
    ok &= require(result.folders.size() == 30, "wrong schema-750 folder count");
    if (!result.folders.isEmpty()) {
        ok &= require(!result.folders.constFirst().serializedRecord.isEmpty(),
                      "folder record was not retained");
    }
    ok &= require(result.carriedData.size() == 0xFC65, "wrong carried-data length");
    ok &= require(result.carriedData.left(16).toHex()
                      == QByteArrayLiteral("0005ffff0082000a0032003200328025"),
                  "wrong carried-data prefix");
    if (result.maps.size() == 332) {
        const auto &first = result.maps.constFirst();
        const auto &correction = result.maps.at(50);
        const auto &knock = result.maps.at(204);
        ok &= require(first.name
                          == QStringLiteral("Boost deviation tolerance - lower threshold"),
                      "native structured display name changed");
        ok &= require(first.getSideProp(QStringLiteral("kpMapIdentifier")).toString()
                          == QStringLiteral("med17.9_boost-deviation-tolerance---lower-threshold_16L-0_0"),
                      "native technical identifier changed");
        ok &= require(first.getSideProp(QStringLiteral("kpInternRecordStart")).toLongLong() == 5,
                      "first native intern record offset changed");
        ok &= require(result.maps.constLast()
                          .getSideProp(QStringLiteral("kpInternRecordEnd")).toLongLong()
                          == 0x3c1da,
                      "last native intern record boundary changed");
        ok &= require(result.mapRecords.constFirst().streamOffset == 5
                          && result.mapRecords.constLast().streamEnd == 0x3c1da
                          && !result.mapRecords.constFirst().fields.isEmpty(),
                      "schema-750 field ledger no longer covers every record byte");
        ok &= require(!correction.columnMajor && !knock.columnMajor,
                      "schema-750 cell ordering changed");
        for (const auto &map : result.maps) {
            ok &= require(map.getSideProp(QStringLiteral("kpSerializedTail"))
                              .toByteArray().size() == 148,
                          "native schema-750 post-axis fields no longer frame exactly");
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

        // The pre-ZIP root stream is native framing, not tolerated padding.
        // Its two marker/count fields must be decoded before ZIP discovery.
        QByteArray badRootMarker = source;
        badRootMarker[0x338] = char(uint8_t(badRootMarker.at(0x338)) ^ 0x01);
        const auto malformedRoot = ols::KpImporter::importFromBytes(
            badRootMarker, 0, 4u * 1024u * 1024u);
        ok &= require(!malformedRoot.error.isEmpty()
                          && malformedRoot.error.contains(QStringLiteral("root stream")),
                      "invalid schema-750 root marker was accepted");

        QByteArray badArchiveLength = source;
        badArchiveLength[0x42a] = char(uint8_t(badArchiveLength.at(0x42a)) ^ 0x01);
        const auto malformedArchiveLength = ols::KpImporter::importFromBytes(
            badArchiveLength, 0, 4u * 1024u * 1024u);
        ok &= require(!malformedArchiveLength.error.isEmpty()
                          && malformedArchiveLength.error.contains(QStringLiteral("root stream")),
                      "invalid schema-750 archive length was accepted");

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
        ok &= require(alternate.mapRecords.size() == 1
                          && alternate.mapRecords.constFirst().streamOffset == 5
                          && alternate.mapRecords.constFirst().streamEnd == 0x2b5,
                      "secondary map field ledger boundary changed");
        ok &= require(alternate.metadata.streamOffset == 0x18
                          && alternate.metadata.streamEnd == 0x355,
                      "wrong secondary metadata stream boundary");
        ok &= require(alternate.root.streamOffset == 0x355
                          && alternate.root.streamEnd == 0x457
                          && alternate.root.records.size() == 2,
                      "wrong secondary root stream boundary or records");
        ok &= require(alternate.carriedData.size() == 0x90,
                      "wrong secondary carried-data length");
    }
    return ok ? 0 : 1;
}
