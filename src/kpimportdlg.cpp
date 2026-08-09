/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "kpimportdlg.h"
#include "appconstants.h"
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QCheckBox>
#include <QLineEdit>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QPainter>
#include <QMouseEvent>
#include <QToolTip>
#include <QColor>
#include <QSettings>
#include <QCloseEvent>
#include <QApplication>
#include <algorithm>
#include <cmath>

// ═══════════════════════════════════════════════════════════════════════════════
//  RomOverviewBar
// ═══════════════════════════════════════════════════════════════════════════════

RomOverviewBar::RomOverviewBar(int romSize, const QVector<MapInfo> &maps,
                               QWidget *parent)
    : QWidget(parent), m_romSize(romSize), m_maps(maps)
{
    setMouseTracking(true);
    setCursor(Qt::SizeAllCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(46);
    setToolTip(tr("ROM overview — Drag top bar left or right to shift map pack offset over the target ROM"));
}

void RomOverviewBar::setOffsets(int32_t offset1, int32_t offset2)
{
    if (m_offset1 != offset1 || m_offset2 != offset2) {
        m_offset1 = offset1;
        m_offset2 = offset2;
        update();
    }
}

void RomOverviewBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int w = width();
    const int h = height();
    const double romSize = (m_romSize > 0) ? (double)m_romSize : 1.0;

    // Dual-track layout:
    // Top track (H=18): Imported Map Pack (moves with Net Offset)
    // Bottom track (H=18): Target Project ROM (fixed baseline 0x0 .. romSize)
    const int trackH = (h - 6) / 2; // 20px each
    const int topY = 2;
    const int botY = topY + trackH + 2;

    // Background
    p.fillRect(rect(), QColor(0x0d, 0x11, 0x17));

    // Top track background (Imported Map Pack - Movable)
    p.fillRect(0, topY, w, trackH, QColor(0x16, 0x1b, 0x22));
    p.setPen(QColor(0x30, 0x36, 0x3d));
    p.drawRect(0, topY, w - 1, trackH - 1);

    // Bottom track background (Target ROM - Fixed Baseline)
    p.fillRect(0, botY, w, trackH, QColor(0x0d, 0x11, 0x17));
    p.setPen(QColor(0x21, 0x26, 0x2d));
    p.drawRect(0, botY, w - 1, trackH - 1);

    // Track Labels
    QFont labelFont = p.font();
    labelFont.setPixelSize(8);
    labelFont.setBold(true);
    p.setFont(labelFont);

    int32_t netOffset = m_offset1 - m_offset2;

    p.setPen(QColor(0x8b, 0x94, 0x9e));
    p.drawText(4, topY + 12, tr("IMPORTED MAP PACK (MOVABLE)"));

    QString romEndStr = QString("TARGET ROM (0x0 .. 0x%1)").arg(m_romSize, 0, 16).toUpper();
    p.drawText(4, botY + 12, romEndStr);

    if (m_maps.isEmpty() || m_romSize <= 0) return;

    // Draw map blocks
    for (int i = 0; i < m_maps.size(); ++i) {
        const MapInfo &mi = m_maps[i];

        int len = mi.length > 0 ? mi.length : (mi.dimensions.x * mi.dimensions.y * mi.dataSize);
        if (len <= 0) len = mi.dataSize;

        QColor col;
        if (mi.type == QStringLiteral("MAP"))
            col = QColor(0x23, 0x86, 0x36, 220);   // green
        else if (mi.type == QStringLiteral("CURVE"))
            col = QColor(0x1f, 0x6f, 0xeb, 220);   // blue
        else
            col = QColor(0xd2, 0x9e, 0x22, 220);   // yellow/value

        // 1. Bottom Track (Target ROM Baseline) — STATIONARY, NEVER MOVES
        if (mi.address < (uint32_t)m_romSize) {
            double rx0 = ((double)mi.address / romSize) * w;
            double rx1 = (((double)mi.address + len) / romSize) * w;
            double rbw = qMax(1.5, rx1 - rx0);
            QColor botCol = col;
            botCol.setAlpha(100);
            p.fillRect(QRectF(rx0, botY + 1, rbw, trackH - 2), botCol);
        }

        // 2. Top Track (Imported Map Pack) — MOVES / SLIDES with netOffset
        int64_t topAddr = (int64_t)mi.address + netOffset;
        double tx0 = ((double)topAddr / romSize) * w;
        double tx1 = (((double)topAddr + len) / romSize) * w;
        double tbw = qMax(1.5, tx1 - tx0);

        bool outOfBounds = (topAddr < 0 || topAddr + len > m_romSize);
        QColor topCol = outOfBounds ? QColor(0xda, 0x36, 0x33, 220) : col; // Red if out of bounds

        p.fillRect(QRectF(tx0, topY + 1, tbw, trackH - 2), topCol);
    }
}

