/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hexwidget.h"
#include "project.h"
#include "appconfig.h"
#include "annotations/AnnotationStore.h"
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QScrollBar>
#include <QFontMetrics>
#include <QContextMenuEvent>
#include <QHelpEvent>
#include <QToolTip>
#include <QMenu>
#include <QApplication>
#include <QClipboard>
#include <QInputDialog>
#include <QRegularExpression>
#include <cmath>

static const int kBarW = 48;

HexWidget::HexWidget(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    m_monoFont = QFont("Consolas", 10);
    m_monoFont.setStyleHint(QFont::Monospace);
    setFont(m_monoFont);

    QFontMetrics fm(m_monoFont);
    m_byteWidth = fm.horizontalAdvance("FF") + 6;
    m_asciiCharWidth = fm.horizontalAdvance('W');
    m_offsetWidth = fm.horizontalAdvance("0x00000000") + 20;

    viewport()->setFocusPolicy(Qt::StrongFocus);
    setFocusPolicy(Qt::StrongFocus);

    setMouseTracking(true);
    viewport()->setMouseTracking(true);

    // Vertical overview minimap on the right edge
    m_overviewBar = new HexOverviewBar(this);
    // Reserve space on the right for the overview bar
    setViewportMargins(0, 0, kBarW, 0);

    connect(m_overviewBar, &HexOverviewBar::scrollRequested, this, [this](int row) {
        verticalScrollBar()->setValue(qBound(0, row, verticalScrollBar()->maximum()));
    });

    connect(verticalScrollBar(), &QScrollBar::valueChanged, m_overviewBar,
            QOverload<>::of(&QWidget::update));

    // Cross-project scroll synchronisation: emit the byte offset of the
    // topmost visible row whenever the user scrolls.  MainWindow listens
    // and fans out to other open hex views (mirror of waveform sync).
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this](int row) {
                if (m_bytesPerRow <= 0) return;
                emit scrollSynced(row * m_bytesPerRow);
            });

    connect(&AppConfig::instance(), &AppConfig::colorsChanged,
            viewport(), QOverload<>::of(&QWidget::update));

    connect(&m_blinkTimer, &QTimer::timeout, this, [this]() {
        if (m_blinkTicksRemaining > 0) {
            m_blinkTicksRemaining--;
            m_blinkHighlightState = !m_blinkHighlightState;
            viewport()->update();
            if (m_overviewBar) m_overviewBar->update();
        } else {
            m_blinkTimer.stop();
            m_blinkHighlightState = false;
            viewport()->update();
        }
    });
}

void HexWidget::syncScrollTo(int byteOffset)
{
    if (m_bytesPerRow <= 0) return;
    const int row = byteOffset / m_bytesPerRow;
    auto *bar = verticalScrollBar();
    QSignalBlocker block(bar);                  // don't re-emit scrollSynced
    bar->setValue(qBound(0, row, bar->maximum()));
    viewport()->update();
    if (m_overviewBar) m_overviewBar->update();
}

void HexWidget::setShowOriginalDiffOverlay(bool on)
{
    if (m_showOriginalDiff == on) return;
    m_showOriginalDiff = on;
    viewport()->update();
}

void HexWidget::loadData(const QByteArray &data, uint32_t baseAddress)
{
    m_data         = data;
    m_originalData = data;
    m_baseAddress  = baseAddress;
    m_modifications.clear();
    m_selectedOffset = -1;
    m_editing = false;
    m_selectionStart = -1;
    m_selectionEnd = -1;
    m_undoStack.clear();
    m_undoIndex = -1;
    m_overviewBar->rebuild();
    updateScrollBar();
    viewport()->update();
}

void HexWidget::loadData(const QByteArray &data, const QByteArray &original,
                         uint32_t baseAddress)
{
    loadData(data, baseAddress);
    if (!original.isEmpty() && original.size() == data.size()) {
        m_originalData = original;
        // Re-derive modifications from the actual original baseline.
        m_modifications.clear();
        const int n = qMin(m_data.size(), m_originalData.size());
        for (int i = 0; i < n; ++i) {
            if (static_cast<uint8_t>(m_data[i])
                != static_cast<uint8_t>(m_originalData[i]))
                m_modifications.insert(i);
        }
        emit dataModified(m_modifications.size());
        viewport()->update();
    }
}

void HexWidget::refreshData(const QByteArray &data)
{
    m_data = data;
    // Recompute modifications against the stable original baseline so the
    // diff-vs-original overlay reflects edits made through other widgets
    // (WaveformEditor, savepoint switch, etc.).
    m_modifications.clear();
    const int n = qMin(m_data.size(), m_originalData.size());
    for (int i = 0; i < n; ++i) {
        if (static_cast<uint8_t>(m_data[i]) != static_cast<uint8_t>(m_originalData[i]))
            m_modifications.insert(i);
    }
    emit dataModified(m_modifications.size());
    m_overviewBar->rebuild();
    viewport()->update();
}

void HexWidget::setAnnotationStore(AnnotationStore *store)
{
    if (m_annotations == store) return;
    if (m_annotations) disconnect(m_annotations, nullptr, this, nullptr);
    m_annotations = store;
    if (m_annotations)
        connect(m_annotations, &AnnotationStore::changed,
                this, [this]() { viewport()->update(); });
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    viewport()->update();
}

bool HexWidget::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip && m_annotations) {
        auto *he = static_cast<QHelpEvent *>(event);
        const int relY = he->pos().y() - m_headerHeight;
        if (relY >= 0) {
            const int row = verticalScrollBar()->value() + relY / m_rowHeight;
            const int rowStart = row * m_bytesPerRow;
            // Pick the first annotation that starts in this row OR whose
            // range covers the byte under the cursor (column-aware).
            int col = -1;
            const int xRel = he->pos().x() - m_offsetWidth;
            if (xRel >= 0 && m_byteWidth > 0)
                col = qBound(0, xRel / m_byteWidth, m_bytesPerRow - 1);
            const qint64 byteIdx = (col >= 0) ? rowStart + col : rowStart;
            QStringList parts;
            for (const Annotation &a : m_annotations->all()) {
                const bool inRow = a.addr >= rowStart
                                   && a.addr < rowStart + m_bytesPerRow;
                const bool covers = (col >= 0)
                                    && byteIdx >= a.addr
                                    && byteIdx < a.addr + a.length;
                if (!inRow && !covers) continue;
                QString tag = a.isMarker() ? tr("Marker") : tr("Comment");
                QString head = QString("<b>%1 @ 0x%2</b>")
                                   .arg(tag)
                                   .arg(a.addr, 8, 16, QChar('0')).toUpper();
                if (a.length > 1) head += QString(" (+%1 B)").arg(a.length);
                parts << head;
                if (!a.text.isEmpty()) parts << a.text.toHtmlEscaped();
            }
            if (parts.isEmpty()) {
                int mapIdx = mapRegionForOffset(byteIdx);
                if (mapIdx >= 0 && mapIdx < m_mapRegions.size()) {
                    const auto &mr = m_mapRegions[mapIdx];
                    QString mapToolTip = QString("<b>Map: %1</b><br>Span: 0x%2 – 0x%3 (%4 bytes)")
                        .arg(mr.name.toHtmlEscaped())
                        .arg(m_baseAddress + mr.start, 8, 16, QChar('0')).toUpper()
                        .arg(m_baseAddress + mr.start + mr.length - 1, 8, 16, QChar('0')).toUpper()
                        .arg(mr.length);
                    parts << mapToolTip;
                }
            }
            if (!parts.isEmpty()) {
                QToolTip::showText(he->globalPos(), parts.join("<br>"), this);
                return true;
            }
            QToolTip::hideText();
        }
    }
    return QAbstractScrollArea::event(event);
}

