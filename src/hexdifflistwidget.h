/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QWidget>
#include <QByteArray>
#include <QVector>
#include "romcompare.h"   // for DiffRun

class QTableView;
class QLabel;
class QStandardItemModel;

class HexDiffListWidget : public QWidget {
    Q_OBJECT
public:
    explicit HexDiffListWidget(QWidget *parent = nullptr);

    // ref / cmp: the compared byte buffers (used to render the per-run preview).
    // runs: contiguous diff runs -- pre-computed by the caller.
    void setData(const QByteArray &ref, const QByteArray &cmp,
                 const QVector<DiffRun> &runs);
    void clear();

signals:
    // Emitted when the user activates a row (click or keyboard return).
    void offsetActivated(qint64 offset);

private:
    QLabel     *m_titleLabel = nullptr;
    QTableView *m_view       = nullptr;
    class DiffRunModel *m_model = nullptr;   // private model class defined in .cpp
    QLabel     *m_emptyLabel = nullptr;
};
