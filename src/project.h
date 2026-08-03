/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QObject>
#include <QIODevice>
#include <QString>
#include <QByteArray>
#include <QVector>
#include <QMap>
#include <QSet>
#include <QDateTime>
#include "romdata.h"
#include "a2lparser.h"

// ── Tuning logbook entry ──────────────────────────────────────────────────────
struct TuningLogEntry {
    QDateTime  timestamp;
    QString    author;      // "AI" or username
    QString    mapName;     // empty = session-level note
    QString    category;    // "modification", "note", "dyno", "recipe", "anomaly"
    QString    message;     // human-readable description
    QString    before;      // optional: old value snapshot (JSON)
    QString    after;       // optional: new value snapshot (JSON)
};

// ── Dyno run result ───────────────────────────────────────────────────────────
struct DynoResult {
    QDateTime timestamp;
    double    peakPower  = 0.0;   // PS or kW
    QString   powerUnit;          // "PS" or "kW"
    double    peakTorque = 0.0;   // Nm
    int       rpmAtPower = 0;     // RPM at peak power
    QString   notes;              // freeform tuner notes
    QString   modifications;      // what was tuned before this run (JSON array)
};

// ── Captured OLS segment for round-trip export ────────────────────────────
// One entry per PRIMARY FADECAFE-anchored segment discovered in a .ols file
// at import time.  `rawBytes` holds the exact on-disk bytes of the segment —
// 26-byte pre-proj header + 18-byte proj_slot + 32-byte 8-u32 descriptor
// (with FADECAFE/CAFEAFFE) + full flash payload (including any embedded
// secondary descriptors).  `flashBase` is the segment's flash base address
// (descriptor d[3] & 0x7FFFFFFF).
//
// Populated by OlsProjectBuilder when building a Project from an
// OlsImportResult.  Consumed by OlsExporter to reproduce OLS's
// per-segment layout byte-for-byte (the 6-u32 pre-descriptor metadata in
// particular is opaque to us and must be passed through verbatim for
// OLS 5.0 to accept the exported file).
//
// Not persisted to .rx14proj — this is an in-memory-only pass-through used
// exclusively by the .ols export path.  Empty vector → exporter falls back
// to a single synthetic segment spanning the entire ROM.
struct OlsSegmentSnapshot {
    uint32_t    flashBase = 0;  // absolute flash base address (no 0x80000000 bit)
    QByteArray  preamble;       // bytes before the segment in the Version slot:
                                //   • segment 0 of a Version: empty
                                //   • segments 1..N: DEADBEEF (4 bytes) plus any
                                //     inter-segment padding OLS inserted
                                //     (observed lengths: 4, 260).  Captured
                                //     verbatim for byte-faithful round-trip.
    QByteArray  rawBytes;       // full on-disk segment bytes, length == flash_size
};

struct ProjectVersion {
    QString   name;
    QDateTime created;
    QByteArray data;   // full ROM snapshot

    // ── OLS .ols round-trip pass-through (per-Version segments) ──────
    // Mirror of Project::olsSegments but scoped to THIS ProjectVersion.
    // Populated by OlsProjectBuilder when a multi-Version .ols file is
    // imported — each non-primary Version (i.e. versions[1..N-1] in the
    // owning Project) carries its own primary FADECAFE segment snapshots
    // here so OlsExporter can reproduce OLS's multi-Version on-disk
    // layout byte-for-byte on export.
    //
    // NOT persisted to .rx14proj (encodeVers / decodeVers emit/read only
    // {name, created, data}).  In-memory-only by design: the user flow is
    // .ols import → [edit] → .ols export.  A reload from .rx14proj that
    // ran after a save will land on the fresh-import synthetic-segment
    // fallback (exporter's `snapshots.isEmpty()` branch), which is known
    // not to satisfy OLS 5.0 validation but keeps the self-round-trip
    // importer happy for headless tests.
    QVector<OlsSegmentSnapshot> olsSegments;
};

// A ROM file linked to this project (same ECU family, different tune/variant)
struct LinkedRom {
    QString    label;
    QString    filePath;
    QByteArray data;
    QDateTime  importedAt;
    bool       isReference = false;  // true = this is the factory/base ROM
    QString    sourceProjectPath;    // path of the project this was linked FROM (for bidirectional comparison)
    // map name → file offset in this ROM (populated by RomLinker, confidence ≥ 60 only)
    QMap<QString, uint32_t> mapOffsets;
    // map name → link confidence 0–100 (ALL maps, including 40% delta-guesses)
    QMap<QString, int>      mapConfidence;
};

class Project : public QObject {
    Q_OBJECT

public:
    explicit Project(QObject *parent = nullptr);

    // Identity
    QString name;
    QString filePath;  // .rx14proj file
    QString romPath;   // source ROM (informational)
    QString a2lPath;   // source A2L (informational)