void RomOverviewBar::mousePressEvent(QMouseEvent *event)
{
    if (m_romSize <= 0 || m_maps.isEmpty()) return;
    if (event->button() == Qt::LeftButton) {
        m_dragStartPos = event->pos();
        m_dragStartNetOffset = m_offset1 - m_offset2;
        m_isDragging = false;
    }
}

void RomOverviewBar::mouseMoveEvent(QMouseEvent *event)
{
    if (m_romSize <= 0 || m_maps.isEmpty()) return;
    if (event->buttons() & Qt::LeftButton) {
        int deltaX = event->pos().x() - m_dragStartPos.x();
        if (!m_isDragging && std::abs(deltaX) > QApplication::startDragDistance()) {
            m_isDragging = true;
        }
        if (m_isDragging) {
            double romBytesPerPixel = (double)m_romSize / qMax(1, width());
            int32_t deltaBytes = (int32_t)std::round(deltaX * romBytesPerPixel);
            int32_t rawNetOffset = m_dragStartNetOffset + deltaBytes;

            // Proximity snapping threshold (~10 pixels)
            double snapThresholdBytes = 10.0 * romBytesPerPixel;

            // Snap targets: 0, common ECU base address shifts
            static const int32_t baseAddrs[] = {
                0,
                (int32_t)-0x80000000LL,
                (int32_t)-0x80040000LL,
                (int32_t)-0x80080000LL,
                (int32_t)-0x80100000LL,
                (int32_t)-0x80800000LL,
                (int32_t)-0xA0000000LL,
                (int32_t)-0xA0040000LL,
                (int32_t)-0x00400000LL,
                (int32_t)-0x00800000LL,
            };

            int32_t finalOffset = rawNetOffset;
            double bestDist = snapThresholdBytes;
            m_isSnapped = false;

            for (int32_t base : baseAddrs) {
                double dist = std::abs((double)rawNetOffset - (double)base);
                if (dist < bestDist) {
                    bestDist = dist;
                    finalOffset = base;
                    m_isSnapped = true;
                }
            }

            // Also check alignment of each map's start address to 0x0
            for (const MapInfo &mi : m_maps) {
                int32_t alignTarget = -(int32_t)mi.address;
                double dist = std::abs((double)rawNetOffset - (double)alignTarget);
                if (dist < bestDist) {
                    bestDist = dist;
                    finalOffset = alignTarget;
                    m_isSnapped = true;
                }
            }

            emit offsetDragged(finalOffset);
        }
    }
}

void RomOverviewBar::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (!m_isDragging && m_romSize > 0 && !m_maps.isEmpty()) {
            double clickFrac = (double)event->pos().x() / width();
            double clickAddr = clickFrac * m_romSize;

            int32_t netOffset = m_offset1 - m_offset2;
            int bestIdx = -1;
            double bestDist = 1e18;
            for (int i = 0; i < m_maps.size(); ++i) {
                double addr = (double)((int32_t)m_maps[i].address + netOffset);
                double dist = std::abs(addr - clickAddr);
                if (dist < bestDist) {
                    bestDist = dist;
                    bestIdx = i;
                }
            }
            if (bestIdx >= 0)
                emit mapClicked(bestIdx);
        }
        m_isDragging = false;
        m_isSnapped = false;
        update();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  KPImportDlg
// ═══════════════════════════════════════════════════════════════════════════════

