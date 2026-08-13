/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "aifunctionsdlg.h"
#include "project.h"
#include "romdata.h"
#include "uiwidgets.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QLineEdit>
#include <QMessageBox>
#include <QScrollArea>
#include <QPropertyAnimation>
#include <QTimer>
#include <QRegularExpression>
#include <QProgressDialog>
#include <QCoreApplication>
#include <QApplication>

// ─────────────────────────────────────────────────────────────────────────────
// Style constants (dark theme matching the rest of the app)
// ─────────────────────────────────────────────────────────────────────────────
static const char *kDialogStyle =
    "QDialog { background: #0d1117; color: #c9d1d9; }"
    "QLabel  { color: #c9d1d9; }"
    "QTreeWidget { background: #161b22; color: #c9d1d9; border: 1px solid #30363d;"
    "  border-radius: 6px; font-family: Consolas, monospace; font-size: 10pt; }"
    "QTreeWidget::item { padding: 4px 0; }"
    "QTreeWidget::item:selected { background: #1f6feb; }"
    "QHeaderView::section { background: #161b22; color: #8b949e; border: none;"
    "  border-bottom: 1px solid #30363d; padding: 6px 8px; font-weight: 600; }"
    "QLineEdit { background: #0d1117; color: #c9d1d9; border: 1px solid #30363d;"
    "  border-radius: 4px; padding: 4px 8px; }"
    "QScrollArea { background: transparent; border: none; }"
    "QComboBox { background: #161b22; color: #c9d1d9; border: 1px solid #30363d;"
    "  border-radius: 4px; padding: 4px 8px; font-size: 10pt; min-width: 200px; }"
    "QComboBox::drop-down { border: none; }"
    "QComboBox QAbstractItemView { background: #161b22; color: #c9d1d9;"
    "  border: 1px solid #30363d; selection-background-color: #1f6feb; }";

static const char *kCardStyle =
    "QPushButton {"
    "  background: #161b22; color: #c9d1d9; border: 1px solid #30363d;"
    "  border-radius: 10px; padding: 16px 12px; text-align: left;"
    "  font-size: 10pt;"
    "}"
    "QPushButton:hover {"
    "  background: #1c2333; border-color: #58a6ff;"
    "}"
    "QPushButton:pressed {"
    "  background: #0d1117;"
    "}";

static const char *kApplyBtnStyle =
    "QPushButton { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
    "  stop:0 #238636, stop:1 #2ea043); color: white; border: none;"
    "  border-radius: 8px; padding: 10px 32px; font-weight: 700; font-size: 10pt; }"
    "QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
    "  stop:0 #2ea043, stop:1 #3fb950); }";

static const char *kCancelBtnStyle =
    "QPushButton { background: rgba(15,22,41,0.6); border: 1px solid rgba(231,238,252,0.1);"
    "  color: #a9b6d3; border-radius: 8px; padding: 10px 24px; font-size: 10pt; }"
    "QPushButton:hover { border-color: #58a6ff; color: #e7eefc; }";


// ═════════════════════════════════════════════════════════════════════════════
//  AIFunctionsDlg  —  main grid of function cards
// ═════════════════════════════════════════════════════════════════════════════