    // ── Vehicle ───────────────────────────────────────────────────────────────
    QString brand;              // Producer / manufacturer (Porsche)
    QString model;              // Series (Panamera S Hybrid)
    QString vehicleType;        // "Passenger car"
    QString vehicleBuild;       // body build / generation
    QString vehicleModel;       // sub-model (e.g. "4S")
    QString vehicleCharacteristic;
    int     year  = 0;
    QString vin;

    // ── Client ────────────────────────────────────────────────────────────────
    QString clientName;
    QString clientNr;
    QString clientLicence;

    // ── ECU ───────────────────────────────────────────────────────────────────
    QString ecuType;            // Build/variant (MED17.1)
    QString ecuUse;             // "Engine"
    QString ecuProducer;        // "Bosch"
    QString ecuNrProd;          // production part number
    QString ecuNrEcu;           // ECU hardware number
    QString ecuSwNumber;        // software number (auto-detected)
    QString ecuSwVersion;       // software version
    QString ecuProcessor;       // "Infineon Tricore"
    QString ecuChecksum;        // computed 8-bit sum

    // ── Engine ────────────────────────────────────────────────────────────────
    QString displacement;
    QString engineProducer;
    QString engineCode;
    QString engineType;
    int     outputPS  = 0;
    int     outputKW  = 0;
    int     maxTorque = 0;
    QString emission;
    QString transmission;

    // ── Project metadata ──────────────────────────────────────────────────────
    QString   projectType;      // "in development" / "released" / …
    QString   mapLanguage;      // "English"
    QDateTime createdAt;
    QString   createdBy;
    QDateTime changedAt;
    QString   changedBy;

    // ── User-defined fields ───────────────────────────────────────────────────
    QString user1, user2, user3, user4, user5, user6;

    // ── Notes ─────────────────────────────────────────────────────────────────
    QString notes;

    // Data
    QByteArray           currentData;
    QByteArray           originalData;  // unedited snapshot, set once when ROM is first loaded
    QByteArray           a2lContent;    // raw A2L file bytes, embedded for portability
    QByteArray           linkedFromData;  // if isLinkedRom, the factory project's ROM data for comparison
    QVector<ProjectVersion> versions;
    QVector<LinkedRom>   linkedRoms;
    QVector<MapInfo>     maps;
    // Auto-detected map candidates produced by ols::MapAutoDetect::scan()
    // right after a raw ROM (.hex/.bin/.s19/.rom) is loaded. Rendered in the
    // waveform overlay ONLY while `maps` is empty (i.e. before an A2L is
    // imported for this project). As soon as `maps` becomes non-empty the
    // A2L-derived maps take over and `autoDetectedMaps` is ignored by the
    // renderer — they serve purely as a best-effort visual fallback.
    //
    // Not persisted: regenerated on every raw-ROM load.
    QVector<MapInfo>     autoDetectedMaps;
    // Per-project override: if true, the waveform skips drawing the
    // auto-detected overlays even when `maps` is empty (user can hide them
    // without forgetting what was scanned).
    bool                 hideAutoDetectedMaps = false;
    // Per-project: if true, ProjectView's "No maps yet — import A2L?"
    // empty-state card is suppressed for this project. User dismissed it
    // explicitly via the small × in its corner. Persisted in BLK_META.
    bool                 noMapsHintDismissed  = false;
    QVector<A2LGroup>    groups;
    QSet<QString>        starredMaps;   // stable map keys; legacy name-only entries remain readable
    QVector<TuningLogEntry> tuningLog;  // chronological tuning logbook
    QVector<DynoResult>     dynoLog;    // dyno run history
    ByteOrder            byteOrder   = ByteOrder::BigEndian;
    uint32_t             baseAddress = 0;
    bool                 modified    = false;
    bool                 isLinkedRom = false;  // true = this window belongs to a linked ROM, not the main project
    bool                 isLinkedReference = false;  // true = this linked ROM is the reference/original (ORI)
    QString              linkedToProjectPath;  // if isLinkedRom, path of the factory project this was linked from

    // Live link back to the parent project for sync. Set when a linked-ROM
    // Project is constructed (in MainWindow::actLinkRom or on reopen). When
    // currentData mutates, the parent's linkedRoms[parentLinkedIndex].data
    // is updated and the parent is marked modified — see MainWindow's wiring.
    // Both fields are *non-owned* references — never delete through them.
    Project *parentProject     = nullptr;
    int      parentLinkedIndex = -1;