static const char *kDialogStyle =
    "QDialog { background: #0d1117; color: #c9d1d9; }"
    "QLabel { color: #c9d1d9; }"
    "QGroupBox { color: #c9d1d9; border: 1px solid #21262d; border-radius: 4px;"
    "  margin-top: 8px; padding-top: 14px; font-weight: bold; }"
    "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"
    "QCheckBox { color: #c9d1d9; spacing: 6px; }"
    "QCheckBox::indicator { width: 14px; height: 14px; }"
    "QLineEdit { background: #161b22; color: #c9d1d9; border: 1px solid #30363d;"
    "  border-radius: 3px; padding: 3px 6px; }"
    "QPushButton { background: #21262d; color: #c9d1d9; border: 1px solid #30363d;"
    "  border-radius: 4px; padding: 4px 12px; }"
    "QPushButton:hover { background: #30363d; }"
    "QPushButton:pressed { background: #161b22; }"
    "QTableWidget { background: #0d1117; color: #c9d1d9; gridline-color: #21262d;"
    "  font-size: 8pt; }"
    "QTableWidget::item:selected { background: #1f6feb; }"
    "QHeaderView::section { background: #161b22; color: #8b949e; border: none; padding: 4px; }";

KPImportDlg::KPImportDlg(const KPVehicleInfo &info,
                           const QVector<MapInfo> &maps,
                           int romSize,
                           const QByteArray &romData,
                           QWidget *parent)
    : QDialog(parent), m_romSize(romSize), m_romData(romData)
{
    m_maps = maps;
    std::sort(m_maps.begin(), m_maps.end(), [](const MapInfo &a, const MapInfo &b) {
        return a.address < b.address;
    });

    buildUi(info, m_maps);
    setWindowTitle(tr("Import Map Pack"));
    setSizeGripEnabled(true);      // draggable resize grip in the corner
    setMinimumSize(560, 480);      // never collapse the map table too small
    resize(820, 780);              // roomier default so more rows are visible
    setStyleSheet(kDialogStyle);

    restoreGeometry(rx14::appSettings()
                    .value("dialogGeometry/KPImportDlg").toByteArray());
}

void KPImportDlg::closeEvent(QCloseEvent *event)
{
    rx14::appSettings()
        .setValue("dialogGeometry/KPImportDlg", saveGeometry());
    QDialog::closeEvent(event);
}