void HexWidget::setBaseAddress(uint32_t baseAddress)
{
    m_baseAddress = baseAddress;
    viewport()->update();
}

void HexWidget::setFontSize(int pt)
{
    pt = qBound(7, pt, 24);
    m_monoFont.setPointSize(pt);
    setFont(m_monoFont);

    QFontMetrics fm(m_monoFont);
    const int hexChars = 2 * m_groupSize;
    m_byteWidth     = fm.horizontalAdvance(QString(hexChars, 'F')) + 8;
    m_asciiCharWidth = fm.horizontalAdvance('W');
    m_offsetWidth   = fm.horizontalAdvance("0x00000000") + 20;
    m_rowHeight     = fm.height() + 6;
    m_headerHeight  = m_rowHeight + 2;

    updateScrollBar();
    viewport()->update();
}

void HexWidget::setDisplayParams(int cellSize, ByteOrder bo, int displayFmt, bool isSigned)
{
    m_groupSize  = qBound(1, cellSize, 4);
    m_byteOrder  = bo;
    m_displayFmt = displayFmt;
    m_isSigned   = isSigned;

    // Recalculate cell width: each group shows 2*groupSize hex digits
    QFontMetrics fm(m_monoFont);
    const int hexChars = 2 * m_groupSize;
    m_byteWidth = fm.horizontalAdvance(QString(hexChars, 'F')) + 8;

    updateScrollBar();
    viewport()->update();
}

QByteArray HexWidget::getData() const { return m_data; }
QByteArray HexWidget::getOriginalData() const { return m_originalData; }
QByteArray HexWidget::exportBinary() const { return m_data; }

void HexWidget::goToAddress(uint32_t offset)
{
    if (m_data.isEmpty() || (int)offset >= m_data.size()) return;
    m_selectedOffset = offset;
    clearSelection();
    int row = offset / m_bytesPerRow;
    verticalScrollBar()->setValue(row);
    viewport()->update();
}

void HexWidget::selectRange(uint32_t offset, int length)
{
    if (m_data.isEmpty() || length <= 0 || (int)offset >= m_data.size()) return;

    const int start = static_cast<int>(offset);
    const int end = qMin(start + length - 1, m_data.size() - 1);
    m_selectedOffset = start;
    m_selectionStart = start;
    m_selectionEnd = end;
    const int startRow = start / m_bytesPerRow;
    const int endRow = end / m_bytesPerRow;
    const int centerRow = (startRow + endRow) / 2;
    const int visibleRows = (viewport()->height() - m_headerHeight) / m_rowHeight;
    const int targetRow = qMax(0, centerRow - (visibleRows / 2));
    verticalScrollBar()->setValue(targetRow);
    viewport()->update();
}

void HexWidget::setMapRegions(const QVector<MapRegion> &regions)
{
    m_mapRegions = regions;
    m_overviewBar->rebuild();
    viewport()->update();
}

void HexWidget::setActiveMapAddress(uint32_t address)
{
    if (m_activeMapAddress == address) return;
    m_activeMapAddress = address;
    viewport()->update();
    if (m_overviewBar) m_overviewBar->update();
}

void HexWidget::setActiveMap(const MapInfo &map)
{
    m_activeMapInfo = map;
    m_activeMapAddress = map.address;
    triggerMapBlinkAnimation();
    if (m_overviewBar) m_overviewBar->update();
}

void HexWidget::triggerMapBlinkAnimation()
{
    m_blinkTicksRemaining = 6;
    m_blinkHighlightState = true;
    viewport()->update();
    if (!m_blinkTimer.isActive()) {
        m_blinkTimer.start(65);
    }
}

void HexWidget::setComparisonData(const QByteArray &data)
{
    m_comparisonData = data;
    viewport()->update();
}

void HexWidget::clearSelection()
{
    m_selectionStart = -1;
    m_selectionEnd = -1;
}

void HexWidget::pushUndo(int offset, const QByteArray &before, const QByteArray &after)
{
    // Discard any redo entries beyond current position
    if (m_undoIndex + 1 < m_undoStack.size())
        m_undoStack.resize(m_undoIndex + 1);

    m_undoStack.append({offset, before, after});

    // Enforce stack limit
    if (m_undoStack.size() > kMaxUndo) {
        m_undoStack.removeFirst();
    }
    m_undoIndex = m_undoStack.size() - 1;
}

void HexWidget::updateModifications(int offset, int length)
{
    for (int i = offset; i < offset + length && i < m_data.size(); ++i) {
        if (i < m_originalData.size() && (uint8_t)m_data[i] != (uint8_t)m_originalData[i])
            m_modifications.insert(i);
        else
            m_modifications.remove(i);
    }
    emit dataModified(m_modifications.size());
    emit bytesModified(offset, length);
}

void HexWidget::updateScrollBar()
{
    if (m_data.isEmpty()) return;
    int totalRows = (m_data.size() + m_bytesPerRow - 1) / m_bytesPerRow;
    int visibleRows = (viewport()->height() - m_headerHeight) / m_rowHeight;
    verticalScrollBar()->setRange(0, qMax(0, totalRows - visibleRows));
    verticalScrollBar()->setPageStep(visibleRows);
    verticalScrollBar()->setSingleStep(1);
}

void HexWidget::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);

    // Position the overview bar on the right side, full height
    int barX = width() - kBarW - verticalScrollBar()->width();
    m_overviewBar->setGeometry(barX, 0, kBarW, height());
    m_overviewBar->rebuild();
    m_overviewBar->show();

    updateScrollBar();
}

int HexWidget::mapRegionForOffset(uint32_t offset) const
{
    for (int i = 0; i < m_mapRegions.size(); i++) {
        const auto &r = m_mapRegions[i];
        if (offset >= r.start && offset < r.start + (uint32_t)r.length)
            return i;
    }
    return -1;
}

// Returns the rect for group index g (not byte index)
QRect HexWidget::byteRect(int g, int y) const
{
    const int halfGroups = 8 / m_groupSize;   // separator after first half
    int x = m_offsetWidth + g * m_byteWidth;
    if (g >= halfGroups) x += m_separatorWidth;
    return QRect(x, y, m_byteWidth, m_rowHeight);
}

