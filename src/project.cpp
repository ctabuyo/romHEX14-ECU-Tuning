/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "project.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStringList>
#include <QBuffer>
#include <QDebug>
#include <QCborStreamWriter>
#include <QCborStreamReader>
#include <QCborMap>
#include <QCborArray>
#include <QCborValue>
#include "io/rx14container.h"
#include "util/Blake3.h"
#include "annotations/AnnotationStore.h"

Project::Project(QObject *parent) : QObject(parent) {}

AnnotationStore *Project::annotations()
{
    if (!m_annotations) {
        m_annotations = new AnnotationStore(this);
        m_annotations->attachTo(this);
    }
    return m_annotations;
}

QString Project::displayName() const
{
    // Linked ROM windows use their label as the primary display name
    if (isLinkedRom && !name.isEmpty()) return name;
    // File name is primary for saved projects
    if (!filePath.isEmpty()) return QFileInfo(filePath).baseName();
    if (!name.isEmpty()) return name;
    if (!romPath.isEmpty()) return QFileInfo(romPath).baseName();
    // Last resort: brand/model for unsaved, unnamed projects
    QString n;
    if (!brand.isEmpty()) n = brand;
    if (!model.isEmpty()) n += (n.isEmpty() ? "" : " ") + model;
    if (!n.isEmpty()) return n;
    return "Untitled";
}

QString Project::listLabel() const
{
    QString base = displayName();

    QStringList parts;
    QString bm;
    if (!brand.isEmpty())   bm = brand;
    if (!model.isEmpty())   bm += (bm.isEmpty() ? "" : " ") + model;
    if (!bm.isEmpty())      parts << bm;
    if (!ecuType.isEmpty()) parts << ecuType;
    if (year > 0)           parts << QString::number(year);

    if (parts.isEmpty()) return base;

    // If displayName already IS the brand/model (unsaved-meta-only case)
    // and that's the only metadata, don't duplicate it in parentheses.
    if (parts.size() == 1 && base == parts.first())
        return base;

    // NOTE: must use \u escape (Unicode code point), NOT \x escape (raw bytes).
    // QStringLiteral lowers to u"...", where \xC2\xB7 becomes two char16_t
    // code units (U+00C2, U+00B7 = "Â·") instead of the single U+00B7 "·".
    const QString joined = parts.join(QStringLiteral("  \u00B7  "));  // "  ·  "
    return base + QStringLiteral("  (") + joined + QStringLiteral(")");
}

QString Project::fullTitle() const
{
    QString n = displayName();

    QStringList parts;
    QString bm;
    if (!brand.isEmpty())   bm = brand;
    if (!model.isEmpty())   bm += (bm.isEmpty() ? "" : " ") + model;
    // Skip brand/model if displayName already equals it (unsaved-meta-only case).
    if (!bm.isEmpty() && n != bm) parts << bm;
    if (!ecuType.isEmpty()) parts << ecuType;
    if (year > 0)           parts << QString("(%1)").arg(year);

    if (!parts.isEmpty())
        n += QStringLiteral("  \u2014  ") + parts.join(QStringLiteral("  "));

    // Linked ROM windows get the label prepended so they're distinguishable
    if (isLinkedRom && !name.isEmpty())
        n = name + QStringLiteral("  |  ") + n;
    // Mark reference/original linked ROMs with [ORI] prefix
    if (isLinkedRom && isLinkedReference)
        n = QStringLiteral("[") + tr("ORI") + QStringLiteral("]  ") + n;
    return n;
}

// ── Versioning ────────────────────────────────────────────────────────────────

void Project::snapshotVersion(const QString &versionName)
{
    ProjectVersion v;
    v.name    = versionName;
    v.created = QDateTime::currentDateTime();
    v.data    = currentData;
    versions.append(v);
    static constexpr int kMaxVersions = 50;
    while (versions.size() > kMaxVersions) versions.removeFirst();
    modified = true;
    emit versionsChanged();
}

