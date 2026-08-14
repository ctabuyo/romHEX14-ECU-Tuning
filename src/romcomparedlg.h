/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QDialog>
#include <QTabWidget>
#include <QTableWidget>
#include <QSplitter>
#include <QLabel>
#include <QComboBox>
#include "project.h"
#include "romcompare.h"
#include "bytediffwidget.h"
#include "rompatch.h"
#include "mappack.h"

class QCloseEvent;

// Compare dialog: shows map-level and byte-level differences between two ROMs.
// "Left" is always the project's currentData; "Right" is chosen from
// linked ROMs or saved versions.
class RomCompareDlg : public QDialog {
    Q_OBJECT

public:
    // Construct and immediately compare refRom vs cmpRom using project maps.
    // cmpOffsets: linked offsets for the compare ROM (empty = same as ref).
    explicit RomCompareDlg(Project *project,
                           const QByteArray &cmpRom,
                           const QString &cmpLabel,
                           const QMap<QString, uint32_t> &cmpOffsets,
                           QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onMapRowClicked(int row, int col);

private:
    void buildMapTab();
    void buildByteTab();
    void populateMapTable(const QVector<MapDiff> &diffs);
    void showMapCells(const MapDiff &md);

    Project        *m_project   = nullptr;
    QByteArray      m_cmpRom;
    QString         m_cmpLabel;
    QMap<QString, uint32_t> m_cmpOffsets;
    QVector<MapDiff>        m_mapDiffs;

    QTabWidget     *m_tabs      = nullptr;

    // Map diff tab
    QTableWidget   *m_mapTable  = nullptr;
    QLabel         *m_mapSummary = nullptr;
    QSplitter      *m_mapSplit  = nullptr;
    QTableWidget   *m_refCells  = nullptr;
    QTableWidget   *m_cmpCells  = nullptr;
    QLabel         *m_refLabel  = nullptr;
    QLabel         *m_cmpLabel2 = nullptr;

    // Byte diff tab
    ByteDiffWidget *m_byteDiff  = nullptr;
};
