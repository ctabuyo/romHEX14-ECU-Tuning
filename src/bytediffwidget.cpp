/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "bytediffwidget.h"
#include "romcompare.h"

#include <QPainter>
#include <QScrollBar>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QFontMetrics>

namespace {

// ── Pre-computed lookup tables ──────────────────────────────────────────────
// Building 256 short QStrings once at startup lets paintEvent skip per-cell
// QString allocation entirely (the dominant cost on the previous implementation).
QString HEX2[256];
QString ASCII1[256];

void initLookupsOnce()
{
    static bool ready = false;
    if (ready) return;
    static const char H[] = "0123456789ABCDEF";
    for (int i = 0; i < 256; ++i) {
        const QChar buf[2] = { QChar::fromLatin1(H[(i >> 4) & 0xF]),
                               QChar::fromLatin1(H[i & 0xF]) };
        HEX2[i] = QString(buf, 2);
        ASCII1[i] = (i >= 0x20 && i < 0x7f)
                    ? QString(QChar::fromLatin1((char)i))
                    : QStringLiteral(".");
    }
    ready = true;
}

// Cached colors — Qt copies QColor on setPen anyway, but constructing once
// saves the QColor::QColor(int,int,int) work per cell.
const QColor BG_COL          (0x0d, 0x11, 0x17);
const QColor DIVIDER_COL     (0x21, 0x26, 0x2d);
const QColor HEADER_COL      (0x58, 0xa6, 0xff);
const QColor ADDR_COL        (0x48, 0x4f, 0x58);
const QColor TEXT_COL        (0xc9, 0xd1, 0xd9);
const QColor DIFF_TEXT_COL   (0xff, 0xb0, 0xa8);  // bright salmon — pops on red bg
const QColor DIFF_BG         (0xf8, 0x51, 0x49, 110);  // vivid red overlay
const QColor ONLY_BG         (0xff, 0x88, 0x33, 110);  // vivid amber for orphan bytes
const QColor ROW_MARKER_COL  (0xf8, 0x51, 0x49);       // solid stripe in addr column

} // namespace

// ── ByteDiffWidget ──────────────────────────────────────────────────────────

ByteDiffWidget::ByteDiffWidget(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    initLookupsOnce();

    m_font = QFont("Consolas", 9);
    m_font.setFixedPitch(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    viewport()->setStyleSheet("background: #0d1117;");
    recomputeMetrics();
}

void ByteDiffWidget::setData(const QByteArray &ref, const QByteArray &cmp)
{
    m_ref = ref;
    m_cmp = cmp;
    const ByteDiffSummary s = RomCompare::diffBytesSummary(ref, cmp);
    m_diffCount       = s.count;
    m_firstDiffOffset = s.firstOffset;
    m_firstRow = 0;
    m_lastEmittedOffset = -1;
    updateScrollbar();
    viewport()->update();
}

void ByteDiffWidget::goToFirstDiff()
{
    if (m_firstDiffOffset < 0) return;
    const int row = (int)(m_firstDiffOffset / m_cols);
    verticalScrollBar()->setValue(row);
}

void ByteDiffWidget::goToOffset(qint64 offset)
{
    if (offset < 0) return;
    const int row = (int)(offset / m_cols);
    verticalScrollBar()->setValue(qMax(0, row - 1)); // -1 = small lead-in padding
}

void ByteDiffWidget::recomputeMetrics()
{
    const QFontMetrics fm(m_font);
    m_charW    = fm.horizontalAdvance(QLatin1Char('F'));
    m_rowH     = fm.height() + 2;
    m_baseline = fm.ascent() + 1;

    // Dual pane = half the viewport, minus 1px center divider.
    const int paneW = qMax(0, (viewport()->width() - 1) / 2);
    if (paneW <= 0) {
        // Widget not shown yet — fall back to a sensible default; will be
        // recomputed on first resize/show once the viewport has real width.
        m_cols     = 16;
        m_addrW    = m_charW * 9;
        m_hexCW    = m_charW * 3;
        m_asciiOff = m_addrW + m_cols * m_hexCW + 4;
        return;
    }

    // Each byte costs hexCW (3 chars) + 1 ASCII char = 4 chars.
    // Fixed overhead: addrW (9 chars) + 4px gap + 4px tail = 9 chars + 8px.
    const int fixedOverhead = m_charW * 9 + 8;
    const int perByteW      = m_charW * 4;
    const int colsFit = qMax(1, (paneW - fixedOverhead) / qMax(1, perByteW));

    // Snap down to nearest power-of-2 in {4, 8, 16, 32}, prefer larger.
    int snapped = 4;
    for (int c : {32, 16, 8, 4}) {
        if (colsFit >= c) { snapped = c; break; }
    }
    m_cols = snapped;

    m_addrW    = m_charW * 9;        // "0xNNNNNN "
    m_hexCW    = m_charW * 3;        // "FF "
    m_asciiOff = m_addrW + m_cols * m_hexCW + 4;
}

void ByteDiffWidget::updateScrollbar()
{
    const qint64 maxLen = qMax(m_ref.size(), m_cmp.size());
    const int totalRows = (int)((maxLen + m_cols - 1) / m_cols);
    const int visRows = qMax(1, viewport()->height() / qMax(1, m_rowH));
    verticalScrollBar()->setRange(0, qMax(0, totalRows - visRows));
    verticalScrollBar()->setPageStep(visRows);
    verticalScrollBar()->setSingleStep(3);
}

void ByteDiffWidget::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    recomputeMetrics();
    updateScrollbar();
    viewport()->update();  // cols may have changed → force repaint with new layout
}