AIFunctionsDlg::AIFunctionsDlg(const QVector<Project*> &projects, Project *activeProject,
                               QWidget *parent)
    : QDialog(parent), m_projects(projects), m_project(activeProject)
{
    setWindowTitle(tr("AI Tuning Functions"));
    setMinimumSize(720, 560);
    resize(780, 620);
    setStyleSheet(kDialogStyle);

    buildFunctions();

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(16);

    // Header
    auto *header = new QLabel(
        QStringLiteral("<span style='font-size:16pt; font-weight:700; color:#58a6ff;'>")
        + tr("AI Tuning Functions")
        + QStringLiteral("</span><br><span style='font-size:9pt; color:#8b949e;'>")
        + tr("Select a function to search and modify ECU maps automatically")
        + QStringLiteral("</span>"));
    header->setTextFormat(Qt::RichText);
    root->addWidget(header);

    // Target ROM selector
    auto *targetRow = new QHBoxLayout();
    targetRow->setSpacing(10);

    auto *projLabel = new QLabel(
        QStringLiteral("<span style='font-size:9pt; color:#8b949e;'>")
        + tr("Project:") + QStringLiteral(" </span><span style='font-size:9pt; color:#c9d1d9; font-weight:600;'>")
        + (m_project ? m_project->listLabel() : tr("(none)"))
        + QStringLiteral("</span>"));
    projLabel->setTextFormat(Qt::RichText);
    targetRow->addWidget(projLabel);

    targetRow->addSpacing(16);

    auto *targetLabel = new QLabel(tr("Apply to:"));
    targetLabel->setStyleSheet("color: #8b949e; font-size: 9pt;");
    targetRow->addWidget(targetLabel);

    m_targetCombo = new QComboBox();
    // List all open projects — active one first
    int activeIdx = 0;
    for (int pi = 0; pi < m_projects.size(); ++pi) {
        auto *p = m_projects[pi];
        QString label = p->listLabel();
        if (p->isLinkedReference)
            label.prepend(tr("[ORI] "));
        if (p == m_project) {
            label += tr(" (active)");
            activeIdx = pi;
        }
        m_targetCombo->addItem(label, pi);
    }
    m_targetCombo->setCurrentIndex(activeIdx);
    connect(m_targetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        int pi = m_targetCombo->itemData(idx).toInt();
        if (pi >= 0 && pi < m_projects.size())
            m_project = m_projects[pi];
    });
    targetRow->addWidget(m_targetCombo);
    targetRow->addStretch();

    root->addLayout(targetRow);

    // Scrollable grid
    auto *scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto *gridWidget = new QWidget();
    m_grid = new QGridLayout(gridWidget);
    m_grid->setSpacing(12);

    const int columns = 3;
    for (int i = 0; i < m_functions.size(); ++i) {
        const auto &fn = m_functions[i];

        auto *btn = new QPushButton();
        btn->setStyleSheet(kCardStyle);
        btn->setMinimumHeight(90);
        btn->setCursor(Qt::PointingHandCursor);

        // Rich-text label inside button via layout
        auto *btnLay = new QVBoxLayout(btn);
        btnLay->setContentsMargins(4, 4, 4, 4);
        btnLay->setSpacing(2);

        auto *titleLabel = new QLabel(
            QStringLiteral("<span style='font-size:14pt;'>") + fn.emoji
            + QStringLiteral("</span> <b>") + fn.name + QStringLiteral("</b>"));
        titleLabel->setTextFormat(Qt::RichText);
        titleLabel->setStyleSheet("color: #c9d1d9; background: transparent;");
        titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

        auto *descLabel = new QLabel(
            QStringLiteral("<span style='font-size:8pt; color:#8b949e;'>")
            + fn.description + QStringLiteral("</span>"));
        descLabel->setTextFormat(Qt::RichText);
        descLabel->setWordWrap(true);
        descLabel->setStyleSheet("background: transparent;");
        descLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

        btnLay->addWidget(titleLabel);
        btnLay->addWidget(descLabel);

        int row = i / columns;
        int col = i % columns;
        m_grid->addWidget(btn, row, col);

        connect(btn, &QPushButton::clicked, this, [this, i]() { onFunctionClicked(i); });
    }

    scrollArea->setWidget(gridWidget);
    root->addWidget(scrollArea, 1);

    // Close button
    auto *bottomRow = new QHBoxLayout();
    bottomRow->addStretch();
    auto *closeBtn = new QPushButton(tr("Close"));
    closeBtn->setStyleSheet(kCancelBtnStyle);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    bottomRow->addWidget(closeBtn);
    root->addLayout(bottomRow);
}

