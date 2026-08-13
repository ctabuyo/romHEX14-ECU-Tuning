/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "patcheditordlg.h"
#include "uiwidgets.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QComboBox>
#include <QLabel>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QDateTime>
#include <QFont>
#include <QColor>
#include <QTableWidget>
#include <QProgressDialog>
#include <QCoreApplication>
#include <QApplication>

// ── Construction ──────────────────────────────────────────────────────────────

PatchEditorDlg::PatchEditorDlg(const RomPatch &patch, Project *project, QWidget *parent)
    : QDialog(parent, Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint
              | Qt::WindowMaximizeButtonHint)
    , m_project(project)
    , m_patch(patch)
{
    build();
    loadPatch(patch);
}

PatchEditorDlg::PatchEditorDlg(Project *project, QWidget *parent)
    : QDialog(parent, Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint
              | Qt::WindowMaximizeButtonHint)
    , m_project(project)
{
    build();
    RomPatch p;
    p.created     = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    p.sourceLabel = project ? project->fullTitle() : QString();
    loadPatch(p);
}

void PatchEditorDlg::build()
{
    setWindowTitle(tr("Patch Script Editor"));
    resize(920, 680);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // ── Metadata row ─────────────────────────────────────────────────────────
    auto *meta = new QGroupBox(tr("Patch metadata"));
    meta->setStyleSheet("QGroupBox { color:#8b949e; font-size:8pt; }");
    auto *gl = new QGridLayout(meta);
    gl->setVerticalSpacing(4);
    gl->setHorizontalSpacing(8);

    gl->addWidget(new QLabel(tr("Label:")), 0, 0);
    m_labelEdit = new QLineEdit(); gl->addWidget(m_labelEdit, 0, 1);

    gl->addWidget(new QLabel(tr("Source:")), 0, 2);
    m_srcEdit = new QLineEdit(); m_srcEdit->setReadOnly(true);
    m_srcEdit->setStyleSheet("color:#8b949e;");
    gl->addWidget(m_srcEdit, 0, 3);

    gl->addWidget(new QLabel(tr("Target:")), 0, 4);
    m_tgtEdit = new QLineEdit(); m_tgtEdit->setReadOnly(true);
    m_tgtEdit->setStyleSheet("color:#8b949e;");
    gl->addWidget(m_tgtEdit, 0, 5);

    gl->setColumnStretch(1, 3);
    gl->setColumnStretch(3, 2);
    gl->setColumnStretch(5, 2);
    root->addWidget(meta);

    // ── Split: map list (left) + JSON editor (right) ─────────────────────────
    auto *split = new QSplitter(Qt::Horizontal);

    // Map table
    m_mapTable = new QTableWidget();
    m_mapTable->setColumnCount(3);
    m_mapTable->setHorizontalHeaderLabels({tr("Map"), tr("Changed cells"), tr("Data size")});
    m_mapTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_mapTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_mapTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_mapTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_mapTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_mapTable->setStyleSheet(
        "QTableWidget { background:#0d1117; color:#c9d1d9; gridline-color:#21262d; font-size:8pt; }"
        "QTableWidget::item:selected { background:#1f6feb; }"
        "QHeaderView::section { background:#161b22; color:#8b949e; border:none; padding:4px; }");
    split->addWidget(m_mapTable);

    // JSON editor
    auto *jsonWrap = new QWidget();
    auto *jvl = new QVBoxLayout(jsonWrap);
    jvl->setContentsMargins(0, 0, 0, 0);
    jvl->setSpacing(3);
    auto *jLabel = new QLabel(tr("Raw JSON  (editable — changes update the patch on Apply)"));
    jLabel->setStyleSheet("color:#8b949e; font-size:8pt;");
    jvl->addWidget(jLabel);

    m_jsonEdit = new QPlainTextEdit();
    QFont mono("Cascadia Code", 9);
    mono.setStyleHint(QFont::Monospace);
    m_jsonEdit->setFont(mono);
    m_jsonEdit->setStyleSheet(
        "QPlainTextEdit { background:#0d1117; color:#c9d1d9; "
        "border:1px solid #30363d; font-size:9pt; }");
    jvl->addWidget(m_jsonEdit, 1);
    split->addWidget(jsonWrap);

    split->setSizes({280, 620});
    root->addWidget(split, 1);

    // ── Status label ─────────────────────────────────────────────────────────
    m_statusLabel = new QLabel();
    m_statusLabel->setStyleSheet("color:#8b949e; font-size:8pt;");
    root->addWidget(m_statusLabel);

    // ── Button row ───────────────────────────────────────────────────────────
    auto *btnRow = new QHBoxLayout();

    auto *openBtn = new QPushButton(tr("Open .rxpatch…"));
    auto *saveBtn = new QPushButton(tr("Save .rxpatch…"));

    m_applyCurrentBtn = new QPushButton(tr("Apply to current ROM"));
    m_applyCurrentBtn->setStyleSheet(
        "QPushButton { background:#238636; color:#fff; border-radius:4px; padding:4px 10px; }"
        "QPushButton:hover { background:#2ea043; }"
        "QPushButton:disabled { background:#21262d; color:#484f58; }");

    m_applyLinkedBtn = new QPushButton(tr("Apply to linked ROM…"));
    auto *applyFileBtn = new QPushButton(tr("Apply to ROM file…"));

    // Project-specific buttons only available when a project is open
    m_applyCurrentBtn->setEnabled(m_project != nullptr);
    m_applyLinkedBtn->setEnabled(m_project != nullptr);

    auto *closeBtn = new QPushButton(tr("Close"));
    closeBtn->setFixedWidth(72);

    btnRow->addWidget(openBtn);
    btnRow->addWidget(saveBtn);
    btnRow->addSpacing(12);
    btnRow->addWidget(m_applyCurrentBtn);
    btnRow->addWidget(m_applyLinkedBtn);
    btnRow->addWidget(applyFileBtn);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);

    connect(openBtn,          &QPushButton::clicked, this, &PatchEditorDlg::onOpen);
    connect(saveBtn,          &QPushButton::clicked, this, &PatchEditorDlg::onSave);
    connect(m_applyCurrentBtn,&QPushButton::clicked, this, &PatchEditorDlg::onApplyCurrent);
    connect(m_applyLinkedBtn, &QPushButton::clicked, this, &PatchEditorDlg::onApplyLinked);
    connect(applyFileBtn,     &QPushButton::clicked, this, &PatchEditorDlg::onApplyFile);
    connect(closeBtn,         &QPushButton::clicked, this, &QDialog::accept);
}