void KPImportDlg::buildUi(const KPVehicleInfo &info, const QVector<MapInfo> &maps)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(6);

    // ── 1. Header info ───────────────────────────────────────────────────────
    m_headerLabel = new QLabel(this);
    m_headerLabel->setWordWrap(true);
    m_headerLabel->setStyleSheet(QStringLiteral("font-size: 9pt;"));

    m_addrLabel = new QLabel(this);
    m_addrLabel->setStyleSheet(QStringLiteral("color: #8b949e; font-size: 8pt;"));

    m_matchLabel = new QLabel(this);
    m_matchLabel->setStyleSheet(QStringLiteral("color: #8b949e; font-size: 8pt;"));

    // Compute address range
    uint32_t minAddr = 0xFFFFFFFF, maxAddr = 0;
    for (const MapInfo &mi : maps) {
        if (mi.address < minAddr) minAddr = mi.address;
        uint32_t end = mi.address + (mi.length > 0 ? mi.length : mi.dataSize);
        if (end > maxAddr) maxAddr = end;
    }
    if (minAddr == 0xFFFFFFFF) minAddr = 0;

    QString sampleNames;
    int showCount = qMin(3, maps.size());
    for (int i = 0; i < showCount; ++i) {
        if (i > 0) sampleNames += QStringLiteral(", ");
        sampleNames += maps[i].name;
    }
    if (maps.size() > showCount)
        sampleNames += QStringLiteral(", ...");

    m_headerLabel->setText(
        tr("You are about to import <b>%1</b> map(s) like <i>%2</i>")
            .arg(maps.size()).arg(sampleNames));

    m_addrLabel->setText(
        tr("Address range: 0x%1 .. 0x%2  |  ECU: %3 %4  |  ROM in file: %5 bytes")
            .arg(minAddr, 6, 16, QChar('0')).toUpper()
            .arg(maxAddr, 6, 16, QChar('0')).toUpper()
            .arg(info.ecuBrand, info.partNumber)
            .arg(info.romByteSize));

    // Compute match percentage — how many map addresses fall within ROM bounds
    int inRange = 0;
    for (const MapInfo &mi : maps) {
        if ((int64_t)mi.address + mi.length <= m_romSize && mi.address < (uint32_t)m_romSize)
            inRange++;
    }
    double matchPct = maps.isEmpty() ? 0.0 : (100.0 * inRange / maps.size());
    m_matchLabel->setText(
        tr("Address match: %1% of maps within current ROM (%2 / %3)  |  Project ROM size: %4 bytes")
            .arg(matchPct, 0, 'f', 1)
            .arg(inRange).arg(maps.size())
            .arg(m_romSize));

    root->addWidget(m_headerLabel);
    root->addWidget(m_addrLabel);
    root->addWidget(m_matchLabel);
    root->addSpacing(4);

    // ── 2. ROM overview bar ──────────────────────────────────────────────────
    auto *barLabel = new QLabel(tr("ROM overview:"), this);
    barLabel->setStyleSheet(QStringLiteral("color: #8b949e; font-size: 8pt;"));
    root->addWidget(barLabel);

    m_overviewBar = new RomOverviewBar(m_romSize, m_maps, this);
    root->addWidget(m_overviewBar);
    root->addSpacing(4);

    // Connect bar click to table scroll and drag to offset update
    connect(m_overviewBar, &RomOverviewBar::mapClicked, this, [this](int idx) {
        if (m_table && idx >= 0 && idx < m_table->rowCount())
            m_table->scrollToItem(m_table->item(idx, 1),
                                  QAbstractItemView::PositionAtCenter);
    });

    connect(m_overviewBar, &RomOverviewBar::offsetDragged, this, [this](int32_t netOffset) {
        if (netOffset >= 0) {
            m_offset1Edit->setText(QString::number(netOffset, 16).toUpper());
            m_offset2Edit->setText(QStringLiteral("0"));
        } else {
            m_offset1Edit->setText(QStringLiteral("0"));
            m_offset2Edit->setText(QString::number(-netOffset, 16).toUpper());
        }
    });

    // ── 3. Offset configuration ─────────────────────────────────────────────
    auto *offsetGroup = new QGroupBox(tr("Offset"), this);
    auto *offsetGrid = new QGridLayout(offsetGroup);
    offsetGrid->setContentsMargins(8, 4, 8, 8);
    offsetGrid->setSpacing(4);

    offsetGrid->addWidget(new QLabel(tr("Offset 1 (+):"), this), 0, 0);
    m_offset1Edit = new QLineEdit(QStringLiteral("0"), this);
    m_offset1Edit->setMaximumWidth(120);
    m_offset1Edit->setToolTip(tr("Positive hex offset added to all map addresses (e.g. 1A0000)"));
    offsetGrid->addWidget(m_offset1Edit, 0, 1);
    offsetGrid->addWidget(new QLabel(tr("hex"), this), 0, 2);

    offsetGrid->addWidget(new QLabel(tr("Offset 2 (-):"), this), 1, 0);
    m_offset2Edit = new QLineEdit(QStringLiteral("0"), this);
    m_offset2Edit->setMaximumWidth(120);
    m_offset2Edit->setToolTip(tr("Negative hex offset subtracted from all map addresses (e.g. 800000)"));
    offsetGrid->addWidget(m_offset2Edit, 1, 1);
    offsetGrid->addWidget(new QLabel(tr("hex"), this), 1, 2);

    auto *autoBtn = new QPushButton(tr("Automatically"), this);
    autoBtn->setToolTip(tr("Try to determine the correct offset automatically"));
    offsetGrid->addWidget(autoBtn, 0, 3, 2, 1);

    offsetGrid->setColumnStretch(4, 1);

    connect(m_offset1Edit, &QLineEdit::textChanged, this, &KPImportDlg::onOffsetChanged);
    connect(m_offset2Edit, &QLineEdit::textChanged, this, &KPImportDlg::onOffsetChanged);
    connect(autoBtn, &QPushButton::clicked, this, &KPImportDlg::onAutoOffset);

    root->addWidget(offsetGroup);

    // ── 4. Duplicates section ────────────────────────────────────────────────
    auto *dupeGroup = new QGroupBox(tr("Duplicates"), this);
    auto *dupeLayout = new QVBoxLayout(dupeGroup);
    dupeLayout->setContentsMargins(8, 4, 8, 8);
    dupeLayout->setSpacing(2);

    m_chkAvoidDupes = new QCheckBox(tr("Avoid duplicates"), this);
    m_chkAvoidDupes->setChecked(false); // Default: unchecked
    dupeLayout->addWidget(m_chkAvoidDupes);

    auto *dupeSubLayout = new QHBoxLayout();
    dupeSubLayout->setContentsMargins(20, 0, 0, 0);
    dupeSubLayout->setSpacing(6);

    auto *whenComparingLabel = new QLabel(tr("when comparing:"), this);
    dupeSubLayout->addWidget(whenComparingLabel);

    m_chkIgnoreAxis = new QCheckBox(tr("Ignore axis"), this);
    m_chkIgnoreTexts = new QCheckBox(tr("Ignore texts"), this);
    dupeSubLayout->addWidget(m_chkIgnoreAxis);
    dupeSubLayout->addWidget(m_chkIgnoreTexts);
    dupeSubLayout->addStretch();
    dupeLayout->addLayout(dupeSubLayout);

    // Initial disabled state since m_chkAvoidDupes is unchecked by default
    whenComparingLabel->setEnabled(false);
    m_chkIgnoreAxis->setEnabled(false);
    m_chkIgnoreTexts->setEnabled(false);

    connect(m_chkAvoidDupes, &QCheckBox::toggled, this, [this, whenComparingLabel](bool on) {
        whenComparingLabel->setEnabled(on);
        m_chkIgnoreAxis->setEnabled(on);
        m_chkIgnoreTexts->setEnabled(on);
    });

    root->addWidget(dupeGroup);

    // ── 5. Import options ────────────────────────────────────────────────────
    auto *optGroup = new QGroupBox(tr("Import options"), this);
    auto *optLayout = new QVBoxLayout(optGroup);
    optLayout->setContentsMargins(8, 4, 8, 8);
    optLayout->setSpacing(2);

    m_chkMapValues = new QCheckBox(tr("Map values"), this);
    m_chkMapValues->setChecked(false); // Default: unchecked
    optLayout->addWidget(m_chkMapValues);

    m_chkMapStruct = new QCheckBox(tr("Map structure"), this);
    m_chkMapStruct->setChecked(true); // Default: checked
    optLayout->addWidget(m_chkMapStruct);

    auto *structSubLayout = new QVBoxLayout();
    structSubLayout->setContentsMargins(20, 0, 0, 0);
    structSubLayout->setSpacing(2);
    m_chkNoHexdump = new QCheckBox(tr("Also maps for which no hexdump is available"), this);
    m_chkNoHexdump->setChecked(false); // Default: unchecked
    m_chkEmptyFolders = new QCheckBox(tr("Also import empty folders"), this);
    m_chkEmptyFolders->setChecked(true); // Default: checked
    structSubLayout->addWidget(m_chkNoHexdump);
    structSubLayout->addWidget(m_chkEmptyFolders);
    optLayout->addLayout(structSubLayout);

    connect(m_chkMapStruct, &QCheckBox::toggled, this, [this](bool on) {
        m_chkNoHexdump->setEnabled(on);
        m_chkEmptyFolders->setEnabled(on);
    });

    root->addWidget(optGroup);

    // ── 6. Mark imported maps ────────────────────────────────────────────────
    auto *markGroup = new QGroupBox(tr("Mark imported maps"), this);
    auto *markGrid = new QGridLayout(markGroup);
    markGrid->setContentsMargins(8, 4, 8, 8);
    markGrid->setSpacing(4);

    markGrid->addWidget(new QLabel(tr("Icon map:"), this), 0, 0);
    m_iconMapEdit = new QLineEdit(this);
    m_iconMapEdit->setPlaceholderText(tr("(none)"));
    markGrid->addWidget(m_iconMapEdit, 0, 1);

    markGrid->addWidget(new QLabel(tr("Prefix map name:"), this), 1, 0);
    m_prefixEdit = new QLineEdit(this);
    m_prefixEdit->setPlaceholderText(tr("e.g. KP_"));
    markGrid->addWidget(m_prefixEdit, 1, 1);

    markGrid->addWidget(new QLabel(tr("Parent folder:"), this), 2, 0);
    m_folderEdit = new QLineEdit(this);
    m_folderEdit->setPlaceholderText(tr("e.g. KP Import"));
    markGrid->addWidget(m_folderEdit, 2, 1);

    root->addWidget(markGroup);

    // ── 7. Map table ─────────────────────────────────────────────────────────
    m_table = new QTableWidget(maps.size(), 8, this);
    m_table->setHorizontalHeaderLabels({tr(""), tr("Name"), tr("Type"),
                                        tr("Address"), tr("Size"), tr("Unit"),
                                        tr("Folder"), tr("Description")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_table->setColumnWidth(0, 28);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_table->setColumnWidth(1, 180);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_table->setColumnWidth(2, 45);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_table->setColumnWidth(3, 90);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_table->setColumnWidth(4, 70);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
    m_table->setColumnWidth(5, 65);
    m_table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Interactive);
    m_table->setColumnWidth(6, 110);
    m_table->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(false);
    m_table->setMinimumHeight(260);   // keep plenty of rows visible even when small
    m_table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    const QColor colVal  (Qt::transparent);
    const QColor colCurve(0x1f, 0x6f, 0xeb, 30);
    const QColor colMap  (0x23, 0x86, 0x36, 30);

    for (int row = 0; row < maps.size(); ++row) {
        const MapInfo &m = maps[row];

        QColor bg;
        QString typeLabel;
        if (m.type == QStringLiteral("CURVE")) {
            bg = colCurve;
            typeLabel = QStringLiteral("C");
        } else if (m.type == QStringLiteral("MAP")) {
            bg = colMap;
            typeLabel = QStringLiteral("M");
        } else {
            bg = colVal;
            typeLabel = QStringLiteral("V");
        }

        // Column 0: checkbox
        auto *chk = new QCheckBox(this);
        chk->setChecked(true);
        auto *chkWidget = new QWidget(this);
        auto *chkLayout = new QHBoxLayout(chkWidget);
        chkLayout->addWidget(chk);
        chkLayout->setAlignment(Qt::AlignCenter);
        chkLayout->setContentsMargins(0, 0, 0, 0);
        m_table->setCellWidget(row, 0, chkWidget);

        auto makeItem = [&](const QString &text) {
            auto *item = new QTableWidgetItem(text);
            if (bg.alpha() > 0)
                item->setBackground(bg);
            return item;
        };

        QString sizeText = QString("%1x%2").arg(m.dimensions.x).arg(m.dimensions.y);
        int totalBytes = m.length > 0 ? m.length : (m.dimensions.x * m.dimensions.y * m.dataSize);
        QString sizeToolTip = tr("%1x%2 elements (%3 bytes, %4-bit %5)")
            .arg(m.dimensions.x)
            .arg(m.dimensions.y)
            .arg(totalBytes)
            .arg(m.dataSize * 8)
            .arg(m.dataSigned ? tr("signed") : tr("unsigned"));

        auto *sizeItem = makeItem(sizeText);
        sizeItem->setToolTip(sizeToolTip);

        m_table->setItem(row, 1, makeItem(m.name));
        m_table->setItem(row, 2, makeItem(typeLabel));
        m_table->setItem(row, 3, makeItem(
            QString("0x%1").arg(m.address, 6, 16, QChar('0')).toUpper()));
        m_table->setItem(row, 4, sizeItem);
        m_table->setItem(row, 5, makeItem(m.scaling.unit));
        m_table->setItem(row, 6, makeItem(m.folderPath));
        m_table->setItem(row, 7, makeItem(m.description));
    }

    root->addWidget(m_table, 1);

    // ── Select all / none ────────────────────────────────────────────────────
    auto *selLayout = new QHBoxLayout;
    auto *btnAll  = new QPushButton(tr("Select all"),  this);
    auto *btnNone = new QPushButton(tr("Select none"), this);
    btnAll->setFixedWidth(88);
    btnNone->setFixedWidth(88);
    connect(btnAll,  &QPushButton::clicked, this, &KPImportDlg::onSelectAll);
    connect(btnNone, &QPushButton::clicked, this, &KPImportDlg::onSelectNone);
    selLayout->addWidget(btnAll);
    selLayout->addWidget(btnNone);
    selLayout->addStretch();
    root->addLayout(selLayout);

    // ── 8. OK / Cancel buttons ───────────────────────────────────────────────
    auto *btnLayout = new QHBoxLayout;
    btnLayout->addStretch();

    m_okBtn = new QPushButton(tr("OK"), this);
    m_okBtn->setDefault(true);
    m_okBtn->setStyleSheet(
        QStringLiteral("QPushButton { background: #238636; color: #fff; border-radius: 4px; padding: 4px 16px; }"
                       "QPushButton:hover { background: #2ea043; }"));

    auto *cancelBtn = new QPushButton(tr("Cancel"), this);
    cancelBtn->setFixedWidth(72);

    connect(m_okBtn,    &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelBtn,  &QPushButton::clicked, this, &QDialog::reject);

    btnLayout->addWidget(m_okBtn);
    btnLayout->addWidget(cancelBtn);
    root->addLayout(btnLayout);
}