void HexWidget::paintEvent(QPaintEvent *)
{
    QPainter p(viewport());
    p.setFont(m_monoFont);

    int w = viewport()->width();
    int h = viewport()->height();

    // Background
    p.fillRect(0, 0, w, h, AppConfig::instance().colors.hexBg);

    if (m_data.isEmpty()) {
        // Welcome screen
        p.setPen(QColor(58, 145, 208));
        QFont bigFont = m_monoFont;
        bigFont.setPointSize(48);
        bigFont.setBold(true);
        p.setFont(bigFont);
        p.drawText(QRect(0, 0, w, h - 60), Qt::AlignCenter, "RX14");
        p.setFont(m_monoFont);
        p.setPen(QColor(107, 127, 163));
        p.drawText(QRect(0, h/2, w, 40), Qt::AlignCenter, "ECU Calibration Suite — Open a ROM file to begin");
        return;
    }

    int startRow = verticalScrollBar()->value();
    int visibleRows = (h - m_headerHeight) / m_rowHeight + 2;

    // Column header
    p.fillRect(0, 0, w, m_headerHeight, AppConfig::instance().colors.hexHeaderBg);
    QColor headerTextCol = AppConfig::instance().colors.hexHeaderText; headerTextCol.setAlpha(150);
    p.setPen(headerTextCol);
    QFont headerFont = m_monoFont;
    headerFont.setPointSize(9);
    p.setFont(headerFont);
    p.drawText(QRect(0, 0, m_offsetWidth, m_headerHeight), Qt::AlignRight | Qt::AlignVCenter, "Offset  ");
    const int groups = m_bytesPerRow / m_groupSize;
    for (int g = 0; g < groups; g++) {
        QRect r = byteRect(g, 0);
        r.setHeight(m_headerHeight);
        p.drawText(r, Qt::AlignCenter,
                   QString("%1").arg(g * m_groupSize, 2, 16, QChar('0')).toUpper());
    }
    int asciiX = m_offsetWidth + groups * m_byteWidth + m_separatorWidth + 12;
    const QString sidebarLabel = (m_sidebarMode == SidebarMode::Bars) ? "Bars" : "ASCII";
    p.drawText(QRect(asciiX, 0, 100, m_headerHeight), Qt::AlignLeft | Qt::AlignVCenter, sidebarLabel);
    p.setFont(m_monoFont);

    // Border under header
    QColor borderCol = AppConfig::instance().colors.hexHeaderText; borderCol.setAlpha(38);
    p.setPen(borderCol);
    p.drawLine(0, m_headerHeight, w, m_headerHeight);

    // Data rows
    for (int i = 0; i < visibleRows; i++) {
        int row = startRow + i;
        int y = m_headerHeight + i * m_rowHeight;
        if (y > h) break;
        renderRow(p, row, y);
    }

    // Draw active map border trace box & title banner overlay
    drawMapOverlayBox(p);
    drawHoveredMapOverlay(p);
}

void HexWidget::drawMapOverlayBox(QPainter &p)
{
    if (m_activeMapAddress == 0 && m_activeMapInfo.address == 0 && m_activeMapInfo.length <= 0)
        return;

    const uint32_t startOff = m_activeMapInfo.address > 0 ? m_activeMapInfo.address : m_activeMapAddress;
    const int mapLen = m_activeMapInfo.length > 0 ? m_activeMapInfo.length : 1;
    const uint32_t endOff = startOff + mapLen - 1;

    if (m_bytesPerRow <= 0 || m_rowHeight <= 0) return;

    const int scrollRow = verticalScrollBar()->value();
    const int vpH = viewport()->height();
    const int visibleRows = (vpH - m_headerHeight) / m_rowHeight + 2;
    const int startVisibleRow = scrollRow;
    const int endVisibleRow = scrollRow + visibleRows;

    const int startRow = startOff / m_bytesPerRow;
    const int endRow = endOff / m_bytesPerRow;

    if (endRow < startVisibleRow || startRow > endVisibleRow)
        return; // map overlay outside current viewport

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    QColor traceColor = m_blinkHighlightState ? QColor(255, 215, 0) : QColor(245, 60, 70);
    int traceWidth = m_blinkHighlightState ? 3 : 2;
    QPen outlinePen(traceColor, traceWidth, Qt::SolidLine);
    p.setPen(outlinePen);

    // 1. Draw Row-by-Row Cell Contour Outline Boxes
    int mapMinY = INT_MAX;
    int mapMaxY = -1;

    for (int row = qMax(startRow, startVisibleRow); row <= qMin(endRow, endVisibleRow); ++row) {
        uint32_t rowStart = row * m_bytesPerRow;
        uint32_t rowEnd = rowStart + m_bytesPerRow - 1;

        uint32_t activeRowStart = qMax(startOff, rowStart);
        uint32_t activeRowEnd = qMin(endOff, rowEnd);

        if (activeRowStart > activeRowEnd) continue;

        int g1 = (activeRowStart % m_bytesPerRow) / m_groupSize;
        int g2 = (activeRowEnd % m_bytesPerRow) / m_groupSize;

        int y = m_headerHeight + (row - scrollRow) * m_rowHeight;
        if (y < mapMinY) mapMinY = y;
        if (y + m_rowHeight > mapMaxY) mapMaxY = y + m_rowHeight;

        QRect r1 = byteRect(g1, y);
        QRect r2 = byteRect(g2, y);

        int leftX = r1.left() - 1;
        int rightX = r2.right() + 1;

        // Draw left & right vertical border lines for this row's map cell segment
        p.drawLine(leftX, y, leftX, y + m_rowHeight);
        p.drawLine(rightX, y, rightX, y + m_rowHeight);

        // Draw top border line if row above is outside map or starts further right
        if (row == startRow) {
            p.drawLine(leftX, y, rightX, y);
        } else if (row > startRow) {
            uint32_t prevRowStart = (row - 1) * m_bytesPerRow;
            uint32_t prevRowActiveStart = qMax(startOff, prevRowStart);
            int prevG1 = (prevRowActiveStart % m_bytesPerRow) / m_groupSize;
            int prevLeftX = byteRect(prevG1, 0).left() - 1;
            if (leftX < prevLeftX) {
                p.drawLine(leftX, y, prevLeftX, y);
            }
        }

        // Draw bottom border line if row below is outside map or ends further left
        if (row == endRow) {
            p.drawLine(leftX, y + m_rowHeight, rightX, y + m_rowHeight);
        } else if (row < endRow) {
            uint32_t nextRowEnd = (row + 1) * m_bytesPerRow + m_bytesPerRow - 1;
            uint32_t nextRowActiveEnd = qMin(endOff, nextRowEnd);
            int nextG2 = (nextRowActiveEnd % m_bytesPerRow) / m_groupSize;
            int nextRightX = byteRect(nextG2, 0).right() + 1;
            if (rightX > nextRightX) {
                p.drawLine(nextRightX, y + m_rowHeight, rightX, y + m_rowHeight);
            }
        }
    }

    // 2. Draw Left-Aligned In-Grid Active Map Title Label Header Banner
    const int bits = (m_activeMapInfo.dataSize > 0 ? m_activeMapInfo.dataSize : 1) * 8;
    const QString dimStr = (m_activeMapInfo.dimensions.x > 0 && m_activeMapInfo.dimensions.y > 0)
        ? QString("%1x%2 (%3 Bit)").arg(m_activeMapInfo.dimensions.x).arg(m_activeMapInfo.dimensions.y).arg(bits)
        : QString("%1 Bit").arg(bits);

    const QString mapTitle = m_activeMapInfo.name.isEmpty()
        ? QString("Map @ 0x%1: %2").arg(m_baseAddress + startOff, 8, 16, QChar('0')).toUpper().arg(dimStr)
        : QString("%1: %2").arg(m_activeMapInfo.name).arg(dimStr);

    QFont labelFont = m_monoFont;
    labelFont.setPointSize(8);
    labelFont.setBold(true);
    p.setFont(labelFont);
    QFontMetrics fm(labelFont);

    // Position left-aligned inside top-left cell of active map
    int gStart = (startOff % m_bytesPerRow) / m_groupSize;
    int topRowY = m_headerHeight + (startRow - scrollRow) * m_rowHeight;
    QRect rTopLeft = byteRect(gStart, topRowY);

    int bannerX = rTopLeft.left() + 2;
    int bannerY = qBound(m_headerHeight + 2, topRowY + 2, vpH - 22);

    int availWidth = qMin(420, viewport()->width() - bannerX - 20);
    if (availWidth > 80 && mapMaxY > m_headerHeight) {
        QString displayText = fm.elidedText(mapTitle, Qt::ElideRight, availWidth - 12);
        int textWidth = fm.horizontalAdvance(displayText) + 14;
        int bannerW = qMin(textWidth, availWidth);
        int bannerH = 16;

        QRect bannerRect(bannerX, bannerY, bannerW, bannerH);

        // WinOLS red banner fill with subtle rounded border
        p.fillRect(bannerRect, QColor(190, 25, 35, 235));
        p.setPen(QPen(QColor(245, 60, 70), 1.0));
        p.drawRoundedRect(bannerRect, 2, 2);

        p.setPen(Qt::white);
        p.drawText(bannerRect, Qt::AlignCenter, displayText);
    }

    p.restore();
}

