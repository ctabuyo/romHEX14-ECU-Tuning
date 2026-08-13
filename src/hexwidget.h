/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QAbstractScrollArea>
#include <QWidget>
#include <QFont>
#include <QSet>
#include <QVector>
#include <QTimer>
#include "romdata.h"

class HexWidget;
class Project;

// ── Vertical overview minimap (sits next to the scrollbar) ──────────────────
class HexOverviewBar : public QWidget {
    Q_OBJECT
public:
    explicit HexOverviewBar(HexWidget *parent);
    void rebuild();

signals:
    void scrollRequested(int row);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    QSize sizeHint() const override { return QSize(48, 100); }

private:
    int rowAtY(int y) const;
    HexWidget *m_hex;
    QVector<float> m_cache;    // per-pixel intensity (0-1)
    bool m_dragging = false;
};

// ── Undo entry for hex edits ────────────────────────────────────────────────
struct HexUndoEntry {
    int offset;
    QByteArray before;
    QByteArray after;
};

// ── Hex dump view ───────────────────────────────────────────────────────────
class HexWidget : public QAbstractScrollArea {
    Q_OBJECT

public:
    enum class SidebarMode { ASCII, Bars };

    explicit HexWidget(QWidget *parent = nullptr);
    void setProject(Project *project) { m_project = project; }

    void loadData(const QByteArray &data, uint32_t baseAddress = 0);
    /// Initial project attach: pass current bytes plus the unedited
    /// baseline so the diff-vs-original overlay has a stable reference
    /// even when a saved project is reopened with edits in place.
    void loadData(const QByteArray &data, const QByteArray &original,
                  uint32_t baseAddress);
    /// Update current bytes after an external edit (WaveformEditor, savepoint
    /// switch, ...). Keeps m_originalData stable so the diff-vs-original
    /// overlay still works.
    void refreshData(const QByteArray &data);
    void setBaseAddress(uint32_t baseAddress);
    void setDisplayParams(int cellSize, ByteOrder bo,
                          int displayFmt = 0, bool isSigned = false);
    void setFontSize(int pt);
    QByteArray getData() const;
    QByteArray getOriginalData() const;
    QByteArray exportBinary() const;
    void goToAddress(uint32_t offset);
    /// Select a byte range and scroll it into view.  Used when a map is
    /// selected from the project tree so its data is visible in the hex view.
    void selectRange(uint32_t offset, int length);
    void setMapRegions(const QVector<MapRegion> &regions);
    void setActiveMapAddress(uint32_t address);
    void setActiveMap(const MapInfo &map);
    void triggerMapBlinkAnimation();
    uint32_t activeMapAddress() const { return m_activeMapAddress; }
    const MapInfo &activeMapInfo() const { return m_activeMapInfo; }
    int modificationCount() const { return m_modifications.size(); }

    // Selection helpers
    bool hasSelection() const { return m_selectionStart >= 0 && m_selectionEnd >= 0; }
    int selStart() const { return qMin(m_selectionStart, m_selectionEnd); }
    int selEnd() const { return qMax(m_selectionStart, m_selectionEnd); }
    /// Current caret offset (set by goToAddress / mouse click). -1 if none.
    int32_t currentOffset() const { return m_selectedOffset; }

    // Accessed by HexOverviewBar
    const QByteArray &romData() const { return m_data; }
    const QVector<MapRegion> &mapRegions() const { return m_mapRegions; }
    uint32_t baseAddress() const { return m_baseAddress; }
    int bytesPerRow() const { return m_bytesPerRow; }
    int headerHeight() const { return m_headerHeight; }
    int rowHeight() const { return m_rowHeight; }

signals:
    void offsetSelected(uint32_t offset, uint8_t value);
    void dataModified(int modCount);
    void bytesModified(int start, int length);

    /// Emitted when the user scrolls the hex view.  Carries the byte
    /// offset of the topmost visible row, so other hex/waveform views can
    /// keep their visible region aligned.  Mirrors WaveformWidget's
    /// scrollSynced contract.
    void scrollSynced(int byteOffset);

public slots:
    void setComparisonData(const QByteArray &data);