// ── Offset helpers ───────────────────────────────────────────────────────────

int32_t KPImportDlg::totalOffset() const
{
    bool ok1 = false, ok2 = false;
    int32_t off1 = (int32_t)m_offset1Edit->text().trimmed().toUInt(&ok1, 16);
    int32_t off2 = (int32_t)m_offset2Edit->text().trimmed().toUInt(&ok2, 16);
    if (!ok1) off1 = 0;
    if (!ok2) off2 = 0;
    return off1 - off2;
}

void KPImportDlg::onOffsetChanged()
{
    bool ok1 = false, ok2 = false;
    int32_t off1 = (int32_t)m_offset1Edit->text().trimmed().toUInt(&ok1, 16);
    int32_t off2 = (int32_t)m_offset2Edit->text().trimmed().toUInt(&ok2, 16);
    if (!ok1) off1 = 0;
    if (!ok2) off2 = 0;

    int32_t off = off1 - off2;

    // Update the overview bar with both offsets
    m_overviewBar->setOffsets(off1, off2);

    // Re-compute match percentage
    int inRange = 0;
    for (const MapInfo &mi : m_maps) {
        int64_t addr = (int64_t)mi.address + off;
        int len = mi.length > 0 ? mi.length : mi.dataSize;
        if (addr >= 0 && addr + len <= m_romSize)
            inRange++;
    }
    double matchPct = m_maps.isEmpty() ? 0.0 : (100.0 * inRange / m_maps.size());
    m_matchLabel->setText(
        tr("Address match: %1% of maps within current ROM (%2 / %3)  |  Project ROM size: %4 bytes")
            .arg(matchPct, 0, 'f', 1)
            .arg(inRange).arg(m_maps.size())
            .arg(m_romSize));

    // Update address column in the table
    for (int row = 0; row < m_table->rowCount(); ++row) {
        int64_t addr = (int64_t)m_maps[row].address + off;
        auto *item = m_table->item(row, 3);
        if (item) {
            if (addr < 0)
                item->setText(tr("(negative)"));
            else
                item->setText(QString("0x%1").arg((uint32_t)addr, 6, 16, QChar('0')).toUpper());
        }
    }
}

