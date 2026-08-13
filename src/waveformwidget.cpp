/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "waveformwidget.h"
#include "project.h"
#include "appconfig.h"
#include "logger.h"
#include "annotations/AnnotationStore.h"
#include <QHelpEvent>
#include <QToolTip>
#include <QPainter>
#include <QPainterPath>
#include <QImage>
#include <QLinearGradient>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>
#include <QElapsedTimer>
#include <QDateTime>
#include <QKeyEvent>
#include <QInputDialog>
#include <QRegularExpression>
#include <QApplication>
#include <QClipboard>
#include <cmath>

static inline QColor waveColor(int idx)
{
    return AppConfig::instance().colors.waveRow[idx % 8];
}

WaveformWidget::WaveformWidget(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);

    auto *controlsLayout = new QHBoxLayout();
    controlsLayout->setContentsMargins(8, 4, 8, 4);
    controlsLayout->setSpacing(8);

    auto mkLabel = [](const QString &t) {
        auto *l = new QLabel(t);
        l->setStyleSheet("color:#6b7fa3; font-size:11px;");
        return l;
    };

    controlsLayout->addWidget(mkLabel(tr("Size:")));
    m_cellSizeCombo = new QComboBox();
    m_cellSizeCombo->addItem(tr("8-bit"),  1);
    m_cellSizeCombo->addItem(tr("16-bit"), 2);
    m_cellSizeCombo->addItem(tr("32-bit"), 4);
    m_cellSizeCombo->setCurrentIndex(1);
    controlsLayout->addWidget(m_cellSizeCombo);

    m_byteOrderCombo = new QComboBox();
    m_byteOrderCombo->addItem(tr("Big Endian"),    (int)ByteOrder::BigEndian);
    m_byteOrderCombo->addItem(tr("Little Endian"), (int)ByteOrder::LittleEndian);
    controlsLayout->addWidget(m_byteOrderCombo);

    controlsLayout->addWidget(mkLabel(tr("Zoom:")));
    m_zoomSlider = new QSlider(Qt::Horizontal);
    m_zoomSlider->setRange(0, 100);
    m_zoomSlider->setValue(100);
    m_zoomSlider->setFixedWidth(100);
    controlsLayout->addWidget(m_zoomSlider);

    m_zoomLabel = new QLabel();
    m_zoomLabel->setStyleSheet(
        "color:#3a91d0; font-family:'Consolas',monospace; font-size:11px; min-width:52px;");
    controlsLayout->addWidget(m_zoomLabel);
    controlsLayout->addStretch();

    m_cursorInfo = new QLabel(tr("Scroll or drag to explore ROM"));
    m_cursorInfo->setStyleSheet(
        "color:#6b7fa3; font-family:'Consolas',monospace; font-size:11px;");
    controlsLayout->addWidget(m_cursorInfo);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto *controlsWidget = new QWidget();
    controlsWidget->setFixedHeight(kControlsH);
    controlsWidget->setLayout(controlsLayout);
    controlsWidget->setStyleSheet(
        "background:#0f1629; border-bottom:1px solid rgba(58,145,208,0.15);");
    mainLayout->addWidget(controlsWidget);
    mainLayout->addStretch();

    connect(m_cellSizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        m_cellSize = m_cellSizeCombo->itemData(idx).toInt();
        if (m_editor) m_editor->setCellSize(m_cellSize);
        m_overviewCache.clear();
        invalidatePixCache();
        buildOverviewCache();
        updateZoomFromSlider(m_zoomSlider->value());
        update();
    });
    connect(m_byteOrderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        m_byteOrder = (ByteOrder)m_byteOrderCombo->itemData(idx).toInt();
        if (m_editor) m_editor->setByteOrder(m_byteOrder);
        m_overviewCache.clear();
        invalidatePixCache();
        buildOverviewCache();
        update();
    });
    connect(m_zoomSlider, &QSlider::valueChanged, this, [this](int val) {
        updateZoomFromSlider(val);
        clampScroll();
        invalidatePixCache();
        update();
        emit zoomSynced(val);   // fan-out to other waveforms when sync is on
    });

    connect(&AppConfig::instance(), &AppConfig::colorsChanged,
            this, QOverload<>::of(&QWidget::update));

    m_scrollTimer = new QTimer(this);
    m_scrollTimer->setSingleShot(true);
    m_scrollTimer->setInterval(16);   // max ~60fps during fast scrolling
    connect(m_scrollTimer, &QTimer::timeout, this, QOverload<>::of(&QWidget::update));

    // Kinetic scroll: tick at ~60fps to animate momentum
    m_kineticTimer = new QTimer(this);
    m_kineticTimer->setInterval(16);
    connect(m_kineticTimer, &QTimer::timeout, this, [this]() {
        const double friction = 0.85;   // per-frame decay (stronger = stops sooner)
        m_scrollVelocity *= friction;

        // Stop when velocity is negligible — higher threshold to avoid lingering creep
        if (qAbs(m_scrollVelocity) < 5.0) {
            m_scrollVelocity = 0.0;
            m_kineticTimer->stop();
            return;
        }

        int delta = (int)(m_scrollVelocity * 0.016);  // velocity * dt(16ms)
        if (delta == 0) delta = (m_scrollVelocity > 0) ? 1 : -1;

        int old = m_scrollOffset;
        m_scrollOffset += delta * m_cellSize;
        clampScroll();

        if (m_scrollOffset != old) {
            invalidatePixCache();
            update();
            // Throttle sync emission during kinetic scroll (~20fps instead of 60fps)
            static int kineticFrame = 0;
            if (++kineticFrame % 3 == 0)
                emit scrollSynced(m_scrollOffset);
        } else {
            // Hit the edge — stop
            m_scrollVelocity = 0.0;
            m_kineticTimer->stop();
            emit scrollSynced(m_scrollOffset); // final position
        }
    });

    // Async pixel stats: debounce 80ms after last scroll, then compute off-thread
    m_asyncTimer = new QTimer(this);
    m_asyncTimer->setSingleShot(true);
    m_asyncTimer->setInterval(80);
    connect(m_asyncTimer, &QTimer::timeout, this, &WaveformWidget::scheduleAsyncRecompute);

    connect(&m_pixWatcher, &QFutureWatcher<PixResult>::finished, this, [this]() {
        m_asyncRunning = false;
        const PixResult res = m_pixWatcher.result();
        // Accept result only if it matches current view (user may have scrolled again)
        if (res.scroll == m_scrollOffset
                && qFuzzyCompare(res.vpp, m_valsPerPx)
                && res.w == width() - kYAxisW) {
            m_pixAvg          = res.avg;
            m_pixSig          = res.sig;
            m_pixCmpAvg       = res.cmpAvg;
            m_pixDirty        = false;
            m_pixCachedScroll = res.scroll;
            m_pixCachedVpp    = res.vpp;
            m_pixCachedW      = res.w;
            m_renderCache     = QPixmap();  // force redraw with fresh stats
            update();
        } else {
            // View changed while computing — schedule another pass
            m_asyncTimer->start();
        }
    });

    // ── WaveformEditor (free-form editing engine) ─────────────────────────
    m_editor = new WaveformEditor(this);
    connect(m_editor, &WaveformEditor::dataModified,
            this, &WaveformWidget::dataModified);
    connect(m_editor, &WaveformEditor::dataModified, this, [this]() {
        invalidatePixCache();
        update();
    });

    setFocusPolicy(Qt::StrongFocus);   // needed for keyPressEvent

    m_monoFont = QFont("Consolas", 8);
    m_monoFont.setStyleHint(QFont::Monospace);
    m_smallFont = QFont("Segoe UI", 7);
    m_smallFont.setStyleHint(QFont::SansSerif);
    m_labelFont = QFont("Segoe UI", 9, QFont::Bold);
    m_titleFont = QFont("Segoe UI", 11);
}

// ── Public API ─────────────────────────────────────────────────────────────

void WaveformWidget::showROM(const QByteArray &romData, const QByteArray &originalData)
{
    m_data         = romData;
    m_originalData = originalData;
    m_scrollOffset = 0;
    m_overviewCache.clear();
    invalidatePixCache();
    updateZoomFromSlider(m_zoomSlider->value());
    buildOverviewCache();

    // Configure editor with live data buffers
    if (m_editor) {
        const QByteArray *origPtr = m_originalData.isEmpty() ? nullptr : &m_originalData;
        m_editor->setData(&m_data, origPtr);
        m_editor->setCellSize(m_cellSize);
        m_editor->setByteOrder(m_byteOrder);
    }

    update();
}

void WaveformWidget::refreshRomData(const QByteArray &romData)
{
    if (m_data == romData) return;
    m_data = romData;
    m_overviewCache.clear();
    invalidatePixCache();
    buildOverviewCache();
    update();
}

void WaveformWidget::setMaps(const QVector<MapInfo> &maps)
{
    m_maps = maps;
    invalidatePixCache();
    update();
}

void WaveformWidget::setAutoDetectedMaps(const QVector<MapInfo> &maps)
{
    m_autoMaps = maps;
    invalidatePixCache();
    update();
}

void WaveformWidget::setCurrentMap(const MapInfo &map)
{
    m_currentMap    = map;
    m_hasCurrentMap = true;
    m_hoverCol      = -1;
    invalidatePixCache();
    update();
}

void WaveformWidget::goToAddress(uint32_t offset)
{
    if (m_data.isEmpty()) return;
    int plotW = width() - kYAxisW;
    int visibleVals = (int)(plotW * m_valsPerPx);
    int targetIdx = offset / m_cellSize;
    m_scrollOffset = qMax(0, (int)((targetIdx - visibleVals * 0.1) * m_cellSize));
    m_selectedOffset = offset;
    clampScroll();
    invalidatePixCache();
    update();
}

// ── Helpers ────────────────────────────────────────────────────────────────

int WaveformWidget::totalValues() const
{
    if (m_data.isEmpty()) return 0;
    return m_data.size() / m_cellSize;
}

uint32_t WaveformWidget::maxVal() const
{
    if (m_cellSize == 1) return 255;
    if (m_cellSize == 2) return 65535;
    return 0xFFFFFFFF;
}

uint32_t WaveformWidget::readValue(uint32_t offset) const
{
    return readRomValue(reinterpret_cast<const uint8_t*>(m_data.constData()),
                        m_data.size(), offset, m_cellSize, m_byteOrder);
}

void WaveformWidget::updateZoomFromSlider(int val)
{
    int tv    = totalValues();
    int plotW = qMax(width() - kYAxisW, 400);
    double minVPP = 0.25;
    double maxVPP = qMax((double)tv / plotW, 1.0);
    double t      = val / 100.0;
    double logMin = std::log(minVPP);
    double logMax = std::log(maxVPP);
    m_valsPerPx = std::exp(logMax - t * (logMax - logMin));

    if (m_zoomLabel) {
        if (m_valsPerPx >= 1)
            m_zoomLabel->setText(QString("%1 v/px").arg((int)m_valsPerPx));
        else
            m_zoomLabel->setText(QString("%1 px/v").arg(1.0 / m_valsPerPx, 0, 'f', 1));
    }
}

void WaveformWidget::clampScroll()
{
    int tv = totalValues();
    int plotW = width() - kYAxisW;
    int visibleVals = (int)(plotW * m_valsPerPx);
    int maxStartIdx = qMax(0, tv - visibleVals);
    int startIdx = m_scrollOffset / m_cellSize;
    m_scrollOffset = qBound(0, startIdx, maxStartIdx) * m_cellSize;
}

QColor WaveformWidget::valColor(double pct) const
{
    if (pct < 0.01) return QColor(20, 40, 80, 150);
    return QColor(qBound(0, (int)(40 + pct*180), 255),
                  qBound(0, (int)(80 + pct*160), 255),
                  qBound(0, (int)(180 + pct* 75), 255));
}

void WaveformWidget::buildOverviewCache()
{
    if (m_data.isEmpty()) return;
    int ow = width();
    if (ow <= 0) return;
    int tv = totalValues();
    uint32_t mv = maxVal();
    m_overviewCache.resize(ow);
    double valsPerPx = (double)tv / ow;
    for (int px = 0; px < ow; px++) {
        int bs = (int)(px * valsPerPx);
        int be = (int)((px + 1) * valsPerPx);
        uint32_t mxV = 0;
        for (int idx = bs; idx < be && idx < tv; idx++) {
            uint32_t v = readValue(idx * m_cellSize);
            if (v > mxV) mxV = v;
        }
        m_overviewCache[px] = (float)mxV / mv;
    }
}

int32_t WaveformWidget::offsetAtX(int x) const
{
    if (m_data.isEmpty() || x < 0) return -1;
    int startIdx = m_scrollOffset / m_cellSize;
    int valIdx = startIdx + (int)(x * m_valsPerPx);
    int offset = valIdx * m_cellSize;
    if (offset < 0 || offset >= m_data.size()) return -1;
    return offset;
}