    // ── Multi-Version sub-project hierarchy ─────────────────────────────────
    // Design choice: APPROACH A (true hierarchy). When a multi-Version .ols
    // file is imported, we create one shell PARENT project (carrying the
    // file metadata) plus N child sub-projects (one per Version, each with
    // its own ROM bytes + maps). The parent owns metadata only — its maps[]
    // and currentData are empty.  Children point back via parentProject.
    //
    // Sub-projects are SIBLINGS of the parent inside MainWindow::m_projects
    // (so each gets its own MDI subwindow & ProjectView), but the project
    // tree renders them nested under the parent. This re-uses the existing
    // openProject() / autosave / linked-ROM machinery instead of inventing
    // a new "child window" concept.
    //
    // Single-Version .ols imports DO NOT create a parent wrapper: we just
    // build one flat top-level project (backward compatible with the old
    // 1-project=1-ROM workflow). See MainWindow::buildProjectsFromOlsImport.
    QVector<Project*> subProjects;       // non-owned; lifetimes managed by MainWindow
    bool              isSubProject = false;
    int               subProjectIndex = -1;   // index into parentProject->subProjects

    // Versioning
    void snapshotVersion(const QString &versionName);
    bool restoreVersion(int index);

    // Forward-compat: unknown TLV blocks preserved on round-trip
    QMap<uint32_t, QByteArray> m_unknownBlocks;

    // ── OLS .ols round-trip pass-through ───────────────────────────────────
    // Captured per-segment raw bytes from the original .ols file, indexed in
    // on-disk order.  Populated by OlsProjectBuilder from OlsVersion::segments.
    // Consumed by OlsExporter to emit the exact multi-segment structure that
    // OLS 5.0 requires (8 primary segments per Version for MED17.5.20, each
    // with structured 6-u32 pre-descriptor metadata that is opaque to us).
    //
    // Not persisted — regenerated only on .ols import.  If empty (fresh .hex
    // import or opened .rx14proj) the exporter falls back to emitting a single
    // synthetic segment covering the entire ROM.
    QVector<OlsSegmentSnapshot> olsSegments;

    // Per-project comments / markers at ROM offsets (Sprint C). Lazily
    // attached the first time it's queried; persisted as
    // `<basename>.comments.json` sidecar next to the .rx14proj.
    class AnnotationStore *annotations();

    // Persistence
    bool save();
    bool saveAs(const QString &path);
    static Project *open(const QString &path, QObject *parent = nullptr);

    // Binary format (RX14 container)
    bool saveToStream(QIODevice *out, QString *err = nullptr) const;
    static Project *loadFromStream(QIODevice *in, QObject *parent = nullptr,
                                   QString *err = nullptr);

    // Legacy JSON format (migration)
    static Project *openLegacyJson(const QString &path, QObject *parent = nullptr);

    QString displayName() const;
    QString listLabel() const;   // "filename (Brand Model · ECU · year)" — for tree/list views
    QString fullTitle() const;   // "filename — Brand Model — ECU (year)" — for window titles

    // ── WinOLS Lua property bridge (Sprint L §5.0.1) ─────────────────────────
    // Read a property identified by a WinOLS ePrjProp* enum integer (defined
    // in src/lua/LuaConstants.cpp).  Returns "" for unmapped / STUB-MISSING
    // ids.  See Sprint L spec §5.2a for the full mapping table.
    QString propertyById(int ePrjPropId, int iOrgVer = 0) const;

    // Write a property by enum id.  Returns false for read-only ids
    // (ePrjPropEcuSoftwaresize, ePrjPropEcuChecksum, ePrjFileCreatedOn,
    // ePrjFileModifiedOn) per WinOLS manual §2.4.2, and for unmapped ids.
    bool setPropertyById(int ePrjPropId, const QString &value);

    // Lua's GetLastError() pulls from here.
    QString lastError() const { return m_lastError; }
    void    setLastError(const QString &msg) { m_lastError = msg; }

signals:
    void dataChanged();
    void versionsChanged();
    void linkedRomsChanged();

private:
    class AnnotationStore *m_annotations = nullptr;
    mutable QString m_lastError;
};

inline QString projectMapStarKey(const MapInfo &map)
{
    return QStringLiteral("map:%1:%2")
        .arg(map.address, 8, 16, QChar('0')).arg(map.name);
}

inline bool isProjectMapStarred(const Project &project, const MapInfo &map)
{
    // Legacy projects stored only names, which marked every duplicate name.
    return project.starredMaps.contains(projectMapStarKey(map))
        || project.starredMaps.contains(map.name);
}

inline void setProjectMapStarred(Project &project, const MapInfo &map, bool starred)
{
    // Upgrade a legacy name-only star the first time it is changed. This
    // preserves old projects' visible stars while allowing duplicate names to
    // be managed independently from then on.
    if (project.starredMaps.remove(map.name)) {
        for (const auto &candidate : project.maps) {
            if (candidate.name == map.name)
                project.starredMaps.insert(projectMapStarKey(candidate));
        }
    }

    const QString key = projectMapStarKey(map);
    if (starred)
        project.starredMaps.insert(key);
    else
        project.starredMaps.remove(key);
}