bool Project::restoreVersion(int index)
{
    if (index < 0 || index >= versions.size()) return false;
    currentData = versions[index].data;
    modified    = true;
    emit dataChanged();
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// CBOR Block Codecs — encode helpers
// ═══════════════════════════════════════════════════════════════════════════════

// Write one block: computes checksum, writes header + payload, returns TocEntry.
static rx14fmt::TocEntry writeBlock(QIODevice *out, uint32_t magic,
                                     const QByteArray &payload)
{
    const qint64 offset = out->pos();

    rx14fmt::BlockHeader bh;
    bh.blockMagic    = magic;
    bh.blockSchema   = 1;
    bh.payloadSize   = static_cast<uint64_t>(payload.size());
    bh.blockChecksum = Blake3::hash16(payload);
    rx14fmt::writeBlockHeader(out, bh);
    out->write(payload);

    rx14fmt::TocEntry te;
    te.blockMagic  = magic;
    te.blockOffset = static_cast<uint64_t>(offset);
    te.blockSize   = static_cast<uint64_t>(rx14fmt::BLOCK_HEADER_SIZE + payload.size());
    return te;
}

// ── CBOR encoding helpers ────────────────────────────────────────────────────

static void cborWriteCompuMethodFull(QCborStreamWriter &w, const CompuMethod &cm)
{
    w.startMap(11);
    w.append(QLatin1String("type"));   w.append(static_cast<int>(cm.type));
    w.append(QLatin1String("unit"));   w.append(cm.unit);
    w.append(QLatin1String("format")); w.append(cm.format);
    w.append(QLatin1String("linA"));   w.append(cm.linA);
    w.append(QLatin1String("linB"));   w.append(cm.linB);
    w.append(QLatin1String("rfA"));    w.append(cm.rfA);
    w.append(QLatin1String("rfB"));    w.append(cm.rfB);
    w.append(QLatin1String("rfC"));    w.append(cm.rfC);
    w.append(QLatin1String("rfD"));    w.append(cm.rfD);
    w.append(QLatin1String("rfE"));    w.append(cm.rfE);
    w.append(QLatin1String("rfF"));    w.append(cm.rfF);
    w.endMap();
}

static void cborWriteAxisInfoFull(QCborStreamWriter &w, const AxisInfo &ax)
{
    w.startMap(9);
    w.append(QLatin1String("inputName"));    w.append(ax.inputName);
    w.append(QLatin1String("hasScaling"));   w.append(ax.hasScaling);
    w.append(QLatin1String("scaling"));      cborWriteCompuMethodFull(w, ax.scaling);
    w.append(QLatin1String("hasPtsAddr"));   w.append(ax.hasPtsAddress);
    w.append(QLatin1String("ptsAddr"));      w.append(static_cast<qint64>(ax.ptsAddress));
    w.append(QLatin1String("ptsCount"));     w.append(ax.ptsCount);
    w.append(QLatin1String("ptsDSize"));     w.append(ax.ptsDataSize);
    w.append(QLatin1String("ptsSigned"));    w.append(ax.ptsSigned);
    w.append(QLatin1String("fixedValues"));
    w.startArray(static_cast<quint64>(ax.fixedValues.size()));
    for (double v : ax.fixedValues)
        w.append(v);
    w.endArray();
    w.endMap();
}

static void cborWriteMapInfo(QCborStreamWriter &w, const MapInfo &m)
{
    w.startMap(20);
    w.append(QLatin1String("name"));        w.append(m.name);
    w.append(QLatin1String("id"));          w.append(m.id);
    w.append(QLatin1String("desc"));        w.append(m.description);
    w.append(QLatin1String("type"));        w.append(m.type);
    w.append(QLatin1String("rawAddr"));     w.append(static_cast<qint64>(m.rawAddress));
    w.append(QLatin1String("addr"));        w.append(static_cast<qint64>(m.address));
    w.append(QLatin1String("len"));         w.append(m.length);
    w.append(QLatin1String("dx"));          w.append(m.dimensions.x);
    w.append(QLatin1String("dy"));          w.append(m.dimensions.y);
    w.append(QLatin1String("dsize"));       w.append(m.dataSize);
    w.append(QLatin1String("dataSigned"));  w.append(m.dataSigned);
    w.append(QLatin1String("hasScaling"));  w.append(m.hasScaling);
    w.append(QLatin1String("scaling"));     cborWriteCompuMethodFull(w, m.scaling);
    w.append(QLatin1String("xAxis"));       cborWriteAxisInfoFull(w, m.xAxis);
    w.append(QLatin1String("yAxis"));       cborWriteAxisInfoFull(w, m.yAxis);
    w.append(QLatin1String("dataOff"));     w.append(static_cast<qint64>(m.mapDataOffset));
    w.append(QLatin1String("linkConf"));    w.append(m.linkConfidence);
    w.append(QLatin1String("colMajor"));    w.append(m.columnMajor);
    w.append(QLatin1String("userNotes"));   w.append(m.userNotes);
    w.append(QLatin1String("folder"));      w.append(m.folderPath);
    w.endMap();
}

// ── Block payload encoders ───────────────────────────────────────────────────

static QByteArray encodeMeta(const Project &p)
{
    QByteArray buf;
    QBuffer dev(&buf);
    dev.open(QIODevice::WriteOnly);
    QCborStreamWriter w(&dev);

    w.startMap(15);   // schema + name + romPath + a2lPath + vehicle + client
                      // + ecu + engine + projectType + mapLanguage + dates
                      // + user + byteOrder + baseAddress + uiHints = 15

    w.append(QLatin1String("schema")); w.append(1);
    w.append(QLatin1String("name"));   w.append(p.name);
    w.append(QLatin1String("romPath")); w.append(p.romPath);
    w.append(QLatin1String("a2lPath")); w.append(p.a2lPath);

    // Vehicle sub-map
    w.append(QLatin1String("vehicle"));
    w.startMap(8);
    w.append(QLatin1String("brand"));  w.append(p.brand);
    w.append(QLatin1String("model"));  w.append(p.model);
    w.append(QLatin1String("type"));   w.append(p.vehicleType);
    w.append(QLatin1String("build"));  w.append(p.vehicleBuild);
    w.append(QLatin1String("subModel")); w.append(p.vehicleModel);
    w.append(QLatin1String("characteristic")); w.append(p.vehicleCharacteristic);
    w.append(QLatin1String("year"));   w.append(p.year);
    w.append(QLatin1String("vin"));    w.append(p.vin);
    w.endMap();

    // Client sub-map
    w.append(QLatin1String("client"));
    w.startMap(3);
    w.append(QLatin1String("name"));    w.append(p.clientName);
    w.append(QLatin1String("nr"));      w.append(p.clientNr);
    w.append(QLatin1String("licence")); w.append(p.clientLicence);
    w.endMap();

    // ECU sub-map
    w.append(QLatin1String("ecu"));
    w.startMap(9);
    w.append(QLatin1String("type"));      w.append(p.ecuType);
    w.append(QLatin1String("use"));       w.append(p.ecuUse);
    w.append(QLatin1String("producer"));  w.append(p.ecuProducer);
    w.append(QLatin1String("nrProd"));    w.append(p.ecuNrProd);
    w.append(QLatin1String("nrEcu"));     w.append(p.ecuNrEcu);
    w.append(QLatin1String("swNumber"));  w.append(p.ecuSwNumber);
    w.append(QLatin1String("swVersion")); w.append(p.ecuSwVersion);
    w.append(QLatin1String("processor")); w.append(p.ecuProcessor);
    w.append(QLatin1String("checksum"));  w.append(p.ecuChecksum);
    w.endMap();

    // Engine sub-map
    w.append(QLatin1String("engine"));
    w.startMap(9);
    w.append(QLatin1String("displacement")); w.append(p.displacement);
    w.append(QLatin1String("producer"));     w.append(p.engineProducer);
    w.append(QLatin1String("code"));         w.append(p.engineCode);
    w.append(QLatin1String("type"));         w.append(p.engineType);
    w.append(QLatin1String("outputPS"));     w.append(p.outputPS);
    w.append(QLatin1String("outputKW"));     w.append(p.outputKW);
    w.append(QLatin1String("maxTorque"));    w.append(p.maxTorque);
    w.append(QLatin1String("emission"));     w.append(p.emission);
    w.append(QLatin1String("transmission")); w.append(p.transmission);
    w.endMap();

    // Project metadata
    w.append(QLatin1String("projectType"));  w.append(p.projectType);
    w.append(QLatin1String("mapLanguage"));  w.append(p.mapLanguage);

    w.append(QLatin1String("dates"));
    w.startMap(4);
    w.append(QLatin1String("createdAt")); w.append(p.createdAt.toString(Qt::ISODate));
    w.append(QLatin1String("createdBy")); w.append(p.createdBy);
    w.append(QLatin1String("changedAt")); w.append(p.changedAt.toString(Qt::ISODate));
    w.append(QLatin1String("changedBy")); w.append(p.changedBy);
    w.endMap();

    w.append(QLatin1String("user"));
    w.startMap(6);
    w.append(QLatin1String("1")); w.append(p.user1);
    w.append(QLatin1String("2")); w.append(p.user2);
    w.append(QLatin1String("3")); w.append(p.user3);
    w.append(QLatin1String("4")); w.append(p.user4);
    w.append(QLatin1String("5")); w.append(p.user5);
    w.append(QLatin1String("6")); w.append(p.user6);
    w.endMap();

    // Byte order + base address + linked-ROM flags
    w.append(QLatin1String("byteOrder"));
    w.append(QLatin1String(p.byteOrder == ByteOrder::LittleEndian ? "little" : "big"));
    w.append(QLatin1String("baseAddress"));
    w.append(static_cast<qint64>(p.baseAddress));

    // Per-project UI preferences (small bools that need to survive
    // close→reopen so the user isn't pestered by dismissed prompts).
    w.append(QLatin1String("uiHints"));
    w.startMap(2);
    w.append(QLatin1String("hideAutoDetectedMaps")); w.append(p.hideAutoDetectedMaps);
    w.append(QLatin1String("noMapsHintDismissed"));  w.append(p.noMapsHintDismissed);
    w.endMap();

    w.endMap(); // top-level map (15 entries)

    dev.close();
    return buf;
}

static QByteArray encodeNots(const Project &p)
{
    QByteArray buf;
    QBuffer dev(&buf);
    dev.open(QIODevice::WriteOnly);
    QCborStreamWriter w(&dev);

    w.startMap(3);

    w.append(QLatin1String("notes")); w.append(p.notes);

    w.append(QLatin1String("tuningLog"));
    w.startArray(static_cast<quint64>(p.tuningLog.size()));
    for (const auto &e : p.tuningLog) {
        w.startMap(7);
        w.append(QLatin1String("ts"));       w.append(e.timestamp.toString(Qt::ISODate));
        w.append(QLatin1String("author"));   w.append(e.author);
        w.append(QLatin1String("map"));      w.append(e.mapName);
        w.append(QLatin1String("category")); w.append(e.category);
        w.append(QLatin1String("message"));  w.append(e.message);
        w.append(QLatin1String("before"));   w.append(e.before);
        w.append(QLatin1String("after"));    w.append(e.after);
        w.endMap();
    }
    w.endArray();

    w.append(QLatin1String("dynoLog"));
    w.startArray(static_cast<quint64>(p.dynoLog.size()));
    for (const auto &d : p.dynoLog) {
        w.startMap(7);
        w.append(QLatin1String("ts"));        w.append(d.timestamp.toString(Qt::ISODate));
        w.append(QLatin1String("power"));     w.append(d.peakPower);
        w.append(QLatin1String("powerUnit")); w.append(d.powerUnit);
        w.append(QLatin1String("torque"));    w.append(d.peakTorque);
        w.append(QLatin1String("rpm"));       w.append(d.rpmAtPower);
        w.append(QLatin1String("notes"));     w.append(d.notes);
        w.append(QLatin1String("mods"));      w.append(d.modifications);
        w.endMap();
    }
    w.endArray();

    w.endMap();
    dev.close();
    return buf;
}

static QByteArray encodeRom(const QByteArray &data)
{
    QByteArray buf;
    QBuffer dev(&buf);
    dev.open(QIODevice::WriteOnly);
    QCborStreamWriter w(&dev);

    w.startMap(1);
    w.append(QLatin1String("data"));
    w.appendByteString(data.constData(), data.size());
    w.endMap();

    dev.close();
    return buf;
}

static QByteArray encodeMaps(const Project &p)
{
    QByteArray buf;
    QBuffer dev(&buf);
    dev.open(QIODevice::WriteOnly);
    QCborStreamWriter w(&dev);

    w.startMap(1);
    w.append(QLatin1String("maps"));
    w.startArray(static_cast<quint64>(p.maps.size()));
    for (const auto &m : p.maps)
        cborWriteMapInfo(w, m);
    w.endArray();
    w.endMap();

    dev.close();
    return buf;
}

// Same wire format as encodeMaps, just from `autoDetectedMaps` instead of
// `maps`. Persisted in BLK_AMAP so reopening the .rx14proj surfaces the
// overlay without having to re-run the (5–30s) scanner.
static QByteArray encodeAutoMaps(const Project &p)
{
    QByteArray buf;
    QBuffer dev(&buf);
    dev.open(QIODevice::WriteOnly);
    QCborStreamWriter w(&dev);

    w.startMap(1);
    w.append(QLatin1String("autoMaps"));
    w.startArray(static_cast<quint64>(p.autoDetectedMaps.size()));
    for (const auto &m : p.autoDetectedMaps)
        cborWriteMapInfo(w, m);
    w.endArray();
    w.endMap();

    dev.close();
    return buf;
}

static QByteArray encodeA2l(const QByteArray &a2lContent)
{
    QByteArray buf;
    QBuffer dev(&buf);
    dev.open(QIODevice::WriteOnly);
    QCborStreamWriter w(&dev);

    w.startMap(1);
    w.append(QLatin1String("content"));
    w.appendByteString(a2lContent.constData(), a2lContent.size());
    w.endMap();

    dev.close();
    return buf;
}

static QByteArray encodeGrps(const Project &p)
{
    QByteArray buf;
    QBuffer dev(&buf);
    dev.open(QIODevice::WriteOnly);
    QCborStreamWriter w(&dev);

    w.startMap(1);
    w.append(QLatin1String("groups"));
    w.startArray(static_cast<quint64>(p.groups.size()));
    for (const auto &g : p.groups) {
        w.startMap(4);
        w.append(QLatin1String("name")); w.append(g.name);
        w.append(QLatin1String("desc")); w.append(g.description);
        w.append(QLatin1String("characteristics"));
        w.startArray(static_cast<quint64>(g.characteristics.size()));
        for (const auto &c : g.characteristics) w.append(c);
        w.endArray();
        w.append(QLatin1String("subGroups"));
        w.startArray(static_cast<quint64>(g.subGroups.size()));
        for (const auto &s : g.subGroups) w.append(s);
        w.endArray();
        w.endMap();
    }
    w.endArray();
    w.endMap();

    dev.close();
    return buf;
}

static QByteArray encodeVers(const Project &p)
{
    QByteArray buf;
    QBuffer dev(&buf);
    dev.open(QIODevice::WriteOnly);
    QCborStreamWriter w(&dev);

    w.startMap(1);
    w.append(QLatin1String("versions"));
    w.startArray(static_cast<quint64>(p.versions.size()));
    for (const auto &v : p.versions) {
        w.startMap(3);
        w.append(QLatin1String("name"));    w.append(v.name);
        w.append(QLatin1String("created")); w.append(v.created.toString(Qt::ISODate));
        w.append(QLatin1String("data"));
        w.appendByteString(v.data.constData(), v.data.size());
        w.endMap();
    }
    w.endArray();
    w.endMap();

    dev.close();
    return buf;
}

static QByteArray encodeLink(const Project &p)
{
    QByteArray buf;
    QBuffer dev(&buf);
    dev.open(QIODevice::WriteOnly);
    QCborStreamWriter w(&dev);

    w.startMap(5);

    // Top-level linked-ROM flags (belong to this project, not a linked ROM entry)
    w.append(QLatin1String("isLinkedRom"));       w.append(p.isLinkedRom);
    w.append(QLatin1String("isLinkedReference"));  w.append(p.isLinkedReference);
    w.append(QLatin1String("linkedToProjectPath")); w.append(p.linkedToProjectPath);

    w.append(QLatin1String("linkedFromData"));
    if (!p.linkedFromData.isEmpty())
        w.appendByteString(p.linkedFromData.constData(), p.linkedFromData.size());
    else
        w.appendByteString("", 0);

    w.append(QLatin1String("linkedRoms"));
    w.startArray(static_cast<quint64>(p.linkedRoms.size()));
    for (const auto &lr : p.linkedRoms) {
        // Count: label, filePath, data, importedAt, isReference, sourceProjectPath,
        //        mapOffsets, mapConfidence = 8
        w.startMap(8);
        w.append(QLatin1String("label"));     w.append(lr.label);
        w.append(QLatin1String("filePath"));  w.append(lr.filePath);
        w.append(QLatin1String("data"));
        w.appendByteString(lr.data.constData(), lr.data.size());
        w.append(QLatin1String("importedAt")); w.append(lr.importedAt.toString(Qt::ISODate));
        w.append(QLatin1String("isReference")); w.append(lr.isReference);
        w.append(QLatin1String("sourceProjectPath")); w.append(lr.sourceProjectPath);

        w.append(QLatin1String("mapOffsets"));
        w.startMap(static_cast<quint64>(lr.mapOffsets.size()));
        for (auto it = lr.mapOffsets.constBegin(); it != lr.mapOffsets.constEnd(); ++it) {
            w.append(it.key());
            w.append(static_cast<qint64>(it.value()));
        }
        w.endMap();

        w.append(QLatin1String("mapConfidence"));
        w.startMap(static_cast<quint64>(lr.mapConfidence.size()));
        for (auto it = lr.mapConfidence.constBegin(); it != lr.mapConfidence.constEnd(); ++it) {
            w.append(it.key());
            w.append(it.value());
        }
        w.endMap();

        w.endMap(); // end linked rom entry
    }
    w.endArray();

    w.endMap(); // top-level
    dev.close();
    return buf;
}

static QByteArray encodeStar(const Project &p)
{
    QByteArray buf;
    QBuffer dev(&buf);
    dev.open(QIODevice::WriteOnly);
    QCborStreamWriter w(&dev);

    w.startMap(1);
    w.append(QLatin1String("starred"));
    w.startArray(static_cast<quint64>(p.starredMaps.size()));
    for (const auto &s : p.starredMaps)
        w.append(s);
    w.endArray();
    w.endMap();

    dev.close();
    return buf;
}

// ═══════════════════════════════════════════════════════════════════════════════
// saveToStream
// ═══════════════════════════════════════════════════════════════════════════════

bool Project::saveToStream(QIODevice *out, QString *err) const
{
    if (!out->isWritable()) {
        if (err) *err = QStringLiteral("Device is not writable");
        return false;
    }

    // 1. Write 64 zero bytes as header placeholder
    QByteArray zeros(rx14fmt::FILE_HEADER_SIZE, '\0');
    out->write(zeros);

    QVector<rx14fmt::TocEntry> toc;

    // 2. Write each block
    // META
    toc.append(writeBlock(out, rx14fmt::BLK_META, encodeMeta(*this)));

    // NOTS
    if (!notes.isEmpty() || !tuningLog.isEmpty() || !dynoLog.isEmpty())
        toc.append(writeBlock(out, rx14fmt::BLK_NOTS, encodeNots(*this)));

    // ROM0
    if (!currentData.isEmpty())
        toc.append(writeBlock(out, rx14fmt::BLK_ROM0, encodeRom(currentData)));

    // ROMO — skip if identical to currentData
    if (!originalData.isEmpty() && originalData != currentData)
        toc.append(writeBlock(out, rx14fmt::BLK_ROMO, encodeRom(originalData)));

    // MAPS
    if (!maps.isEmpty())
        toc.append(writeBlock(out, rx14fmt::BLK_MAPS, encodeMaps(*this)));

    // AMAP — auto-detected map overlay (only when no real A2L maps exist;
    // once the user imports an A2L the overlay is replaced and we skip
    // persisting it to keep the file lean).
    if (maps.isEmpty() && !autoDetectedMaps.isEmpty())
        toc.append(writeBlock(out, rx14fmt::BLK_AMAP, encodeAutoMaps(*this)));

    // A2L0
    if (!a2lContent.isEmpty())
        toc.append(writeBlock(out, rx14fmt::BLK_A2L0, encodeA2l(a2lContent)));

    // GRPS
    if (!groups.isEmpty())
        toc.append(writeBlock(out, rx14fmt::BLK_GRPS, encodeGrps(*this)));

    // VERS
    if (!versions.isEmpty())
        toc.append(writeBlock(out, rx14fmt::BLK_VERS, encodeVers(*this)));

    // LINK
    if (!linkedRoms.isEmpty() || isLinkedRom)
        toc.append(writeBlock(out, rx14fmt::BLK_LINK, encodeLink(*this)));

    // STAR
    if (!starredMaps.isEmpty())
        toc.append(writeBlock(out, rx14fmt::BLK_STAR, encodeStar(*this)));

    // 3. Write unknown blocks verbatim
    for (auto it = m_unknownBlocks.constBegin(); it != m_unknownBlocks.constEnd(); ++it) {
        const qint64 offset = out->pos();
        out->write(it.value());  // raw block header + payload bytes

        rx14fmt::TocEntry te;
        te.blockMagic  = it.key();
        te.blockOffset = static_cast<uint64_t>(offset);
        te.blockSize   = static_cast<uint64_t>(it.value().size());
        toc.append(te);
    }

    // 4. Write the TOC
    const qint64 tocOffset = out->pos();
    rx14fmt::writeTocMagic(out);
    for (const auto &te : toc)
        rx14fmt::writeTocEntry(out, te);

    const qint64 totalSize = out->pos();

    // 5. Compute body checksum (bytes from 0x40 to tocOffset)
    const qint64 bodyStart = rx14fmt::FILE_HEADER_SIZE;
    const qint64 bodyLen   = tocOffset - bodyStart;
    QByteArray bodyChecksum;
    if (bodyLen > 0) {
        out->seek(bodyStart);
        QByteArray body = out->read(bodyLen);
        bodyChecksum = Blake3::hash16(body);
    } else {
        bodyChecksum = Blake3::hash16(QByteArray());
    }

    // 6. Seek to 0, rewrite the header
    rx14fmt::FileHeader hdr;
    hdr.magic         = 0x52583134;
    hdr.formatVersion = 1;
    hdr.totalFileSize = static_cast<uint64_t>(totalSize);
    hdr.tocOffset     = static_cast<uint32_t>(tocOffset);
    hdr.tocBlockCount = static_cast<uint32_t>(toc.size());
    hdr.bodyChecksum  = bodyChecksum;

    out->seek(0);
    rx14fmt::writeHeader(out, hdr);

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// CBOR Block Codecs — decode helpers
// ═══════════════════════════════════════════════════════════════════════════════

// Read a full CBOR payload into a QCborValue tree.
static QCborValue readCborPayload(const QByteArray &payload)
{
    return QCborValue::fromCbor(payload);
}

static CompuMethod decodeCompuMethod(const QCborMap &m)
{
    CompuMethod cm;
    cm.type   = static_cast<CompuMethod::Type>(m[QLatin1String("type")].toInteger(0));
    cm.unit   = m[QLatin1String("unit")].toString();
    cm.format = m[QLatin1String("format")].toString();
    cm.linA   = m[QLatin1String("linA")].toDouble(1.0);
    cm.linB   = m[QLatin1String("linB")].toDouble(0.0);
    cm.rfA    = m[QLatin1String("rfA")].toDouble();
    cm.rfB    = m[QLatin1String("rfB")].toDouble();
    cm.rfC    = m[QLatin1String("rfC")].toDouble();
    cm.rfD    = m[QLatin1String("rfD")].toDouble();
    cm.rfE    = m[QLatin1String("rfE")].toDouble(1.0);
    cm.rfF    = m[QLatin1String("rfF")].toDouble();
    return cm;
}

static AxisInfo decodeAxisInfo(const QCborMap &m)
{
    AxisInfo ax;
    ax.inputName    = m[QLatin1String("inputName")].toString();
    ax.hasScaling   = m[QLatin1String("hasScaling")].toBool();
    ax.scaling      = decodeCompuMethod(m[QLatin1String("scaling")].toMap());
    ax.hasPtsAddress = m[QLatin1String("hasPtsAddr")].toBool();
    ax.ptsAddress   = static_cast<uint32_t>(m[QLatin1String("ptsAddr")].toInteger());
    ax.ptsCount     = static_cast<int>(m[QLatin1String("ptsCount")].toInteger());
    ax.ptsDataSize  = static_cast<int>(m[QLatin1String("ptsDSize")].toInteger(2));
    ax.ptsSigned    = m[QLatin1String("ptsSigned")].toBool();

    const QCborArray fv = m[QLatin1String("fixedValues")].toArray();
    for (const auto &v : fv)
        ax.fixedValues.append(v.toDouble());

    return ax;
}

static MapInfo decodeMapInfo(const QCborMap &m)
{
    MapInfo mi;
    mi.name           = m[QLatin1String("name")].toString();
    mi.id             = m[QLatin1String("id")].toString();
    mi.description    = m[QLatin1String("desc")].toString();
    mi.type           = m[QLatin1String("type")].toString();
    mi.rawAddress     = static_cast<uint32_t>(m[QLatin1String("rawAddr")].toInteger());
    mi.address        = static_cast<uint32_t>(m[QLatin1String("addr")].toInteger());
    mi.length         = static_cast<int>(m[QLatin1String("len")].toInteger());
    mi.dimensions     = { static_cast<int>(m[QLatin1String("dx")].toInteger(1)),
                          static_cast<int>(m[QLatin1String("dy")].toInteger(1)) };
    mi.dataSize       = static_cast<int>(m[QLatin1String("dsize")].toInteger(2));
    mi.dataSigned     = m[QLatin1String("dataSigned")].toBool();
    mi.hasScaling     = m[QLatin1String("hasScaling")].toBool();
    mi.scaling        = decodeCompuMethod(m[QLatin1String("scaling")].toMap());
    mi.xAxis          = decodeAxisInfo(m[QLatin1String("xAxis")].toMap());
    mi.yAxis          = decodeAxisInfo(m[QLatin1String("yAxis")].toMap());
    mi.mapDataOffset  = static_cast<uint32_t>(m[QLatin1String("dataOff")].toInteger());
    mi.linkConfidence = static_cast<int>(m[QLatin1String("linkConf")].toInteger());
    mi.columnMajor    = m[QLatin1String("colMajor")].toBool();
    mi.userNotes      = m[QLatin1String("userNotes")].toString();
    mi.folderPath     = m[QLatin1String("folder")].toString();
    return mi;
}

// ── Block decoders ───────────────────────────────────────────────────────────

static void decodeMeta(Project *p, const QByteArray &payload)
{
    const QCborMap root = readCborPayload(payload).toMap();

    p->name    = root[QLatin1String("name")].toString();
    p->romPath = root[QLatin1String("romPath")].toString();
    p->a2lPath = root[QLatin1String("a2lPath")].toString();

    const QCborMap veh = root[QLatin1String("vehicle")].toMap();
    p->brand                  = veh[QLatin1String("brand")].toString();
    p->model                  = veh[QLatin1String("model")].toString();
    p->vehicleType            = veh[QLatin1String("type")].toString();
    p->vehicleBuild           = veh[QLatin1String("build")].toString();
    p->vehicleModel           = veh[QLatin1String("subModel")].toString();
    p->vehicleCharacteristic  = veh[QLatin1String("characteristic")].toString();
    p->year                   = static_cast<int>(veh[QLatin1String("year")].toInteger());
    p->vin                    = veh[QLatin1String("vin")].toString();

    const QCborMap cli = root[QLatin1String("client")].toMap();
    p->clientName    = cli[QLatin1String("name")].toString();
    p->clientNr      = cli[QLatin1String("nr")].toString();
    p->clientLicence = cli[QLatin1String("licence")].toString();

    const QCborMap ecu = root[QLatin1String("ecu")].toMap();
    p->ecuType      = ecu[QLatin1String("type")].toString();
    p->ecuUse       = ecu[QLatin1String("use")].toString();
    p->ecuProducer  = ecu[QLatin1String("producer")].toString();
    p->ecuNrProd    = ecu[QLatin1String("nrProd")].toString();
    p->ecuNrEcu     = ecu[QLatin1String("nrEcu")].toString();
    p->ecuSwNumber  = ecu[QLatin1String("swNumber")].toString();
    p->ecuSwVersion = ecu[QLatin1String("swVersion")].toString();
    p->ecuProcessor = ecu[QLatin1String("processor")].toString();
    p->ecuChecksum  = ecu[QLatin1String("checksum")].toString();

    const QCborMap eng = root[QLatin1String("engine")].toMap();
    p->displacement  = eng[QLatin1String("displacement")].toString();
    p->engineProducer = eng[QLatin1String("producer")].toString();
    p->engineCode    = eng[QLatin1String("code")].toString();
    p->engineType    = eng[QLatin1String("type")].toString();
    p->outputPS      = static_cast<int>(eng[QLatin1String("outputPS")].toInteger());
    p->outputKW      = static_cast<int>(eng[QLatin1String("outputKW")].toInteger());
    p->maxTorque     = static_cast<int>(eng[QLatin1String("maxTorque")].toInteger());
    p->emission      = eng[QLatin1String("emission")].toString();
    p->transmission  = eng[QLatin1String("transmission")].toString();

    p->projectType  = root[QLatin1String("projectType")].toString();
    p->mapLanguage  = root[QLatin1String("mapLanguage")].toString();

    const QCborMap dates = root[QLatin1String("dates")].toMap();
    p->createdAt = QDateTime::fromString(dates[QLatin1String("createdAt")].toString(), Qt::ISODate);
    p->createdBy = dates[QLatin1String("createdBy")].toString();
    p->changedAt = QDateTime::fromString(dates[QLatin1String("changedAt")].toString(), Qt::ISODate);
    p->changedBy = dates[QLatin1String("changedBy")].toString();

    const QCborMap usr = root[QLatin1String("user")].toMap();
    p->user1 = usr[QLatin1String("1")].toString();
    p->user2 = usr[QLatin1String("2")].toString();
    p->user3 = usr[QLatin1String("3")].toString();
    p->user4 = usr[QLatin1String("4")].toString();
    p->user5 = usr[QLatin1String("5")].toString();
    p->user6 = usr[QLatin1String("6")].toString();

    p->byteOrder   = root[QLatin1String("byteOrder")].toString() == QLatin1String("little")
                         ? ByteOrder::LittleEndian : ByteOrder::BigEndian;
    p->baseAddress = static_cast<uint32_t>(root[QLatin1String("baseAddress")].toInteger());

    // uiHints sub-map — present in newer files; absent in older files,
    // toMap() returns empty map and toBool() defaults to false.
    const QCborMap hints = root[QLatin1String("uiHints")].toMap();
    p->hideAutoDetectedMaps = hints[QLatin1String("hideAutoDetectedMaps")].toBool();
    p->noMapsHintDismissed  = hints[QLatin1String("noMapsHintDismissed")].toBool();
}

static void decodeNots(Project *p, const QByteArray &payload)
{
    const QCborMap root = readCborPayload(payload).toMap();

    p->notes = root[QLatin1String("notes")].toString();

    const QCborArray logArr = root[QLatin1String("tuningLog")].toArray();
    for (const auto &ev : logArr) {
        const QCborMap e = ev.toMap();
        TuningLogEntry entry;
        entry.timestamp = QDateTime::fromString(e[QLatin1String("ts")].toString(), Qt::ISODate);
        entry.author    = e[QLatin1String("author")].toString();
        entry.mapName   = e[QLatin1String("map")].toString();
        entry.category  = e[QLatin1String("category")].toString();
        entry.message   = e[QLatin1String("message")].toString();
        entry.before    = e[QLatin1String("before")].toString();
        entry.after     = e[QLatin1String("after")].toString();
        p->tuningLog.append(entry);
    }

    const QCborArray dynoArr = root[QLatin1String("dynoLog")].toArray();
    for (const auto &dv : dynoArr) {
        const QCborMap d = dv.toMap();
        DynoResult dr;
        dr.timestamp  = QDateTime::fromString(d[QLatin1String("ts")].toString(), Qt::ISODate);
        dr.peakPower  = d[QLatin1String("power")].toDouble();
        dr.powerUnit  = d[QLatin1String("powerUnit")].toString();
        dr.peakTorque = d[QLatin1String("torque")].toDouble();
        dr.rpmAtPower = static_cast<int>(d[QLatin1String("rpm")].toInteger());
        dr.notes      = d[QLatin1String("notes")].toString();
        dr.modifications = d[QLatin1String("mods")].toString();
        p->dynoLog.append(dr);
    }
}

static QByteArray extractBytes(const QCborValue &v)
{
    if (v.isByteArray())
        return v.toByteArray();
    return QByteArray();
}

static void decodeRom0(Project *p, const QByteArray &payload)
{
    const QCborMap root = readCborPayload(payload).toMap();
    p->currentData = extractBytes(root[QLatin1String("data")]);
}

static void decodeRomO(Project *p, const QByteArray &payload)
{
    const QCborMap root = readCborPayload(payload).toMap();
    p->originalData = extractBytes(root[QLatin1String("data")]);
}

static void decodeMaps(Project *p, const QByteArray &payload)
{
    const QCborMap root = readCborPayload(payload).toMap();
    const QCborArray arr = root[QLatin1String("maps")].toArray();
    for (const auto &mv : arr)
        p->maps.append(decodeMapInfo(mv.toMap()));
}

static void decodeAutoMaps(Project *p, const QByteArray &payload)
{
    const QCborMap root = readCborPayload(payload).toMap();
    const QCborArray arr = root[QLatin1String("autoMaps")].toArray();
    for (const auto &mv : arr)
        p->autoDetectedMaps.append(decodeMapInfo(mv.toMap()));
}

static void decodeA2l0(Project *p, const QByteArray &payload)
{
    const QCborMap root = readCborPayload(payload).toMap();
    p->a2lContent = extractBytes(root[QLatin1String("content")]);
}

static void decodeGrps(Project *p, const QByteArray &payload)
{
    const QCborMap root = readCborPayload(payload).toMap();
    const QCborArray arr = root[QLatin1String("groups")].toArray();
    for (const auto &gv : arr) {
        const QCborMap g = gv.toMap();
        A2LGroup grp;
        grp.name        = g[QLatin1String("name")].toString();
        grp.description = g[QLatin1String("desc")].toString();
        const QCborArray chars = g[QLatin1String("characteristics")].toArray();
        for (const auto &c : chars)
            grp.characteristics.append(c.toString());
        const QCborArray subs = g[QLatin1String("subGroups")].toArray();
        for (const auto &s : subs)
            grp.subGroups.append(s.toString());
        p->groups.append(grp);
    }
}

static void decodeVers(Project *p, const QByteArray &payload)
{
    const QCborMap root = readCborPayload(payload).toMap();
    const QCborArray arr = root[QLatin1String("versions")].toArray();
    for (const auto &vv : arr) {
        const QCborMap v = vv.toMap();
        ProjectVersion pv;
        pv.name    = v[QLatin1String("name")].toString();
        pv.created = QDateTime::fromString(v[QLatin1String("created")].toString(), Qt::ISODate);
        pv.data    = extractBytes(v[QLatin1String("data")]);
        p->versions.append(pv);
    }
}

static void decodeLink(Project *p, const QByteArray &payload)
{
    const QCborMap root = readCborPayload(payload).toMap();

    p->isLinkedRom         = root[QLatin1String("isLinkedRom")].toBool();
    p->isLinkedReference   = root[QLatin1String("isLinkedReference")].toBool();
    p->linkedToProjectPath = root[QLatin1String("linkedToProjectPath")].toString();
    p->linkedFromData      = extractBytes(root[QLatin1String("linkedFromData")]);

    const QCborArray arr = root[QLatin1String("linkedRoms")].toArray();
    for (const auto &lv : arr) {
        const QCborMap l = lv.toMap();
        LinkedRom lr;
        lr.label       = l[QLatin1String("label")].toString();
        lr.filePath    = l[QLatin1String("filePath")].toString();
        lr.data        = extractBytes(l[QLatin1String("data")]);
        lr.importedAt  = QDateTime::fromString(l[QLatin1String("importedAt")].toString(), Qt::ISODate);
        lr.isReference = l[QLatin1String("isReference")].toBool();
        lr.sourceProjectPath = l[QLatin1String("sourceProjectPath")].toString();

        const QCborMap offsets = l[QLatin1String("mapOffsets")].toMap();
        for (auto it = offsets.constBegin(); it != offsets.constEnd(); ++it)
            lr.mapOffsets[it.key().toString()] = static_cast<uint32_t>(it.value().toInteger());

        const QCborMap conf = l[QLatin1String("mapConfidence")].toMap();
        for (auto it = conf.constBegin(); it != conf.constEnd(); ++it)
            lr.mapConfidence[it.key().toString()] = static_cast<int>(it.value().toInteger());

        p->linkedRoms.append(lr);
    }
}

static void decodeStar(Project *p, const QByteArray &payload)
{
    const QCborMap root = readCborPayload(payload).toMap();
    const QCborArray arr = root[QLatin1String("starred")].toArray();
    for (const auto &sv : arr)
        p->starredMaps.insert(sv.toString());
}

// ═══════════════════════════════════════════════════════════════════════════════
// loadFromStream
// ═══════════════════════════════════════════════════════════════════════════════

Project *Project::loadFromStream(QIODevice *in, QObject *parent, QString *err)
{
    // 1. Read header
    rx14fmt::FileHeader hdr = rx14fmt::readHeader(in, err);
    if (hdr.magic != 0x52583134) {
        if (err && err->isEmpty())
            *err = QStringLiteral("Not an RX14 file (bad magic)");
        return nullptr;
    }

    // 2. Read TOC
    QVector<rx14fmt::TocEntry> toc = rx14fmt::readToc(in, hdr, err);
    if (toc.isEmpty() && hdr.tocBlockCount > 0) {
        if (err && err->isEmpty())
            *err = QStringLiteral("Failed to read TOC");
        return nullptr;
    }

    // 3. Verify body checksum
    const qint64 bodyStart = rx14fmt::FILE_HEADER_SIZE;
    const qint64 bodyLen   = static_cast<qint64>(hdr.tocOffset) - bodyStart;
    if (bodyLen > 0) {
        in->seek(bodyStart);
        QByteArray body = in->read(bodyLen);
        QByteArray computed = Blake3::hash16(body);
        if (computed != hdr.bodyChecksum) {
            if (err)
                *err = QStringLiteral("Body checksum mismatch — file may be corrupted");
            return nullptr;
        }
    }

    // 4. Decode blocks
    auto *proj = new Project(parent);

    bool hasRomO = false;

    for (const auto &te : toc) {
        if (!in->seek(static_cast<qint64>(te.blockOffset))) {
            qWarning() << "loadFromStream: failed to seek to block"
                       << rx14fmt::magicToTag(te.blockMagic);
            continue;
        }

        rx14fmt::BlockHeader bh = rx14fmt::readBlockHeader(in, err);

        // Read payload
        QByteArray payload = in->read(static_cast<qint64>(bh.payloadSize));
        if (static_cast<uint64_t>(payload.size()) != bh.payloadSize) {
            qWarning() << "loadFromStream: short read for block"
                       << rx14fmt::magicToTag(te.blockMagic);
            continue;
        }

        // Verify per-block checksum
        QByteArray computed = Blake3::hash16(payload);
        if (computed != bh.blockChecksum) {
            qWarning() << "loadFromStream: checksum mismatch for block"
                       << rx14fmt::magicToTag(te.blockMagic);
            continue;
        }

        // Dispatch
        switch (te.blockMagic) {
        case rx14fmt::BLK_META: decodeMeta(proj, payload); break;
        case rx14fmt::BLK_NOTS: decodeNots(proj, payload); break;
        case rx14fmt::BLK_ROM0: decodeRom0(proj, payload); break;
        case rx14fmt::BLK_ROMO: decodeRomO(proj, payload); hasRomO = true; break;
        case rx14fmt::BLK_MAPS: decodeMaps(proj, payload); break;
        case rx14fmt::BLK_AMAP: decodeAutoMaps(proj, payload); break;
        case rx14fmt::BLK_A2L0: decodeA2l0(proj, payload); break;
        case rx14fmt::BLK_GRPS: decodeGrps(proj, payload); break;
        case rx14fmt::BLK_VERS: decodeVers(proj, payload); break;
        case rx14fmt::BLK_LINK: decodeLink(proj, payload); break;
        case rx14fmt::BLK_STAR: decodeStar(proj, payload); break;
        default:
            // Unknown block — preserve verbatim (header + payload)
            {
                in->seek(static_cast<qint64>(te.blockOffset));
                QByteArray raw = in->read(static_cast<qint64>(te.blockSize));
                proj->m_unknownBlocks[te.blockMagic] = raw;
            }
            break;
        }
    }

    // If no separate ROMO block, originalData = currentData
    if (!hasRomO)
        proj->originalData = proj->currentData;

    return proj;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Legacy JSON helpers (extracted from the old save/open)
// ═══════════════════════════════════════════════════════════════════════════════

static QJsonObject saveCompuMethod(const CompuMethod &cm)
{
    QJsonObject o;
    o["type"]   = (int)cm.type;
    o["unit"]   = cm.unit;
    o["format"] = cm.format;
    o["linA"]   = cm.linA;
    o["linB"]   = cm.linB;
    o["rfA"]    = cm.rfA; o["rfB"] = cm.rfB; o["rfC"] = cm.rfC;
    o["rfD"]    = cm.rfD; o["rfE"] = cm.rfE; o["rfF"] = cm.rfF;
    return o;
}

static CompuMethod loadCompuMethod(const QJsonObject &o)
{
    CompuMethod cm;
    cm.type   = (CompuMethod::Type)o["type"].toInt(0);
    cm.unit   = o["unit"].toString();
    cm.format = o["format"].toString();
    cm.linA   = o["linA"].toDouble(1.0);
    cm.linB   = o["linB"].toDouble(0.0);
    cm.rfA    = o["rfA"].toDouble(); cm.rfB = o["rfB"].toDouble(); cm.rfC = o["rfC"].toDouble();
    cm.rfD    = o["rfD"].toDouble(); cm.rfE = o["rfE"].toDouble(1.0); cm.rfF = o["rfF"].toDouble();
    return cm;
}

static QJsonObject saveAxisInfo(const AxisInfo &ax)
{
    QJsonObject o;
    o["inputName"]    = ax.inputName;
    o["hasScaling"]   = ax.hasScaling;
    o["scaling"]      = saveCompuMethod(ax.scaling);
    o["hasPtsAddr"]   = ax.hasPtsAddress;
    o["ptsAddr"]      = QString("0x%1").arg(ax.ptsAddress, 0, 16);
    o["ptsCount"]     = ax.ptsCount;
    o["ptsDSize"]     = ax.ptsDataSize;
    QJsonArray fv;
    for (double v : ax.fixedValues) fv.append(v);
    o["fixedValues"]  = fv;
    return o;
}

static AxisInfo loadAxisInfo(const QJsonObject &o)
{
    AxisInfo ax;
    ax.inputName    = o["inputName"].toString();
    ax.hasScaling   = o["hasScaling"].toBool();
    ax.scaling      = loadCompuMethod(o["scaling"].toObject());
    ax.hasPtsAddress = o["hasPtsAddr"].toBool();
    ax.ptsAddress   = o["ptsAddr"].toString().toUInt(nullptr, 0);
    ax.ptsCount     = o["ptsCount"].toInt();
    ax.ptsDataSize  = o["ptsDSize"].toInt(2);
    for (const auto &v : o["fixedValues"].toArray())
        ax.fixedValues.append(v.toDouble());
    return ax;
}

static QJsonObject saveMapInfo(const MapInfo &m)
{
    QJsonObject o;
    o["name"]       = m.name;
    o["id"]         = m.id;
    o["desc"]       = m.description;
    o["type"]       = m.type;
    o["addr"]       = QString("0x%1").arg(m.address, 0, 16);
    o["raw"]        = QString("0x%1").arg(m.rawAddress, 0, 16);
    o["len"]        = m.length;
    o["dsize"]      = m.dataSize;
    o["dx"]         = m.dimensions.x;
    o["dy"]         = m.dimensions.y;
    o["dataOff"]    = (int)m.mapDataOffset;
    o["hasScaling"] = m.hasScaling;
    o["scaling"]    = saveCompuMethod(m.scaling);
    o["xAxis"]      = saveAxisInfo(m.xAxis);
    o["yAxis"]      = saveAxisInfo(m.yAxis);
    if (!m.userNotes.isEmpty())
        o["userNotes"] = m.userNotes;
    if (m.linkConfidence > 0)
        o["linkConf"] = m.linkConfidence;
    if (m.columnMajor)
        o["colMajor"] = true;
    if (!m.folderPath.isEmpty())
        o["folder"] = m.folderPath;
    return o;
}

static MapInfo loadMapInfo(const QJsonObject &o)
{
    MapInfo m;
    m.name           = o["name"].toString();
    m.id             = o["id"].toString();
    m.description    = o["desc"].toString();
    m.type           = o["type"].toString();
    m.address        = o["addr"].toString().toUInt(nullptr, 0);
    m.rawAddress     = o["raw"].toString().toUInt(nullptr, 0);
    m.length         = o["len"].toInt();
    m.dataSize       = o["dsize"].toInt(2);
    m.dimensions     = { o["dx"].toInt(1), o["dy"].toInt(1) };
    m.mapDataOffset  = (uint32_t)o["dataOff"].toInt(0);
    m.hasScaling     = o["hasScaling"].toBool();
    m.scaling        = loadCompuMethod(o["scaling"].toObject());
    m.xAxis          = loadAxisInfo(o["xAxis"].toObject());
    m.yAxis          = loadAxisInfo(o["yAxis"].toObject());
    m.userNotes      = o["userNotes"].toString();
    m.linkConfidence = o["linkConf"].toInt(0);
    m.columnMajor    = o["colMajor"].toBool(false);
    m.folderPath     = o["folder"].toString();
    return m;
}

static QJsonObject saveA2LGroup(const A2LGroup &g)
{
    QJsonObject o;
    o["name"]        = g.name;
    o["desc"]        = g.description;
    QJsonArray chars;
    for (const auto &c : g.characteristics) chars.append(c);
    o["chars"]       = chars;
    QJsonArray subs;
    for (const auto &s : g.subGroups)      subs.append(s);
    o["subs"]        = subs;
    return o;
}

static A2LGroup loadA2LGroup(const QJsonObject &o)
{
    A2LGroup g;
    g.name        = o["name"].toString();
    g.description = o["desc"].toString();
    for (const auto &c : o["chars"].toArray())  g.characteristics.append(c.toString());
    for (const auto &s : o["subs"].toArray())   g.subGroups.append(s.toString());
    return g;
}

// ── Legacy JSON migration chain ──────────────────────────────────────────────

static constexpr int kLegacySchemaVersion = 2;

static void migrateV0toV1(QJsonObject &) {}
static void migrateV1toV2(QJsonObject &) {}

static void applyMigrations(QJsonObject &root)
{
    int ver = root["schemaVersion"].toInt(0);
    if (ver < 1) migrateV0toV1(root);
    if (ver < 2) migrateV1toV2(root);
}

// ── openLegacyJson ───────────────────────────────────────────────────────────

Project *Project::openLegacyJson(const QString &path, QObject *parent)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return nullptr;

    QJsonParseError parseErr;
    auto doc = QJsonDocument::fromJson(f.readAll(), &parseErr);
    f.close();
    if (parseErr.error != QJsonParseError::NoError) return nullptr;

    auto root = doc.object();
    applyMigrations(root);

    auto *proj = new Project(parent);
    proj->filePath     = path;
    proj->name         = root["name"].toString();
    proj->romPath      = root["romPath"].toString();
    proj->a2lPath      = root["a2lPath"].toString();
    proj->brand               = root["brand"].toString();
    proj->model               = root["model"].toString();
    proj->vehicleType         = root["vehicleType"].toString();
    proj->vehicleBuild        = root["vehicleBuild"].toString();
    proj->vehicleModel        = root["vehicleModel"].toString();
    proj->vehicleCharacteristic = root["vehicleChar"].toString();
    proj->year                = root["year"].toInt();
    proj->vin                 = root["vin"].toString();
    proj->clientName          = root["clientName"].toString();
    proj->clientNr            = root["clientNr"].toString();
    proj->clientLicence       = root["clientLic"].toString();
    proj->ecuType             = root["ecuType"].toString();
    proj->ecuUse              = root["ecuUse"].toString();
    proj->ecuProducer         = root["ecuProducer"].toString();
    proj->ecuNrProd           = root["ecuNrProd"].toString();
    proj->ecuNrEcu            = root["ecuNrEcu"].toString();
    proj->ecuSwNumber         = root["ecuSwNum"].toString();
    proj->ecuSwVersion        = root["ecuSwVer"].toString();
    proj->ecuProcessor        = root["ecuProc"].toString();
    proj->ecuChecksum         = root["ecuChecksum"].toString();
    proj->displacement        = root["displacement"].toString();
    proj->engineProducer      = root["engProducer"].toString();
    proj->engineCode          = root["engCode"].toString();
    proj->engineType          = root["engType"].toString();
    proj->outputPS            = root["outputPS"].toInt();
    proj->outputKW            = root["outputKW"].toInt();
    proj->maxTorque           = root["maxTorque"].toInt();
    proj->emission            = root["emission"].toString();
    proj->transmission        = root["transmission"].toString();
    proj->projectType         = root["projectType"].toString();
    proj->mapLanguage         = root["mapLanguage"].toString();
    proj->createdAt           = QDateTime::fromString(root["createdAt"].toString(), Qt::ISODate);
    proj->createdBy           = root["createdBy"].toString();
    proj->changedAt           = QDateTime::fromString(root["changedAt"].toString(), Qt::ISODate);
    proj->changedBy           = root["changedBy"].toString();
    proj->user1 = root["user1"].toString(); proj->user2 = root["user2"].toString();
    proj->user3 = root["user3"].toString(); proj->user4 = root["user4"].toString();
    proj->user5 = root["user5"].toString(); proj->user6 = root["user6"].toString();
    proj->notes               = root["notes"].toString();
    proj->byteOrder    = root["byteOrder"].toString() == "little"
                        ? ByteOrder::LittleEndian : ByteOrder::BigEndian;
    proj->baseAddress  = root["baseAddress"].toString().toUInt(nullptr, 0);
    proj->currentData  = QByteArray::fromBase64(root["currentData"].toString().toLatin1());
    proj->originalData = QByteArray::fromBase64(root["originalData"].toString().toLatin1());
    if (proj->originalData.isEmpty())
        proj->originalData = proj->currentData;
    if (root.contains("a2lContent"))
        proj->a2lContent = QByteArray::fromBase64(root["a2lContent"].toString().toLatin1());

    // Linked ROM metadata
    proj->isLinkedRom          = root["isLinkedRom"].toBool(false);
    proj->isLinkedReference    = root["isLinkedReference"].toBool(false);
    proj->linkedToProjectPath  = root["linkedToProjectPath"].toString();
    if (root.contains("linkedFromData"))
        proj->linkedFromData = QByteArray::fromBase64(root["linkedFromData"].toString().toLatin1());

    for (const auto &vv : root["versions"].toArray()) {
        auto vObj = vv.toObject();
        ProjectVersion v;
        v.name    = vObj["name"].toString();
        v.created = QDateTime::fromString(vObj["created"].toString(), Qt::ISODate);
        v.data    = QByteArray::fromBase64(vObj["data"].toString().toLatin1());
        proj->versions.append(v);
    }

    for (const auto &lv : root["linkedRoms"].toArray()) {
        auto lObj = lv.toObject();
        LinkedRom lr;
        lr.label       = lObj["label"].toString();
        lr.filePath    = lObj["filePath"].toString();
        lr.data        = QByteArray::fromBase64(lObj["data"].toString().toLatin1());
        lr.importedAt  = QDateTime::fromString(lObj["importedAt"].toString(), Qt::ISODate);
        lr.isReference = lObj["isReference"].toBool();
        lr.sourceProjectPath = lObj["sourceProjectPath"].toString();
        auto offsets = lObj["mapOffsets"].toObject();
        for (auto it = offsets.constBegin(); it != offsets.constEnd(); ++it)
            lr.mapOffsets[it.key()] = it.value().toString().toUInt(nullptr, 0);
        auto confidence = lObj["mapConfidence"].toObject();
        for (auto it = confidence.constBegin(); it != confidence.constEnd(); ++it)
            lr.mapConfidence[it.key()] = it.value().toInt();
        proj->linkedRoms.append(lr);
    }

    for (const auto &mv : root["maps"].toArray())
        proj->maps.append(loadMapInfo(mv.toObject()));

    for (const auto &sv : root["starredMaps"].toArray())
        proj->starredMaps.insert(sv.toString());

    for (const auto &gv : root["groups"].toArray())
        proj->groups.append(loadA2LGroup(gv.toObject()));

    // Tuning logbook
    for (const auto &ev : root["tuningLog"].toArray()) {
        auto eObj = ev.toObject();
        TuningLogEntry e;
        e.timestamp = QDateTime::fromString(eObj["ts"].toString(), Qt::ISODate);
        e.author    = eObj["author"].toString("AI");
        e.mapName   = eObj["map"].toString();
        e.category  = eObj["category"].toString("note");
        e.message   = eObj["message"].toString();
        e.before    = eObj["before"].toString();
        e.after     = eObj["after"].toString();
        proj->tuningLog.append(e);
    }

    // Dyno log
    for (const auto &dv : root["dynoLog"].toArray()) {
        auto dObj = dv.toObject();
        DynoResult d;
        d.timestamp  = QDateTime::fromString(dObj["ts"].toString(), Qt::ISODate);
        d.peakPower  = dObj["power"].toDouble();
        d.powerUnit  = dObj["powerUnit"].toString("PS");
        d.peakTorque = dObj["torque"].toDouble();
        d.rpmAtPower = dObj["rpm"].toInt();
        d.notes      = dObj["notes"].toString();
        d.modifications = dObj["mods"].toString();
        proj->dynoLog.append(d);
    }

    // ── Auto-migrate to new binary format ────────────────────────────────────
    // Rename old JSON file to .legacy.bak and save in new format
    const QString bakPath = path + QStringLiteral(".legacy.bak");
    QFile::remove(bakPath);              // remove stale backup if any
    QFile::rename(path, bakPath);        // preserve original
    proj->filePath = path;
    if (!proj->save()) {
        qWarning() << "openLegacyJson: auto-migration save failed for" << path;
        // Restore the original file so the user doesn't lose data
        QFile::remove(path);
        QFile::rename(bakPath, path);
    } else {
        qDebug() << "Migrated legacy JSON project to RX14 binary format:" << path
                 << "(backup at" << bakPath << ")";
    }
    proj->modified = false;

    return proj;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Public persistence API
// ═══════════════════════════════════════════════════════════════════════════════

bool Project::saveAs(const QString &path)
{
    filePath = path;
    return save();
}

bool Project::save()
{
    if (filePath.isEmpty()) return false;

    const QString tmp = filePath + QStringLiteral(".tmp");
    QFile f(tmp);
    if (!f.open(QIODevice::ReadWrite)) {
        qWarning() << "Project::save: cannot open for write:" << tmp
                   << f.errorString();
        return false;
    }

    QString err;
    if (!saveToStream(&f, &err)) {
        qWarning() << "Project::save:" << err;
        f.close();
        f.remove();
        return false;
    }

    f.flush();
    f.close();

    // Atomic rename
    QFile::remove(filePath);
    if (!QFile::rename(tmp, filePath)) {
        qWarning() << "Project::save: atomic rename failed from" << tmp
                   << "to" << filePath;
        return false;
    }

    modified = false;
    return true;
}

Project *Project::open(const QString &path, QObject *parent)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return nullptr;

    // Peek the first 4 bytes so we can distinguish formats on the full
    // magic rather than a single byte. The RX14 container header stores
    // magic = 0x52583134 as a little-endian u32, so on disk the byte
    // sequence is 34 31 58 52 ("41XR") — NOT 'R' as an earlier check
    // assumed. That mismatch meant every saved .rx14proj fell through to
    // openLegacyJson() and failed to open.
    const QByteArray peek = f.peek(4);
    if (peek.isEmpty()) return nullptr;
    const char first = peek.at(0);

    // Legacy JSON starts with '{'
    if (first == '{') {
        f.close();
        return openLegacyJson(path, parent);
    }

    // RX14 binary format: 32-bit little-endian magic 0x52583134 ('RX14').
    // On disk the first four bytes read "41XR" (0x34 0x31 0x58 0x52).
    if (peek.size() >= 4
        && static_cast<uint8_t>(peek.at(0)) == 0x34
        && static_cast<uint8_t>(peek.at(1)) == 0x31
        && static_cast<uint8_t>(peek.at(2)) == 0x58
        && static_cast<uint8_t>(peek.at(3)) == 0x52) {
        QString err;
        auto *p = loadFromStream(&f, parent, &err);
        f.close();
        if (p) {
            p->filePath = path;
            p->modified = false;
        } else {
            qWarning() << "Project::open:" << err;
        }
        return p;
    }

    // Fallback: try legacy JSON
    f.close();
    return openLegacyJson(path, parent);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Sprint L §5.0.1 — WinOLS Lua property bridge
// ─────────────────────────────────────────────────────────────────────────────
//
// Switch over numeric IDs defined in src/lua/LuaPropertyIds.h.  The Lua side
// (LuaConstants.cpp) exposes the same numeric values as globals.  Mapping
// per Sprint L spec §5.2a.

#include "lua/LuaPropertyIds.h"
#include <QCryptographicHash>

QString Project::propertyById(int id, int iOrgVer) const
{
    using namespace lua_ids;
    switch (id) {
    // ── Client ──
    case ePrjPropClientName:         return clientName;
    case ePrjPropClientNumber:       return clientNr;
    case ePrjPropClientLicenceplace: return clientLicence;
    // ── Vehicle ──
    case ePrjPropVehicleType:        return vehicleType;
    case ePrjPropVehicleProducer:    return brand;
    case ePrjPropVehicleChassis:     return vehicleBuild;
    case ePrjVehicleBuild:           return vehicleBuild;
    case ePrjPropVehicleModel:       return model;
    case ePrjVehicleCharacteristic:  return vehicleCharacteristic;
    case ePrjPropVehicleModelyear:   return (year > 0) ? QString::number(year) : QString();
    case ePrjPropVehicleVIN:         return vin;
    // ── ECU ──
    case ePrjPropEcuProducer:        return ecuProducer;
    case ePrjPropEcuBuild:           return ecuType;
    case ePrjPropEcuProdNr:          return ecuNrProd;
    case ePrjPropEcuStgNr:           return ecuNrEcu;
    case ePrjPropEcuSoftwareversion: return ecuSwNumber;
    case ePrjPropEcuSoftwareversionVersion: return ecuSwVersion;
    case ePrjPropEcuChecksum:        return ecuChecksum;
    case ePrjPropEcuSoftwaresize:    return QString::number(originalData.size());
    case ePrjPropEcuUse:             return ecuUse;
    // ── Engine ──
    case ePrjPropEngineName:         return engineCode;
    case ePrjPropEngineType:         return engineType;
    case ePrjPropEngineDisplacement: return displacement;
    case ePrjPropEngineTransmission: return transmission;
    case ePrjPropEngineOutputPS:     return (outputPS > 0) ? QString::number(outputPS) : QString();
    case ePrjPropEngineOutputKW:     return (outputKW > 0) ? QString::number(outputKW) : QString();
    case ePrjPropEngineEmissionStd:  return emission;
    case ePrjPropEngineTorque:       return (maxTorque > 0) ? QString::number(maxTorque) : QString();
    // ── Status ──
    case ePrjPropProjectType:        return projectType;
    case ePrjProjectStatus: {
        // Iter 10.3: return integer code per manual so scripts can do
        // `1*projectGetProperty(ePrjProjectStatus) == ePrjDeveloping`.
        // ePrjDeveloping=1, ePrjFinished=2, ePrjMaster=3 (LuaPropertyIds.h).
        const QString p = projectType.toLower();
        int v = 1;   // ePrjDeveloping (default)
        if      (p.contains(QStringLiteral("master")))   v = 3;
        else if (p.contains(QStringLiteral("finished"))) v = 2;
        return QString::number(v);
    }
    // ── File metadata ──
    case ePrjFileCreatedBy:          return createdBy;
    case ePrjFileModifiedBy:         return changedBy;
    case ePrjFileCreatedOn:          return createdAt.toString(Qt::ISODate);
    case ePrjFileModifiedOn:         return changedAt.toString(Qt::ISODate);
    case ePrjComment:                return notes;
    // ── Userdef ──
    case ePrjUserdef1:               return user1;
    case ePrjUserdef2:               return user2;
    case ePrjUserdef3:               return user3;
    case ePrjUserdef4:               return user4;
    case ePrjUserdef5:               return user5;
    // ── File paths (read-only) ──
    case ePrjImportFilename:         return romPath;
    case ePrjImportPath: {
        int slash = romPath.lastIndexOf(QLatin1Char('/'));
        int bslash = romPath.lastIndexOf(QLatin1Char('\\'));
        int cut = qMax(slash, bslash);
        return (cut > 0) ? romPath.left(cut + 1) : QString();
    }
    case ePrjFilename:               return filePath;
    // ── Original-version checksums (iOrgVer==1 only) ──
    case ePrjPropChecksumMD5:
        if (iOrgVer == 1 && !originalData.isEmpty())
            return QString::fromLatin1(QCryptographicHash::hash(
                originalData, QCryptographicHash::Md5).toHex());
        return QString();
    case ePrjPropChecksumSHA1:
        if (iOrgVer == 1 && !originalData.isEmpty())
            return QString::fromLatin1(QCryptographicHash::hash(
                originalData, QCryptographicHash::Sha1).toHex());
        return QString();
    case ePrjPropChecksumSHA256:
        if (iOrgVer == 1 && !originalData.isEmpty())
            return QString::fromLatin1(QCryptographicHash::hash(
                originalData, QCryptographicHash::Sha256).toHex());
        return QString();
    // ── STUB-MISSING (no backing field) ──
    case ePrjPropCommunicationsReadhardware:
    case ePrjPropNoreadTag:
    case ePrjPropSpiTag:
    case ePrjPropBdmTag:
    case ePrjPropUserTag:
    case ePrjPropUserTagText:
    case ePrjPropResellerCredits:
    case ePrjPropResellerProjectType:
    case ePrjPropResellerProjectDetails:
    case ePrjPropChecksum8Bit:
    case ePrjPropChecksum8BitCpu:
    case ePrjPropChecksum8BitEpr:
    case ePrjPropChecksumMD5Cpu:
    case ePrjPropChecksumMD5Epr:
        return QString();
    default:
        m_lastError = QStringLiteral("unknown property id %1").arg(id);
        return QString();
    }
}

bool Project::setPropertyById(int id, const QString &value)
{
    using namespace lua_ids;
    // Read-only ids per WinOLS manual §2.4.2
    if (id == ePrjPropEcuSoftwaresize
     || id == ePrjPropEcuChecksum
     || id == ePrjFileCreatedOn
     || id == ePrjFileModifiedOn
     || id == ePrjImportFilename
     || id == ePrjImportPath
     || id == ePrjFilename) {
        m_lastError = QStringLiteral("property %1 is read-only").arg(id);
        return false;
    }
    bool ok = true;
    switch (id) {
    case ePrjPropClientName:         clientName = value; break;
    case ePrjPropClientNumber:       clientNr = value; break;
    case ePrjPropClientLicenceplace: clientLicence = value; break;
    case ePrjPropVehicleType:        vehicleType = value; break;
    case ePrjPropVehicleProducer:    brand = value; break;
    case ePrjPropVehicleChassis:     vehicleBuild = value; break;
    case ePrjVehicleBuild:           vehicleBuild = value; break;
    case ePrjPropVehicleModel:       model = value; break;
    case ePrjVehicleCharacteristic:  vehicleCharacteristic = value; break;
    case ePrjPropVehicleModelyear:   year = value.toInt(); break;
    case ePrjPropVehicleVIN:         vin = value; break;
    case ePrjPropEcuProducer:        ecuProducer = value; break;
    case ePrjPropEcuBuild:           ecuType = value; break;
    case ePrjPropEcuProdNr:          ecuNrProd = value; break;
    case ePrjPropEcuStgNr:           ecuNrEcu = value; break;
    case ePrjPropEcuSoftwareversion: ecuSwNumber = value; break;
    case ePrjPropEcuSoftwareversionVersion: ecuSwVersion = value; break;
    case ePrjPropEcuUse:             ecuUse = value; break;
    case ePrjPropEngineName:         engineCode = value; break;
    case ePrjPropEngineType:         engineType = value; break;
    case ePrjPropEngineDisplacement: displacement = value; break;
    case ePrjPropEngineTransmission: transmission = value; break;
    case ePrjPropEngineOutputPS:     outputPS = value.toInt(); break;
    case ePrjPropEngineOutputKW:     outputKW = value.toInt(); break;
    case ePrjPropEngineEmissionStd:  emission = value; break;
    case ePrjPropEngineTorque:       maxTorque = value.toInt(); break;
    case ePrjPropProjectType:        projectType = value; break;
    case ePrjFileCreatedBy:          createdBy = value; break;
    case ePrjFileModifiedBy:         changedBy = value; break;
    case ePrjComment:                notes = value; break;
    case ePrjUserdef1:               user1 = value; break;
    case ePrjUserdef2:               user2 = value; break;
    case ePrjUserdef3:               user3 = value; break;
    case ePrjUserdef4:               user4 = value; break;
    case ePrjUserdef5:               user5 = value; break;
    // STUB-MISSING — accept but discard (no backing field)
    case ePrjPropCommunicationsReadhardware:
    case ePrjPropNoreadTag:
    case ePrjPropSpiTag:
    case ePrjPropBdmTag:
    case ePrjPropUserTag:
    case ePrjPropUserTagText:
    case ePrjPropResellerCredits:
    case ePrjPropResellerProjectType:
    case ePrjPropResellerProjectDetails:
        break;
    default:
        m_lastError = QStringLiteral("unknown property id %1").arg(id);
        ok = false;
        break;
    }
    if (ok) {
        modified = true;
        changedAt = QDateTime::currentDateTime();
    }
    return ok;
}