void WaveformWidget::renderSelection(QPainter &p)
{
    if (!hasSelection() || m_data.isEmpty()) return;

    int plotW = width() - kYAxisW;
    int topY  = kControlsH;
    int plotH = height() - kControlsH - kXAxisH - kOverviewH;

    int startIdx = m_scrollOffset / m_cellSize;
    int endIdx = startIdx + (int)(plotW * m_valsPerPx);

    int selStartVal = (int)(selectionStart() / m_cellSize);
    int selEndVal   = (int)(selectionEnd() / m_cellSize);

    // Clip to visible range
    if (selEndVal < startIdx || selStartVal > endIdx) return;

    int x1 = qMax(0, (int)((selStartVal - startIdx) / m_valsPerPx));
    int x2 = qMin(plotW, (int)((selEndVal - startIdx) / m_valsPerPx));

    if (x2 <= x1) return;

    // Selection highlight
    p.fillRect(x1, topY, x2 - x1, plotH, QColor(58, 145, 208, 40));

    // Selection borders
    p.setPen(QPen(QColor(58, 145, 208, 180), 1, Qt::DashLine));
    p.drawLine(x1, topY, x1, topY + plotH);
    p.drawLine(x2, topY, x2, topY + plotH);

    // Selection info label
    int len = selectionLength();
    QString info = QString("%1 bytes | 0x%2 - 0x%3")
        .arg(len)
        .arg(selectionStart(), 0, 16, QChar('0')).toUpper()
        .arg(selectionEnd(), 0, 16, QChar('0')).toUpper();
    p.setFont(m_monoFont);
    QFontMetrics fm(m_monoFont);
    int tw = fm.horizontalAdvance(info) + 12;
    int tx = qMin(x1 + 4, plotW - tw);
    int ty = topY + 4;
    p.fillRect(tx - 2, ty - 1, tw, fm.height() + 2, QColor(12, 18, 34, 220));
    p.setPen(QColor(58, 145, 208));
    p.drawText(tx + 4, ty + fm.ascent(), info);
}

void WaveformWidget::showContextMenu(const QPoint &pos)
{
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu{background:#0c1222;color:#e7eefc;border:1px solid rgba(231,238,252,0.1);padding:4px}"
        "QMenu::item{padding:6px 24px 6px 12px;border-radius:4px}"
        "QMenu::item:selected{background:#1a2744}"
        "QMenu::separator{height:1px;background:rgba(231,238,252,0.08);margin:4px 8px}");

    bool hasSel = hasSelection();
    int selLen = selectionLength();

    // ── Selection → Map ──
    auto *actSelToMap = menu.addAction(QIcon(), tr("Selection → Map") + "\tK");
    actSelToMap->setEnabled(hasSel && selLen >= 2);
    connect(actSelToMap, &QAction::triggered, this, [this]() {
        emit selectionToMapRequested(selectionStart(), selectionLength());
    });

    menu.addSeparator();

    // ── Value operations ──
    auto *actValPlus = menu.addAction(tr("Value + 1") + "\t+");
    actValPlus->setEnabled(hasSel);
    connect(actValPlus, &QAction::triggered, this, [this]() {
        if (!m_editor || !hasSelection()) return;
        m_editor->increment(selectionStart(), selectionEnd(), 1);
    });

    auto *actValMinus = menu.addAction(tr("Value - 1") + "\t-");
    actValMinus->setEnabled(hasSel);
    connect(actValMinus, &QAction::triggered, this, [this]() {
        if (!m_editor || !hasSelection()) return;
        m_editor->increment(selectionStart(), selectionEnd(), -1);
    });

    menu.addSeparator();

    auto *actChangeAbs = menu.addAction(tr("Change absolute...") + "\t=");
    actChangeAbs->setEnabled(hasSel);
    connect(actChangeAbs, &QAction::triggered, this, [this]() {
        if (!m_editor || !hasSelection()) return;
        bool ok = false;
        double val = QInputDialog::getDouble(this, tr("Set Value"), tr("New value:"),
                                              0, -2147483647, 2147483647, 2, &ok);
        if (ok) m_editor->setAbsolute(selectionStart(), selectionEnd(), val);
    });

    auto *actChangeRel = menu.addAction(tr("Change relative...") + "\t%");
    actChangeRel->setEnabled(hasSel);
    connect(actChangeRel, &QAction::triggered, this, [this]() {
        if (!m_editor || !hasSelection()) return;
        bool ok = false;
        double delta = QInputDialog::getDouble(this, tr("Add Value"), tr("Value to add (+/-):"),
                                                0, -2147483647, 2147483647, 2, &ok);
        if (ok) m_editor->addDelta(selectionStart(), selectionEnd(), delta);
    });

    menu.addSeparator();

    // ── Copy / Select ──
    auto *actCopy = menu.addAction(tr("Copy selection") + "\tCtrl+C");
    actCopy->setEnabled(hasSel);
    connect(actCopy, &QAction::triggered, this, [this]() {
        copySelectionToClipboard();
    });

    auto *actSelectAll = menu.addAction(tr("Select all") + "\tCtrl+A");
    connect(actSelectAll, &QAction::triggered, this, [this]() {
        m_selStart = 0;
        m_selEnd = m_data.size() - 1;
        emit selectionChanged(selectionStart(), selectionEnd());
        update();
    });

    auto *actDeselect = menu.addAction(tr("Deselect") + "\tEsc");
    actDeselect->setEnabled(hasSel);
    connect(actDeselect, &QAction::triggered, this, [this]() {
        m_selStart = m_selEnd = -1;
        update();
    });

    menu.addSeparator();

    // ── Zoom ──
    auto *actZoomSel = menu.addAction(tr("Zoom to selection"));
    actZoomSel->setEnabled(hasSel);
    connect(actZoomSel, &QAction::triggered, this, [this]() {
        if (!hasSelection()) return;
        int plotW = width() - kYAxisW;
        int selVals = selectionLength() / m_cellSize;
        if (selVals < 1) selVals = 1;
        m_valsPerPx = (double)selVals / plotW;
        m_scrollOffset = (int)selectionStart();
        clampScroll();
        invalidatePixCache();
        update();
        emit scrollSynced(m_scrollOffset);
    });

    auto *actOrigVal = menu.addAction(tr("Original value") + "\tF11");
    actOrigVal->setEnabled(hasSel && !m_originalData.isEmpty());
    connect(actOrigVal, &QAction::triggered, this, [this]() {
        if (!m_editor || !hasSelection()) return;
        m_editor->restoreOriginal(selectionStart(), selectionEnd());
    });

    menu.addSeparator();

    // ── Advanced edit operations ──
    auto *actScale = menu.addAction(tr("Scale %..."));
    actScale->setEnabled(hasSel);
    connect(actScale, &QAction::triggered, this, [this]() {
        if (!m_editor || !hasSelection()) return;
        bool ok = false;
        double factor = QInputDialog::getDouble(this, tr("Scale"), tr("Factor (e.g. 1.10 = +10%):"),
                                                 1.0, -1000.0, 1000.0, 4, &ok);
        if (ok) m_editor->scale(selectionStart(), selectionEnd(), factor);
    });

    auto *actInterpolate = menu.addAction(tr("Interpolate"));
    actInterpolate->setEnabled(hasSel);
    connect(actInterpolate, &QAction::triggered, this, [this]() {
        if (!m_editor || !hasSelection()) return;
        m_editor->interpolate(selectionStart(), selectionEnd());
    });

    auto *actSmooth = menu.addAction(tr("Smooth"));
    actSmooth->setEnabled(hasSel);
    connect(actSmooth, &QAction::triggered, this, [this]() {
        if (!m_editor || !hasSelection()) return;
        m_editor->smooth(selectionStart(), selectionEnd(), 3);
    });

    auto *actFlatten = menu.addAction(tr("Flatten"));
    actFlatten->setEnabled(hasSel);
    connect(actFlatten, &QAction::triggered, this, [this]() {
        if (!m_editor || !hasSelection()) return;
        m_editor->flatten(selectionStart(), selectionEnd());
    });

    menu.addSeparator();

    // ── Undo / Redo ──
    auto *actUndo = menu.addAction(tr("Undo") + "\tCtrl+Z");
    actUndo->setEnabled(m_project ? m_project->canUndoRomEdit()
                                  : (m_editor && m_editor->canUndo()));
    connect(actUndo, &QAction::triggered, this, [this]() {
        if (m_project) m_project->undoRomEdit();
        else if (m_editor) m_editor->undo();
    });

    auto *actRedo = menu.addAction(tr("Redo") + "\tCtrl+Y");
    actRedo->setEnabled(m_project ? m_project->canRedoRomEdit()
                                  : (m_editor && m_editor->canRedo()));
    connect(actRedo, &QAction::triggered, this, [this]() {
        if (m_project) m_project->redoRomEdit();
        else if (m_editor) m_editor->redo();
    });

    menu.exec(mapToGlobal(pos));
}

const MapInfo *WaveformWidget::mapAtX(int x) const
{
    if (m_data.isEmpty()) return nullptr;
    const int startIdx = m_scrollOffset / m_cellSize;
    const double vpp   = m_valsPerPx;
    const uint32_t romSzHit = (uint32_t)m_data.size();
    // Prefer A2L-provided maps; when none present, hit-test the auto-detected
    // overlays so users can click them in the 2D view.
    const QVector<MapInfo> &src = !m_maps.isEmpty() ? m_maps : m_autoMaps;
    for (const MapInfo &mi : src) {
        if (mi.length <= 0) continue;
        if (mi.address + mi.mapDataOffset + (uint32_t)mi.length > romSzHit) continue;
        if (mi.linkConfidence == 40) continue;
        int ms = mi.address / m_cellSize;
        int me = (mi.address + mi.mapDataOffset + (uint32_t)mi.length - 1) / m_cellSize;
        int x1 = vpp >= 1 ? (int)((ms - startIdx) / vpp)
                           : (int)((ms - startIdx) * (1.0 / vpp));
        int x2 = vpp >= 1 ? (int)((me - startIdx) / vpp)
                           : (int)((me - startIdx) * (1.0 / vpp));
        x2 = qMax(x2, x1 + 2);   // ensure at least 3px hit area
        if (x >= x1 && x <= x2) return &mi;
    }
    return nullptr;
}

int WaveformWidget::mapColAtX(int x) const
{
    int cols = qMax(1, m_currentMap.dimensions.x);
    int plotW = width() - kYAxisW;
    if (plotW <= 0) return -1;
    int col = (int)((double)x / plotW * cols);
    return qBound(0, col, cols - 1);
}

// ── Paint ──────────────────────────────────────────────────────────────────

void WaveformWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const int w = width(), h = height();

    try {
        p.fillRect(0, kControlsH, w, h - kControlsH, AppConfig::instance().colors.waveBg);

        if (m_data.isEmpty()) {
            p.setPen(QColor(70, 100, 140));
            p.setFont(m_titleFont);
            p.drawText(QRect(0, kControlsH, w, h - kControlsH - kOverviewH),
                       Qt::AlignCenter, tr("No ROM loaded"));
            return;
        }

        // Always use ROM waveform mode (map curves disabled)
        const int plotW = w - kYAxisW;
        const bool cacheValid = !m_renderCache.isNull()
            && m_renderCache.size() == QSize(w, h)
            && !m_pixDirty
            && m_pixCachedScroll == m_scrollOffset
            && qFuzzyCompare(m_pixCachedVpp, m_valsPerPx)
            && m_pixCachedW == plotW;

        if (!cacheValid) {
            if (m_renderCache.size() != QSize(w, h))
                m_renderCache = QPixmap(w, h);
            m_renderCache.fill(AppConfig::instance().colors.waveBg);
            {
                QPainter cp(&m_renderCache);
                renderRomWaveform(cp);
                renderOverview(cp);
            }
        }
        p.drawPixmap(0, 0, m_renderCache);
        renderModifiedOverlay(p);  // highlight modified cells in orange
        renderAnnotations(p);      // Sprint C — pins at comment offsets
        renderSelection(p);        // dynamic overlay

    } catch (const std::exception &ex) {
        LOG_ERROR(QString("WaveformWidget::paintEvent exception: %1").arg(ex.what()));
        p.fillRect(0, kControlsH, w, h - kControlsH, QColor(0x1a, 0x08, 0x08));
        p.setPen(QColor(255, 80, 80));
        p.setFont(QFont("Segoe UI", 10));
        p.drawText(QRect(10, kControlsH + 10, w - 20, h - kControlsH - 20),
                   Qt::AlignCenter | Qt::TextWordWrap,
                   tr("2D view render error — see log for details\n%1").arg(ex.what()));
    } catch (...) {
        LOG_ERROR("WaveformWidget::paintEvent unknown exception");
        p.fillRect(0, kControlsH, w, h - kControlsH, QColor(0x1a, 0x08, 0x08));
        p.setPen(QColor(255, 80, 80));
        p.setFont(QFont("Segoe UI", 10));
        p.drawText(QRect(10, kControlsH + 10, w - 20, h - kControlsH - 20),
                   Qt::AlignCenter, tr("2D view render error — see log for details"));
    }
}