void AIFunctionsDlg::buildFunctions()
{
    m_functions = {
        { "decat",      QString::fromUtf8("\xF0\x9F\x90\xB1"),
          tr("Decat / Catalyst Off"),
          tr("Disable catalyst monitoring and related DTCs"),
          {"KAT*", "DKAT*", "*DKAT*", "*KATDIAG*", "OSCKAT*", "DFC*KAT*"},
          "zero" },

        { "dpf",        QString::fromUtf8("\xF0\x9F\x92\xA8"),
          tr("DPF Delete"),
          tr("Remove DPF regeneration and soot monitoring"),
          {"DPF*", "*DPF*", "RUSS*", "PARMON*", "DFC*DPF*"},
          "zero" },

        { "egr",        QString::fromUtf8("\xE2\x99\xBB\xEF\xB8\x8F"),
          tr("EGR Off"),
          tr("Disable exhaust gas recirculation valve"),
          {"AGR*", "*AGR*", "DAGR*", "EGR*", "DFC*EGR*"},
          "zero" },

        { "adblue",     QString::fromUtf8("\xF0\x9F\x92\xA7"),
          tr("AdBlue / SCR Off"),
          tr("Disable AdBlue dosing, SCR catalyst and NOx reduction"),
          {"HARNST*", "DOSMOD*", "*DOSMOD*", "SCRDOS*", "*SCRDOS*", "ADBLUE*", "*ADBLUE*", "DNOX*", "*DNOX*", "NOXRED*", "*NOXRED*", "REDUCT*", "DFC*SCR*", "DFC*NOX*", "DFC*HARNST*"},
          "zero" },

        { "swirl",      QString::fromUtf8("\xF0\x9F\x8C\x80"),
          tr("Swirl Flap Delete"),
          tr("Disable swirl flap / charge motion flap (LBK) control"),
          {"DKBA*", "SWIRL*", "DKSBA*", "LBK*", "*LBK*", "KLBK*", "DFC*LBK*", "DFC*DKBA*", "DFC*SWIRL*"},
          "zero" },

        { "speedlim",   QString::fromUtf8("\xF0\x9F\x8F\x8E"),
          tr("Speed Limiter Off"),
          tr("Remove or raise the top speed limiter"),
          {"VMAX*", "VFIL*", "VBEG*", "*VMAX*"},
          "limiter" },

        { "startstop",  QString::fromUtf8("\xE2\x8F\xB9"),
          tr("Start-Stop Disable"),
          tr("Disable automatic engine start-stop system"),
          {"SSA*", "STST*", "STARTSTOP*"},
          "zero" },
    };
}

bool AIFunctionsDlg::wildcardMatch(const QString &name, const QString &pattern) const
{
    // Convert simple wildcard pattern to case-insensitive regex
    // Patterns: "KAT*" (starts with), "*KAT*" (contains), "DFC*KAT*" (starts with DFC, contains KAT)
    QString re;
    for (int i = 0; i < pattern.size(); ++i) {
        QChar c = pattern[i];
        if (c == '*')
            re += ".*";
        else if (c == '?')
            re += ".";
        else
            re += QRegularExpression::escape(QString(c));
    }
    QRegularExpression regex("^" + re + "$", QRegularExpression::CaseInsensitiveOption);
    return regex.match(name).hasMatch();
}

QVector<MapInfo> AIFunctionsDlg::searchMaps(const QStringList &patterns) const
{
    QVector<MapInfo> result;
    if (!m_project) return result;

    QSet<QString> seen;
    for (const MapInfo &mi : m_project->maps) {
        if (seen.contains(mi.name)) continue;
        for (const QString &pat : patterns) {
            if (wildcardMatch(mi.name, pat)) {
                result.append(mi);
                seen.insert(mi.name);
                break;
            }
        }
    }
    return result;
}

