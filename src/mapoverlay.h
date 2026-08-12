/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QToolButton>
#include <QPushButton>
#include <QTableWidget>
#include <QLabel>
#include <QFrame>
#include <QHash>
#include <QStackedWidget>
#include <QSpinBox>
#include <QVector>
#include "romdata.h"
#include "map3dwidget.h"
#include "map3dsimwidget.h"

class WaveformEditor;
class Project;

struct CellEdit {
    uint32_t offset;
    uint32_t oldRaw;
    uint32_t newRaw;
};
using EditBatch = QVector<CellEdit>;

class MapOverlay : public QDialog {
    Q_OBJECT

public:
    explicit MapOverlay(QWidget *parent = nullptr);

    void showMap(const QByteArray &romData,
                 const MapInfo   &map,
                 ByteOrder        byteOrder,
                 const QByteArray &originalData = {});

    void setDisplayParams(int cellSize, ByteOrder byteOrder, bool heightColors);
    void retranslateUi();
    bool displaysMap(const MapInfo &map) const;
    Project *targetProject() const { return m_syncProject; }

    Q_SLOT void previewScalingFormat(const QString &format);
    void previewMapUpdate(const MapInfo &preview);

    // Share the owning ProjectView's editor so this dialog's edits land on
    // the SAME undo stack as the hex / waveform / 3D views (and vice-versa).
    // Passing nullptr reverts to the dialog-local undo stack (standalone use).
    void setSharedEditor(WaveformEditor *editor, Project *project);

signals:
    void romPatchReady(uint32_t offset, QByteArray bytes);
    void editBatchDone();
    void addressCorrected(const QString &mapName, uint32_t newAddress);
    void mapInfoChanged(const MapInfo &updated, ByteOrder byteOrder);
    void mapInfoPreview(const MapInfo &preview);
    // Re-emitted from the embedded 3D view's "Edit map" submenu so the host
    // routes it through the shared edit dispatcher (MainWindow::EditOp code).
    void editOpRequested(int opCode);

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

private slots:
    void buildTable();
    void applyBatchOp(int mode);   // 0=+%  1=+val  2==val
    void interpolateSelection();   // bilinear fill of the selected rectangle
    void undoEdit();
    void redoEdit();

private:
    // Grid display mode — plain values, or numeric difference vs the
    // original ROM (absolute delta or percentage).
    enum class DisplayMode { Values = 0, DeltaAbs = 1, DeltaPct = 2 };

    void autoResize();
    CellEdit writeCell(int row, int col, double newPhys);
    // Commit a batch of cell edits as ONE undo step — routed to the shared
    // editor when one is set, otherwise to the dialog-local undo stack.
    void commitBatch(const EditBatch &batch);
    // Reload m_data from the shared project after an external edit/undo.
    void reloadFromProject();
    // Current physical value of one cell (respects scaling/sign/byte order).
    double cellPhys(int row, int col) const;
    // True when the grid is a read-only view (showing original, or a
    // difference view) and edits must be blocked.
    bool isReadOnlyView() const;
    void pushUndo(const EditBatch &batch);
    void updateUndoRedoButtons();
    void updateStatusBar();

    // ── Inline editor ──────────────────────────────────────────────────────
    void openInlineEditor(const QString &prefill = {});
    void commitInlineEditor(int dCol = 0, int dRow = 0);
    void cancelInlineEditor();
    void repositionInlineEditor();
    void markPendingCells(bool mark);

    // ── Table-level keyboard helpers ────────────────────────────────────────
    int  firstDataCol() const;
    int  lastDataCol()  const;
    int  firstDataRow() const;
    int  lastDataRow()  const;
    void navigateCell(int dRow, int dCol, bool extend);
    void incrementSelectedCells(int delta);
    void deleteSelectedCells();
    void copySelectionToClipboard();
    void pasteFromClipboard();

    // ── Data ────────────────────────────────────────────────────────────────
    QByteArray m_data;
    QByteArray m_originalData;
    MapInfo    m_map;
    MapDimensions m_previewOrigDimensions = {0, 0};
    int        m_previewOrigDataSize = 0;
    int        m_cellSize        = 2;
    ByteOrder  m_byteOrder       = ByteOrder::BigEndian;
    bool       m_showingOriginal = false;
    bool       m_heatEnabled     = true;
    int        m_fontSize        = 9;
    DisplayMode m_displayMode    = DisplayMode::Values;

    QVector<EditBatch> m_undoStack;
    QVector<EditBatch> m_redoStack;

    // Shared-undo wiring (null when running standalone → local stack used)
    WaveformEditor *m_undoEditor    = nullptr;
    Project        *m_syncProject   = nullptr;
    bool            m_committingSelf = false;   // guards reload re-entrancy

    // Inline editor state
    bool m_editOpen = false;
    int  m_editRow  = -1;
    int  m_editCol  = -1;

    // ── Widgets ─────────────────────────────────────────────────────────────
    // Toolbar
    QLabel      *m_statsLabel     = nullptr;
    QComboBox   *m_cellSizeCombo  = nullptr;
    QComboBox   *m_byteOrderCombo = nullptr;
    QToolButton *m_btnUndo        = nullptr;
    QToolButton *m_btnRedo        = nullptr;
    QToolButton *m_btnOri         = nullptr;
    QToolButton *m_btnHeat        = nullptr;
    QToolButton *m_btn3D          = nullptr;
    QToolButton *m_btn3DSim       = nullptr;
    QPushButton *m_btnTranslate   = nullptr;
    QPushButton *m_btnAIExplain  = nullptr;
    QSpinBox    *m_fontSpin       = nullptr;

    // Operation bar
    QWidget     *m_opBarWidget = nullptr; // entire operation bar row
    QLineEdit   *m_deltaInput  = nullptr;
    QToolButton *m_btnAddPct   = nullptr;
    QToolButton *m_btnAddVal   = nullptr;
    QToolButton *m_btnSetVal   = nullptr;
    QToolButton *m_btnInterp   = nullptr;
    QLabel      *m_opHint      = nullptr;

    // Difference-view selector (Values / Δ vs original / % vs original)
    QComboBox   *m_diffCombo   = nullptr;

    // Toolbar extras (for hiding on small maps)
    QLabel      *m_cellLabel   = nullptr;
    QLabel      *m_orderLabel  = nullptr;

    // Axis info bar
    QLabel *m_axisBar = nullptr;

    // AI explain cache: map name → response text
    QHash<QString, QString> m_aiCache;

    // Status bar
    QLabel *m_statusBar = nullptr;

    // Low-confidence warning bar
    QFrame  *m_warningBar   = nullptr;
    QLabel  *m_warningLabel = nullptr;

    // Main area
    QStackedWidget *m_stack  = nullptr;
    QTableWidget   *m_table  = nullptr;
    Map3DWidget    *m_map3d  = nullptr;
    Map3DSimWidget *m_map3dSim = nullptr;

    // Inline editor (child of table viewport)
    QLineEdit *m_inlineEdit = nullptr;
};
