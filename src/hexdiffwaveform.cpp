/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hexdiffwaveform.h"

#include <QPainter>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QToolTip>
#include <cmath>

namespace {
const QColor BG_COL        (0x0d, 0x11, 0x17);
const QColor BAR_TOP       (0xff, 0x6b, 0x60);   // bright salmon-red
const QColor BAR_BOTTOM    (0xc8, 0x21, 0x1c);   // deep red
const QColor BASE_WASH     (0xf8, 0x51, 0x49, 55); // translucent red ambient
const QColor MARKER_COL    (0x58, 0xa6, 0xff);
const QColor BORDER_COL    (0x30, 0x36, 0x3d);
constexpr int PAD = 4;   // 4px padding on each side
} // namespace

HexDiffWaveform::HexDiffWaveform(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(56);
    setMaximumHeight(72);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
    setToolTip(tr("Difference density across the file — click to jump"));
}

void HexDiffWaveform::setData(qint64 totalSize, const QVector<DiffRun> &runs)
{
    m_total = totalSize;
    m_runs  = runs;
    rebuildBuckets();
    update();
}

void HexDiffWaveform::clear()
{
    m_total = 0;
    m_runs.clear();
    m_buckets.clear();
    m_marker = -1;
    update();
}

void HexDiffWaveform::setMarker(qint64 offset)
{
    m_marker = offset;
    update();
}

void HexDiffWaveform::rebuildBuckets()
{
    const int N = qMax(1, width() - 2 * PAD);
    m_buckets.resize(N); std::fill(m_buckets.begin(), m_buckets.end(), 0.0f);
    if (m_total <= 0 || m_runs.isEmpty()) {
        return;
    }

    // Single linear pass over sorted runs, assigning intersections into buckets.
    int runIdx = 0;
    for (int i = 0; i < N; ++i) {
        const qint64 bStart = (qint64)i * m_total / N;
        const qint64 bEnd   = (qint64)(i + 1) * m_total / N;
        const qint64 bSize  = bEnd - bStart;
        if (bSize <= 0) continue;

        // Skip runs entirely before this bucket.
        while (runIdx < m_runs.size()
               && (m_runs[runIdx].offset + m_runs[runIdx].length) <= bStart) {
            ++runIdx;
        }

        qint64 bytesInBucket = 0;
        // Accumulate intersection with all runs overlapping this bucket.
        // Runs may also extend into subsequent buckets (don't advance runIdx
        // past them).
        for (int j = runIdx; j < m_runs.size(); ++j) {
            const qint64 rStart = m_runs[j].offset;
            const qint64 rEnd   = rStart + m_runs[j].length;
            if (rStart >= bEnd) break;
            const qint64 s = qMax(rStart, bStart);
            const qint64 e = qMin(rEnd,   bEnd);
            if (e > s) bytesInBucket += (e - s);
        }

        double d = (double)bytesInBucket / (double)bSize;
        if (d > 1.0) d = 1.0;
        d = std::sqrt(d);   // emphasize small clusters
        m_buckets[i] = (float)d;
    }

    // Normalize against peak so the tallest bucket fills available height.
    float peak = 0.0f;
    for (float v : m_buckets) if (v > peak) peak = v;
    if (peak > 0.0f) {
        for (int i = 0; i < m_buckets.size(); ++i)
            m_buckets[i] /= peak;
    }
}

qint64 HexDiffWaveform::xToOffset(int x) const
{
    const int span = qMax(1, width() - 2 * PAD);
    qint64 result = (qint64)(x - PAD) * (qint64)m_total / span;
    if (result < 0) result = 0;
    if (m_total > 0 && result > m_total - 1) result = m_total - 1;
    return result;
}

void HexDiffWaveform::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int w = width();
    const int h = height();

    // Background fill + rounded border.
    QRectF frame(0.5, 0.5, w - 1.0, h - 1.0);
    p.setPen(QPen(BORDER_COL, 1));
    p.setBrush(BG_COL);
    p.drawRoundedRect(frame, 4.0, 4.0);

    // Bars — vertical gradient (bright at top, deep red at bottom) plus a
    // translucent red ambient wash for any bucket with diffs so even a
    // single-byte change in a 4 MB ROM is visible at a glance.
    const int availH = h - 2 * PAD;
    if (availH > 0 && !m_buckets.isEmpty()) {
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(Qt::NoPen);

        // Pre-build the bar gradient once (shared across all bars).
        QLinearGradient grad(0, PAD, 0, h - PAD);
        grad.setColorAt(0.0, BAR_TOP);
        grad.setColorAt(1.0, BAR_BOTTOM);
        const QBrush gradBrush(grad);

        for (int i = 0; i < m_buckets.size(); ++i) {
            const float d = m_buckets[i];
            if (d <= 0.0f) continue;

            // Ambient red wash — subtle full-height rectangle so empty buckets
            // surrounded by activity stay legible as "this whole region is hot".
            p.fillRect(QRect(PAD + i, PAD, 1, availH), BASE_WASH);

            // Solid gradient bar on top, scaled by density.
            const int barHeight = (int)(availH * d);
            if (barHeight <= 0) continue;
            p.fillRect(QRect(PAD + i, h - PAD - barHeight, 1, barHeight), gradBrush);
        }
    }

    // Marker.
    if (m_marker >= 0 && m_total > 0) {
        const int span = qMax(1, w - 2 * PAD);
        const int mx = PAD + (int)((m_marker * (qint64)span) / m_total);
        p.setPen(QPen(MARKER_COL, 2));
        p.drawLine(mx, 0, mx, h);
    }
}

void HexDiffWaveform::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    rebuildBuckets();
    update();
}

void HexDiffWaveform::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_total > 0) {
        emit offsetClicked(xToOffset(event->pos().x()));
    }
    QWidget::mousePressEvent(event);
}

void HexDiffWaveform::mouseMoveEvent(QMouseEvent *event)
{
    if ((event->buttons() & Qt::LeftButton) && m_total > 0) {
        emit offsetClicked(xToOffset(event->pos().x()));
    }
    QWidget::mouseMoveEvent(event);
}