void AIFunctionsDlg::onFunctionClicked(int index)
{
    if (index < 0 || index >= m_functions.size()) return;
    if (!m_project) return;

    const auto &fn = m_functions[index];
    QVector<MapInfo> matched = searchMaps(fn.patterns);

    if (matched.isEmpty()) {
        QMessageBox::information(this,
            tr("No Maps Found"),
            tr("No maps matching the patterns for <b>%1</b> were found in this project.")
                .arg(fn.name));
        return;
    }

    // ── AI Risk Notice (universal confirm, no robot animation) ──────────────
    {
        UI::RiskyChangeConfirmDialog notice(this);
        notice.setHeadline(tr("AI Tuning: %1").arg(fn.name));
        notice.setDescription(tr(
            "AI tuning functions automatically identify and modify ECU maps "
            "based on pattern matching. Incorrect changes can damage your "
            "engine or vehicle.\n\n"
            "'%1' matched %2 map(s). On the next screen, you will see each "
            "map with its current value and proposed new value. Please review "
            "every map before confirming.")
            .arg(fn.name).arg(matched.size()));
        notice.setRisk(UI::RiskyChangeConfirmDialog::Risk::Caution);
        notice.setSnapshotOption(false);
        notice.setActionText(tr("Continue"));
        if (notice.exec() != QDialog::Accepted)
            return;
    }

    // Determine target name from combo selection
    QString targetName = m_targetCombo->currentText();
    int targetIdx = m_targetCombo->currentData().toInt();

    // Warn if a version snapshot is selected
    if (targetIdx >= 0) {
        auto answer = QMessageBox::warning(this,
            tr("Version Snapshot Selected"),
            tr("You selected <b>%1</b>, which is a saved snapshot.<br><br>"
               "Changes will be applied to the <b>Current ROM (working)</b> data. "
               "If you want to modify that version, restore it first from the Versions panel, "
               "then apply the AI function.")
                .arg(targetName),
            QMessageBox::Ok | QMessageBox::Cancel);
        if (answer != QMessageBox::Ok)
            return;
        targetName = tr("Current ROM (working)");
    }

    AIFunctionConfirmDlg dlg(fn, matched, m_project, targetName, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    // ── Build the universal risky-change confirm from pending rows ─────────
    auto pending = dlg.pendingRows();
    if (pending.isEmpty()) return;

    QVector<UI::RiskyChangeConfirmDialog::ChangeRow> rows;
    rows.reserve(pending.size());
    for (const auto &pr : pending) {
        UI::RiskyChangeConfirmDialog::ChangeRow cr;
        cr.label    = pr.mapName;
        cr.oldValue = pr.oldValue;
        cr.newValue = pr.newValue;
        rows.append(cr);
    }

    UI::RiskyChangeConfirmDialog confirm(this);
    confirm.setHeadline(tr("Apply AI function: %1").arg(fn.name));
    confirm.setDescription(tr(
        "This will overwrite %1 map(s) in the active ROM. A version snapshot "
        "will let you revert if needed.").arg(pending.size()));
    confirm.setRisk(UI::RiskyChangeConfirmDialog::Risk::Caution);
    confirm.setChanges(rows);
    confirm.setSnapshotOption(true, true);
    confirm.setActionText(tr("Apply"));
    if (pending.size() > 20)
        confirm.setRequireTypedConfirmation(QStringLiteral("APPLY"));

    if (confirm.exec() != QDialog::Accepted)
        return;

    if (confirm.snapshotChecked())
        m_project->snapshotVersion(tr("Before AI function: %1").arg(fn.name));

    QByteArray backup = m_project->currentData;

    QProgressDialog progress(tr("Applying changes..."), tr("Cancel"),
                             0, pending.size(), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(500);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.setValue(0);

    int count = dlg.applyChanges(&progress);
    progress.close();
    QApplication::restoreOverrideCursor();

    if (count < 0) {
        // Canceled mid-apply — roll back in memory
        m_project->currentData = backup;
        QMessageBox::information(this, tr("Canceled"),
            tr("Apply was canceled and changes have been rolled back."));
        return;
    }

    if (count > 0) {
        QByteArray mutated = m_project->currentData;
        m_project->currentData = backup;

        QVector<QPair<int, QByteArray>> patches;
        const int len = qMin(mutated.size(), m_project->currentData.size());
        for (int i = 0; i < len; ++i) {
            if (mutated[i] != m_project->currentData[i]) {
                int start = i;
                while (i < len && mutated[i] != m_project->currentData[i]) ++i;
                patches.append({start, mutated.mid(start, i - start)});
            }
        }
        if (!patches.isEmpty()) {
            m_project->applyRomPatches(patches, tr("AI function: %1").arg(fn.name));
        }
        emit projectModified();

        QMessageBox::information(this,
            tr("Changes Applied"),
            tr("%1 map(s) modified for <b>%2</b>.")
                .arg(count).arg(fn.name));
    }
}


// ═════════════════════════════════════════════════════════════════════════════
//  AIFunctionConfirmDlg  —  review & apply matched maps
// ═════════════════════════════════════════════════════════════════════════════

AIFunctionConfirmDlg::AIFunctionConfirmDlg(const AIFunction &func,
                                           const QVector<MapInfo> &matched,
                                           Project *project,
                                           const QString &targetName,
                                           QWidget *parent)
    : QDialog(parent), m_func(func), m_project(project),
      m_targetName(targetName), m_maps(matched)
{
    setWindowTitle(tr("Confirm: %1").arg(func.name));
    setMinimumSize(640, 480);
    resize(720, 540);
    setStyleSheet(kDialogStyle);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    // Header
    auto *header = new QLabel(
        QStringLiteral("<span style='font-size:13pt; font-weight:700; color:#58a6ff;'>")
        + func.emoji + QStringLiteral(" ") + func.name
        + QStringLiteral("</span><br><span style='font-size:9pt; color:#8b949e;'>")
        + tr("Review matched maps and uncheck any you want to skip")
        + QStringLiteral("</span>"));
    header->setTextFormat(Qt::RichText);
    root->addWidget(header);

    // Target indicator
    auto *targetInfo = new QLabel(
        QStringLiteral("<span style='font-size:9pt; color:#8b949e;'>")
        + tr("Applying to:") + QStringLiteral(" </span>")
        + QStringLiteral("<span style='font-size:9pt; color:#3fb950; font-weight:600;'>")
        + m_targetName
        + QStringLiteral("</span>"));
    targetInfo->setTextFormat(Qt::RichText);
    targetInfo->setStyleSheet("background: #161b22; border: 1px solid #30363d;"
                              " border-radius: 4px; padding: 6px 10px;");
    root->addWidget(targetInfo);

    // Tree widget
    m_tree = new QTreeWidget();
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({
        tr("Include"),
        tr("Map Name"),
        tr("Current Value"),
        tr("Action")
    });
    m_tree->header()->setStretchLastSection(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(false);
    m_tree->setSelectionMode(QAbstractItemView::NoSelection);

    populateTree(matched);
    root->addWidget(m_tree, 1);

    // Summary
    m_summary = new QLabel();
    m_summary->setStyleSheet("color: #8b949e; font-size: 9pt; padding: 4px;");
    auto updateSummary = [this]() {
        int checked = 0;
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
            if (m_tree->topLevelItem(i)->checkState(0) == Qt::Checked)
                ++checked;
        }
        m_summary->setText(tr("%1 of %2 maps selected")
            .arg(checked).arg(m_tree->topLevelItemCount()));
    };
    updateSummary();
    connect(m_tree, &QTreeWidget::itemChanged, this, [updateSummary]() { updateSummary(); });
    root->addWidget(m_summary);

    // Buttons
    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch();

    auto *cancelBtn = new QPushButton(tr("Cancel"));
    cancelBtn->setStyleSheet(kCancelBtnStyle);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(cancelBtn);

    auto *applyBtn = new QPushButton(tr("Apply Changes"));
    applyBtn->setStyleSheet(kApplyBtnStyle);
    applyBtn->setDefault(true);
    connect(applyBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(applyBtn);

    root->addLayout(btnRow);
}

double AIFunctionConfirmDlg::firstCellValue(const MapInfo &mi) const
{
    if (!m_project || m_project->currentData.isEmpty()) return 0;
    const uint8_t *rom = reinterpret_cast<const uint8_t*>(m_project->currentData.constData());
    int romLen = m_project->currentData.size();
    uint32_t offset = mi.address + mi.mapDataOffset;
    return readRomValueAsDouble(rom, romLen, offset, mi.dataSize, m_project->byteOrder, mi.dataSigned);
}

void AIFunctionConfirmDlg::populateTree(const QVector<MapInfo> &matched)
{
    for (int i = 0; i < matched.size(); ++i) {
        const MapInfo &mi = matched[i];
        auto *item = new QTreeWidgetItem();

        // Column 0: checkbox
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, Qt::Checked);

        // Column 1: map name
        item->setText(1, mi.name);

        // Column 2: current value (first cell, raw number)
        double val = firstCellValue(mi);
        if (mi.hasScaling)
            item->setText(2, mi.scaling.formatValue(mi.scaling.toPhysical(val)));
        else
            item->setText(2, QString::number(val, 'f', (val == (int)val) ? 0 : 2));

        // Column 3: action — always editable
        item->setText(3, "");
        item->setData(0, Qt::UserRole, i); // store index into m_maps
        m_tree->addTopLevelItem(item);
    }

    // Add editable line-edit in column 3 for ALL functions
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto *item = m_tree->topLevelItem(i);
        auto *edit = new QLineEdit();
        edit->setPlaceholderText(tr("New value"));
        edit->setStyleSheet(
            "QLineEdit { background: #0d1117; color: #c9d1d9; border: 1px solid #30363d;"
            "  border-radius: 4px; padding: 2px 6px; max-width: 120px; }");
        // Pre-fill: 0 for zero-type, current value for limiter-type
        if (m_func.actionType == "limiter")
            edit->setText(item->text(2));
        else
            edit->setText("0");
        m_tree->setItemWidget(item, 3, edit);
    }
}

int AIFunctionConfirmDlg::applyChanges(QProgressDialog *progress)
{
    if (!m_project || m_project->currentData.isEmpty()) return 0;

    uint8_t *rom = reinterpret_cast<uint8_t*>(m_project->currentData.data());
    int romLen = m_project->currentData.size();
    ByteOrder bo = m_project->byteOrder;
    int written = 0;

    // Collect the checked items once (so progress total matches).
    QVector<int> checkedRows;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        if (m_tree->topLevelItem(i)->checkState(0) == Qt::Checked)
            checkedRows.append(i);
    }

    if (progress) {
        progress->setRange(0, checkedRows.size());
        progress->setValue(0);
    }

    for (int step = 0; step < checkedRows.size(); ++step) {
        if (progress && progress->wasCanceled())
            return -1;   // caller must roll back

        int i = checkedRows[step];
        auto *item = m_tree->topLevelItem(i);

        int mapIdx = item->data(0, Qt::UserRole).toInt();
        if (mapIdx < 0 || mapIdx >= m_maps.size()) continue;
        const MapInfo &mi = m_maps[mapIdx];

        uint32_t baseOff = mi.address + mi.mapDataOffset;
        int totalCells = mi.dimensions.x * mi.dimensions.y;

        // Read new value from the line-edit widget (all types are now editable)
        auto *edit = qobject_cast<QLineEdit*>(m_tree->itemWidget(item, 3));
        double newPhys = 0;
        if (edit) {
            bool ok = false;
            newPhys = edit->text().toDouble(&ok);
            if (!ok) newPhys = 0;
        }

        // Convert physical to raw
        uint32_t rawVal;
        if (mi.hasScaling)
            rawVal = static_cast<uint32_t>(mi.scaling.toRaw(newPhys));
        else
            rawVal = static_cast<uint32_t>(newPhys);

        // Write to every cell
        for (int c = 0; c < totalCells; ++c) {
            uint32_t off = baseOff + c * mi.dataSize;
            writeRomValue(rom, romLen, off, mi.dataSize, bo, rawVal);
        }
        ++written;

        if (progress) {
            progress->setValue(step + 1);
            QCoreApplication::processEvents();
        }
    }

    return written;
}

QVector<AIFunctionConfirmDlg::PendingRow> AIFunctionConfirmDlg::pendingRows() const
{
    QVector<PendingRow> rows;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto *item = m_tree->topLevelItem(i);
        if (item->checkState(0) != Qt::Checked) continue;
        int mapIdx = item->data(0, Qt::UserRole).toInt();
        if (mapIdx < 0 || mapIdx >= m_maps.size()) continue;

        PendingRow r;
        r.mapName  = m_maps[mapIdx].name;
        r.oldValue = item->text(2);
        auto *edit = qobject_cast<QLineEdit*>(m_tree->itemWidget(item, 3));
        r.newValue = edit ? edit->text() : QString();
        rows.append(r);
    }
    return rows;
}
