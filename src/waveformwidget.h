/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QWidget>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QTimer>
#include <QMenu>
#include <QFutureWatcher>
#include <atomic>
#include "romdata.h"
#include "waveformeditor.h"

class WaveformWidget : public QWidget {
    Q_OBJECT

public:
    explicit WaveformWidget(QWidget *parent = nullptr);
    void setProject(class Project *project) { m_project = project; }

    void showROM(const QByteArray &romData, const QByteArray &originalData);
    /// Refresh the rendering cache from the project-owned ROM without
    /// re-binding the editor (which would discard its transient state).
    void refreshRomData(const QByteArray &romData);
    void goToAddress(uint32_t offset);

    /// Access to the underlying free-form editor.  Used by MainWindow's
    /// edit-op dispatcher (Selection menu) so all three views — hex,
    /// waveform, 3D — funnel through one editor with one undo stack
    /// per ProjectView.
    class WaveformEditor *editor() const { return m_editor; }
    void setMaps(const QVector<MapInfo> &maps);
    // Set the auto-detected map candidates (rendered as semi-transparent
    // "AUTO" overlays). These are drawn ONLY when the real A2L-derived map
    // list (set via setMaps()) is empty — as soon as an A2L is imported the
    // auto overlays disappear and the real maps take their place.
    void setAutoDetectedMaps(const QVector<MapInfo> &maps);
    void setCurrentMap(const MapInfo &map);
    void retranslateUi();
    int scrollOffset() const { return m_scrollOffset; }
    const QByteArray &romData() const { return m_data; }

    // Selection
    bool hasSelection() const { return m_selStart >= 0 && m_selEnd >= 0; }
    uint32_t selectionStart() const { return (uint32_t)qMin(m_selStart, m_selEnd); }
    uint32_t selectionEnd() const { return (uint32_t)qMax(m_selStart, m_selEnd); }
    int selectionLength() const { return hasSelection() ? (int)(selectionEnd() - selectionStart()) : 0; }
    void clearSelection();

signals:
    void offsetClicked(uint32_t offset);
    void mapClicked(const MapInfo &map);
    void scrollSynced(int scrollOffset);  // emitted after any user-driven scroll
    void selectionChanged(uint32_t start, uint32_t end);
    void createMapRequested(uint32_t address, int length, int cellSize);
    void selectionToMapRequested(uint32_t address, int length);
    void dataModified(int start, int end);

    /// Emitted when the user moves the zoom slider.  Carries the slider's
    /// raw value (0..100) so paired waveforms can match scale via
    /// syncZoomTo().  Mirrors the scrollSynced contract.
    void zoomSynced(int sliderValue);

public slots:
    void syncScrollTo(int scrollOffset);  // receive sync without re-emitting
    void setComparisonData(const QByteArray &data);

    /// Programmatically set the zoom slider; does NOT re-emit zoomSynced.
    void syncZoomTo(int sliderValue);

    /// Sprint B: render a translucent red baseline marker at every byte
    /// that differs from m_originalData.  Toggled by View → Differences
    /// vs Original.
    void setShowOriginalDiffOverlay(bool on);

    /// Sprint C: bind to the project's annotation store for vertical
    /// pin markers at annotation addresses.
    void setAnnotationStore(class AnnotationStore *store);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool event(QEvent *event) override;          // tooltip for annotations

private:
    // ROM waveform / no-map state
    void buildOverviewCache();
    void renderRomWaveform(QPainter &p);
    void renderOverview(QPainter &p);
    void updateZoomFromSlider(int val);
    void clampScroll();
    int  totalValues() const;
    uint32_t maxVal() const;
    uint32_t readValue(uint32_t offset) const;
    QColor valColor(double pct) const;

    // Pixel stats cache
    void invalidatePixCache();
    void recomputePixelStats(int plotW);
    void scheduleAsyncRecompute();   // kicks off background recompute, displays stale cache meanwhile

    // Selection rendering & context menu
    void renderSelection(QPainter &p);
    void showContextMenu(const QPoint &pos);
    int32_t offsetAtX(int x) const;

    // Free-form drawing helpers
    double yToVal(int y) const;
    void   copySelectionToClipboard();
    void   renderModifiedOverlay(QPainter &p);