// ── Map curve mode ─────────────────────────────────────────────────────────

void WaveformWidget::renderMapCurve(QPainter &p)
{
    const int w     = width();
    const int h     = height() - kControlsH - kOverviewH;
    const int plotW = w - kYAxisW;
    const int plotH = h - kXAxisH;
    const int topY  = kControlsH;

    if (plotW <= 0 || plotH <= 0) return;

    const auto *raw    = reinterpret_cast<const uint8_t*>(m_data.constData());
    const int   datLen = m_data.size();
    const int   cols   = qMax(1, m_currentMap.dimensions.x);
    const int   rows   = qMax(1, m_currentMap.dimensions.y);
    const int   cs     = m_currentMap.dataSize > 0 ? m_currentMap.dataSize : 2;
    const bool  scaled = m_currentMap.hasScaling
                         && m_currentMap.scaling.type != CompuMethod::Type::Identical;
    const CompuMethod &cm = m_currentMap.scaling;

    // ── Read all values ──────────────────────────────────────────────────
    QVector<QVector<double>> phys(rows, QVector<double>(cols, 0.0));
    double minV =  1e18, maxV = -1e18;

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            uint32_t off = m_currentMap.address + m_currentMap.mapDataOffset
                           + (uint32_t)(r * cols + c) * cs;
            uint32_t raw_v = readRomValue(raw, datLen, off, cs, m_byteOrder);
            double v = scaled ? cm.toPhysical(signExtendRaw(raw_v, cs, m_currentMap.dataSigned))
                              : signExtendRaw(raw_v, cs, m_currentMap.dataSigned);
            phys[r][c] = v;
            if (v < minV) minV = v;
            if (v > maxV) maxV = v;
        }
    }

    // 8 % headroom top, 5 % bottom — keeps values off the edge
    double span = (maxV > minV) ? (maxV - minV) : 1.0;
    double lo   = minV - span * 0.05;
    double hi   = maxV + span * 0.08;
    double rng  = (hi > lo) ? (hi - lo) : 1.0;

    // helpers
    auto colToX = [&](int c) -> double {
        return (cols > 1) ? (8.0 + (double)c / (cols - 1) * (plotW - 16)) : plotW / 2.0;
    };
    auto valToY = [&](double v) -> double {
        double t = (v - lo) / rng;
        return topY + plotH - t * plotH;
    };
    const int baseline = topY + plotH;   // y of x-axis line
    const int showRows = qMin(rows, 32);

    // ── Background ───────────────────────────────────────────────────────
    // Slight gradient: darker at bottom (zero), slightly lighter at top
    const AppColors &cc = AppConfig::instance().colors;
    const WaveStyle &ws = AppConfig::instance().waveStyle;
    QLinearGradient bgGrad(0, topY, 0, topY + plotH);
    bgGrad.setColorAt(0.0, cc.waveBg.lighter(160));
    bgGrad.setColorAt(1.0, cc.waveBg);
    p.fillRect(0, topY, w, h, bgGrad);

    // ── Grid lines ────────────────────────────────────────────────────────
    // Horizontal (value) grid — solid subtle lines
    const int gridY = 8;
    for (int i = 0; i <= gridY; i++) {
        int gy = topY + plotH - (int)((double)i / gridY * plotH);
        bool major = (i == 0 || i == gridY || i == gridY / 2);
        p.setPen(QPen(major ? cc.waveGridMajor : cc.waveGridMinor, 1));
        p.drawLine(0, gy, plotW, gy);
    }
    // Vertical (column) grid
    int colStep = qMax(1, cols / 8);
    for (int c = 0; c < cols; c += colStep) {
        int gx = (int)colToX(c);
        p.setPen(QPen(cc.waveGridMinor, 1));
        p.drawLine(gx, topY, gx, baseline);
    }

    // ── Area fills (rendered before lines so lines sit on top) ────────────
    p.setRenderHint(QPainter::Antialiasing, true);

    const bool drawFill = (ws.fillUnderCurve || ws.shape == WaveStyle::Shape::Filled)
                          && ws.shape != WaveStyle::Shape::Bars;
    const bool drawLine = ws.shape != WaveStyle::Shape::Dots
                          && ws.shape != WaveStyle::Shape::Bars;
    const bool drawDots = ws.shape == WaveStyle::Shape::LineDots
                          || ws.shape == WaveStyle::Shape::Dots;

    // Draw fills in reverse order so row 0 is on top
    if (drawFill) for (int r = showRows - 1; r >= 0; r--) {
        QColor lc = waveColor(r);

        QPainterPath fillPath;
        fillPath.moveTo(colToX(0), baseline);
        for (int c = 0; c < cols; c++)
            fillPath.lineTo(colToX(c), valToY(phys[r][c]));
        fillPath.lineTo(colToX(cols - 1), baseline);
        fillPath.closeSubpath();

        QLinearGradient fillGrad(0, topY, 0, baseline);
        QColor topFill = lc;
        topFill.setAlpha(showRows == 1 ? 70 : 38);
        QColor botFill = lc;
        botFill.setAlpha(0);
        fillGrad.setColorAt(0.0, topFill);
        fillGrad.setColorAt(1.0, botFill);
        p.fillPath(fillPath, fillGrad);
    }

    // ── Curves (lines / dots / bars per configured shape) ─────────────────
    // Base thickness comes from settings; scale up slightly when few rows
    // are shown so a single curve stays prominent (matches old behavior at
    // the default 1.5 px: 2.4 / 2.0 / 1.5).
    const double lwBase = ws.lineWidth
                          * (showRows == 1 ? 1.6 : (showRows <= 4 ? 1.33 : 1.0));

    for (int r = 0; r < showRows; r++) {
        QColor lc = waveColor(r);
        lc.setAlpha(showRows > 8 ? 180 : 230);

        // Bars — vertical bars from the baseline, one per column
        if (ws.shape == WaveStyle::Shape::Bars) {
            double spacing = (cols > 1) ? (colToX(1) - colToX(0)) : plotW;
            double bw = qBound(2.0, spacing * 0.55, 24.0);
            QColor fillC = lc; fillC.setAlpha(showRows == 1 ? 170 : 110);
            p.setPen(QPen(lc, 1));
            p.setBrush(fillC);
            for (int c = 0; c < cols; c++) {
                double px = colToX(c), py = valToY(phys[r][c]);
                p.drawRect(QRectF(px - bw / 2, py, bw, baseline - py));
            }
            continue;
        }

        // Line
        if (drawLine) {
            p.setPen(QPen(lc, lwBase, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            QPainterPath path;
            for (int c = 0; c < cols; c++) {
                double px = colToX(c), py = valToY(phys[r][c]);
                if (c == 0) path.moveTo(px, py); else path.lineTo(px, py);
            }
            p.drawPath(path);
        }

        // Dot markers — size from settings (0 = auto by column count)
        int dotR = 0;
        if (drawDots) {
            dotR = ws.dotSize > 0 ? ws.dotSize
                 : (cols <= 8) ? 4 : (cols <= 24) ? 3 : (cols <= 64) ? 2 : 0;
            if (ws.shape == WaveStyle::Shape::Dots && dotR == 0)
                dotR = 2;   // points-only must always show something
        }
        if (dotR > 0) {
            p.setBrush(lc);
            p.setPen(QPen(cc.waveBg, 1));
            for (int c = 0; c < cols; c++)
                p.drawEllipse(QPointF(colToX(c), valToY(phys[r][c])), dotR, dotR);
        }

        // Inline value labels at data points (small maps only, single row)
        if (showRows == 1 && cols <= 20) {
            QFont vFont("Consolas", 8);
            p.setFont(vFont);
            QFontMetrics vfm(vFont);
            p.setPen(QColor(200, 220, 240));
            for (int c = 0; c < cols; c++) {
                double v = phys[r][c];
                QString lbl = scaled ? cm.formatValue(v) : QString::number((int)v);
                int tx = (int)colToX(c) - vfm.horizontalAdvance(lbl) / 2;
                int ty = (int)valToY(v) - dotR - 4;
                ty = qBound(topY + 10, ty, baseline - 4);
                p.drawText(tx, ty, lbl);
            }
        }
    }
    p.setRenderHint(QPainter::Antialiasing, false);

    // ── Comparison overlay (selected comparison ROM) ────────────────────
    if (!m_comparisonData.isEmpty() && m_comparisonData.size() >= m_data.size()) {

        const auto *origRaw = reinterpret_cast<const uint8_t*>(m_comparisonData.constData());
        const int   origLen = m_comparisonData.size();

        // Read original values (row 0 only for comparison line)
        QVector<double> origPhys(cols);
        for (int c = 0; c < cols; c++) {
            uint32_t off = m_currentMap.address + m_currentMap.mapDataOffset
                           + (uint32_t)c * cs;
            uint32_t raw_v = readRomValue(origRaw, origLen, off, cs, m_byteOrder);
            double v = scaled ? cm.toPhysical(signExtendRaw(raw_v, cs, m_currentMap.dataSigned))
                              : signExtendRaw(raw_v, cs, m_currentMap.dataSigned);
            origPhys[c] = v;
        }

        // Delta fill — area between row-0 current and stock
        p.setRenderHint(QPainter::Antialiasing, true);
        const QColor kDeltaFill(255, 160, 50, 28);
        QPainterPath deltaPath;
        // forward along current row 0
        for (int c = 0; c < cols; c++) {
            double px = colToX(c), py = valToY(phys[0][c]);
            if (c == 0) deltaPath.moveTo(px, py); else deltaPath.lineTo(px, py);
        }
        // backward along comparison
        for (int c = cols - 1; c >= 0; c--)
            deltaPath.lineTo(colToX(c), valToY(origPhys[c]));
        deltaPath.closeSubpath();
        p.fillPath(deltaPath, kDeltaFill);

        // Comparison line — dashed amber, 2px
        const QColor kCmpColor(255, 160, 50);
        QPen cmpPen(kCmpColor, 2.0, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(cmpPen);
        p.setBrush(Qt::NoBrush);
        QPainterPath cmpPath;
        for (int c = 0; c < cols; c++) {
            double px = colToX(c), py = valToY(origPhys[c]);
            if (c == 0) cmpPath.moveTo(px, py); else cmpPath.lineTo(px, py);
        }
        p.drawPath(cmpPath);

        // Hollow dot markers
        int dotR = (cols <= 8) ? 4 : (cols <= 24) ? 3 : (cols <= 64) ? 2 : 0;
        if (dotR > 0) {
            p.setBrush(cc.waveBg.lighter(160));   // hollow — bg fill
            p.setPen(QPen(kCmpColor, 1.5));
            for (int c = 0; c < cols; c++)
                p.drawEllipse(QPointF(colToX(c), valToY(origPhys[c])), dotR + 1, dotR + 1);
        }

        p.setRenderHint(QPainter::Antialiasing, false);
    }

    // ── Legend ────────────────────────────────────────────────────────────
    if (rows > 1 && rows <= 16) {
        QFont legFont("Segoe UI", 8);
        p.setFont(legFont);
        QFontMetrics lfm(legFont);
        // Position at top-right to avoid interfering with the curve
        int lx = plotW - 80, ly = topY + 8;
        p.fillRect(lx - 4, ly - 2, 80, rows * 14 + 4, QColor(8, 14, 26, 180));
        for (int r = 0; r < qMin(rows, 8); r++) {
            QColor lc = waveColor(r);
            p.fillRect(lx, ly + r * 14 + 3, 10, 8, lc);
            p.setPen(QColor(160, 185, 210));
            p.drawText(lx + 14, ly + r * 14 + lfm.ascent(), QString("Row %1").arg(r));
        }
        if (rows > 8) {
            p.setPen(QColor(100, 130, 160));
            p.drawText(lx, ly + 8 * 14 + lfm.ascent(), QString("…+%1 more").arg(rows - 8));
        }
    }

    // ── Legend for comparison mode ─────────────────────────────────────────
    if (!m_comparisonData.isEmpty()) {
        QFont legFont("Segoe UI", 8);
        p.setFont(legFont);
        QFontMetrics lfm(legFont);
        int lx = plotW - 80, ly = topY + 8 + qMin(rows > 0 ? rows : 1, 8) * 14 + 12;
        p.fillRect(lx - 4, ly - 2, 80, 40, QColor(8, 14, 26, 180));
        // Current line
        p.fillRect(lx, ly + 3, 10, 3, waveColor(0));
        p.setPen(QColor(160, 185, 210));
        p.drawText(lx + 14, ly + 2 + lfm.ascent(), tr("Current"));
        // Stock line — dashed amber
        QPen dp(QColor(255, 160, 50), 2, Qt::DashLine);
        p.setPen(dp);
        p.drawLine(lx, ly + 18, lx + 16, ly + 18);
        p.setPen(QColor(160, 185, 210));
        p.drawText(lx + 14, ly + 18 + lfm.ascent() - 4, tr("Stock"));
    }

    // ── Hover crosshair + floating value tooltip ──────────────────────────
    if (m_hoverCol >= 0 && m_hoverCol < cols) {
        int hx = (int)colToX(m_hoverCol);

        // Vertical crosshair
        p.setPen(QPen(QColor(220, 235, 255, 100), 1, Qt::DashLine));
        p.drawLine(hx, topY, hx, baseline);

        QFont vFont("Consolas", 9);
        p.setFont(vFont);
        QFontMetrics vfm(vFont);

        int bubRows = qMin(rows, 6);
        int bubW = 0;
        QStringList labels;
        for (int r = 0; r < bubRows; r++) {
            double v = phys[r][m_hoverCol];
            QString txt = scaled
                ? (cm.formatValue(v) + (cm.unit.isEmpty() ? "" : " " + cm.unit))
                : QString::number((int)v);
            labels.append(txt);
            bubW = qMax(bubW, vfm.horizontalAdvance(txt));
        }
        bubW += 20;
        int bubH   = bubRows * 18 + 4;
        int bubX   = qBound(2, hx - bubW / 2, plotW - bubW - 2);
        int bubY   = topY + 30;

        p.fillRect(bubX, bubY, bubW, bubH, QColor(10, 18, 36, 230));
        p.setPen(QPen(QColor(58, 130, 200, 150), 1));
        p.drawRect(bubX, bubY, bubW, bubH);

        for (int r = 0; r < bubRows; r++) {
            QColor lc = waveColor(r);
            p.fillRect(bubX + 4, bubY + 4 + r * 18 + 4, 6, 8, lc);
            p.setPen(QColor(210, 225, 240));
            p.drawText(QRect(bubX + 14, bubY + 2 + r * 18, bubW - 16, 18),
                       Qt::AlignLeft | Qt::AlignVCenter, labels[r]);
        }

        // Dot highlight on each curve at hover column
        p.setRenderHint(QPainter::Antialiasing, true);
        for (int r = 0; r < showRows; r++) {
            QColor lc = waveColor(r);
            p.setBrush(lc);
            p.setPen(QPen(Qt::white, 1));
            p.drawEllipse(QPointF(hx, valToY(phys[r][m_hoverCol])), 4, 4);
        }
        p.setRenderHint(QPainter::Antialiasing, false);
    }

    // ── Y-axis ────────────────────────────────────────────────────────────
    p.fillRect(plotW, topY, kYAxisW, h, QColor(8, 14, 26));
    p.setPen(QPen(QColor(40, 70, 110), 1));
    p.drawLine(plotW, topY, plotW, topY + plotH);

    QFont axFont("Consolas", 8);
    axFont.setStyleHint(QFont::Monospace);
    p.setFont(axFont);
    p.setPen(QColor(110, 140, 170));
    for (int i = 0; i <= gridY; i++) {
        double v  = lo + (double)i / gridY * rng;
        int    gy = topY + plotH - (int)((double)i / gridY * plotH);
        QString lbl = scaled ? cm.formatValue(v) : QString::number((int)v);
        p.drawText(QRect(plotW + 2, gy - 6, kYAxisW - 4, 12),
                   Qt::AlignRight | Qt::AlignVCenter, lbl);
    }

    // Unit label (rotated, at mid-right)
    if (scaled && !cm.unit.isEmpty()) {
        p.save();
        p.setPen(QColor(90, 130, 170));
        QFont uFont("Segoe UI", 8);
        p.setFont(uFont);
        p.translate(w - 4, topY + plotH / 2);
        p.rotate(-90);
        QFontMetrics ufm(uFont);
        p.drawText(QRect(-50, -10, 100, 14), Qt::AlignCenter, cm.unit);
        p.restore();
    }

    // ── X-axis ────────────────────────────────────────────────────────────
    p.fillRect(0, topY + plotH, plotW, kXAxisH, QColor(8, 14, 26));
    p.setPen(QPen(QColor(40, 70, 110), 1));
    p.drawLine(0, topY + plotH, plotW, topY + plotH);
    p.setPen(QColor(110, 140, 170));
    QFont smallFont("Consolas", 8);
    smallFont.setStyleHint(QFont::Monospace);
    p.setFont(smallFont);
    int step = qMax(1, cols / 10);
    for (int c = 0; c < cols; c += step) {
        int gx = (int)colToX(c);
        p.drawText(QRect(gx - 18, topY + plotH + 3, 36, 16), Qt::AlignCenter,
                   QString::number(c));
    }

    // ── Map title ─────────────────────────────────────────────────────────
    p.setPen(QColor(150, 180, 220));
    QFont titleFont("Segoe UI", 9, QFont::Bold);
    p.setFont(titleFont);
    QString title = m_currentMap.name;
    if (!m_currentMap.description.isEmpty())
        title += "   —   " + m_currentMap.description;
    QString dims = QString("  [%1×%2  %3-bit]")
        .arg(m_currentMap.dimensions.x)
        .arg(m_currentMap.dimensions.y)
        .arg(cs * 8);
    p.drawText(QRect(4, topY + 4, plotW - 8, 18), Qt::AlignLeft | Qt::AlignVCenter,
               title + dims);
}

// Safe double-to-int pixel cast: clamps to [-32000, 32000] to prevent UB on large ROMs
static inline int safePx(double v) {
    return (int)qBound(-32000.0, v, 32000.0);
}

// ── Pixel stats cache ──────────────────────────────────────────────────────

void WaveformWidget::invalidatePixCache()
{
    m_pixDirty = true;
    m_renderCache = QPixmap();    // force repaint with new (possibly stale) stats
    if (m_asyncTimer) m_asyncTimer->start();   // refine after scroll settles
}

void WaveformWidget::recomputePixelStats(int plotW)
{
    m_pixAvg.resize(plotW);
    m_pixSig.resize(plotW);

    const bool hasCmp = !m_comparisonData.isEmpty();
    if (hasCmp)
        m_pixCmpAvg.resize(plotW);
    else
        m_pixCmpAvg.clear();

    const int    tv       = totalValues();
    const double vpp      = m_valsPerPx;
    const int    startIdx = m_scrollOffset / m_cellSize;
    const int    maxSamp  = 8;

    const uint8_t *cmpRaw = hasCmp ? reinterpret_cast<const uint8_t*>(m_comparisonData.constData()) : nullptr;
    const int cmpLen = m_comparisonData.size();

    for (int px = 0; px < plotW; px++) {
        const int bs = vpp >= 1 ? startIdx + (int)(px * vpp)
                                : startIdx + (int)(px / (1.0 / vpp));
        const int be = vpp >= 1 ? startIdx + (int)((px + 1) * vpp)
                                : bs + 1;
        if (bs >= tv) {
            m_pixAvg[px] = m_pixSig[px] = 0.0;
            if (hasCmp) m_pixCmpAvg[px] = 0.0;
            continue;
        }

        const int total  = qMax(1, qMin(be, tv) - bs);
        const int stride = qMax(1, total / maxSamp);

        uint64_t sum = 0, sumSq = 0;
        uint64_t cmpSum = 0;
        int samples = 0;
        for (int i = bs; i < bs + total && i < tv; i += stride) {
            uint64_t v = readValue((uint32_t)i * m_cellSize);
            sum   += v;
            sumSq += v * v;
            if (hasCmp)
                cmpSum += readRomValue(cmpRaw, cmpLen, (uint32_t)i * m_cellSize, m_cellSize, m_byteOrder);
            ++samples;
        }
        if (samples == 0) {
            m_pixAvg[px] = m_pixSig[px] = 0.0;
            if (hasCmp) m_pixCmpAvg[px] = 0.0;
            continue;
        }
        double avg = (double)sum / samples;
        double var = (double)sumSq / samples - avg * avg;
        m_pixAvg[px] = avg;
        m_pixSig[px] = var > 0.0 ? sqrt(var) : 0.0;
        if (hasCmp) m_pixCmpAvg[px] = (double)cmpSum / samples;
    }

    m_pixDirty        = false;
    m_pixCachedScroll = m_scrollOffset;
    m_pixCachedVpp    = vpp;
    m_pixCachedW      = plotW;
}

void WaveformWidget::scheduleAsyncRecompute()
{
    if (m_data.isEmpty() || m_asyncRunning) return;
    const int plotW = width() - kYAxisW;
    if (plotW <= 0) return;

    // Capture everything the worker needs by value — no shared mutable state
    const QByteArray data      = m_data;      // implicit-share, O(1) copy
    const QByteArray cmpData   = m_comparisonData;  // comparison ROM (may be empty)
    const int        cellSize  = m_cellSize;
    const ByteOrder  byteOrder = m_byteOrder;
    const double     vpp       = m_valsPerPx;
    const int        startIdx  = m_scrollOffset / m_cellSize;
    const int        scroll    = m_scrollOffset;
    const int        tv        = data.size() / cellSize;

    m_asyncRunning = true;
    m_pixWatcher.setFuture(QtConcurrent::run([=]() -> PixResult {
        PixResult r;
        r.scroll = scroll;
        r.vpp    = vpp;
        r.w      = plotW;
        r.avg.resize(plotW);
        r.sig.resize(plotW);

        const bool hasCmp = !cmpData.isEmpty();
        if (hasCmp)
            r.cmpAvg.resize(plotW);

        const int maxSamp = 64;
        const uint8_t* raw = reinterpret_cast<const uint8_t*>(data.constData());
        const uint8_t* cmpRaw = hasCmp ? reinterpret_cast<const uint8_t*>(cmpData.constData()) : nullptr;
        const int cmpLen = cmpData.size();

        for (int px = 0; px < plotW; px++) {
            const int bs = vpp >= 1 ? startIdx + (int)(px * vpp)
                                    : startIdx + (int)(px / (1.0 / vpp));
            const int be = vpp >= 1 ? startIdx + (int)((px + 1) * vpp)
                                    : bs + 1;
            if (bs >= tv) {
                r.avg[px] = r.sig[px] = 0.0;
                if (hasCmp) r.cmpAvg[px] = 0.0;
                continue;
            }

            const int total  = qMax(1, qMin(be, tv) - bs);
            const int stride = qMax(1, total / maxSamp);

            uint64_t sum = 0, sumSq = 0;
            uint64_t cmpSum = 0;
            int samples = 0;
            for (int i = bs; i < bs + total && i < tv; i += stride) {
                uint64_t v = readRomValue(raw, data.size(), (uint32_t)i * cellSize, cellSize, byteOrder);
                sum   += v;
                sumSq += v * v;
                if (hasCmp)
                    cmpSum += readRomValue(cmpRaw, cmpLen, (uint32_t)i * cellSize, cellSize, byteOrder);
                ++samples;
            }
            if (samples == 0) {
                r.avg[px] = r.sig[px] = 0.0;
                if (hasCmp) r.cmpAvg[px] = 0.0;
                continue;
            }
            double avg = (double)sum / samples;
            double var = (double)sumSq / samples - avg * avg;
            r.avg[px] = avg;
            r.sig[px] = var > 0.0 ? std::sqrt(var) : 0.0;
            if (hasCmp) r.cmpAvg[px] = (double)cmpSum / samples;
        }
        return r;
    }));
}

// ── Shared region renderer (real maps and auto-detected overlays) ─────────
//
// Pixel-coordinate helper used by renderRomWaveform. Factored out so the
// code path for real A2L maps and the code path for auto-detected overlays
// is identical except for the visual style toggle (`isAuto`).
void WaveformWidget::drawMapRegions(QPainter &p,
                                    const QVector<MapInfo> &maps,
                                    const QVector<int> &visIndices,
                                    int plotW, int plotH, int topY,
                                    int startIdx, double vpp,
                                    bool isAuto)
{
    const int bandH = plotH;
    for (int idx : visIndices) {
        if (idx < 0 || idx >= maps.size()) continue;
        const MapInfo &mi = maps[idx];
        bool cur = !isAuto && m_hasCurrentMap && (mi == m_currentMap);
        int64_t ms = (int64_t)mi.address / m_cellSize;
        int64_t me = (int64_t)(mi.address + mi.mapDataOffset + (uint32_t)mi.length - 1) / m_cellSize;
        int x1 = safePx(vpp >= 1 ? (double)(ms - startIdx) / vpp
                                  : (double)(ms - startIdx) * (1.0 / vpp));
        int x2 = safePx(vpp >= 1 ? (double)(me - startIdx) / vpp
                                  : (double)(me - startIdx) * (1.0 / vpp));
        if (x2 < 0 || x1 > plotW) continue;
        x1 = qBound(0, x1, plotW);
        x2 = qBound(0, x2, plotW);
        int bw = qMax(2, x2 - x1);

        QColor col = cur ? QColor(58, 200, 255) : waveColor(idx);

        if (isAuto) {
            // Distinct visual for auto overlays: dashed outline + ~40%
            // opacity fill. A warm amber tint keeps them visually separate
            // from the cool-blue real-map palette.
            QColor fill = QColor(230, 170, 60); fill.setAlpha(102);  // ~40%
            p.fillRect(x1, topY, bw, bandH, fill);

            QPen dashPen(QColor(230, 170, 60, 220), 1.2, Qt::DashLine);
            dashPen.setDashPattern({4.0, 3.0});
            p.setPen(dashPen);
            p.setBrush(Qt::NoBrush);
            // Outline (slight inset so full rect is visible)
            p.drawRect(x1, topY + 1, qMax(1, bw - 1), bandH - 2);
        } else {
            QColor fill = col; fill.setAlpha(cur ? 50 : 22);
            p.fillRect(x1, topY, bw, bandH, fill);

            QColor edge = col; edge.setAlpha(cur ? 255 : 120);
            p.fillRect(x1, topY, bw, 2, edge);

            if (bw >= 3) {
                QColor side = col; side.setAlpha(cur ? 180 : 60);
                p.fillRect(x1,          topY, 1, bandH, side);
                p.fillRect(x1 + bw - 1, topY, 1, bandH, side);
            }
        }
    }
}

// ── ROM waveform mode ──────────────────────────────────────────────────────

void WaveformWidget::renderRomWaveform(QPainter &p)
{
    const int w     = width();
    const int h     = height() - kControlsH - kOverviewH;
    const int plotW = w - kYAxisW;
    const int plotH = h - kXAxisH;
    const int topY  = kControlsH;

    if (plotW <= 0 || plotH <= 0) return;
    if (m_cellSize <= 0) { LOG_WARN("renderRomWaveform: m_cellSize <= 0"); return; }
    if (m_valsPerPx <= 0) { LOG_WARN("renderRomWaveform: m_valsPerPx <= 0"); return; }

    const double vpp      = m_valsPerPx;
    const uint32_t mv     = maxVal();
    const int startIdx    = m_scrollOffset / m_cellSize;
    const int tv          = totalValues();
    const uint32_t romSz  = (uint32_t)m_data.size();

    // ── Pre-filter visible maps (avoids iterating 26K+ maps per pass) ─────
    const int visStartIdx = startIdx;
    const int visEndIdx   = startIdx + (int)(plotW * qMax(vpp, 1.0)) + 1;
    QVector<int> visMaps;
    if (!m_maps.isEmpty()) {
        visMaps.reserve(256);
        for (int i = 0; i < m_maps.size(); i++) {
            const MapInfo &mi = m_maps[i];
            if (mi.length <= 0) continue;
            if (mi.address + mi.mapDataOffset + (uint32_t)mi.length > romSz) continue;
            if (mi.linkConfidence == 40) continue;
            int ms = (int)mi.address / m_cellSize;
            int me = (int)(mi.address + mi.mapDataOffset + (uint32_t)mi.length - 1) / m_cellSize;
            if (me < visStartIdx || ms > visEndIdx) continue;
            visMaps.append(i);
        }
    }

    // Auto-detected overlays are used ONLY when no real (A2L-derived) maps
    // are present — as soon as the user imports an A2L we switch exclusively
    // to the real maps.
    QVector<int> visAutoMaps;
    const bool useAutoMaps = m_maps.isEmpty() && !m_autoMaps.isEmpty();
    if (useAutoMaps) {
        visAutoMaps.reserve(256);
        for (int i = 0; i < m_autoMaps.size(); i++) {
            const MapInfo &mi = m_autoMaps[i];
            if (mi.length <= 0) continue;
            if (mi.address + mi.mapDataOffset + (uint32_t)mi.length > romSz) continue;
            int ms = (int)mi.address / m_cellSize;
            int me = (int)(mi.address + mi.mapDataOffset + (uint32_t)mi.length - 1) / m_cellSize;
            if (me < visStartIdx || ms > visEndIdx) continue;
            visAutoMaps.append(i);
        }
    }

    // ── Background ────────────────────────────────────────────────────────
    p.fillRect(0, topY, w, h, AppConfig::instance().colors.waveBg);

    // ── Per-pixel average + sigma (cached, recomputed only when view changes) ─
    if (m_pixDirty || m_pixCachedW != plotW
            || m_pixCachedScroll != m_scrollOffset
            || m_pixCachedVpp   != vpp) {
        recomputePixelStats(plotW);
    }
    const QVector<double> &avgV = m_pixAvg;
    const QVector<double> &sigV = m_pixSig;

    double visMin =  1e18, visMax = -1e18;
    for (int px = 0; px < plotW; px++) {
        double avg = avgV[px];
        if (avg < visMin) visMin = avg;
        if (avg > visMax) visMax = avg;
    }

    // ── Adaptive Y-scale (based on visible average range) ────────────────
    double span = (visMax > visMin) ? (visMax - visMin) : 1.0;
    const double lo = visMin - span * 0.06;
    const double hi = visMax + span * 0.06;
    const double rng = hi - lo;

    // Cache for yToVal() used by drawing mode
    m_visLo     = lo;
    m_visHi     = hi;
    m_plotTopY  = topY;
    m_plotHeight = plotH;

    auto valToY = [&](double v) -> int {
        return topY + plotH - (int)((v - lo) / rng * plotH);
    };

    // ── Grid lines ────────────────────────────────────────────────────────
    const int gridSteps = 8;
    for (int i = 0; i <= gridSteps; i++) {
        int gy = topY + plotH - (int)((double)i / gridSteps * plotH);
        bool major = (i == 0 || i == gridSteps || i == gridSteps / 2);
        p.setPen(QPen(major ? AppConfig::instance().colors.waveGridMajor
                            : AppConfig::instance().colors.waveGridMinor, 1));
        p.drawLine(0, gy, plotW, gy);
    }

    // ── Map region rectangles (drawn before waveform) ─────────────────────
    // Each map is a colored rectangle with fill + 1px top-edge line.
    // This makes them look like OLS colored squares on the waveform.
    if (!visMaps.isEmpty()) {
        drawMapRegions(p, m_maps, visMaps, plotW, plotH, topY,
                       startIdx, vpp, /*isAuto=*/false);
    } else if (useAutoMaps && !visAutoMaps.isEmpty()) {
        drawMapRegions(p, m_autoMaps, visAutoMaps, plotW, plotH, topY,
                       startIdx, vpp, /*isAuto=*/true);
    }

    // ── Draw waveform: sigma fill + avg line ──────────────────────────────
    // Sigma fill + waveform dot (QImage batch for per-pixel ops)
    {
        QImage waveImg(plotW, plotH, QImage::Format_ARGB32_Premultiplied);
        waveImg.fill(0);   // transparent

        const bool hasCmp = !m_pixCmpAvg.isEmpty() && m_pixCmpAvg.size() == plotW;

        for (int px = 0; px < plotW; px++) {
            const int bs = vpp >= 1 ? startIdx + (int)(px * vpp)
                                    : startIdx + (int)(px / (1.0 / vpp));
            if (bs >= tv) break;
            const double avg = avgV[px];
            const double sig = sigV[px];
            const double pct = avg / mv;

            // Determine color: normal, green (addition), or red (subtraction)
            QColor sc = valColor(pct);
            if (hasCmp) {
                double delta = avg - m_pixCmpAvg[px];
                if (delta > 0.5) {
                    sc = QColor(0, 200, 100);   // Green for additions
                } else if (delta < -0.5) {
                    sc = QColor(220, 50, 50);   // Red for subtractions
                }
            }

            // Sigma band
            int sTop = qBound(0, valToY(avg + sig) - topY, plotH);
            int sBot = qBound(0, valToY(avg - sig) - topY, plotH);
            if (sBot > sTop) {
                const uint32_t argb = qPremultiply(qRgba(sc.red(), sc.green(), sc.blue(), 35));
                for (int y = sTop; y < sBot; y++)
                    reinterpret_cast<uint32_t*>(waveImg.scanLine(y))[px] = argb;
            }

            // Zoomed-out waveform dot (when vpp >= 2)
            if (vpp >= 2.0) {
                int y = qBound(0, valToY(avg) - topY, plotH - 2);
                const uint32_t wArgb = qPremultiply(qRgba(sc.red(), sc.green(), sc.blue(), 255));
                if (y >= 0 && y < plotH)
                    reinterpret_cast<uint32_t*>(waveImg.scanLine(y))[px] = wArgb;
                if (y + 1 >= 0 && y + 1 < plotH)
                    reinterpret_cast<uint32_t*>(waveImg.scanLine(y + 1))[px] = wArgb;

                // Bars / filled shapes: fill the column down to the baseline
                const WaveStyle &wsz = AppConfig::instance().waveStyle;
                const bool colFill = wsz.shape == WaveStyle::Shape::Bars
                                  || wsz.shape == WaveStyle::Shape::Filled
                                  || wsz.fillUnderCurve;
                if (colFill) {
                    const int a = (wsz.shape == WaveStyle::Shape::Bars) ? 90 : 45;
                    const uint32_t bArgb = qPremultiply(qRgba(sc.red(), sc.green(), sc.blue(), a));
                    for (int yy = y + 2; yy < plotH; yy++)
                        reinterpret_cast<uint32_t*>(waveImg.scanLine(yy))[px] = bArgb;
                }
            }
        }
        p.drawImage(0, topY, waveImg);
    }

    // Zoomed-in smooth curve (colored by comparison when active), drawn in
    // the configured shape: line, line + points, points only, bars, filled.
    if (vpp < 2.0) {
        const bool hasCmp = !m_pixCmpAvg.isEmpty() && m_pixCmpAvg.size() >= plotW;
        const WaveStyle &ws = AppConfig::instance().waveStyle;
        const QColor defaultColor = AppConfig::instance().colors.waveLine;

        auto segColorAt = [&](int px) -> QColor {
            if (hasCmp) {
                double delta = avgV[px] - m_pixCmpAvg[px];
                if (delta > 0.5)  return QColor(0, 220, 100);   // Green for additions
                if (delta < -0.5) return QColor(240, 60, 60);   // Red for subtractions
            }
            return defaultColor;
        };
        // last visible pixel column (values beyond the ROM end are not drawn)
        int lastPx = plotW - 1;
        for (int px = 0; px < plotW; px++) {
            const int bs = vpp >= 1 ? startIdx + (int)(px * vpp)
                                    : startIdx + (int)(px / (1.0 / vpp));
            if (bs >= tv) { lastPx = px - 1; break; }
        }
        p.setRenderHint(QPainter::Antialiasing, true);

        // Filled area under the curve (drawn first so the line sits on top)
        if (lastPx >= 0 && ws.shape != WaveStyle::Shape::Bars
            && (ws.shape == WaveStyle::Shape::Filled || ws.fillUnderCurve)) {
            QPainterPath fillPath;
            fillPath.moveTo(0, topY + plotH);
            for (int px = 0; px <= lastPx; px++)
                fillPath.lineTo(px, valToY(avgV[px]));
            fillPath.lineTo(lastPx, topY + plotH);
            fillPath.closeSubpath();
            QLinearGradient fillGrad(0, topY, 0, topY + plotH);
            QColor topFill = defaultColor; topFill.setAlpha(60);
            QColor botFill = defaultColor; botFill.setAlpha(0);
            fillGrad.setColorAt(0.0, topFill);
            fillGrad.setColorAt(1.0, botFill);
            p.fillPath(fillPath, fillGrad);
        }

        if (lastPx < 0) {
            // nothing visible — fall through to axes/labels below
        } else if (ws.shape == WaveStyle::Shape::Bars) {
            // Vertical bars from the baseline, one per pixel column
            for (int px = 0; px <= lastPx; px++) {
                QColor bc = segColorAt(px); bc.setAlpha(160);
                p.setPen(QPen(bc, 1));
                p.drawLine(QPointF(px, valToY(avgV[px])),
                           QPointF(px, topY + plotH));
            }
        } else if (ws.shape == WaveStyle::Shape::Dots) {
            // Point markers only — one per value when spread out, else per px
            const double pxPerVal = 1.0 / vpp;
            const int    step = qMax(1, (int)pxPerVal);
            const double r = ws.dotSize > 0 ? ws.dotSize
                                            : qMax(1.2, ws.lineWidth);
            p.setPen(Qt::NoPen);
            for (int px = 0; px <= lastPx; px += step) {
                p.setBrush(segColorAt(px));
                p.drawEllipse(QPointF(px, valToY(avgV[px])), r, r);
            }
        } else {
            // Line / line+points / filled — connected segments
            for (int px = 1; px <= lastPx; px++) {
                p.setPen(QPen(segColorAt(px), ws.lineWidth));
                p.drawLine(QPointF(px - 1, valToY(avgV[px - 1])),
                           QPointF(px,     valToY(avgV[px])));
            }
            // Point markers at value positions, once individual values are
            // spread far enough apart to be distinguishable.
            const double pxPerVal = 1.0 / vpp;
            if (ws.shape == WaveStyle::Shape::LineDots && pxPerVal >= 6.0) {
                const double r = ws.dotSize > 0 ? ws.dotSize
                                                : qMin(4.0, pxPerVal / 4.0);
                p.setPen(QPen(AppConfig::instance().colors.waveBg, 1));
                for (int px = 0; px <= lastPx; px += qMax(1, (int)pxPerVal)) {
                    p.setBrush(segColorAt(px));
                    p.drawEllipse(QPointF(px, valToY(avgV[px])), r, r);
                }
            }
        }
        p.setRenderHint(QPainter::Antialiasing, false);
    }

    // ── Map name labels + current map bright border ───────────────────────
    {
        const QVector<MapInfo> &srcMaps = !visMaps.isEmpty() ? m_maps
                                        : (useAutoMaps ? m_autoMaps : m_maps);
        const QVector<int>     &srcVis  = !visMaps.isEmpty() ? visMaps
                                        : (useAutoMaps ? visAutoMaps : visMaps);
        const bool autoLabels = visMaps.isEmpty() && useAutoMaps;

        p.setFont(m_smallFont);
        QFontMetrics fm(m_smallFont);
        const int labelH = fm.height() + 2;
        for (int idx : srcVis) {
            const MapInfo &mi = srcMaps[idx];
            bool cur = !autoLabels && m_hasCurrentMap && (mi == m_currentMap);
            int64_t ms = (int64_t)mi.address / m_cellSize;
            int64_t me = (int64_t)(mi.address + mi.mapDataOffset + (uint32_t)mi.length - 1) / m_cellSize;
            int x1 = safePx(vpp >= 1 ? (double)(ms - startIdx) / vpp
                                      : (double)(ms - startIdx) * (1.0 / vpp));
            int x2 = safePx(vpp >= 1 ? (double)(me - startIdx) / vpp
                                      : (double)(me - startIdx) * (1.0 / vpp));
            if (x2 < 0 || x1 > plotW) continue;
            x1 = qBound(0, x1, plotW); x2 = qBound(0, x2, plotW);
            int bw = qMax(2, x2 - x1);
            QColor col = cur ? QColor(58, 220, 255) : waveColor(idx);

            if (cur) {
                p.setPen(QPen(QColor(58, 220, 255, 230), 1.5));
                p.setBrush(Qt::NoBrush);
                p.drawRect(x1, topY + 1, bw - 1, plotH - 2);
            }

            if (bw >= 14) {
                QString lbl = mi.name;
                // Prefix auto-detected overlay labels so the user can tell
                // them apart from real (A2L) maps at a glance.
                if (autoLabels) lbl = QStringLiteral("AUTO ") + lbl;
                while (lbl.size() > 3 && fm.horizontalAdvance(lbl) + 6 > bw)
                    lbl = lbl.left(lbl.size() - 2) + "\u2026";
                int tw = fm.horizontalAdvance(lbl) + 6;
                int tx = x1 + (bw - tw) / 2;
                int txMax = qMax(x1, plotW - tw);
                tx = qBound(x1, tx, txMax);
                int ty = topY + 4;
                QColor bg = col; bg.setAlpha(200);
                p.fillRect(tx, ty, tw, labelH, QColor(8, 14, 26, 190));
                p.setPen(col.lighter(cur ? 100 : (autoLabels ? 120 : 140)));
                p.drawText(tx + 3, ty + fm.ascent(), lbl);
            }
        }
    }

    // ── Y-axis ────────────────────────────────────────────────────────────
    p.fillRect(plotW, topY, kYAxisW, h, QColor(8, 14, 26));
    p.setPen(QPen(QColor(36, 62, 100), 1));
    p.drawLine(plotW, topY, plotW, topY + plotH);
    p.setFont(m_monoFont);
    p.setPen(QColor(100, 130, 160));
    for (int i = 0; i <= gridSteps; i++) {
        double v  = lo + (double)i / gridSteps * rng;
        int    gy = topY + plotH - (int)((double)i / gridSteps * plotH);
        p.drawText(QRect(plotW + 2, gy - 6, kYAxisW - 4, 12),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString::number((int)qBound(0.0, v, (double)mv)));
    }

    // ── X-axis ────────────────────────────────────────────────────────────
    p.fillRect(0, topY + plotH, plotW, kXAxisH, QColor(8, 14, 26));
    p.setPen(QPen(QColor(36, 62, 100), 1));
    p.drawLine(0, topY + plotH, plotW, topY + plotH);
    p.setPen(QColor(100, 130, 160));
    p.setFont(m_monoFont);
    for (int px = 0; px < plotW; px += 90) {
        int vi = vpp >= 1 ? startIdx + (int)(px * vpp)
                          : startIdx + (int)(px / (1.0 / vpp));
        if (vi >= tv) break;
        p.drawText(QRect(px - 40, topY + plotH + 2, 80, 18), Qt::AlignCenter,
                   "0x" + QString::number((uint32_t)vi * m_cellSize, 16).toUpper());
    }

    // ── Selected map label OR no-map hint ────────────────────────────────
    if (m_hasCurrentMap) {
        p.setFont(m_smallFont);
        p.setPen(QColor(58, 220, 255, 200));
        QString mapLabel = m_currentMap.name;
        if (!m_currentMap.description.isEmpty())
            mapLabel += "  —  " + m_currentMap.description;
        p.drawText(QRect(4, topY + 4, plotW - 8, 16), Qt::AlignCenter, mapLabel);
    } else if (!m_maps.isEmpty()) {
        p.setFont(m_labelFont);
        p.setPen(QColor(40, 65, 100, 160));
        p.drawText(QRect(4, topY + 4, plotW - 8, 18), Qt::AlignCenter,
                   tr("Click a map in the left panel to highlight it"));
    } else if (useAutoMaps) {
        p.setFont(m_labelFont);
        p.setPen(QColor(130, 110, 70, 180));
        p.drawText(QRect(4, topY + 4, plotW - 8, 18), Qt::AlignCenter,
                   tr("Auto-detected maps shown — import an A2L to replace them"));
    }
}

// ── Overview minimap ───────────────────────────────────────────────────────

void WaveformWidget::renderOverview(QPainter &p)
{
    if (m_overviewCache.isEmpty()) return;

    int w  = width();
    int oy = height() - kOverviewH;

    p.fillRect(0, oy, w, kOverviewH, AppConfig::instance().colors.waveOverviewBg);
    p.setPen(QPen(QColor(28, 50, 80, 120), 1));
    p.drawLine(0, oy, w, oy);

    // Waveform bars (batch to QImage)
    {
        const int ow = qMin(w, m_overviewCache.size());
        QImage ovImg(ow, kOverviewH, QImage::Format_ARGB32_Premultiplied);
        ovImg.fill(0);
        for (int px = 0; px < ow; px++) {
            const float pct = m_overviewCache[px];
            const int bh = (int)(pct * (kOverviewH - 4));
            if (bh <= 0) continue;
            const QColor c = valColor(pct);
            const uint32_t argb = qPremultiply(qRgba(c.red(), c.green(), c.blue(), c.alpha()));
            const int yStart = kOverviewH - 2 - bh;
            for (int y = yStart; y < yStart + bh && y < kOverviewH; y++)
                reinterpret_cast<uint32_t*>(ovImg.scanLine(y))[px] = argb;
        }
        p.drawImage(0, oy, ovImg);
    }

    // Map ticks
    int tv = totalValues();
    // Prefer real maps; only show auto-detected ticks as a fallback.
    const QVector<MapInfo> &tickMaps = !m_maps.isEmpty() ? m_maps : m_autoMaps;
    const bool tickIsAuto = m_maps.isEmpty() && !m_autoMaps.isEmpty();
    if (tv > 0 && !tickMaps.isEmpty()) {
        const uint32_t romSzOv = (uint32_t)m_data.size();
        for (const MapInfo &mi : tickMaps) {
            if (mi.length <= 0) continue;
            if (mi.address + mi.mapDataOffset + (uint32_t)mi.length > romSzOv) continue;
            if (!tickIsAuto && mi.linkConfidence == 40) continue;
            bool cur = !tickIsAuto && m_hasCurrentMap && (mi == m_currentMap);
            int px1 = (int)((double)(mi.address / m_cellSize) / tv * w);
            int px2 = (int)((double)((mi.address + mi.mapDataOffset + (uint32_t)mi.length - 1) / m_cellSize) / tv * w);
            px1 = qBound(0, px1, w - 1);
            px2 = qBound(px1, px2, w - 1);
            QColor tc = cur ? QColor(58, 220, 255, 230)
                            : (tickIsAuto ? QColor(220, 170, 60, 110)
                                          : QColor(58, 145, 208, 110));
            p.fillRect(px1, oy, qMax(1, px2 - px1), cur ? 4 : 2, tc);
        }
    }

    // Viewport indicator
    if (tv > 0) {
        int plotW = w - kYAxisW;
        int vis   = (int)(plotW * m_valsPerPx);
        int si    = m_scrollOffset / m_cellSize;
        int vl    = (int)((double)si  / tv * w);
        int vw    = qMax(4, (int)((double)vis / tv * w));

        p.fillRect(0,       oy, vl,           kOverviewH, QColor(0,0,0,120));
        p.fillRect(vl + vw, oy, w - vl - vw, kOverviewH, QColor(0,0,0,120));
        p.setPen(QPen(QColor(58, 145, 208), 1.5));
        p.drawRect(vl, oy, vw, kOverviewH - 1);
    }
}

// ── Mouse ──────────────────────────────────────────────────────────────────

void WaveformWidget::mousePressEvent(QMouseEvent *event)
{
    // Stop kinetic scroll on any click
    m_scrollVelocity = 0.0;
    m_kineticTimer->stop();

    int oy = height() - kOverviewH;
    m_pressPos = event->pos();
    m_didDrag  = false;

    // Right-click → context menu
    if (event->button() == Qt::RightButton) {
        showContextMenu(event->pos());
        return;
    }

    if (event->pos().y() >= oy) {
        // Overview click
        m_isOverviewDrag = true;
        int tv = totalValues();
        int plotW = width() - kYAxisW;
        int vis = (int)(plotW * m_valsPerPx);
        double pct = (double)event->pos().x() / width();
        m_scrollOffset = ((int)(pct * tv) - vis / 2) * m_cellSize;
        clampScroll();
        update();
        emit scrollSynced(m_scrollOffset);
    } else if (event->pos().y() > kControlsH && event->pos().x() < width() - kYAxisW) {
        if (event->button() == Qt::MiddleButton) {
            // Middle-click → pan (old left-click behavior)
            m_dragStartX      = event->pos().x();
            m_dragStartOffset = m_scrollOffset;
        } else if (event->button() == Qt::LeftButton
                   && (event->modifiers() & Qt::ShiftModifier)
                   && m_editor && !m_data.isEmpty()) {
            // Shift+left-click → free-form drawing mode
            int32_t off = offsetAtX(event->pos().x());
            if (off >= 0) {
                double val = yToVal(event->pos().y());
                m_editor->beginDraw(off, val);
                m_isDrawing = true;
                setCursor(Qt::CrossCursor);
                event->accept();
                return;
            }
        } else if (event->button() == Qt::LeftButton) {
            // Left-click → start range selection
            int32_t off = offsetAtX(event->pos().x());
            if (off >= 0) {
                m_selStart = off;
                m_selEnd   = off;
                m_isSelecting = true;
                setCursor(Qt::IBeamCursor);
                update();
            }
        }
    }
}

void WaveformWidget::mouseMoveEvent(QMouseEvent *event)
{
    // Free-form drawing drag
    if (m_isDrawing && m_editor && (event->buttons() & Qt::LeftButton)) {
        int32_t off = offsetAtX(event->pos().x());
        if (off >= 0) {
            double val = yToVal(event->pos().y());
            // Clamp to cell's valid range
            double maxV = (double)maxVal();
            val = qBound(0.0, val, maxV);
            m_editor->continueDraw(off, val);
            invalidatePixCache();
            update();
        }
        event->accept();
        return;
    }

    // Range selection drag
    if (m_isSelecting && (event->buttons() & Qt::LeftButton)) {
        int32_t off = offsetAtX(event->pos().x());
        if (off >= 0) {
            m_selEnd = off;
            update();
        }
        // Auto-scroll when dragging near edges
        int plotW = width() - kYAxisW;
        if (event->pos().x() < 20) {
            int scrollVals = qMax(1, (int)(plotW * m_valsPerPx * 0.02));
            m_scrollOffset -= scrollVals * m_cellSize;
            clampScroll();
            invalidatePixCache();
            emit scrollSynced(m_scrollOffset);
        } else if (event->pos().x() > plotW - 20) {
            int scrollVals = qMax(1, (int)(plotW * m_valsPerPx * 0.02));
            m_scrollOffset += scrollVals * m_cellSize;
            clampScroll();
            invalidatePixCache();
            emit scrollSynced(m_scrollOffset);
        }
        return;
    }

    // Middle-button pan
    if (!m_isDragging && !m_isOverviewDrag
            && (event->buttons() & Qt::MiddleButton)
            && event->pos().y() > kControlsH
            && (event->pos() - m_pressPos).manhattanLength() > 5) {
        m_isDragging = true;
        m_didDrag    = true;
        setCursor(Qt::ClosedHandCursor);
    }

    if (m_isDragging) {
        int dx = m_dragStartX - event->pos().x();
        m_scrollOffset = m_dragStartOffset + (int)(dx * m_valsPerPx) * m_cellSize;
        clampScroll();
        invalidatePixCache();
        m_scrollTimer->start();
        emit scrollSynced(m_scrollOffset);
        return;
    }
    if (m_isOverviewDrag) {
        int tv = totalValues();
        int plotW = width() - kYAxisW;
        int vis = (int)(plotW * m_valsPerPx);
        double pct = (double)event->pos().x() / width();
        m_scrollOffset = ((int)(pct * tv) - vis / 2) * m_cellSize;
        clampScroll();
        invalidatePixCache();
        m_scrollTimer->start();
        emit scrollSynced(m_scrollOffset);
        return;
    }

    // Hover tracking
    int plotW = width() - kYAxisW;
    int x = event->pos().x();
    if (x >= 0 && x < plotW && event->pos().y() > kControlsH) {
        int startIdx = m_scrollOffset / m_cellSize;
        int valIdx   = startIdx + (int)(x * m_valsPerPx);
        m_hoverOffset = valIdx * m_cellSize;
        if (m_hoverOffset >= m_data.size()) m_hoverOffset = -1;
    } else {
        m_hoverOffset = -1;
    }

    // Update cursor info label (cheap text update, no repaint needed)
    if (m_hoverOffset >= 0 && m_hoverOffset < m_data.size()) {
        uint32_t val = readValue(m_hoverOffset);
        if (m_cursorInfo)
            m_cursorInfo->setText(
                QString("0x%1  =  %2  (0x%3)")
                    .arg(m_hoverOffset, 8, 16, QChar('0')).toUpper()
                    .arg(val)
                    .arg(val, m_cellSize * 2, 16, QChar('0')).toUpper());
    }

    // Map curve mode: repaint for hover crosshair. ROM mode: pixmap cache is valid, skip.
    if (m_hasCurrentMap) {
        m_hoverCol = mapColAtX(x);
        update();
    }
}

void WaveformWidget::mouseReleaseEvent(QMouseEvent *event)
{
    // End free-form drawing
    if (m_isDrawing) {
        if (m_editor) m_editor->endDraw();
        m_isDrawing = false;
        setCursor(Qt::CrossCursor);
        event->accept();
        return;
    }

    if (m_isSelecting) {
        m_isSelecting = false;
        setCursor(Qt::CrossCursor);
        // If selection is too tiny (just a click), clear it and treat as map click
        if (selectionLength() < m_cellSize * 2) {
            m_selStart = m_selEnd = -1;
            // Single click → open map if hit
            if (event->pos().y() > kControlsH
                    && event->pos().y() < height() - kOverviewH
                    && event->pos().x() < width() - kYAxisW) {
                if (const MapInfo *hit = mapAtX(event->pos().x()))
                    emit mapClicked(*hit);
            }
        } else {
            emit selectionChanged(selectionStart(), selectionEnd());
        }
        update();
        return;
    }

    const bool wasDrag = m_isDragging;
    if (m_isDragging) {
        m_isDragging = false;
        setCursor(Qt::CrossCursor);
    }
    m_isOverviewDrag = false;

    // Single click (no drag) in plot area → open map overlay if a map was hit
    if (!wasDrag && !m_didDrag
            && event->pos().y() > kControlsH
            && event->pos().y() < height() - kOverviewH
            && event->pos().x() < width() - kYAxisW) {
        if (const MapInfo *hit = mapAtX(event->pos().x()))
            emit mapClicked(*hit);
    }
}

void WaveformWidget::wheelEvent(QWheelEvent *event)
{
    int plotW = width() - kYAxisW;
    int vis = (int)(plotW * m_valsPerPx);

    if (!event->pixelDelta().isNull()) {
        // Smooth trackpad input (macOS): apply pixel delta directly.
        // The OS already handles momentum/deceleration after finger lift —
        // running our own kinetic timer on top causes the two to fight,
        // producing jitter and overshoot. Stop any in-flight kinetic animation
        // and let the OS-provided momentum events do the work instead.
        m_kineticTimer->stop();
        m_scrollVelocity = 0.0;

        // Use whichever axis carries more movement. Horizontal swipes give x,
        // vertical two-finger scrolls give y — both are valid for a horizontal
        // waveform (matches typical macOS timeline/scrolling behaviour).
        int px = event->pixelDelta().x();
        int py = event->pixelDelta().y();
        int raw = (qAbs(px) >= qAbs(py)) ? px : py;

        // Negate: positive delta (fingers moving down/right) = move right in ROM.
        m_scrollOffset -= raw;
        clampScroll();
        invalidatePixCache();
        update();
        emit scrollSynced(m_scrollOffset);
        event->accept();
        return;
    }

    // Mouse wheel (angleDelta only): use kinetic animation for smooth deceleration.
    double rawDelta = event->angleDelta().y() / 120.0 * 40.0;
    double impulse = -rawDelta * (vis / 400.0) * 0.6;

    // Decay old velocity if there's a gap between wheel events
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastWheelMs > 0 && (now - m_lastWheelMs) > 200)
        m_scrollVelocity *= 0.3;   // large gap — dampen leftover momentum
    m_lastWheelMs = now;

    // Accumulate: same-direction flicks build speed, opposite direction brakes
    if ((m_scrollVelocity > 0) == (impulse > 0))
        m_scrollVelocity = m_scrollVelocity * 0.7 + impulse;
    else
        m_scrollVelocity = impulse;  // direction reversal — snap to new direction

    // Cap maximum velocity to keep things controllable
    double maxVel = vis * 4.0;
    m_scrollVelocity = qBound(-maxVel, m_scrollVelocity, maxVel);

    // Apply an immediate small jump so the first frame isn't empty
    if (qAbs(impulse) > 0.001) {
        int immediateDelta = qMax(1, (int)(qAbs(impulse) * 0.016));
        m_scrollOffset += (impulse > 0 ? immediateDelta : -immediateDelta) * m_cellSize;
    }
    clampScroll();
    invalidatePixCache();
    update();
    emit scrollSynced(m_scrollOffset);

    // Start kinetic animation
    if (!m_kineticTimer->isActive())
        m_kineticTimer->start();
}

void WaveformWidget::syncScrollTo(int scrollOffset)
{
    if (m_scrollOffset == scrollOffset) return; // no change
    m_scrollVelocity = 0.0;
    m_kineticTimer->stop();
    m_scrollOffset = scrollOffset;
    clampScroll();
    // Use stale pixel cache for immediate repaint — schedule async recompute later.
    // This keeps sync scrolling smooth instead of blocking on pixel stats recalc.
    update();
    scheduleAsyncRecompute();
    // deliberately does NOT re-emit scrollSynced — prevents feedback loops
}

void WaveformWidget::setComparisonData(const QByteArray &data)
{
    m_comparisonData = data;
    m_pixCmpAvg.clear();    // clear stale comparison averages immediately
    invalidatePixCache();   // clears render cache, marks pixel stats dirty, starts async timer
    update();
}

void WaveformWidget::syncZoomTo(int sliderValue)
{
    if (!m_zoomSlider) return;
    if (m_zoomSlider->value() == sliderValue) return;
    QSignalBlocker blk(m_zoomSlider);   // do not re-emit zoomSynced
    m_zoomSlider->setValue(sliderValue);
    updateZoomFromSlider(sliderValue);
    clampScroll();
    invalidatePixCache();
    update();
}

void WaveformWidget::setShowOriginalDiffOverlay(bool on)
{
    if (m_showOriginalDiff == on) return;
    m_showOriginalDiff = on;
    update();
}

void WaveformWidget::setAnnotationStore(AnnotationStore *store)
{
    if (m_annotations == store) return;
    if (m_annotations) disconnect(m_annotations, nullptr, this, nullptr);
    m_annotations = store;
    if (m_annotations)
        connect(m_annotations, &AnnotationStore::changed,
                this, [this]() { update(); });
    setMouseTracking(true);
    update();
}

bool WaveformWidget::event(QEvent *evt)
{
    if (evt->type() == QEvent::ToolTip && m_annotations
        && !m_data.isEmpty() && m_cellSize > 0) {
        auto *he = static_cast<QHelpEvent *>(evt);
        const int x = he->pos().x();
        if (x >= kYAxisW) {
            const int xRel = x - kYAxisW;
            const int valIdx = m_scrollOffset + static_cast<int>(xRel * m_valsPerPx);
            const qint64 byteIdx = static_cast<qint64>(valIdx) * m_cellSize;

            // Find the closest annotation to this byte (within 4 px tolerance)
            const double tolBytes = 4.0 * m_valsPerPx * m_cellSize;
            QStringList parts;
            for (const Annotation &a : m_annotations->all()) {
                const bool covers = byteIdx >= a.addr
                                    && byteIdx < a.addr + a.length;
                const bool near   = qAbs<qint64>(byteIdx - a.addr) <= tolBytes;
                if (!covers && !near) continue;
                QString tag = a.isMarker() ? tr("Marker") : tr("Comment");
                QString head = QString("<b>%1 @ 0x%2</b>")
                                   .arg(tag)
                                   .arg(a.addr, 8, 16, QChar('0')).toUpper();
                if (a.length > 1) head += QString(" (+%1 B)").arg(a.length);
                parts << head;
                if (!a.text.isEmpty()) parts << a.text.toHtmlEscaped();
            }
            if (!parts.isEmpty()) {
                QToolTip::showText(he->globalPos(), parts.join("<br>"), this);
                return true;
            }
            QToolTip::hideText();
        }
    }
    return QWidget::event(evt);
}

void WaveformWidget::renderAnnotations(QPainter &p)
{
    if (!m_annotations) return;
    if (m_annotations->all().isEmpty()) return;
    if (m_data.isEmpty() || m_cellSize <= 0) return;
    const int w = width();
    const int h = height();
    const int plotW = w - kYAxisW - 4;
    if (plotW <= 0) return;
    const int topY = kControlsH;
    const int botY = h - kXAxisH - kOverviewH;
    if (botY <= topY) return;

    p.save();
    for (const Annotation &a : m_annotations->all()) {
        const int valIdx = static_cast<int>(a.addr / m_cellSize);
        const int relIdx = valIdx - m_scrollOffset;
        if (relIdx < 0) continue;
        const double xd = relIdx / m_valsPerPx;
        if (xd < 0 || xd > plotW) continue;
        const int x = kYAxisW + static_cast<int>(xd);

        // Vertical pin line from top to bottom of plot area.
        const QColor col = a.isMarker() ? QColor(0xff, 0xc8, 0x4c)
                                        : QColor(0x4c, 0xb8, 0xff);
        p.setPen(QPen(col, 1.5, Qt::DashLine));
        p.drawLine(x, topY, x, botY);

        // Pin head at the top.
        p.setPen(Qt::NoPen);
        p.setBrush(col);
        p.drawEllipse(QPoint(x, topY + 4), 5, 5);

        // Length-range bar (thicker line at the bottom across covered values).
        if (a.length > 1) {
            const int valLen = qMax<qint64>(1, (a.length + m_cellSize - 1)
                                                  / m_cellSize);
            const int relEnd = relIdx + valLen;
            const double xeD = relEnd / m_valsPerPx;
            const int xe = kYAxisW + static_cast<int>(qMin((double)plotW, xeD));
            p.setPen(QPen(col, 3));
            p.drawLine(x, botY - 2, xe, botY - 2);
        }
    }
    p.restore();
}

void WaveformWidget::leaveEvent(QEvent *)
{
    m_hoverOffset = -1;
    m_hoverCol    = -1;
    if (m_cursorInfo)
        m_cursorInfo->setText(tr("Scroll or drag to explore ROM"));
    update();
}

void WaveformWidget::retranslateUi()
{
    const int sizeIdx  = m_cellSizeCombo->currentIndex();
    const int orderIdx = m_byteOrderCombo->currentIndex();

    m_cellSizeCombo->blockSignals(true);
    m_cellSizeCombo->clear();
    m_cellSizeCombo->addItem(tr("8-bit"),  1);
    m_cellSizeCombo->addItem(tr("16-bit"), 2);
    m_cellSizeCombo->setCurrentIndex(sizeIdx);
    m_cellSizeCombo->blockSignals(false);

    m_byteOrderCombo->blockSignals(true);
    m_byteOrderCombo->clear();
    m_byteOrderCombo->addItem(tr("Big Endian"),    (int)ByteOrder::BigEndian);
    m_byteOrderCombo->addItem(tr("Little Endian"), (int)ByteOrder::LittleEndian);
    m_byteOrderCombo->setCurrentIndex(orderIdx);
    m_byteOrderCombo->blockSignals(false);

    if (m_cursorInfo)
        m_cursorInfo->setText(tr("Scroll or drag to explore ROM"));
}

// ── Y → value conversion (reverse of valToY in renderRomWaveform) ──────────

double WaveformWidget::yToVal(int y) const
{
    if (m_plotHeight <= 0) return 0.0;
    double fraction = 1.0 - (double)(y - m_plotTopY) / m_plotHeight;
    return m_visLo + fraction * (m_visHi - m_visLo);
}

// ── Copy selection values as CSV to clipboard ──────────────────────────────

void WaveformWidget::copySelectionToClipboard()
{
    if (!hasSelection() || m_data.isEmpty()) return;
    QStringList vals;
    int start = (int)selectionStart();
    int end   = (int)selectionEnd();
    for (int off = start; off < end && off + m_cellSize <= m_data.size(); off += m_cellSize) {
        if (m_editor)
            vals << QString::number(m_editor->readValue(off));
        else
            vals << QString::number(readValue((uint32_t)off));
    }
    QApplication::clipboard()->setText(vals.join(QStringLiteral(",")));
}

void WaveformWidget::clearSelection()
{
    if (!hasSelection()) return;
    m_selStart = -1;
    m_selEnd = -1;
    update();
}

// ── Keyboard shortcuts ─────────────────────────────────────────────────────

void WaveformWidget::keyPressEvent(QKeyEvent *e)
{
    const auto mods = e->modifiers();
    const bool ctrl  = mods & Qt::ControlModifier;
    const bool shift = mods & Qt::ShiftModifier;

    // ── +/- increment (Key_Plus is Shift+= on US keyboards) ──────────
    if (e->key() == Qt::Key_Plus) {
        if (m_editor && hasSelection()) {
            m_editor->increment(selectionStart(), selectionEnd(), 1);
            e->accept();
            return;
        }
    }
    if (e->key() == Qt::Key_Minus && !ctrl) {
        if (m_editor && hasSelection()) {
            m_editor->increment(selectionStart(), selectionEnd(), -1);
            e->accept();
            return;
        }
    }

    // ── = (bare, no Shift) → set absolute value dialog ───────────────
    if (e->key() == Qt::Key_Equal && !shift && !ctrl) {
        if (m_editor && hasSelection()) {
            bool ok = false;
            double val = QInputDialog::getDouble(
                this, tr("Set Value"),
                tr("Set all selected cells to:"),
                0.0, -2147483648.0, 2147483647.0, 2, &ok);
            if (ok) {
                m_editor->setAbsolute(selectionStart(), selectionEnd(), val);
            }
            e->accept();
            return;
        }
    }

    // ── Ctrl+Z / Ctrl+Shift+Z undo/redo ──────────────────────────────
    if (e->key() == Qt::Key_Z && ctrl) {
        if (m_project || m_editor) {
            if (shift)
                m_project ? m_project->redoRomEdit() : m_editor->redo();
            else
                m_project ? m_project->undoRomEdit() : m_editor->undo();
            e->accept();
            return;
        }
    }
    if (e->key() == Qt::Key_Y && ctrl) {
        if (m_project || m_editor) {
            m_project ? m_project->redoRomEdit() : m_editor->redo();
            e->accept();
            return;
        }
    }

    // ── Delete → zero selection ──────────────────────────────────────
    if (e->key() == Qt::Key_Delete) {
        if (m_editor && hasSelection()) {
            m_editor->setAbsolute(selectionStart(), selectionEnd(), 0);
            e->accept();
            return;
        }
    }

    // ── Ctrl+A → select all ──────────────────────────────────────────
    if (e->key() == Qt::Key_A && ctrl) {
        if (!m_data.isEmpty()) {
            m_selStart = 0;
            m_selEnd = m_data.size() - 1;
            emit selectionChanged(selectionStart(), selectionEnd());
            update();
            e->accept();
            return;
        }
    }

    // ── Ctrl+C → copy ────────────────────────────────────────────────
    if (e->key() == Qt::Key_C && ctrl) {
        if (hasSelection()) {
            copySelectionToClipboard();
            e->accept();
            return;
        }
    }

    // ── Ctrl+V → paste comma/space-separated values into selection ───
    if (e->key() == Qt::Key_V && ctrl) {
        if (m_editor && hasSelection()) {
            QString text = QApplication::clipboard()->text().trimmed();
            if (!text.isEmpty()) {
                // Split on comma or whitespace
                QStringList tokens = text.split(QRegularExpression("[,\\s]+"),
                                                Qt::SkipEmptyParts);
                int start = (int)selectionStart();
                int end   = (int)selectionEnd();
                if (!tokens.isEmpty()) {
                    m_editor->beginDraw(start, tokens[0].toDouble());
                    for (int i = 1; i < tokens.size(); i++) {
                        int off = start + i * m_cellSize;
                        if (off + m_cellSize > end && off + m_cellSize > m_data.size())
                            break;
                        m_editor->continueDraw(off, tokens[i].toDouble());
                    }
                    m_editor->endDraw();
                }
            }
            e->accept();
            return;
        }
    }

    // ── Arrow keys: adjust selection or scroll ───────────────────────
    if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right) {
        if (hasSelection()) {
            if (e->key() == Qt::Key_Left) {
                if (shift) {
                    // Shift+Left: expand/shrink left boundary
                    m_selStart -= m_cellSize;
                    if (m_selStart < 0) m_selStart = 0;
                } else {
                    // Left: shrink selection from the right
                    m_selEnd -= m_cellSize;
                    if (m_selEnd <= m_selStart) {
                        m_selStart = -1;
                        m_selEnd = -1;
                    }
                }
            } else { // Key_Right
                if (shift) {
                    // Shift+Right: expand right boundary
                    m_selEnd += m_cellSize;
                    if (m_selEnd >= m_data.size()) m_selEnd = m_data.size() - 1;
                } else {
                    // Right: expand selection to the right
                    m_selEnd += m_cellSize;
                    if (m_selEnd >= m_data.size()) m_selEnd = m_data.size() - 1;
                }
            }
            if (hasSelection())
                emit selectionChanged(selectionStart(), selectionEnd());
            update();
        } else {
            // No selection: scroll by one cell width
            if (e->key() == Qt::Key_Left)
                m_scrollOffset -= m_cellSize;
            else
                m_scrollOffset += m_cellSize;
            clampScroll();
            invalidatePixCache();
            update();
            emit scrollSynced(m_scrollOffset);
        }
        e->accept();
        return;
    }

    // ── Home / End ───────────────────────────────────────────────────
    if (e->key() == Qt::Key_Home) {
        if (shift && hasSelection()) {
            // Shift+Home: extend selection to byte 0
            m_selStart = 0;
            emit selectionChanged(selectionStart(), selectionEnd());
        } else {
            // Home: scroll to beginning, clear selection
            m_selStart = -1;
            m_selEnd = -1;
            m_scrollOffset = 0;
            clampScroll();
            invalidatePixCache();
            emit scrollSynced(m_scrollOffset);
        }
        update();
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_End) {
        if (shift && hasSelection()) {
            // Shift+End: extend selection to last byte
            m_selEnd = m_data.size() - 1;
            emit selectionChanged(selectionStart(), selectionEnd());
        } else {
            // End: scroll to end, clear selection
            m_selStart = -1;
            m_selEnd = -1;
            int tv = totalValues();
            int plotW = width() - kYAxisW;
            int visibleVals = (int)(plotW * m_valsPerPx);
            m_scrollOffset = qMax(0, tv - visibleVals) * m_cellSize;
            clampScroll();
            invalidatePixCache();
            emit scrollSynced(m_scrollOffset);
        }
        update();
        e->accept();
        return;
    }

    // ── Page Up / Page Down: scroll by one viewport width ────────────
    if (e->key() == Qt::Key_PageUp || e->key() == Qt::Key_PageDown) {
        int plotW = width() - kYAxisW;
        int visibleVals = (int)(plotW * m_valsPerPx);
        int pageBytes = visibleVals * m_cellSize;
        if (e->key() == Qt::Key_PageUp)
            m_scrollOffset -= pageBytes;
        else
            m_scrollOffset += pageBytes;
        clampScroll();
        invalidatePixCache();
        update();
        emit scrollSynced(m_scrollOffset);
        e->accept();
        return;
    }

    QWidget::keyPressEvent(e);
}