    /// Programmatically scroll so the byte at @p byteOffset is the top
    /// visible row.  Does NOT re-emit scrollSynced (avoids feedback loops
    /// when the sender is the sync coordinator).
    void syncScrollTo(int byteOffset);

    /// Sprint B: when true, every cell that differs from m_originalData
    /// is rendered with a delta-magnitude background (green→yellow→
    /// orange→red by |now-was|) instead of the plain "modified" tint.
    void setShowOriginalDiffOverlay(bool on);

    /// Sprint C: bind to the project's annotation store so the offset
    /// gutter can paint a ✎ glyph at addresses that have a comment, and
    /// hover tooltips show the comment text.
    void setAnnotationStore(class AnnotationStore *store);

signals:
    /// Emitted when the user picks an edit op from the right-click menu.
    /// MainWindow listens via ProjectView and routes through the same
    /// `applyEditOp()` dispatcher used by the global Selection menu so
    /// hex / waveform / 3D share one undo stack per project.
    /// @p opCode mirrors MainWindow::EditOp.
    void editOpRequested(int opCode);

    /// Selection → Map: mirrors WaveformWidget's contract so the same
    /// `CreateMapDlg` flow in ProjectView handles both views.  Emitted
    /// from the right-click "Selection → Map…" entry; @p address is
    /// the start byte offset, @p length is in bytes.
    void selectionToMapRequested(uint32_t address, int length);

    /// Emitted when clicking on an identified map block in HexWidget.
    void mapClickedInHex(uint32_t address);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool event(QEvent *event) override;          // tooltip for annotations

private:
    void updateScrollBar();
    void renderRow(QPainter &p, int row, int y);
    int mapRegionForOffset(uint32_t offset) const;
    QRect byteRect(int col, int y) const;
    void clearSelection();
    void pushUndo(int offset, const QByteArray &before, const QByteArray &after);
    void updateModifications(int offset, int length);

    void drawMapOverlayBox(QPainter &p);
    void drawHoveredMapOverlay(QPainter &p);

    QByteArray m_data;
    QByteArray m_originalData;
    QSet<uint32_t> m_modifications;
    Project       *m_project = nullptr;  // non-owning shared ROM document
    QByteArray m_comparisonData;
    QVector<MapRegion> m_mapRegions;
    uint32_t           m_activeMapAddress = 0;
    MapInfo            m_activeMapInfo;
    int                m_hoveredMapIndex = -1;
    QTimer             m_blinkTimer;
    int                m_blinkTicksRemaining = 0;
    bool               m_blinkHighlightState = false;

    int m_bytesPerRow = 16;
    int m_rowHeight = 22;
    int m_headerHeight = 24;
    int m_offsetWidth = 90;
    int m_byteWidth = 26;
    int m_separatorWidth = 10;
    int m_asciiCharWidth = 9;

    SidebarMode m_sidebarMode = SidebarMode::Bars;
    int       m_groupSize   = 1;
    ByteOrder m_byteOrder   = ByteOrder::BigEndian;
    int       m_displayFmt  = 0;
    bool      m_isSigned    = false;

    uint32_t m_baseAddress   = 0;
    int32_t m_selectedOffset = -1;
    bool m_editing = false;
    int m_editNibble = 0;
    uint8_t m_editByteBefore = 0;  // byte value before nibble editing started

    // Selection range (-1 = no selection)
    int m_selectionStart = -1;
    int m_selectionEnd   = -1;

    // Undo/redo
    QVector<HexUndoEntry> m_undoStack;
    int m_undoIndex = -1;
    static const int kMaxUndo = 100;

    HexOverviewBar *m_overviewBar = nullptr;

    QFont m_monoFont;

    // Sprint B — diff-vs-original overlay flag.
    bool m_showOriginalDiff = false;

    // Sprint C — bound annotation store (owned by Project, not us).
    class AnnotationStore *m_annotations = nullptr;
};
