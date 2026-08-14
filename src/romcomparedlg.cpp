/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "romcomparedlg.h"
#include "patcheditordlg.h"
#include "mappackdlg.h"
#include "rompatch.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QLabel>
#include <QDialog>
#include <QSettings>
#include <QCloseEvent>
#include <cmath>

RomCompareDlg::RomCompareDlg(Project *project,
                              const QByteArray &cmpRom,
                              const QString &cmpLabel,
                              const QMap<QString, uint32_t> &cmpOffsets,
                              QWidget *parent)
    : QDialog(parent, Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint
              | Qt::WindowMaximizeButtonHint)
    , m_project(project)
    , m_cmpRom(cmpRom)
    , m_cmpLabel(cmpLabel)
    , m_cmpOffsets(cmpOffsets)
{
    setWindowTitle(tr("Compare  —  %1  vs  %2")
                   .arg(project->fullTitle()).arg(cmpLabel));
    resize(1100, 680);

    // Compute diffs up front
    m_mapDiffs = RomCompare::diffMaps(
        project->currentData, cmpRom,
        project->maps, project->byteOrder, cmpOffsets);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    m_tabs = new QTabWidget();
    root->addWidget(m_tabs, 1);

    buildMapTab();
    buildByteTab();

    auto *close = new QPushButton(tr("Close"));
    close->setFixedWidth(84);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch();
    btnRow->addWidget(close);
    root->addLayout(btnRow);

    restoreGeometry(QSettings("CT14", "RX14")
                    .value("dialogGeometry/RomCompareDlg").toByteArray());
}

void RomCompareDlg::closeEvent(QCloseEvent *event)
{
    QSettings("CT14", "RX14")
        .setValue("dialogGeometry/RomCompareDlg", saveGeometry());
    QDialog::closeEvent(event);
}

// ── Map differences tab ───────────────────────────────────────────────────────

