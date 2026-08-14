/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QWidget>
#include <QVector>
#include "romcompare.h"  // for DiffRun

class HexDiffWaveform : public QWidget {
    Q_OBJECT
public:
    explicit HexDiffWaveform(QWidget *parent = nullptr);

    // totalSize = max(ref.size(), cmp.size()).  runs are sorted ascending.
    void setData(qint64 totalSize, const QVector<DiffRun> &runs);
    void clear();

    // Show a marker at `offset` (e.g. follows the byte-diff widget's scroll).
    void setMarker(qint64 offset);

signals:
    void offsetClicked(qint64 offset);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    void rebuildBuckets();   // recompute m_buckets from m_runs / current width
    qint64 xToOffset(int x) const;

    qint64 m_total = 0;
    QVector<DiffRun> m_runs;
    QVector<float> m_buckets;   // 0..1 normalized density per pixel-bucket
    qint64 m_marker = -1;
};