void ByteDiffWidget::wheelEvent(QWheelEvent *event)
{
    const int delta = event->angleDelta().y() / 40;
    verticalScrollBar()->setValue(verticalScrollBar()->value() - delta);
    event->accept();
}

void ByteDiffWidget::scrollContentsBy(int /*dx*/, int /*dy*/)
{
    // Repaint the whole viewport — pixel-shift scrolling would tear because
    // every row's content is recomputed from m_firstRow on each paint.
    viewport()->update();
}

void ByteDiffWidget::paintEvent(QPaintEvent *)
{
    QPainter p(viewport());
    p.setFont(m_font);

    const int w = viewport()->width();
    const int h = viewport()->height();
    m_firstRow  = verticalScrollBar()->value();
    const qint64 byteOff = (qint64)m_firstRow * m_cols;
    if (byteOff != m_lastEmittedOffset) {
        m_lastEmittedOffset = byteOff;
        emit viewportOffsetChanged(byteOff);
    }
    const int visRows = (h / m_rowH) + 1;

    const int divX = w / 2;

    p.fillRect(0, 0, w, h, BG_COL);

    p.setPen(DIVIDER_COL);
    p.drawLine(divX, 0, divX, h);

    // Header (single row at top of each pane)
    p.setPen(HEADER_COL);
    p.drawText(QRect(2, 0, divX - 4, m_rowH),
               Qt::AlignVCenter, tr("Reference"));
    p.drawText(QRect(divX + 2, 0, divX - 4, m_rowH),
               Qt::AlignVCenter, tr("Compare"));

    drawPane(p, 0,        m_ref, m_cmp, m_firstRow, visRows);
    drawPane(p, divX + 1, m_cmp, m_ref, m_firstRow, visRows);
}

void ByteDiffWidget::drawPane(QPainter &p, int paneX,
                              const QByteArray &data, const QByteArray &other,
                              int firstRow, int visRows)
{
    const char *dataPtr  = data.constData();
    const char *otherPtr = other.constData();
    const qint64 dataSize  = data.size();
    const qint64 otherSize = other.size();
    const qint64 maxSize   = qMax(dataSize, otherSize);

    const int headerH = m_rowH;
    const int asciiX0 = paneX + m_asciiOff;

    char addrBuf[12];   // "0xFFFFFFFF\0"

    // Iterate visible rows once. For each cell we do a single byte compare
    // and reuse pre-built QString lookups for the hex/ascii glyphs.
    for (int row = 0; row < visRows; ++row) {
        const qint64 absRow = (qint64)firstRow + row;
        const qint64 offset = absRow * m_cols;
        if (offset >= maxSize) break;

        const int y  = headerH + row * m_rowH;
        const int ty = y + m_baseline;

        // Pass 1 — highlight backgrounds for differing / orphan cells, and
        // remember whether THIS row contains any change so we can paint a
        // solid red stripe at the left edge (lets users scan-scroll for diffs).
        bool rowHasChange = false;
        for (int col = 0; col < m_cols; ++col) {
            const qint64 byteOff = offset + col;
            if (byteOff >= dataSize) break;
            const bool hasOther = byteOff < otherSize;
            const uint8_t self  = (uint8_t)dataPtr[byteOff];
            const uint8_t oth   = hasOther ? (uint8_t)otherPtr[byteOff] : 0;
            const bool differs  = hasOther && (self != oth);
            const bool onlyHere = !hasOther;
            if (!differs && !onlyHere) continue;

            rowHasChange = true;
            const int hx = paneX + m_addrW + col * m_hexCW;
            const int ax = asciiX0 + col * m_charW;
            const QColor &bg = onlyHere ? ONLY_BG : DIFF_BG;
            p.fillRect(hx, y, m_hexCW - 1, m_rowH, bg);
            p.fillRect(ax, y, m_charW,     m_rowH, bg);
        }

        // Row marker — 3px solid red strip in the leftmost gutter when this
        // row has any diff. Drawn AFTER cell highlights so it's never clipped.
        if (rowHasChange) {
            p.fillRect(paneX, y, 3, m_rowH, ROW_MARKER_COL);
        }

        // Address — sprintf into a stack buffer is much cheaper than QString::arg.
        std::snprintf(addrBuf, sizeof(addrBuf),
                      "0x%06llX", (unsigned long long)offset);
        p.setPen(ADDR_COL);
        p.drawText(paneX + 6, ty, QLatin1String(addrBuf));

        // Pass 2 — draw text. Group pen-color changes (most cells share TEXT_COL).
        bool penIsDiff = false;
        p.setPen(TEXT_COL);
        for (int col = 0; col < m_cols; ++col) {
            const qint64 byteOff = offset + col;
            if (byteOff >= dataSize) break;
            const bool hasOther = byteOff < otherSize;
            const uint8_t self  = (uint8_t)dataPtr[byteOff];
            const uint8_t oth   = hasOther ? (uint8_t)otherPtr[byteOff] : 0;
            const bool differs  = hasOther && (self != oth);

            if (differs != penIsDiff) {
                p.setPen(differs ? DIFF_TEXT_COL : TEXT_COL);
                penIsDiff = differs;
            }

            const int hx = paneX + m_addrW + col * m_hexCW;
            const int ax = asciiX0 + col * m_charW;
            p.drawText(hx, ty, HEX2[self]);
            p.drawText(ax, ty, ASCII1[self]);
        }
    }
}
