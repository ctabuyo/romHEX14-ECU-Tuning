/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#include <QDialog>
#include <QVector>
#include <QByteArray>
#include <QWidget>
#include "romdata.h"
#include "kpparser.h"

class QTableWidget;
class QPushButton;
class QCheckBox;
class QLineEdit;
class QLabel;
class QComboBox;
class QCloseEvent;

// ── ROM overview bar widget ──────────────────────────────────────────────────

class RomOverviewBar : public QWidget {
    Q_OBJECT
public:
    explicit RomOverviewBar(int romSize, const QVector<MapInfo> &maps,
                            QWidget *parent = nullptr);
    void setOffset(int32_t offset);

signals:
    void mapClicked(int mapIndex);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    QSize sizeHint() const override { return QSize(600, 32); }
    QSize minimumSizeHint() const override { return QSize(200, 24); }

private:
    int m_romSize = 0;
    int32_t m_offset = 0;
    QVector<MapInfo> m_maps;
};

// ── KP Import Dialog ─────────────────────────────────────────────────────────

class KPImportDlg : public QDialog {
    Q_OBJECT
public:
    explicit KPImportDlg(const KPVehicleInfo &info,
                         const QVector<MapInfo> &maps,
                         int romSize,
                         const QByteArray &romData,
                         QWidget *parent = nullptr);
    QVector<MapInfo> selectedMaps() const;
    bool importMapValues() const;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onSelectAll();
    void onSelectNone();
    void onAutoOffset();
    void onOffsetChanged();

private:
    void buildUi(const KPVehicleInfo &info, const QVector<MapInfo> &maps);
    void updateHeaderInfo();
    int32_t totalOffset() const;

    QTableWidget       *m_table          = nullptr;
    QPushButton        *m_okBtn          = nullptr;
    RomOverviewBar     *m_overviewBar    = nullptr;

    QLineEdit          *m_offset1Edit    = nullptr;
    QLineEdit          *m_offset2Edit    = nullptr;

    QCheckBox          *m_chkAvoidDupes  = nullptr;
    QCheckBox          *m_chkIgnoreAxis  = nullptr;
    QCheckBox          *m_chkIgnoreTexts = nullptr;

    QCheckBox          *m_chkMapValues   = nullptr;
    QCheckBox          *m_chkMapStruct   = nullptr;
    QCheckBox          *m_chkStructDims  = nullptr;
    QCheckBox          *m_chkStructPrec  = nullptr;
    QCheckBox          *m_chkStructSign  = nullptr;

    QLineEdit          *m_iconMapEdit    = nullptr;
    QLineEdit          *m_prefixEdit     = nullptr;
    QLineEdit          *m_folderEdit     = nullptr;

    QLabel             *m_headerLabel    = nullptr;
    QLabel             *m_addrLabel      = nullptr;
    QLabel             *m_matchLabel     = nullptr;

    QVector<MapInfo>    m_maps;
    int                 m_romSize        = 0;
    QByteArray          m_romData;
};