void HexWidget::drawHoveredMapOverlay(QPainter &p)
{
    if (m_hoveredMapIndex < 0 || m_hoveredMapIndex >= m_mapRegions.size())
        return;

    const MapRegion &mr = m_mapRegions[m_hoveredMapIndex];
    const uint32_t startOff = mr.start;
    const uint32_t endOff = mr.start + (mr.length > 0 ? mr.length - 1 : 0);

    // Skip if this is the currently active map (already drawn in red/gold)
    const uint32_t activeOff = m_activeMapInfo.address > 0 ? m_activeMapInfo.address : m_activeMapAddress;
    if (activeOff > 0 && startOff == activeOff)
        return;

    if (m_bytesPerRow <= 0 || m_rowHeight <= 0) return;

    const int scrollRow = verticalScrollBar()->value();
    const int vpH = viewport()->height();
    const int visibleRows = (vpH - m_headerHeight) / m_rowHeight + 2;
    const int startVisibleRow = scrollRow;
    const int endVisibleRow = scrollRow + visibleRows;

    const int startRow = startOff / m_bytesPerRow;
    const int endRow = endOff / m_bytesPerRow;

    if (endRow < startVisibleRow || startRow > endVisibleRow)
        return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    // WinOLS Purple Hover Contour Trace Box
    QPen purplePen(QColor(170, 70, 240), 2, Qt::SolidLine);
    p.setPen(purplePen);

    struct RowSegment {
        int row;
        int leftX, rightX;
        int topY, botY;
    };
    QVector<RowSegment> segments;
    int mapMaxY = -1;

    for (int row = qMax(startRow, startVisibleRow); row <= qMin(endRow, endVisibleRow); ++row) {
        uint32_t rowStart = row * m_bytesPerRow;
        uint32_t rowEnd = rowStart + m_bytesPerRow - 1;

        if (endOff < rowStart || startOff > rowEnd)
            continue;

        uint32_t segStart = qMax(startOff, rowStart);
        uint32_t segEnd   = qMin(endOff, rowEnd);

        int g1 = (segStart % m_bytesPerRow) / m_groupSize;
        int g2 = (segEnd % m_bytesPerRow) / m_groupSize;

        int screenY = m_headerHeight + (row - scrollRow) * m_rowHeight;
        QRect r1 = byteRect(g1, screenY);
        QRect r2 = byteRect(g2, screenY);

        int leftX  = r1.left() - 2;
        int rightX = r2.right() + 2;
        int topY   = screenY - 1;
        int botY   = screenY + m_rowHeight;

        if (botY > mapMaxY) mapMaxY = botY;
        segments.append({row, leftX, rightX, topY, botY});
    }

    for (int i = 0; i < segments.size(); ++i) {
        const auto &s = segments[i];

        // Left and Right outer vertical edges
        p.drawLine(s.leftX, s.topY, s.leftX, s.botY);
        p.drawLine(s.rightX, s.topY, s.rightX, s.botY);

        // Top horizontal edge for first row
        if (i == 0 || s.row != segments[i - 1].row + 1) {
            p.drawLine(s.leftX, s.topY, s.rightX, s.topY);
        } else {
            // Step-in / step-out connector between row i-1 and row i
            const auto &prev = segments[i - 1];
            if (s.leftX != prev.leftX) p.drawLine(prev.leftX, s.topY, s.leftX, s.topY);
            if (s.rightX != prev.rightX) p.drawLine(prev.rightX, s.topY, s.rightX, s.topY);
        }

        // Bottom horizontal edge for last row
        if (i == segments.size() - 1 || segments[i + 1].row != s.row + 1) {
            p.drawLine(s.leftX, s.botY, s.rightX, s.botY);
        }
    }

    // WinOLS Purple Hover Title Banner
    int bits = 16;
    int dimX = 0, dimY = 0;
    if (m_project) {
        for (const MapInfo &mi : m_project->maps) {
            if (mi.address == startOff) {
                dimX = mi.dimensions.x;
                dimY = mi.dimensions.y;
                if (mi.dataSize > 0) bits = mi.dataSize * 8;
                break;
            }
        }
    }

    const QString dimStr = (dimX > 0 && dimY > 0)
        ? QString("%1x%2 (%3 Bit)").arg(dimX).arg(dimY).arg(bits)
        : QString("%1 Bit").arg(bits);

    const QString mapTitle = mr.name.isEmpty()
        ? QString("Map @ 0x%1: %2").arg(m_baseAddress + startOff, 8, 16, QChar('0')).toUpper().arg(dimStr)
        : QString("%1: %2").arg(mr.name).arg(dimStr);

    QFont labelFont = m_monoFont;
    labelFont.setPointSize(8);
    labelFont.setBold(true);
    p.setFont(labelFont);
    QFontMetrics fm(labelFont);

    int gStart = (startOff % m_bytesPerRow) / m_groupSize;
    int topRowY = m_headerHeight + (startRow - scrollRow) * m_rowHeight;
    QRect rTopLeft = byteRect(gStart, topRowY);

    int bannerX = rTopLeft.left() + 2;
    int bannerY = qBound(m_headerHeight + 2, topRowY + 2, vpH - 22);

    int availWidth = qMin(420, viewport()->width() - bannerX - 20);
    if (availWidth > 80 && mapMaxY > m_headerHeight) {
        QString displayText = fm.elidedText(mapTitle, Qt::ElideRight, availWidth - 12);
        int textWidth = fm.horizontalAdvance(displayText) + 14;
        int bannerW = qMin(textWidth, availWidth);
        int bannerH = 16;

        QRect bannerRect(bannerX, bannerY, bannerW, bannerH);

        // WinOLS Purple fill & border
        p.fillRect(bannerRect, QColor(140, 40, 210, 235));
        p.setPen(QPen(QColor(180, 80, 250), 1.0));
        p.drawRoundedRect(bannerRect, 2, 2);

        p.setPen(Qt::white);
        p.drawText(bannerRect, Qt::AlignCenter, displayText);
    }

    p.restore();
}