void RomCompareDlg::buildMapTab()
{
    auto *page = new QWidget();
    auto *vl   = new QVBoxLayout(page);
    vl->setContentsMargins(6, 6, 6, 6);
    vl->setSpacing(6);

    // Summary banner
    m_mapSummary = new QLabel();
    m_mapSummary->setStyleSheet("color:#c9d1d9; font-size:9pt;");
    vl->addWidget(m_mapSummary);

    // Export actions
    {
        auto *row = new QHBoxLayout();
        row->setSpacing(6);

        auto *patchBtn = new QPushButton(tr("Create Patch Script…"));
        patchBtn->setToolTip(tr("Generate a .rxpatch script from these differences "
                                "and open the script editor"));
        patchBtn->setStyleSheet(
            "QPushButton { background:#1f6feb; color:#fff; border-radius:4px; padding:3px 10px; }"
            "QPushButton:hover { background:#388bfd; }");

        auto *packBtn = new QPushButton(tr("Export Map Pack…"));
        packBtn->setToolTip(tr("Save the modified maps as a .rxpack file "
                               "that can be applied to other ROMs"));
        packBtn->setStyleSheet(
            "QPushButton { background:#238636; color:#fff; border-radius:4px; padding:3px 10px; }"
            "QPushButton:hover { background:#2ea043; }");

        row->addWidget(patchBtn);
        row->addWidget(packBtn);
        row->addStretch();
        vl->addLayout(row);

        connect(patchBtn, &QPushButton::clicked, this, [this] {
            // Ask user whether to include raw byte differences (checksums/CRC)
            auto *dlg = new QDialog(this);
            dlg->setWindowTitle(tr("Create Patch"));
            dlg->resize(420, 0);
            auto *vl = new QVBoxLayout(dlg);
            vl->setContentsMargins(16, 16, 16, 16);
            vl->setSpacing(10);

            auto *title = new QLabel(tr("<b>What should the patch include?</b>"));
            title->setStyleSheet("color:#c9d1d9;");
            vl->addWidget(title);

            auto *mapsOnlyBtn = new QPushButton(
                tr("Maps only  (cross-ECU compatible)"));
            mapsOnlyBtn->setToolTip(tr(
                "Only includes A2L map cell changes.\n"
                "Safe to apply to any ECU with the same map layout.\n"
                "You will need to recalculate checksums separately."));
            mapsOnlyBtn->setStyleSheet(
                "QPushButton { background:#238636; color:#fff; border-radius:4px; "
                "padding:6px 14px; text-align:left; }"
                "QPushButton:hover { background:#2ea043; }");

            auto *fullBtn = new QPushButton(
                tr("Full patch  (exact reproduction — same ECU only)"));
            fullBtn->setToolTip(tr(
                "Also includes checksum/CRC bytes outside map regions.\n"
                "Reproduces the target ROM byte-for-byte.\n"
                "WARNING: Only apply to the same ECU variant and base ROM."));
            fullBtn->setStyleSheet(
                "QPushButton { background:#b45309; color:#fff; border-radius:4px; "
                "padding:6px 14px; text-align:left; }"
                "QPushButton:hover { background:#d97706; }");

            auto *note = new QLabel(tr(
                "<small style='color:#8b949e;'>"
                "The %1 ROM has byte differences outside A2L maps "
                "(likely ECU-specific checksums). A full patch captures these for "
                "exact reproduction but will produce incorrect checksums if applied "
                "to a different base ROM.</small>")
                .arg(m_cmpLabel));
            note->setWordWrap(true);
            vl->addWidget(title);
            vl->addWidget(note);
            vl->addSpacing(4);
            vl->addWidget(mapsOnlyBtn);
            vl->addWidget(fullBtn);

            auto *cancelBtn = new QPushButton(tr("Cancel"));
            cancelBtn->setFlat(true);
            cancelBtn->setStyleSheet("color:#8b949e;");
            vl->addWidget(cancelBtn);

            bool includeRaw = false;
            bool cancelled  = true;
            connect(mapsOnlyBtn, &QPushButton::clicked, dlg, [&]{ includeRaw = false; cancelled = false; dlg->accept(); });
            connect(fullBtn,     &QPushButton::clicked, dlg, [&]{ includeRaw = true;  cancelled = false; dlg->accept(); });
            connect(cancelBtn,   &QPushButton::clicked, dlg, &QDialog::reject);
            dlg->exec();
            dlg->deleteLater();
            if (cancelled) return;

            RomPatch p = RomPatch::fromDiffs(m_mapDiffs,
                                             m_project->currentData,
                                             m_cmpRom,
                                             m_project->byteOrder,
                                             includeRaw,
                                             m_project->fullTitle(),
                                             m_cmpLabel);
            auto *ed = new PatchEditorDlg(p, m_project, this);
            ed->setAttribute(Qt::WA_DeleteOnClose);
            ed->show();
        });

        connect(packBtn, &QPushButton::clicked, this, [this] {
            MapPackDlg::exportFromDiffs(m_mapDiffs, m_cmpRom,
                                        m_project->byteOrder,
                                        m_cmpLabel, m_project, this);
        });
    }

    // Map diff table (top half)
    m_mapTable = new QTableWidget();
    m_mapTable->setColumnCount(5);
    m_mapTable->setHorizontalHeaderLabels({
        tr("Map"), tr("Type"), tr("Changed cells"), tr("Max Δ"), tr("Avg Δ")});
    m_mapTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_mapTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_mapTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_mapTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_mapTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_mapTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_mapTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_mapTable->setAlternatingRowColors(true);
    m_mapTable->setSortingEnabled(true);
    m_mapTable->setStyleSheet(
        "QTableWidget { background:#0d1117; color:#c9d1d9; gridline-color:#21262d; }"
        "QTableWidget::item:alternate { background:#161b22; }"
        "QHeaderView::section { background:#161b22; color:#8b949e; border:none; padding:4px; }");
    connect(m_mapTable, &QTableWidget::cellClicked, this, &RomCompareDlg::onMapRowClicked);

    // Cell-value split view (bottom half)
    m_mapSplit = new QSplitter(Qt::Horizontal);

    auto *refPane = new QWidget();
    auto *refVl   = new QVBoxLayout(refPane);
    refVl->setContentsMargins(0,0,0,0);
    m_refLabel = new QLabel(tr("Reference: %1").arg(m_project->fullTitle()));
    m_refLabel->setStyleSheet("color:#58a6ff; font-size:9pt; font-weight:bold;");
    m_refCells = new QTableWidget();
    m_refCells->setEditTriggers(QAbstractItemView::NoEditTriggers);
    {
        QPalette pal = m_refCells->palette();
        pal.setColor(QPalette::Base,            QColor(0x0d, 0x11, 0x17));
        pal.setColor(QPalette::Text,            QColor(0xc9, 0xd1, 0xd9));
        pal.setColor(QPalette::AlternateBase,   QColor(0x16, 0x1b, 0x22));
        m_refCells->setPalette(pal);
    }
    m_refCells->setStyleSheet(
        "QTableWidget { gridline-color:#21262d; font-size:8pt; }"
        "QTableWidget::item:selected { background:#1f6feb; }"
        "QHeaderView::section { background:#161b22; color:#8b949e; border:none; padding:3px; }");
    refVl->addWidget(m_refLabel);
    refVl->addWidget(m_refCells, 1);
    m_mapSplit->addWidget(refPane);

    auto *cmpPane = new QWidget();
    auto *cmpVl   = new QVBoxLayout(cmpPane);
    cmpVl->setContentsMargins(0,0,0,0);
    m_cmpLabel2 = new QLabel(tr("Compare: %1").arg(m_cmpLabel));
    m_cmpLabel2->setStyleSheet("color:#d29a22; font-size:9pt; font-weight:bold;");
    m_cmpCells = new QTableWidget();
    m_cmpCells->setEditTriggers(QAbstractItemView::NoEditTriggers);
    {
        QPalette pal = m_cmpCells->palette();
        pal.setColor(QPalette::Base,            QColor(0x0d, 0x11, 0x17));
        pal.setColor(QPalette::Text,            QColor(0xc9, 0xd1, 0xd9));
        pal.setColor(QPalette::AlternateBase,   QColor(0x16, 0x1b, 0x22));
        m_cmpCells->setPalette(pal);
    }
    m_cmpCells->setStyleSheet(
        "QTableWidget { gridline-color:#21262d; font-size:8pt; }"
        "QTableWidget::item:selected { background:#1f6feb; }"
        "QHeaderView::section { background:#161b22; color:#8b949e; border:none; padding:3px; }");
    cmpVl->addWidget(m_cmpLabel2);
    cmpVl->addWidget(m_cmpCells, 1);
    m_mapSplit->addWidget(cmpPane);

    auto *topBottom = new QSplitter(Qt::Vertical);
    topBottom->addWidget(m_mapTable);
    topBottom->addWidget(m_mapSplit);
    topBottom->setSizes({300, 300});
    vl->addWidget(topBottom, 1);

    populateMapTable(m_mapDiffs);

    m_tabs->addTab(page, tr("Map Differences  (%1)").arg(m_mapDiffs.size()));
}