// ── Modified cells overlay (orange highlight for edited bytes) ─────────────

void WaveformWidget::renderModifiedOverlay(QPainter &p)
{
    if (m_originalData.isEmpty() || m_data.isEmpty()) return;
    if (m_originalData.size() != m_data.size()) return;

    const int w     = width();
    const int plotW = w - kYAxisW;
    const int plotH = m_plotHeight;
    const int topY  = m_plotTopY;

    if (plotW <= 0 || plotH <= 0) return;
    if (m_pixAvg.size() < plotW) return;

    const double vpp      = m_valsPerPx;
    const int startIdx    = m_scrollOffset / m_cellSize;
    const int tv          = totalValues();
    const double lo       = m_visLo;
    const double rng      = m_visHi - m_visLo;
    if (rng <= 0.0) return;

    auto valToY = [&](double v) -> int {
        return topY + plotH - (int)((v - lo) / rng * plotH);
    };

    // Collect modified pixel segments for the thick orange overlay line.
    // Sprint B: when "Diff vs Original" overlay is on, swap orange for
    // bright red and increase line weight so it stands out vs the regular
    // waveform — same visual key the hex/3D widgets use.
    const QColor modColor = m_showOriginalDiff
                                ? QColor(0xff, 0x55, 0x55)   // red
                                : QColor(0xf0, 0x88, 0x3e);  // orange
    const qreal modWeight = m_showOriginalDiff ? 3.5 : 2.5;
    p.setRenderHint(QPainter::Antialiasing, true);

    bool prevModified = false;
    for (int px = 0; px < plotW; px++) {
        const int bs = vpp >= 1 ? startIdx + (int)(px * vpp)
                                : startIdx + (int)(px / (1.0 / vpp));
        if (bs >= tv) break;

        // Check if any cell in this pixel column is modified
        const int be = vpp >= 1 ? startIdx + (int)((px + 1) * vpp) : bs + 1;
        bool modified = false;
        for (int vi = bs; vi < be && vi < tv; vi++) {
            int off = vi * m_cellSize;
            if (off + m_cellSize > m_data.size()) break;
            if (off + m_cellSize > m_originalData.size()) break;
            if (memcmp(m_data.constData() + off, m_originalData.constData() + off, m_cellSize) != 0) {
                modified = true;
                break;
            }
        }

        if (modified && px > 0 && prevModified) {
            p.setPen(QPen(modColor, modWeight));
            p.drawLine(QPointF(px - 1, valToY(m_pixAvg[px - 1])),
                       QPointF(px,     valToY(m_pixAvg[px])));
        } else if (modified && px > 0) {
            // First modified pixel after unmodified — draw a dot
            p.setPen(QPen(modColor, modWeight));
            p.drawPoint(QPointF(px, valToY(m_pixAvg[px])));
        }
        prevModified = modified;
    }

    p.setRenderHint(QPainter::Antialiasing, false);
}

void WaveformWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    m_overviewCache.clear();
    buildOverviewCache();
    updateZoomFromSlider(m_zoomSlider->value());
}
