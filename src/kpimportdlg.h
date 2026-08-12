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
#include <cstdint>
#include "romdata.h"

// Presentation-only project information shown by the KP review dialog. This
// intentionally does not depend on the retired heuristic KP parser.
struct KPVehicleInfo {
    QString manufacturer, model, variant, year, power;
    QString ecuBrand, partNumber, swVersion;
    uint32_t romWordCount = 0;
    uint32_t romByteSize = 0;
};

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
    void setOffsets(int32_t offset1, int32_t offset2);

signals:
    void mapClicked(int mapIndex);
    void offsetDragged(int32_t netOffset);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    QSize sizeHint() const override { return QSize(600, 46); }
    QSize minimumSizeHint() const override { return QSize(200, 36); }

private:
    int m_romSize = 0;
    int32_t m_offset1 = 0;
    int32_t m_offset2 = 0;
    QVector<MapInfo> m_maps;
    QPoint m_dragStartPos;
    int32_t m_dragStartNetOffset = 0;
    bool m_isDragging = false;
    bool m_isSnapped = false;
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
    bool importNoHexdump() const;
    bool importEmptyFolders() const;

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
    QCheckBox          *m_chkNoHexdump   = nullptr;
    QCheckBox          *m_chkEmptyFolders= nullptr;

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
