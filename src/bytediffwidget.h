/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QAbstractScrollArea>
#include <QByteArray>
#include <QFont>

// Dual-pane read-only hex view for showing byte differences between two ROMs.
class ByteDiffWidget : public QAbstractScrollArea {
    Q_OBJECT

public:
    explicit ByteDiffWidget(QWidget *parent = nullptr);

    void setData(const QByteArray &ref, const QByteArray &cmp);
    void goToFirstDiff();
    void goToOffset(qint64 offset);

signals:
    void viewportOffsetChanged(qint64 firstVisibleByte);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    void recomputeMetrics();
    void updateScrollbar();
    void drawPane(QPainter &p, int paneX,
                  const QByteArray &data, const QByteArray &other,
                  int firstRow, int visRows);

    QByteArray m_ref;
    QByteArray m_cmp;
    qint64     m_diffCount       = 0;
    qint64     m_firstDiffOffset = -1;

    QFont   m_font;
    int     m_charW    = 8;
    int     m_rowH     = 16;
    int     m_baseline = 12;     // text baseline within a row
    int     m_cols;              // bytes per row (computed in recomputeMetrics)
    int     m_firstRow = 0;

    // Pre-computed layout (recomputed only when font / cols change).
    int m_addrW    = 0;
    int m_hexCW    = 0;
    int m_asciiOff = 0;

    qint64 m_lastEmittedOffset = -1;
};