void HexWidget::mouseMoveEvent(QMouseEvent *event)
{
    QAbstractScrollArea::mouseMoveEvent(event);
    int x = event->pos().x();
    int y = event->pos().y();

    if (y >= m_headerHeight) {
        int row = verticalScrollBar()->value() + (y - m_headerHeight) / m_rowHeight;
        int screenY = m_headerHeight + ((y - m_headerHeight) / m_rowHeight) * m_rowHeight;
        const int groups = m_bytesPerRow / m_groupSize;

        for (int g = 0; g < groups; g++) {
            QRect br = byteRect(g, screenY);
            if (x >= br.left() && x < br.right()) {
                uint32_t hoverOff = row * m_bytesPerRow + g * m_groupSize;
                if ((int)hoverOff < m_data.size()) {
                    int mapIdx = mapRegionForOffset(hoverOff);
                    if (mapIdx != m_hoveredMapIndex) {
                        m_hoveredMapIndex = mapIdx;
                        viewport()->update();
                    }
                    return;
                }
            }
        }
    }

    if (m_hoveredMapIndex != -1) {
        m_hoveredMapIndex = -1;
        viewport()->update();
    }
}

void HexWidget::leaveEvent(QEvent *event)
{
    QAbstractScrollArea::leaveEvent(event);
    if (m_hoveredMapIndex != -1) {
        m_hoveredMapIndex = -1;
        viewport()->update();
    }
}

void HexWidget::renderRow(QPainter &p, int row, int y)
{
    uint32_t offset = row * m_bytesPerRow;
    if ((int)offset >= m_data.size()) return;

    const uint8_t *raw = reinterpret_cast<const uint8_t*>(m_data.constData());

    // Precompute selection range
    const bool hasSel = hasSelection();
    const int sStart = hasSel ? selStart() : -1;
    const int sEnd   = hasSel ? selEnd()   : -1;

    // Offset column
    p.setPen(AppConfig::instance().colors.hexOffset);
    QString addr = QString("0x%1").arg(m_baseAddress + offset, 8, 16, QChar('0')).toUpper();
    p.drawText(QRect(0, y, m_offsetWidth, m_rowHeight), Qt::AlignRight | Qt::AlignVCenter, addr + "  ");

    // Sprint C — annotation glyph(s) in the offset gutter for any
    // annotation whose start address falls inside this row.
    if (m_annotations) {
        for (const Annotation &a : m_annotations->all()) {
            if (a.addr < (qint64)offset
                || a.addr >= (qint64)offset + m_bytesPerRow) continue;
            const QRect g(2, y + (m_rowHeight - 14) / 2, 14, 14);
            const bool isMarker = a.isMarker();
            p.setBrush(isMarker ? QColor(0xff, 0xc8, 0x4c)
                                : QColor(0x4c, 0xb8, 0xff));
            p.setPen(QPen(QColor(0, 0, 0, 200), 0.8));
            p.drawEllipse(g);
            p.setPen(Qt::black);
            QFont fnt = p.font();
            QFont small = fnt; small.setPointSize(7); small.setBold(true);
            p.setFont(small);
            p.drawText(g, Qt::AlignCenter,
                       isMarker ? QStringLiteral("M")
                                : QStringLiteral("✎"));
            p.setFont(fnt);
            p.setBrush(Qt::NoBrush);
            p.setPen(AppConfig::instance().colors.hexOffset);
            break;   // one glyph per row is enough
        }
    }

    // Bytes + ASCII
    QString asciiStr;
    const int groups = m_bytesPerRow / m_groupSize;
    int asciiX = m_offsetWidth + groups * m_byteWidth + m_separatorWidth + 12;

    for (int g = 0; g < groups; g++) {
        const int byteCol = g * m_groupSize;
        uint32_t  baseIdx = offset + byteCol;
        QRect br = byteRect(g, y);

        if ((int)baseIdx >= m_data.size()) {
            for (int b = 0; b < m_groupSize; b++) asciiStr += ' ';
            continue;
        }

        // Assemble multi-byte value respecting byte order
        const int bytesAvail = qMin(m_groupSize, (int)(m_data.size() - (int)baseIdx));
        uint32_t groupVal = 0;
        if (m_byteOrder == ByteOrder::BigEndian) {
            for (int b = 0; b < bytesAvail; b++)
                groupVal = (groupVal << 8) | raw[baseIdx + b];
        } else {
            for (int b = bytesAvail - 1; b >= 0; b--)
                groupVal = (groupVal << 8) | raw[baseIdx + b];
        }

        // Format display string
        QString cellStr;
        if (m_displayFmt == 1) { // decimal
            if (m_isSigned) {
                int32_t sv = (m_groupSize == 1) ? (int8_t)groupVal :
                             (m_groupSize == 2) ? (int16_t)groupVal : (int32_t)groupVal;
                cellStr = QString::number(sv);
            } else {
                cellStr = QString::number(groupVal);
            }
        } else if (m_displayFmt == 2) { // binary
            cellStr = QString("%1").arg(groupVal, 8 * m_groupSize, 2, QChar('0'));
        } else { // hex
            cellStr = QString("%1").arg(groupVal, 2 * m_groupSize, 16, QChar('0')).toUpper();
        }

        // Check if this group is selected, in range selection, or modified
        bool isSelected = false, isModified = false, isInSelection = false;
        for (int b = 0; b < bytesAvail; b++) {
            uint32_t idx = baseIdx + b;
            if ((int32_t)idx == m_selectedOffset) isSelected = true;
            if (m_modifications.contains(idx)) isModified = true;
            if (hasSel && (int)idx >= sStart && (int)idx <= sEnd) isInSelection = true;
        }

        // Background: map region
        int mapIdx = mapRegionForOffset(baseIdx);
        if (mapIdx >= 0 && mapIdx < m_mapRegions.size()) {
            const auto &mr = m_mapRegions[mapIdx];
            const bool isActiveMap = (mr.start == m_activeMapAddress);
            if (isActiveMap) {
                QColor activeCol = QColor(40, 120, 225, 80);
                p.fillRect(br, activeCol);
            } else {
                QColor mc = AppConfig::instance().colors.mapBand[mapIdx % 5]; mc.setAlpha(45);
                p.fillRect(br, mc);
            }
        }

        // Check if byte differs from comparison data
        bool isDiff = (int)baseIdx < m_comparisonData.size()
                      && m_data[baseIdx] != m_comparisonData[baseIdx];

        // Background: range selection highlight
        if (isInSelection && !isSelected) {
            p.fillRect(br, QColor(31, 111, 235, 64));
            p.setPen(AppConfig::instance().colors.hexText);
        }

        // Background: selected / editing (cursor takes priority)
        if (isSelected) {
            p.fillRect(br, AppConfig::instance().colors.hexSelected);
            p.setPen(Qt::white);
            if (m_editing) p.drawRect(br.adjusted(0, 0, -1, -1));
        } else if (isInSelection) {
            // pen already set above
        } else if (m_showOriginalDiff && isModified
                   && !m_originalData.isEmpty()) {
            // Sprint B — delta-magnitude colour ramp.  Compute |now-was|
            // for the first byte of the group (groups are 1..4 bytes, the
            // colour is per-cell so first byte is representative enough).
            const int origByte = (int)baseIdx < m_originalData.size()
                                     ? static_cast<uint8_t>(m_originalData[baseIdx])
                                     : 0;
            const int curByte  = static_cast<uint8_t>(m_data[baseIdx]);
            const int delta    = qAbs(curByte - origByte);
            QColor bg;
            QColor fg;
            if (delta <= 2)        { bg = QColor( 35, 145,  60, 180); fg = Qt::white; } // green
            else if (delta <= 16)  { bg = QColor(160, 175,  40, 190); fg = Qt::black; } // yellow-green
            else if (delta <= 64)  { bg = QColor(220, 160,  30, 200); fg = Qt::black; } // amber
            else                   { bg = QColor(220,  55,  55, 215); fg = Qt::white; } // red
            p.fillRect(br, bg);
            p.setPen(fg);
        } else if (isDiff) {
            // Side-by-side compare highlight.  Muted amber so dense-diff
            // regions don't drown out the rest of the view, but still
            // distinguishable from the default cell colour.
            p.fillRect(br, QColor(120,  75,  20, 160));
            p.setPen(QColor(255, 200, 110));
        } else if (isModified) {
            p.setPen(AppConfig::instance().colors.hexModified);
        } else {
            p.setPen(AppConfig::instance().colors.hexText);
        }

        p.drawText(br, Qt::AlignCenter, cellStr);

        // Collect ASCII chars for sidebar
        for (int b = 0; b < bytesAvail; b++) {
            uint8_t val = raw[baseIdx + b];
            asciiStr += (val >= 32 && val <= 126) ? (char)val : '.';
        }
    }

    // ── Sidebar: ASCII text or bar chart ─────────────────────────────────
    const int sidebarW = m_asciiCharWidth * m_bytesPerRow;

    if (m_sidebarMode == SidebarMode::Bars) {
        // Each byte -> a vertical bar whose height is proportional to its value (0-255)
        const int barW    = qMax(1, sidebarW / m_bytesPerRow);
        const int padV    = 2;                          // top/bottom padding
        const int areaH   = m_rowHeight - padV * 2;    // available bar height

        for (int i = 0; i < m_bytesPerRow; i++) {
            uint32_t idx = offset + i;
            if ((int)idx >= m_data.size()) break;

            uint8_t val = raw[idx];
            int barH = (val * areaH + 127) / 255;      // round to nearest pixel
            int bx   = asciiX + i * barW;
            int by   = y + padV + (areaH - barH);       // bars grow upward from bottom

            // Choose fill colour
            QColor fill = AppConfig::instance().colors.hexBarDefault;

            int mapIdx = mapRegionForOffset(idx);
            if (mapIdx >= 0)
                fill = AppConfig::instance().colors.mapBand[mapIdx % 5];

            if (m_modifications.contains(idx))
                fill = AppConfig::instance().colors.hexModified;

            // Compare highlight: byte differs from setComparisonData().
            // Bright amber so dense-diff regions remain visible even in
            // the slim bar-mode bands.
            if ((int)idx < m_comparisonData.size()
                && m_data[idx] != m_comparisonData[idx]) {
                fill = QColor(255, 145, 35, 220);
            }

            // Selection highlight in bar mode
            if (hasSel && (int)idx >= sStart && (int)idx <= sEnd)
                fill = QColor(31, 111, 235, 180);

            if ((int32_t)idx == m_selectedOffset)
                fill = QColor(88, 166, 255);             // blue for selected

            if (barH > 0)
                p.fillRect(bx, by, barW - 1, barH, fill);
        }
    } else {
        // ASCII text column - draw char by char for selection highlighting
        for (int i = 0; i < m_bytesPerRow; i++) {
            uint32_t idx = offset + i;
            if ((int)idx >= m_data.size()) break;

            int cx = asciiX + i * m_asciiCharWidth;
            QRect charRect(cx, y, m_asciiCharWidth, m_rowHeight);

            // Selection highlight in ASCII column
            if (hasSel && (int)idx >= sStart && (int)idx <= sEnd) {
                p.fillRect(charRect, QColor(31, 111, 235, 64));
            }
            if ((int32_t)idx == m_selectedOffset) {
                p.fillRect(charRect, QColor(31, 111, 235, 100));
            }

            p.setPen(AppConfig::instance().colors.hexOffset);
            QChar ch = (i < asciiStr.size()) ? asciiStr[i] : QChar('.');
            p.drawText(charRect, Qt::AlignLeft | Qt::AlignVCenter, QString(ch));
        }
    }
}