// ── Filename sanitizer ────────────────────────────────────────────────────────

// Strips characters that are illegal in Windows file names: \ / : * ? " < > |
// Also collapses runs of whitespace/dots and trims leading/trailing spaces.
QString PatchEditorDlg::safeFileName(const QString &label)
{
    static const QString illegal = QStringLiteral("\\/:*?\"<>|");
    QString out;
    out.reserve(label.size());
    for (const QChar &c : label) {
        if (illegal.contains(c) || c.unicode() < 32)
            out += QLatin1Char('_');
        else
            out += c;
    }
    // Remove leading/trailing dots and spaces (Windows rejects them)
    while (!out.isEmpty() && (out.front() == '.' || out.front() == ' '))
        out.remove(0, 1);
    while (!out.isEmpty() && (out.back() == '.' || out.back() == ' '))
        out.chop(1);
    if (out.isEmpty()) out = QStringLiteral("patch");
    return out;
}

// ── Load / refresh ────────────────────────────────────────────────────────────

void PatchEditorDlg::loadPatch(const RomPatch &p)
{
    m_patch = p;
    m_labelEdit->setText(p.label);
    m_srcEdit->setText(p.sourceLabel);
    m_tgtEdit->setText(p.targetLabel);

    // Populate map table
    m_mapTable->setRowCount(p.maps.size());
    for (int i = 0; i < p.maps.size(); ++i) {
        const MapPatch &mp = p.maps[i];
        m_mapTable->setItem(i, 0, new QTableWidgetItem(mp.name));
        m_mapTable->setItem(i, 1, new QTableWidgetItem(QString::number(mp.cells.size())));
        m_mapTable->setItem(i, 2, new QTableWidgetItem(
            QString("%1B  %2×%3").arg(mp.dataSize).arg(mp.cols).arg(mp.rows)));
    }

    // Render JSON
    RomPatch pCopy = p;
    pCopy.label = m_labelEdit->text();
    m_jsonEdit->setPlainText(pCopy.toJson().toJson(QJsonDocument::Indented));

    int totalCells = 0;
    for (const MapPatch &mp : p.maps) totalCells += mp.cells.size();
    m_statusLabel->setText(tr("%1 map(s)  ·  %2 cell(s) total  ·  created %3")
                           .arg(p.maps.size()).arg(totalCells).arg(p.created));
}