void RomCompareDlg::populateMapTable(const QVector<MapDiff> &diffs)
{
    m_mapTable->setSortingEnabled(false);
    m_mapTable->setRowCount(diffs.size());

    for (int i = 0; i < diffs.size(); ++i) {
        const auto &md = diffs[i];
        const QString &unit = md.map.hasScaling ? md.map.scaling.unit : QString();

        auto *nameItem = new QTableWidgetItem(md.map.name);
        auto *typeItem = new QTableWidgetItem(md.map.type);
        auto *chgItem  = new QTableWidgetItem();
        chgItem->setData(Qt::DisplayRole,
            QString("%1 / %2").arg(md.changedCells)
                .arg(md.map.dimensions.x * md.map.dimensions.y));
        chgItem->setData(Qt::UserRole, md.changedCells);

        auto *maxItem  = new QTableWidgetItem();
        maxItem->setData(Qt::DisplayRole,
            QString("%1%2").arg(md.maxAbsDelta, 0, 'f', 3)
                .arg(unit.isEmpty() ? "" : " " + unit));
        maxItem->setData(Qt::UserRole, md.maxAbsDelta);

        auto *avgItem  = new QTableWidgetItem();
        avgItem->setData(Qt::DisplayRole,
            QString("%1%2").arg(md.avgAbsDelta, 0, 'f', 3)
                .arg(unit.isEmpty() ? "" : " " + unit));
        avgItem->setData(Qt::UserRole, md.avgAbsDelta);

        // Colour code by change severity
        if (md.changedCells > 0) {
            QColor rowColor = md.maxAbsDelta > 5.0
                ? QColor(0xff, 0x7b, 0x72)   // red — big changes
                : QColor(0xd2, 0x9a, 0x22);  // yellow — small changes
            nameItem->setForeground(rowColor);
        }

        m_mapTable->setItem(i, 0, nameItem);
        m_mapTable->setItem(i, 1, typeItem);
        m_mapTable->setItem(i, 2, chgItem);
        m_mapTable->setItem(i, 3, maxItem);
        m_mapTable->setItem(i, 4, avgItem);
    }

    m_mapTable->setSortingEnabled(true);
    m_mapTable->sortByColumn(3, Qt::DescendingOrder);  // sort by max delta

    int totalMaps    = m_project->maps.size();
    int changedMaps  = m_mapDiffs.size();
    m_mapSummary->setText(tr(
        "<b>%1</b> of <b>%2</b> maps have differences.  "
        "Reference: <span style='color:#58a6ff;'>%3</span>   "
        "Compare: <span style='color:#d29a22;'>%4</span>")
        .arg(changedMaps).arg(totalMaps)
        .arg(m_project->fullTitle()).arg(m_cmpLabel));
}