void HexWidget::mousePressEvent(QMouseEvent *event)
{
    if (m_data.isEmpty()) return;

    // Right-click is only for the context menu; never clear / move the
    // selection on press, otherwise "Selection → Map…" sees an empty
    // selection by the time `contextMenuEvent` fires.
    if (event->button() == Qt::RightButton) return;

    int x = event->pos().x();
    int y = event->pos().y();

    if (y < m_headerHeight) return;

    int row = verticalScrollBar()->value() + (y - m_headerHeight) / m_rowHeight;
    int screenY = m_headerHeight + ((y - m_headerHeight) / m_rowHeight) * m_rowHeight;
    const int groups = m_bytesPerRow / m_groupSize;

    // Check if click is in the bytes area
    for (int g = 0; g < groups; g++) {
        QRect br = byteRect(g, screenY);
        if (x >= br.left() && x < br.right()) {
            uint32_t clickOffset = row * m_bytesPerRow + g * m_groupSize;
            if ((int)clickOffset < m_data.size()) {
                if (event->modifiers() & Qt::ShiftModifier) {
                    // Shift+Click: extend selection
                    if (!hasSelection()) {
                        m_selectionStart = m_selectedOffset;
                    }
                    m_selectionEnd = clickOffset;
                    m_selectedOffset = clickOffset;
                } else {
                    // Normal click: clear selection, set cursor
                    clearSelection();
                    m_selectedOffset = clickOffset;
                }
                m_editing = false;
                m_editNibble = 0;
                emit offsetSelected(clickOffset, (uint8_t)m_data[clickOffset]);

                int mapIdx = mapRegionForOffset(clickOffset);
                if (mapIdx >= 0 && mapIdx < m_mapRegions.size()) {
                    emit mapClickedInHex(m_mapRegions[mapIdx].start);
                }

                viewport()->update();
            }
            return;
        }
    }
}

void HexWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    mousePressEvent(event);
    if (m_selectedOffset >= 0) {
        m_editing = true;
        m_editNibble = 0;
        m_editByteBefore = (uint8_t)m_data[m_selectedOffset];
        viewport()->update();
    }
}