    // Shared region-rendering helper used by both real maps (m_maps) and
    // auto-detected overlays (m_autoMaps). `isAuto` switches to the distinct
    // visual style (dashed outline, ~40% opacity fill, "AUTO" label).
    void drawMapRegions(QPainter &p, const QVector<MapInfo> &maps,
                        const QVector<int> &visIndices,
                        int plotW, int plotH, int topY,
                        int startIdx, double vpp,
                        bool isAuto);

    // Map curve mode
    void renderMapCurve(QPainter &p);
    int  mapColAtX(int x) const;
    const MapInfo *mapAtX(int x) const;

    QByteArray m_data;
    QByteArray m_originalData;
    int m_cellSize = 2;
    ByteOrder m_byteOrder = ByteOrder::BigEndian;
    double m_valsPerPx = 1.0;
    int m_scrollOffset = 0;
    int32_t m_selectedOffset = -1;
    int32_t m_hoverOffset = -1;

    // Range selection (byte offsets into ROM)
    int32_t m_selStart = -1;
    int32_t m_selEnd   = -1;
    bool m_isSelecting = false;

    bool m_isDragging = false;
    bool m_isOverviewDrag = false;
    bool m_didDrag = false;
    int m_dragStartX = 0;
    int m_dragStartOffset = 0;
    QPoint m_pressPos;

    // Map overlay data
    QVector<MapInfo> m_maps;
    // Auto-detected map overlays — only rendered when m_maps.isEmpty()
    QVector<MapInfo> m_autoMaps;
    MapInfo          m_currentMap;
    bool             m_hasCurrentMap = false;
    int              m_hoverCol = -1;   // for map curve hover
    QByteArray       m_comparisonData;  // ROM data to compare against (original or linked ROM)

    // WaveformEditor (free-form editing engine)
    WaveformEditor *m_editor = nullptr;
    class Project  *m_project = nullptr;  // non-owning shared ROM document

    // Free-form drawing state
    bool   m_isDrawing  = false;

    // Cached plot geometry + Y-axis range (set in renderRomWaveform, used by yToVal)
    double m_visLo      = 0.0;
    double m_visHi      = 1.0;
    int    m_plotTopY   = 0;
    int    m_plotHeight  = 0;

    static const int kYAxisW    = 64;
    static const int kXAxisH    = 22;
    static const int kOverviewH = 44;
    static const int kControlsH = 32;

    QVector<float>  m_overviewCache;
    QPixmap m_renderCache;           // cached full scene render (ROM waveform + overview)

    // Pixel stats cache (pre-computed per-pixel avg/sigma for renderRomWaveform)
    QVector<double> m_pixAvg;
    QVector<double> m_pixSig;
    QVector<double> m_pixCmpAvg;  // comparison ROM per-pixel averages (empty if no comparison)
    bool   m_pixDirty       = true;
    int    m_pixCachedScroll = -1;
    double m_pixCachedVpp   = 0.0;
    int    m_pixCachedW     = 0;

    QTimer *m_scrollTimer  = nullptr;   // throttle repaints to ~60fps
    QTimer *m_asyncTimer   = nullptr;   // debounce before async recompute
    QTimer *m_kineticTimer = nullptr;   // kinetic scroll animation (~60fps)
    double  m_scrollVelocity = 0.0;     // current kinetic velocity (values/sec)
    qint64  m_lastWheelMs    = 0;       // timestamp of last wheel event

    // Async pixel stats recompute
    struct PixResult { QVector<double> avg; QVector<double> sig; QVector<double> cmpAvg; int scroll; double vpp; int w; };
    QFutureWatcher<PixResult> m_pixWatcher;
    std::atomic<bool>         m_asyncRunning{false};

    // Controls
    QComboBox *m_cellSizeCombo  = nullptr;
    QComboBox *m_byteOrderCombo = nullptr;
    QSlider   *m_zoomSlider     = nullptr;
    QLabel    *m_zoomLabel      = nullptr;
    QLabel    *m_cursorInfo     = nullptr;

    // Cached fonts (avoid per-frame construction)
    QFont m_monoFont;
    QFont m_smallFont;
    QFont m_labelFont;
    QFont m_titleFont;

    // Sprint B — diff-vs-original overlay flag
    bool m_showOriginalDiff = false;

    // Sprint C — bound annotation store (owned by Project, not us).
    class AnnotationStore *m_annotations = nullptr;
    void renderAnnotations(QPainter &p);
};