void KPImportDlg::onAutoOffset()
{
    // Automatic offset detection strategy:
    // The KP file stores raw ECU addresses (e.g. 0x80xxxxxx for Tricore).
    // The ROM file is a flat dump starting at some base address.
    // We try common base addresses and pick the one that puts the most maps in range.

    static const uint32_t candidates[] = {
        0x80000000, 0x80010000, 0x80020000, 0x80040000, 0x80080000,
        0x80100000, 0x80800000, 0xA0000000, 0xA0010000, 0xA0080000,
        0x00000000, 0x00010000, 0x00020000, 0x00040000, 0x00100000,
        0x00400000, 0x00800000, 0x01000000,
    };

    int bestCount = -1;
    uint32_t bestBase = 0;

    for (uint32_t base : candidates) {
        int count = 0;
        for (const MapInfo &mi : m_maps) {
            int64_t addr = (int64_t)mi.address - (int64_t)base;
            int len = mi.length > 0 ? mi.length : mi.dataSize;
            if (addr >= 0 && addr + len <= m_romSize)
                count++;
        }
        if (count > bestCount) {
            bestCount = count;
            bestBase = base;
        }
    }

    // Also try using the difference between first map address and a scan of the ROM
    // for byte patterns (simple heuristic).

    // Apply: offset1 = 0, offset2 = bestBase (subtracted)
    m_offset1Edit->setText(QStringLiteral("0"));
    m_offset2Edit->setText(QString::number(bestBase, 16).toUpper());
}