void HexWidget::keyPressEvent(QKeyEvent *event)
{
    if (m_data.isEmpty() || m_selectedOffset < 0) {
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }

    uint8_t *raw = reinterpret_cast<uint8_t*>(m_data.data());

    // ── Ctrl+A: select all ───────────────────────────────────────────────
    if (event->matches(QKeySequence::SelectAll)) {
        m_selectionStart = 0;
        m_selectionEnd = m_data.size() - 1;
        viewport()->update();
        return;
    }

    // ── Ctrl+C: copy selected bytes as hex ───────────────────────────────
    if (event->matches(QKeySequence::Copy) && hasSelection()) {
        QString hex;
        for (int i = selStart(); i <= selEnd(); ++i)
            hex += QString("%1 ").arg((uint8_t)m_data[i], 2, 16, QChar('0')).toUpper();
        QApplication::clipboard()->setText(hex.trimmed());
        return;
    }

    // ── Ctrl+V: paste hex bytes ──────────────────────────────────────────
    if (event->matches(QKeySequence::Paste)) {
        QString text = QApplication::clipboard()->text().trimmed();
        QStringList tokens = text.split(QRegularExpression("\\s+"));
        int off = m_selectedOffset;
        int count = 0;
        for (const QString &t : tokens) {
            if (off + count >= m_data.size()) break;
            bool ok;
            uint val = t.toUInt(&ok, 16);
            if (!ok || val > 255) continue;
            ++count;
        }
        if (count == 0) return;

        // Snapshot before for undo
        QByteArray before = m_data.mid(off, count);

        // Apply paste
        int idx = off;
        for (const QString &t : tokens) {
            if (idx >= off + count) break;
            bool ok;
            uint val = t.toUInt(&ok, 16);
            if (!ok || val > 255) continue;
            m_data[idx] = (char)val;
            ++idx;
        }

        QByteArray after = m_data.mid(off, count);
        pushUndo(off, before, after);
        updateModifications(off, count);
        viewport()->update();
        return;
    }

    // ── Ctrl+Z: undo ────────────────────────────────────────────────────
    if (event->matches(QKeySequence::Undo)) {
        if (m_project) {
            m_project->undoRomEdit();
            return;
        }
        if (m_undoIndex >= 0 && m_undoIndex < m_undoStack.size()) {
            const HexUndoEntry &entry = m_undoStack[m_undoIndex];
            m_data.replace(entry.offset, entry.before.size(), entry.before);
            updateModifications(entry.offset, entry.before.size());
            m_selectedOffset = entry.offset;
            m_undoIndex--;
            viewport()->update();
        }
        return;
    }

    // ── Ctrl+Y / Ctrl+Shift+Z: redo ─────────────────────────────────────
    if (event->matches(QKeySequence::Redo)) {
        if (m_project) {
            m_project->redoRomEdit();
            return;
        }
        if (m_undoIndex + 1 < m_undoStack.size()) {
            m_undoIndex++;
            const HexUndoEntry &entry = m_undoStack[m_undoIndex];
            m_data.replace(entry.offset, entry.after.size(), entry.after);
            updateModifications(entry.offset, entry.after.size());
            m_selectedOffset = entry.offset;
            viewport()->update();
        }
        return;
    }

    // ── Ctrl+G: go to address ────────────────────────────────────────────
    if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_G) {
        bool ok;
        QString s = QInputDialog::getText(this, tr("Go to Address"),
            tr("Address (hex):"), QLineEdit::Normal, {}, &ok);
        if (ok && !s.isEmpty()) {
            if (s.startsWith("0x", Qt::CaseInsensitive)) s = s.mid(2);
            uint32_t addr = s.toUInt(nullptr, 16);
            goToAddress(addr);
        }
        return;
    }

    // ── Delete key: zero selected bytes ──────────────────────────────────
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        if (hasSelection()) {
            int s = selStart(), e = selEnd();
            int len = e - s + 1;
            QByteArray before = m_data.mid(s, len);
            for (int i = s; i <= e; ++i)
                m_data[i] = 0x00;
            QByteArray after = m_data.mid(s, len);
            pushUndo(s, before, after);
            updateModifications(s, len);
            clearSelection();
        } else {
            QByteArray before = m_data.mid(m_selectedOffset, 1);
            m_data[m_selectedOffset] = 0x00;
            QByteArray after = m_data.mid(m_selectedOffset, 1);
            pushUndo(m_selectedOffset, before, after);
            updateModifications(m_selectedOffset, 1);
        }
        viewport()->update();
        return;
    }

    // ── Nibble editing ───────────────────────────────────────────────────
    if (m_editing) {
        QString key = event->text().toLower();
        if (key.size() == 1 && QString("0123456789abcdef").contains(key[0])) {
            int nibbleVal = key[0].digitValue();
            if (nibbleVal < 0) nibbleVal = key[0].unicode() - 'a' + 10;

            uint8_t current = raw[m_selectedOffset];

            if (m_editNibble == 0) {
                raw[m_selectedOffset] = (nibbleVal << 4) | (current & 0x0F);
                m_editNibble = 1;
            } else {
                raw[m_selectedOffset] = (raw[m_selectedOffset] & 0xF0) | nibbleVal;
                m_editNibble = 0;
                m_editing = false;

                QByteArray before(1, (char)m_editByteBefore);
                QByteArray after(1, (char)raw[m_selectedOffset]);
                pushUndo(m_selectedOffset, before, after);
                updateModifications(m_selectedOffset, 1);

                // Move to next byte
                if (m_selectedOffset < m_data.size() - 1) {
                    m_selectedOffset++;
                }
            }
            viewport()->update();
            return;
        }

        if (event->key() == Qt::Key_Escape) {
            m_editing = false;
            m_editNibble = 0;
            viewport()->update();
            return;
        }
    }

    // ── Navigation (with optional Shift for selection) ───────────────────
    int delta = 0;
    switch (event->key()) {
    case Qt::Key_Right:  delta = 1; break;
    case Qt::Key_Left:   delta = -1; break;
    case Qt::Key_Down:   delta = m_bytesPerRow; break;
    case Qt::Key_Up:     delta = -m_bytesPerRow; break;
    case Qt::Key_PageDown: delta = m_bytesPerRow * 16; break;
    case Qt::Key_PageUp:   delta = -m_bytesPerRow * 16; break;
    case Qt::Key_Home:
        if (event->modifiers() & Qt::ShiftModifier) {
            if (!hasSelection()) m_selectionStart = m_selectedOffset;
            m_selectionEnd = 0;
        } else {
            clearSelection();
        }
        m_selectedOffset = 0;
        viewport()->update();
        return;
    case Qt::Key_End:
        if (event->modifiers() & Qt::ShiftModifier) {
            if (!hasSelection()) m_selectionStart = m_selectedOffset;
            m_selectionEnd = m_data.size() - 1;
        } else {
            clearSelection();
        }
        m_selectedOffset = m_data.size() - 1;
        viewport()->update();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        m_editing = true;
        m_editNibble = 0;
        m_editByteBefore = (uint8_t)m_data[m_selectedOffset];
        viewport()->update();
        return;
    default:
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }

    int32_t newOffset = m_selectedOffset + delta;
    if (newOffset >= 0 && newOffset < m_data.size()) {
        if (event->modifiers() & Qt::ShiftModifier) {
            // Shift+Arrow: extend selection
            if (!hasSelection()) {
                m_selectionStart = m_selectedOffset;
            }
            m_selectionEnd = newOffset;
        } else {
            // Normal arrow: clear selection
            clearSelection();
        }

        m_selectedOffset = newOffset;

        // Ensure visible
        int row = m_selectedOffset / m_bytesPerRow;
        int firstVisible = verticalScrollBar()->value();
        int visibleRows = (viewport()->height() - m_headerHeight) / m_rowHeight;
        if (row < firstVisible) verticalScrollBar()->setValue(row);
        else if (row >= firstVisible + visibleRows) verticalScrollBar()->setValue(row - visibleRows + 1);

        emit offsetSelected(m_selectedOffset, raw[m_selectedOffset]);
        viewport()->update();
    }
}

