/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QVector>
#include "project.h"
#include "aiprovider.h"   // for AIToolDef

// Executes AI tool calls against the project, showing user confirmation for write ops.
// All methods are synchronous from the caller's perspective (may show modal dialogs).
class AIToolExecutor : public QObject {
    Q_OBJECT
public:
    explicit AIToolExecutor(QObject *parent = nullptr);

    void setProject(Project *project) { m_project = project; }

    // Execute a tool call. Returns JSON result string.
    // For write operations, shows a confirmation dialog first.
    // Returns {"error":"cancelled"} if user rejects.
    QString execute(const QString &toolName, QJsonObject input);

    // Returns the tool definitions to send to the AI
    static QVector<AIToolDef> toolDefinitions();

signals:
    void projectModified();  // emitted after a successful write operation

private:
    Project *m_project = nullptr;

    // Target ROM: -1 = main project currentData, >=0 = linkedRoms[index].data
    int m_targetLinkedRomIndex = -1;

    // Resolve which ROM data to read/write based on target
    QByteArray &targetData();
    const QByteArray &targetDataConst() const;
    uint32_t mapOffsetInTarget(const MapInfo &m) const; // file offset for a map in the target ROM

    // Error helpers
    QString noProjectError() const;
    QString mapNotFound(const QString &name) const;
    MapInfo *findMap(const QString &name);

    // Auto-snapshot before any write
    void ensureVersionSnapshot(const QString &reason);

    // Read tools
    QString toolListMaps();
    QString toolListFolders();
    QString toolGetFolderMaps(const QJsonObject &input);
    QString toolGetProjectInfo();
    QString toolGetMapValues(const QJsonObject &input);
    QString toolGetOriginalValues(const QJsonObject &input);
    QString toolGetModifiedMaps();
    QString toolSearchMaps(const QJsonObject &input);
    QString toolGetMapInfo(const QJsonObject &input);
    QString toolCompareMapValues(const QJsonObject &input);
    QString toolGetMapStatistics(const QJsonObject &input);
    QString toolListGroups();
    QString toolGetGroupMaps(const QJsonObject &input);
    QString toolGetAxisValues(const QJsonObject &input);
    QString toolGetRomBytes(const QJsonObject &input);
    QString toolFindMapsByValue(const QJsonObject &input);
    QString toolGetAllChangesSummary();

    // Analysis tools (read-only, no confirmation needed)
    QString toolDescribeMapShape(const QJsonObject &input);      // topology: monotonic/plateau/peak/valley
    QString toolGetRelatedMaps(const QJsonObject &input);        // dependency graph: axis siblings, semantic group
    QString toolIdentifyMapPurpose(const QJsonObject &input);    // pattern-based purpose guess + confidence
    QString toolValidateMapChanges(const QJsonObject &input);    // physics sanity check before write
    QString toolDetectAnomalies(const QJsonObject &input);       // scan map for suspicious patterns
    QString toolSummarizeAllDifferences();                       // natural-language cross-ROM diff summary
    QString toolGetTuningNotes(const QJsonObject &input);        // read logbook entries
    QString toolConfidenceSearch(const QJsonObject &input);      // search_maps with confidence scores

    // Linked ROM tools
    QString toolListLinkedRoms();
    QString toolSelectTargetRom(const QJsonObject &input);
    QString toolGetLinkedRomMapValues(const QJsonObject &input);
    QString toolCompareWithLinkedRom(const QJsonObject &input);

    // Version tools
    QString toolListVersions();
    QString toolGetVersionMapValues(const QJsonObject &input);
    QString toolRestoreVersion(const QJsonObject &input);

    // Write tools (require confirmation)
    QString toolSetMapValues(const QJsonObject &input);
    QString toolSetCellValue(const QJsonObject &input);
    QString toolZeroMap(const QJsonObject &input);
    QString toolScaleMapValues(const QJsonObject &input);
    QString toolRestoreMap(const QJsonObject &input);
    QString toolFillMap(const QJsonObject &input);
    QString toolOffsetMapValues(const QJsonObject &input);
    QString toolClampMapValues(const QJsonObject &input);
    QString toolCopyMapValues(const QJsonObject &input);
    QString toolWriteRomBytes(const QJsonObject &input);
    QString toolSmoothMap(const QJsonObject &input);
    QString toolSetAxisValues(const QJsonObject &input);
    QString toolUndoLastChange();
    QString toolBatchZeroMaps(const QJsonObject &input);
    QString toolBatchModifyMaps(const QJsonObject &input);
    QString toolCompareTwoMaps(const QJsonObject &input);
    QString toolSnapshotVersion(const QJsonObject &input);
    QString toolEvaluateMapExpression(const QJsonObject &input); // formula-based cell editing
    QString toolApplyDeltaToRom(const QJsonObject &input);       // copy modifications from linked ROM
    QString toolAppendTuningNote(const QJsonObject &input);      // write logbook entry
    QString toolLogDynoResult(const QJsonObject &input);         // record dyno run
    QString toolUndoWithReason(const QJsonObject &input);        // restore version + log explanation
};