// ── Parse current JSON ────────────────────────────────────────────────────────

RomPatch PatchEditorDlg::currentPatch() const
{
    QJsonParseError pe;
    auto doc = QJsonDocument::fromJson(m_jsonEdit->toPlainText().toUtf8(), &pe);
    if (pe.error != QJsonParseError::NoError)
        return m_patch;   // fall back to last loaded

    QString err;
    RomPatch p = RomPatch::fromJson(doc, &err);
    // Override label from UI
    p.label = m_labelEdit->text();
    return p;
}

// ── Apply helper ─────────────────────────────────────────────────────────────

void PatchEditorDlg::applyToRom(QByteArray &rom,
                                 const QString &romLabel,
                                 const QMap<QString, uint32_t> &offsets)
{
    RomPatch p = currentPatch();

    // Warn if patch contains raw bytes (ECU-specific checksums)
    if (!p.rawBytes.isEmpty()) {
        QString warn = tr(
            "This patch contains %1 raw byte change(s) outside A2L map regions "
            "(ECU-specific checksums/CRC data).\n\n"
            "These are only correct for the exact same ECU variant and base ROM. "
            "Applying them to a different base ROM will produce wrong checksums.\n\n"
            "Apply checksum bytes anyway?").arg(p.rawBytes.size());
        if (QMessageBox::warning(this, tr("ECU-Specific Checksum Data"),
                warn, QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No) == QMessageBox::No) {
            // Apply maps only — strip raw bytes temporarily
            p.rawBytes.clear();
        }
    }

    const QVector<MapInfo> noMaps;
    const QVector<MapInfo> &projectMaps = m_project ? m_project->maps : noMaps;
    const ByteOrder bo = m_project ? m_project->byteOrder : ByteOrder::BigEndian;

    // ── Apply map-by-map with a cancellable progress dialog ───────────────
    PatchApplyResult res;
    const int totalMaps = p.maps.size();
    const int totalUnits = totalMaps + (p.rawBytes.isEmpty() ? 0 : 1);

    QProgressDialog progress(tr("Applying changes..."), tr("Cancel"),
                             0, qMax(1, totalUnits), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(500);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.setValue(0);

    QByteArray rollback;
    if (m_project)
        rollback = rom;    // cheap copy-on-write snapshot for in-memory rollback

    bool canceled = false;
    for (int i = 0; i < totalMaps; ++i) {
        if (progress.wasCanceled()) { canceled = true; break; }

        RomPatch slice;
        slice.version     = p.version;
        slice.created     = p.created;
        slice.label       = p.label;
        slice.sourceLabel = p.sourceLabel;
        slice.targetLabel = p.targetLabel;
        slice.maps.append(p.maps[i]);

        PatchApplyResult sliceRes = slice.apply(rom, projectMaps, bo, offsets);
        for (const auto &mr : sliceRes.maps) res.maps.append(mr);

        progress.setValue(i + 1);
        QCoreApplication::processEvents();
    }

    if (!canceled && !p.rawBytes.isEmpty()) {
        if (progress.wasCanceled()) {
            canceled = true;
        } else {
            RomPatch rawOnly;
            rawOnly.rawBytes = p.rawBytes;
            PatchApplyResult rawRes = rawOnly.apply(rom, projectMaps, bo, offsets);
            res.rawBytesApplied    += rawRes.rawBytesApplied;
            res.rawBytesMismatched += rawRes.rawBytesMismatched;
            progress.setValue(totalUnits);
            QCoreApplication::processEvents();
        }
    }

    progress.close();
    QApplication::restoreOverrideCursor();

    if (canceled) {
        if (m_project && !rollback.isEmpty() && rom.data() == m_project->currentData.data()) {
            // Roll back in-memory — snapshot was taken by caller before this call,
            // but we also kept a pre-apply copy so we don't need to reload.
            rom = rollback;
        }
        m_statusLabel->setText(tr("Apply canceled — no changes committed"));
        return;
    }

    // ── Build a colored results dialog ────────────────────────────────────────
    auto *dlg = new QDialog(this);
    dlg->setWindowTitle(tr("Patch Results — %1").arg(romLabel));
    dlg->resize(680, 420);
    auto *vl = new QVBoxLayout(dlg);
    vl->setContentsMargins(10, 10, 10, 10);
    vl->setSpacing(6);

    // Summary header
    int nOk   = 0, nWarn = 0, nFail = 0;
    for (const auto &mr : res.maps) {
        if (mr.status == MapApplyResult::Applied)             ++nOk;
        else if (mr.status == MapApplyResult::AppliedWithWarnings) ++nWarn;
        else                                                  ++nFail;
    }
    QString hdr = tr("%1 map(s): %2 applied, %3 with warnings, %4 failed")
                  .arg(res.maps.size()).arg(nOk).arg(nWarn).arg(nFail);
    if (res.rawBytesApplied > 0)
        hdr += tr("  ·  %1 checksum byte(s) applied").arg(res.rawBytesApplied);
    auto *hdrLbl = new QLabel(hdr);
    hdrLbl->setStyleSheet("font-weight:bold; color:#c9d1d9;");
    vl->addWidget(hdrLbl);

    // Per-map table
    auto *tbl = new QTableWidget(res.maps.size(), 3);
    tbl->setHorizontalHeaderLabels({tr("Map"), tr("Status"), tr("Detail")});
    tbl->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tbl->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tbl->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    tbl->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tbl->setSelectionBehavior(QAbstractItemView::SelectRows);
    tbl->verticalHeader()->setVisible(false);
    tbl->setStyleSheet(
        "QTableWidget { background:#0d1117; color:#c9d1d9; gridline-color:#21262d; font-size:8pt; }"
        "QTableWidget::item:selected { background:#1f6feb; }"
        "QHeaderView::section { background:#161b22; color:#8b949e; border:none; padding:4px; }");

    for (int i = 0; i < res.maps.size(); ++i) {
        const MapApplyResult &mr = res.maps[i];
        QString statusStr;
        QColor  statusColor;
        switch (mr.status) {
        case MapApplyResult::Applied:
            statusStr  = tr("OK");
            statusColor = QColor("#3fb950");
            break;
        case MapApplyResult::AppliedWithWarnings:
            statusStr  = tr("Warning");
            statusColor = QColor("#d29922");
            break;
        case MapApplyResult::Failed:
            statusStr  = tr("Failed");
            statusColor = QColor("#f85149");
            break;
        }
        auto *nameItem   = new QTableWidgetItem(mr.mapName);
        auto *statusItem = new QTableWidgetItem(statusStr);
        auto *detailItem = new QTableWidgetItem(mr.detail);
        statusItem->setForeground(statusColor);
        tbl->setItem(i, 0, nameItem);
        tbl->setItem(i, 1, statusItem);
        tbl->setItem(i, 2, detailItem);
    }
    vl->addWidget(tbl, 1);

    auto *bb = new QHBoxLayout();
    bb->addStretch();
    auto *okBtn = new QPushButton(tr("OK"));
    okBtn->setDefault(true);
    bb->addWidget(okBtn);
    vl->addLayout(bb);
    connect(okBtn, &QPushButton::clicked, dlg, &QDialog::accept);
    dlg->exec();
    dlg->deleteLater();

    QString statusMsg = tr("Applied to %1 at %2 — %3 OK, %4 warn, %5 failed")
                       .arg(romLabel)
                       .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                       .arg(nOk).arg(nWarn).arg(nFail);
    if (res.rawBytesApplied > 0)
        statusMsg += tr("  ·  %1 checksum byte(s)").arg(res.rawBytesApplied);
    m_statusLabel->setText(statusMsg);
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void PatchEditorDlg::onOpen()
{
    QString path = QFileDialog::getOpenFileName(
        this, tr("Open Patch Script"), {},
        tr("Patch scripts (*.rxpatch);;All files (*)"));
    if (path.isEmpty()) return;

    QString err;
    RomPatch p = RomPatch::load(path, &err);
    if (!err.isEmpty()) {
        QMessageBox::warning(this, tr("Open failed"), err);
        return;
    }
    loadPatch(p);
}

void PatchEditorDlg::onSave()
{
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save Patch Script"),
        safeFileName(m_labelEdit->text().isEmpty() ? m_patch.label : m_labelEdit->text()) + ".rxpatch",
        tr("Patch scripts (*.rxpatch);;All files (*)"));
    if (path.isEmpty()) return;

    RomPatch p = currentPatch();
    QString err;
    if (!p.save(path, &err))
        QMessageBox::warning(this, tr("Save failed"), err);
    else
        m_statusLabel->setText(tr("Saved to %1").arg(path));
}

void PatchEditorDlg::onApplyCurrent()
{
    if (!m_project) return;

    RomPatch preview = currentPatch();

    QVector<UI::RiskyChangeConfirmDialog::ChangeRow> rows;
    int totalCells = 0;
    for (const MapPatch &mp : preview.maps) {
        totalCells += mp.cells.size();
        UI::RiskyChangeConfirmDialog::ChangeRow r;
        r.label    = mp.name;
        r.oldValue = tr("%n cell(s)", "", mp.cells.size());
        r.newValue = QStringLiteral("%1×%2  %3B").arg(mp.cols).arg(mp.rows).arg(mp.dataSize);
        r.delta    = QString();
        rows.append(r);
    }
    if (!preview.rawBytes.isEmpty()) {
        UI::RiskyChangeConfirmDialog::ChangeRow r;
        r.label    = tr("Raw / checksum bytes");
        r.oldValue = QStringLiteral("—");
        r.newValue = tr("%n byte(s)", "", preview.rawBytes.size());
        rows.append(r);
    }

    UI::RiskyChangeConfirmDialog confirm(this);
    confirm.setHeadline(tr("Apply patch: %1").arg(m_labelEdit->text().isEmpty()
                                                  ? tr("(unnamed)")
                                                  : m_labelEdit->text()));
    confirm.setDescription(tr(
        "This patch will rewrite %1 map(s) (%2 cell(s)) in the active ROM. "
        "A version snapshot will let you revert if needed.")
        .arg(preview.maps.size()).arg(totalCells));
    confirm.setRisk(UI::RiskyChangeConfirmDialog::Risk::Caution);
    confirm.setChanges(rows);
    confirm.setSnapshotOption(true, true);
    confirm.setActionText(tr("Apply"));
    if (totalCells > 500)
        confirm.setRequireTypedConfirmation(QStringLiteral("APPLY"));

    if (confirm.exec() != QDialog::Accepted)
        return;

    if (confirm.snapshotChecked())
        m_project->snapshotVersion(tr("Before patch: %1").arg(m_labelEdit->text()));

    QByteArray targetRom = m_project->currentData;
    applyToRom(targetRom, m_project->fullTitle());

    QVector<QPair<int, QByteArray>> patches;
    const int len = qMin(targetRom.size(), m_project->currentData.size());
    for (int i = 0; i < len; ++i) {
        if (targetRom[i] != m_project->currentData[i]) {
            int start = i;
            while (i < len && targetRom[i] != m_project->currentData[i]) ++i;
            patches.append({start, targetRom.mid(start, i - start)});
        }
    }
    if (!patches.isEmpty()) {
        m_project->applyRomPatches(patches, tr("Apply patch: %1").arg(m_labelEdit->text()));
    }
}

void PatchEditorDlg::onApplyLinked()
{
    if (!m_project) return;
    if (m_project->linkedRoms.isEmpty()) {
        QMessageBox::information(this, tr("No linked ROMs"),
            tr("No ROMs are linked to this project.\nLink a ROM first via the Compare menu."));
        return;
    }

    // Build a list of linked ROMs for selection
    QStringList names;
    for (const auto &lr : m_project->linkedRoms)
        names << lr.label;

    bool ok = false;
    // Use a simple QInputDialog-style combo picker
    auto *dlg = new QDialog(this);
    dlg->setWindowTitle(tr("Select linked ROM"));
    dlg->resize(340, 100);
    auto *vl = new QVBoxLayout(dlg);
    vl->addWidget(new QLabel(tr("Apply patch to:")));
    auto *combo = new QComboBox();
    combo->addItems(names);
    vl->addWidget(combo);
    auto *bb = new QHBoxLayout();
    auto *ok_btn = new QPushButton(tr("Apply")); ok_btn->setDefault(true);
    auto *cl_btn = new QPushButton(tr("Cancel"));
    bb->addStretch(); bb->addWidget(ok_btn); bb->addWidget(cl_btn);
    vl->addLayout(bb);
    connect(ok_btn, &QPushButton::clicked, dlg, [&]{ ok = true; dlg->accept(); });
    connect(cl_btn, &QPushButton::clicked, dlg, &QDialog::reject);
    dlg->exec();
    dlg->deleteLater();
    if (!ok) return;

    int idx = combo->currentIndex();
    if (idx < 0 || idx >= m_project->linkedRoms.size()) return;
    LinkedRom &lr = m_project->linkedRoms[idx];

    applyToRom(lr.data, lr.label, lr.mapOffsets);
}

void PatchEditorDlg::onApplyFile()
{
    QString srcPath = QFileDialog::getOpenFileName(
        this, tr("Select ROM file to patch"), {},
        tr("ROM files (*.bin *.hex *.rom *.ori *.mpc *.HEX);;All files (*)"));
    if (srcPath.isEmpty()) return;

    // Read source ROM
    QFile src(srcPath);
    if (!src.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Error"), src.errorString());
        return;
    }
    QByteArray rom = src.readAll();
    src.close();

    // Suggest output path: same directory, PATCHED- prefix
    QFileInfo fi(srcPath);
    QString suggested = fi.dir().filePath(
        QStringLiteral("PATCHED-") + fi.fileName());

    QString outPath = QFileDialog::getSaveFileName(
        this, tr("Save patched ROM as…"), suggested,
        tr("ROM files (*.bin *.hex *.rom *.ori *.mpc *.HEX);;All files (*)"));
    if (outPath.isEmpty()) return;

    applyToRom(rom, fi.fileName());

    QFile out(outPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Error"), out.errorString());
        return;
    }
    out.write(rom);
    out.close();
    m_statusLabel->setText(tr("Saved patched ROM to %1").arg(outPath));
}