void HexWidget::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);

    const bool sel = hasSelection();

    // ── Selection → Map (WinOLS-style hex-dump map definition) ──
    QAction *actSelToMap = menu.addAction(tr("Selection → Map…"));
    actSelToMap->setEnabled(sel && (selEnd() - selStart() + 1) >= 2);
    connect(actSelToMap, &QAction::triggered, this, [this]() {
        const int s = selStart();
        const int e = selEnd();
        emit selectionToMapRequested(static_cast<uint32_t>(s), e - s + 1);
    });
    menu.addSeparator();

    // ── Edit selection — same ops as the global Selection menu and the
    //    waveform's right-click.  Op codes match MainWindow::EditOp so
    //    MainWindow can route them straight through applyEditOp() with
    //    a single dispatcher.
    {
        QMenu *editMenu = menu.addMenu(tr("Edit selection"));
        editMenu->setEnabled(sel);
        struct E { const char *label; int code; };
        const E entries[] = {
            { QT_TR_NOOP("Value +1"),                 0 },   // EditOp::ValuePlus1
            { QT_TR_NOOP("Value −1"),                 1 },   // EditOp::ValueMinus1
            { QT_TR_NOOP("Change absolute…"),         2 },
            { QT_TR_NOOP("Change relative…"),         3 },
            { QT_TR_NOOP("Change by slider…"),        4 },
            { QT_TR_NOOP("Round / limit…"),           5 },
            { QT_TR_NOOP("Restore original value"),   6 },
            { QT_TR_NOOP("Interpolate"),              7 },
            { QT_TR_NOOP("Smooth"),                   8 },
            { QT_TR_NOOP("Flatten"),                  9 },
        };
        for (const E &e : entries) {
            QAction *a = editMenu->addAction(tr(e.label));
            const int code = e.code;
            connect(a, &QAction::triggered, this,
                    [this, code]() { emit editOpRequested(code); });
        }
        menu.addSeparator();
    }

    QAction *actAscii = menu.addAction(tr("ASCII view"));
    QAction *actBars  = menu.addAction(tr("Bar view"));
    actAscii->setCheckable(true);
    actBars ->setCheckable(true);
    actAscii->setChecked(m_sidebarMode == SidebarMode::ASCII);
    actBars ->setChecked(m_sidebarMode == SidebarMode::Bars);

    QAction *chosen = menu.exec(event->globalPos());
    if      (chosen == actAscii) m_sidebarMode = SidebarMode::ASCII;
    else if (chosen == actBars)  m_sidebarMode = SidebarMode::Bars;
    else return;

    viewport()->update();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  HexOverviewBar — vertical minimap next to the scrollbar
// ═══════════════════════════════════════════════════════════════════════════════

HexOverviewBar::HexOverviewBar(HexWidget *parent)
    : QWidget(parent), m_hex(parent)
{
    setFixedWidth(kBarW);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
}

void HexOverviewBar::rebuild()
{
    const QByteArray &data = m_hex->romData();
    int h = qMax(height(), 100);
    m_cache.resize(h);

    if (data.isEmpty()) { m_cache.fill(0); update(); return; }

    const int dataSize = data.size();
    const uint8_t *raw = reinterpret_cast<const uint8_t *>(data.constData());

    for (int py = 0; py < h; py++) {
        int byteStart = (int)((double)py / h * dataSize);
        int byteEnd   = (int)((double)(py + 1) / h * dataSize);
        byteEnd = qMin(byteEnd, dataSize);
        if (byteStart >= byteEnd) { m_cache[py] = 0; continue; }

        uint8_t mx = 0;
        int step = qMax(1, (byteEnd - byteStart) / 32);
        for (int i = byteStart; i < byteEnd; i += step)
            if (raw[i] > mx) mx = raw[i];
        m_cache[py] = mx / 255.0f;
    }
    update();
}

int HexOverviewBar::rowAtY(int y) const
{
    const QByteArray &data = m_hex->romData();
    if (data.isEmpty()) return 0;
    int totalRows = (data.size() + m_hex->bytesPerRow() - 1) / m_hex->bytesPerRow();
    double pct = (double)y / height();
    int visibleRows = (m_hex->viewport()->height() - m_hex->headerHeight()) / m_hex->rowHeight();
    return (int)(pct * totalRows) - visibleRows / 2;
}

void HexOverviewBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    int w = width();
    int h = height();

    // Background
    p.fillRect(0, 0, w, h, QColor(0x0d, 0x11, 0x17));

    // Left border
    p.setPen(QColor(0x21, 0x26, 0x2d));
    p.drawLine(0, 0, 0, h);

    const QByteArray &data = m_hex->romData();
    if (data.isEmpty() || m_cache.isEmpty()) return;

    // Intensity bars (horizontal, one per pixel row)
    int ch = qMin(h, m_cache.size());
    for (int py = 0; py < ch; py++) {
        float pct = m_cache[py];
        if (pct <= 0.01f) continue;
        int bw = (int)(pct * (w - 4));
        if (bw <= 0) continue;

        int r, g, b;
        if (pct < 0.5f) {
            r = (int)(25 + pct * 2 * 30);
            g = (int)(50 + pct * 2 * 130);
            b = (int)(100 + pct * 2 * 100);
        } else {
            r = (int)(55 + (pct - 0.5f) * 2 * 200);
            g = (int)(180 - (pct - 0.5f) * 2 * 40);
            b = (int)(200 - (pct - 0.5f) * 2 * 160);
        }
        p.fillRect(2, py, bw, 1, QColor(r, g, b, 160));
    }

    // Map region ticks (vertical marks on left edge)
    const auto &regions = m_hex->mapRegions();
    if (!regions.isEmpty()) {
        double romSz = (double)data.size();
        for (const MapRegion &mr : regions) {
            int py1 = (int)((double)mr.start / romSz * h);
            int py2 = (int)(((double)mr.start + mr.length) / romSz * h);
            py1 = qBound(0, py1, h - 1);
            py2 = qBound(py1 + 1, py2, h);
            const bool isActive = (mr.start == m_hex->activeMapAddress());
            if (isActive) {
                p.fillRect(0, py1, 6, qMax(2, py2 - py1), QColor(255, 215, 0, 255));
            } else {
                p.fillRect(0, py1, 3, qMax(1, py2 - py1), QColor(58, 220, 255, 180));
            }
        }
    }

    // Viewport indicator
    int totalRows = (data.size() + m_hex->bytesPerRow() - 1) / m_hex->bytesPerRow();
    if (totalRows > 0) {
        int vpH = m_hex->viewport()->height();
        int visibleRows = (vpH - m_hex->headerHeight()) / m_hex->rowHeight();
        int startRow = m_hex->verticalScrollBar()->value();

        int vy = (int)((double)startRow / totalRows * h);
        int vh = qMax(4, (int)((double)visibleRows / totalRows * h));

        // Darken areas outside viewport
        p.fillRect(0, 0, w, vy, QColor(0, 0, 0, 80));
        p.fillRect(0, vy + vh, w, h - vy - vh, QColor(0, 0, 0, 80));

        // Viewport frame
        p.setPen(QPen(QColor(58, 145, 208), 1.5));
        p.drawRect(1, vy, w - 2, vh);
    }
}

void HexOverviewBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        emit scrollRequested(rowAtY(event->pos().y()));
    }
}

void HexOverviewBar::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging)
        emit scrollRequested(rowAtY(event->pos().y()));
}

void HexOverviewBar::mouseReleaseEvent(QMouseEvent *)
{
    m_dragging = false;
}