// ── Select all / none ────────────────────────────────────────────────────────

void KPImportDlg::onSelectAll()
{
    for (int row = 0; row < m_table->rowCount(); ++row) {
        auto *w = m_table->cellWidget(row, 0);
        if (auto *chk = w ? w->findChild<QCheckBox *>() : nullptr)
            chk->setChecked(true);
    }
}

void KPImportDlg::onSelectNone()
{
    for (int row = 0; row < m_table->rowCount(); ++row) {
        auto *w = m_table->cellWidget(row, 0);
        if (auto *chk = w ? w->findChild<QCheckBox *>() : nullptr)
            chk->setChecked(false);
    }
}

// ── selectedMaps — returns maps with offset applied ──────────────────────────

QVector<MapInfo> KPImportDlg::selectedMaps() const
{
    QVector<MapInfo> result;
    int32_t off = totalOffset();
    QString prefix = m_prefixEdit->text().trimmed();

    for (int row = 0; row < m_table->rowCount(); ++row) {
        auto *w = m_table->cellWidget(row, 0);
        auto *chk = w ? w->findChild<QCheckBox *>() : nullptr;
        if (!chk || !chk->isChecked())
            continue;

        MapInfo mi = m_maps[row];

        // Apply offset to all addresses
        mi.address    = (uint32_t)((int64_t)mi.address + off);
        mi.rawAddress = (uint32_t)((int64_t)mi.rawAddress + off);
        if (mi.xAxis.hasPtsAddress)
            mi.xAxis.ptsAddress = (uint32_t)((int64_t)mi.xAxis.ptsAddress + off);
        if (mi.yAxis.hasPtsAddress)
            mi.yAxis.ptsAddress = (uint32_t)((int64_t)mi.yAxis.ptsAddress + off);

        // If "Also maps for which no hexdump is available" is UNCHECKED,
        // filter out maps whose calculated target address falls outside ROM bounds or is negative
        if (m_chkNoHexdump && !m_chkNoHexdump->isChecked()) {
            int len = mi.length > 0 ? mi.length : (mi.dimensions.x * mi.dimensions.y * mi.dataSize);
            if (len <= 0) len = mi.dataSize;
            if ((int64_t)mi.address < 0 || (int64_t)mi.address + len > (int64_t)m_romSize) {
                continue; // Skip out-of-bounds map when no-hexdump option is unchecked
            }
        }

        // Apply prefix
        if (!prefix.isEmpty())
            mi.name = prefix + mi.name;

        // Import structure options — if user unchecked "Map structure", clear
        // dimension/precision info so the caller only gets values
        if (!m_chkMapStruct->isChecked()) {
            mi.dimensions = {1, 1};
            mi.dataSize = 2;
            mi.dataSigned = false;
            mi.hasScaling = false;
            mi.scaling = CompuMethod();
        }

        result.append(mi);
    }

    // Avoid duplicates: remove maps whose address already appears earlier in the result
    if (m_chkAvoidDupes->isChecked()) {
        QVector<MapInfo> unique;
        QSet<uint32_t> seenAddrs;
        for (const MapInfo &mi : result) {
            if (seenAddrs.contains(mi.address))
                continue;
            seenAddrs.insert(mi.address);
            unique.append(mi);
        }
        return unique;
    }

    return result;
}

bool KPImportDlg::importMapValues() const
{
    return m_chkMapValues && m_chkMapValues->isChecked();
}

bool KPImportDlg::importNoHexdump() const
{
    return m_chkNoHexdump && m_chkNoHexdump->isChecked();
}

bool KPImportDlg::importEmptyFolders() const
{
    return m_chkEmptyFolders && m_chkEmptyFolders->isChecked();
}