void RomCompareDlg::onMapRowClicked(int row, int /*col*/)
{
    // Find the MapDiff corresponding to this (sorted) row
    auto *nameItem = m_mapTable->item(row, 0);
    if (!nameItem) return;
    QString mapName = nameItem->text();
    for (const auto &md : m_mapDiffs) {
        if (md.map.name == mapName) {
            showMapCells(md);
            return;
        }
    }
}

void RomCompareDlg::showMapCells(const MapDiff &md)
{
    const int rows   = qMax(1, md.map.dimensions.y);
    const int cols   = qMax(1, md.map.dimensions.x);
    const int cells  = rows * cols;
    const int dSize  = md.map.dataSize;
    const ByteOrder bo     = m_project->byteOrder;
    const bool      colMaj = md.map.columnMajor;
    const QString   unit   = md.map.hasScaling ? md.map.scaling.unit : QString();

    // Memory index for display cell (r,c) respecting storage order
    auto memIdx = [&](int r, int c) -> int {
        return colMaj ? c * rows + r : r * cols + c;
    };

    // Axis header labels — read from refRom using absolute ptsAddress
    const QByteArray &refRom = m_project->currentData;
    auto readAxisLabels = [&](const AxisInfo &ax, int count) -> QStringList {
        // Unit suffix appended to every label, e.g. "512 rpm", "25 %"
        const QString suffix = (ax.hasScaling && !ax.scaling.unit.isEmpty())
                               ? (" " + ax.scaling.unit) : QString();
        QStringList out;
        if (!ax.fixedValues.isEmpty()) {
            for (int i = 0; i < count && i < ax.fixedValues.size(); ++i) {
                double v = ax.fixedValues[i];
                out << (ax.hasScaling ? ax.scaling.formatValue(v) : QString::number(v))
                       + suffix;
            }
        } else if (ax.hasPtsAddress && ax.ptsDataSize > 0) {
            // ptsCount == 0 for COM_AXIS — use display 'count' as fallback
            int readCount = ax.ptsCount > 0 ? ax.ptsCount : count;
            int len = readCount * ax.ptsDataSize;
            if ((int)(ax.ptsAddress + len) <= refRom.size()) {
                const uint8_t *dp = (const uint8_t *)refRom.constData() + ax.ptsAddress;
                for (int i = 0; i < readCount && out.size() < count; ++i) {
                    uint32_t raw = readRomValue(dp, len,
                                               (uint32_t)(i * ax.ptsDataSize),
                                               ax.ptsDataSize, bo);
                    double phys = ax.hasScaling ? ax.scaling.toPhysical(signExtendRaw(raw, ax.ptsDataSize, ax.ptsSigned))
                                               : signExtendRaw(raw, ax.ptsDataSize, ax.ptsSigned);
                    out << (ax.hasScaling ? ax.scaling.formatValue(phys)
                                         : QString::number(raw))
                           + suffix;
                }
            }
        }
        while (out.size() < count) out << QString::number(out.size());
        return out;
    };

    // Axis name + unit for labels — e.g.  "nmot_w [1/min]"
    auto axTitle = [](const AxisInfo &ax) -> QString {
        QString s = ax.inputName;
        if (ax.hasScaling && !ax.scaling.unit.isEmpty())
            s += "  [" + ax.scaling.unit + "]";
        return s;
    };

    QStringList xLabels = readAxisLabels(md.map.xAxis, cols);
    QStringList yLabels = readAxisLabels(md.map.yAxis, rows);

    // Update panel labels to show the selected map + axis context
    QString axInfo;
    QString xTitle = axTitle(md.map.xAxis);
    QString yTitle = axTitle(md.map.yAxis);
    if (!xTitle.trimmed().isEmpty() || !yTitle.trimmed().isEmpty())
        axInfo = QString("   ·   X: %1   Y: %2").arg(xTitle).arg(yTitle);
    QString mapDesc = QString("%1  (%2  %3×%4)%5")
                      .arg(md.map.name).arg(md.map.type)
                      .arg(cols).arg(rows).arg(axInfo);
    m_refLabel->setText(tr("Reference: %1   —   %2").arg(m_project->fullTitle()).arg(mapDesc));
    m_cmpLabel2->setText(tr("Compare: %1   —   %2").arg(m_cmpLabel).arg(mapDesc));

    // Populate a table with plain values (no coloring yet)
    auto setupTable = [&](QTableWidget *t, const QByteArray &romData, uint32_t romOff) {
        t->clear();
        t->setRowCount(rows);
        t->setColumnCount(cols);
        t->setHorizontalHeaderLabels(xLabels);
        t->setVerticalHeaderLabels(yLabels);

        int dataOff = (int)md.map.mapDataOffset;
        int dataLen = cells * dSize;
        if ((int)(romOff + dataOff + dataLen) > romData.size()) return;
        const uint8_t *dp = (const uint8_t *)romData.constData() + romOff + dataOff;

        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                int      mi  = memIdx(r, c);
                uint32_t raw = readRomValue(dp, dataLen, (uint32_t)(mi * dSize), dSize, bo);
                double   phy = md.map.hasScaling ? md.map.scaling.toPhysical(signExtendRaw(raw, dSize, md.map.dataSigned))
                                                 : signExtendRaw(raw, dSize, md.map.dataSigned);
                QString  txt = md.map.hasScaling ? md.map.scaling.formatValue(phy)
                                                 : QString::number(raw);
                auto *item = new QTableWidgetItem(txt);
                item->setTextAlignment(Qt::AlignCenter);
                t->setItem(r, c, item);
            }
        }
    };

    setupTable(m_refCells, m_project->currentData, md.refOffset);
    setupTable(m_cmpCells, m_cmpRom,               md.cmpOffset);

    // ── Per-cell coloring + delta annotation ─────────────────────────────────
    // Fully-opaque colours blended from the dark bg toward a target colour.
    // norm 0→1 maps to t 0.35→1.0 so even the smallest change is clearly visible.
    auto blend = [](int r0, int g0, int b0, double norm) -> QColor {
        double t = 0.35 + 0.65 * norm;
        const int bgR = 13, bgG = 17, bgB = 23;
        return QColor(bgR + (int)((r0 - bgR) * t),
                      bgG + (int)((g0 - bgG) * t),
                      bgB + (int)((b0 - bgB) * t));
    };

    int dataOff = (int)md.map.mapDataOffset;
    int dataLen = cells * dSize;
    const uint8_t *rp = (const uint8_t *)refRom.constData()   + md.refOffset + dataOff;
    const uint8_t *cp = (const uint8_t *)m_cmpRom.constData() + md.cmpOffset + dataOff;

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int      mi = memIdx(r, c);
            uint32_t rv = readRomValue(rp, dataLen, (uint32_t)(mi * dSize), dSize, bo);
            uint32_t cv = readRomValue(cp, dataLen, (uint32_t)(mi * dSize), dSize, bo);
            if (rv == cv) continue;

            double delta = md.cellDeltas.value(mi, 0.0);
            double norm  = (md.maxAbsDelta > 0)
                           ? std::fabs(delta) / md.maxAbsDelta : 0.0;

            // Ref — blue tint: minimum (35%) = rgb(27,42,81), max = rgb(55,95,175)
            if (auto *item = m_refCells->item(r, c))
                item->setBackground(blend(55, 95, 175, norm));

            // Cmp — green (up) or red (down)
            if (auto *item = m_cmpCells->item(r, c)) {
                bool up = delta > 0;
                // green min = rgb(24,53,30) max = rgb(40,140,55)
                // red   min = rgb(64,22,22) max = rgb(180,40,40)
                item->setBackground(up ? blend(40, 140, 55, norm)
                                       : blend(180, 40, 40, norm));
                item->setForeground(QColor(220, 230, 240));

                // Delta: compact percentage under the value in the cell text
                double refPhys = md.map.hasScaling
                                 ? md.map.scaling.toPhysical(signExtendRaw(rv, dSize, md.map.dataSigned))
                                 : signExtendRaw(rv, dSize, md.map.dataSigned);
                QString deltaStr;
                if (std::fabs(refPhys) > 1e-9) {
                    double pct = (delta / std::fabs(refPhys)) * 100.0;
                    deltaStr = QString("%1%2%")
                               .arg(pct >= 0 ? "+" : "")
                               .arg(pct, 0, 'f', std::fabs(pct) < 10.0 ? 1 : 0);
                } else {
                    QString absStr = md.map.hasScaling
                                  ? md.map.scaling.formatValue(std::fabs(delta))
                                  : QString::number((long long)std::round(std::fabs(delta)));
                    deltaStr = (delta >= 0 ? "+" : "-") + absStr;
                }
                item->setText(item->text() + "\n" + deltaStr);
            }
        }
    }

    // Uniform row height — tall enough for value + delta line
    // Use the table's own font metrics so it scales with screen DPI / zoom
    {
        int lineH  = QFontMetrics(m_refCells->font()).height();
        int rowH   = lineH * 3 + 6;   // ~2 text lines + padding
        m_refCells->verticalHeader()->setDefaultSectionSize(rowH);
        m_cmpCells->verticalHeader()->setDefaultSectionSize(rowH);
    }
    m_refCells->resizeColumnsToContents();
    m_cmpCells->resizeColumnsToContents();
}

// ── Byte diff tab ─────────────────────────────────────────────────────────────

void RomCompareDlg::buildByteTab()
{
    auto *page = new QWidget();
    auto *vl   = new QVBoxLayout(page);
    vl->setContentsMargins(4, 4, 4, 4);

    auto *info = new QLabel(tr("Byte-level differences between the two ROMs (same-length comparison):"));
    info->setStyleSheet("color:#8b949e; font-size:8pt;");
    vl->addWidget(info);

    m_byteDiff = new ByteDiffWidget();
    m_byteDiff->setData(m_project->currentData, m_cmpRom);
    vl->addWidget(m_byteDiff, 1);

    const qint64 diffCount =
        RomCompare::diffBytesSummary(m_project->currentData, m_cmpRom).count;
    m_tabs->addTab(page, tr("Byte Diff  (%1 bytes)").arg(diffCount));
}
