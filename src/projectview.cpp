/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "projectview.h"
#include "createmapdlg.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QScrollBar>
#include <QShortcut>
#include <QFileInfo>
#include <QResizeEvent>
#include <QAction>
#include <QMetaObject>
#include <QApplication>
#include <QMainWindow>
#include <climits>
#include <cstring>

ProjectView::ProjectView(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Top bar: title, version, address ──────────────────────────────
    auto *topBar = new QWidget();
    topBar->setObjectName("pvTopBar");
    topBar->setStyleSheet(
        "#pvTopBar { background:#161b22; border-bottom:1px solid #30363d; }");
    auto *topLay = new QHBoxLayout(topBar);
    topLay->setContentsMargins(8, 4, 8, 4);
    topLay->setSpacing(8);

    m_titleLabel = new QLabel(tr("No project"));
    m_titleLabel->setStyleSheet(
        "color:#58a6ff; font-weight:bold; font-size:10pt; background:transparent;");
    topLay->addWidget(m_titleLabel);
    topLay->addStretch();

    auto mkLbl = [](const QString &t) {
        auto *l = new QLabel(t);
        l->setStyleSheet("color:#8b949e; background:transparent;");
        return l;
    };

    m_versionLabel = new QLabel(tr("Version:"));
    m_versionLabel->setStyleSheet("color:#8b949e; background:transparent;");
    topLay->addWidget(m_versionLabel);
    m_versionCombo = new QComboBox();
    m_versionCombo->setMinimumWidth(190);
    m_versionCombo->setToolTip(tr("Switch between saved ROM version snapshots"));
    topLay->addWidget(m_versionCombo);

    m_btnAddVer = new QPushButton(tr("+ Version"));
    m_btnAddVer->setToolTip(tr("Snapshot the current ROM state as a named version"));
    topLay->addWidget(m_btnAddVer);

    topLay->addWidget(mkLbl("|"));

    m_cmpLabel = new QLabel(tr("Compare with:"));
    m_cmpLabel->setStyleSheet("color:#8b949e; background:transparent;");
    topLay->addWidget(m_cmpLabel);

    m_cmpCombo = new QComboBox();
    m_cmpCombo->setMinimumWidth(160);
    m_cmpCombo->setToolTip(tr("Select ROM to compare against current data"));
    topLay->addWidget(m_cmpCombo);

    topLay->addWidget(mkLbl("|"));
    m_gotoLabel = new QLabel(tr("Go to:"));
    m_gotoLabel->setStyleSheet("color:#8b949e; background:transparent;");
    topLay->addWidget(m_gotoLabel);
    m_addressInput = new QLineEdit();
    m_addressInput->setPlaceholderText("0x00000000");
    m_addressInput->setFixedWidth(110);
    m_addressInput->setFont(QFont("Consolas", 9));
    topLay->addWidget(m_addressInput);

    root->addWidget(topBar);

    // ── View stack ────────────────────────────────────────────────────
    m_viewStack  = new QStackedWidget();
    m_hexWidget  = new HexWidget();
    m_waveWidget = new WaveformWidget();
    m_map3d      = new Map3DWidget();
    m_viewStack->addWidget(m_hexWidget);   // 0 – Text
    m_viewStack->addWidget(m_waveWidget);  // 1 – 2D
    m_viewStack->addWidget(m_map3d);       // 2 – 3D
    root->addWidget(m_viewStack, 1);

    // ── OLS-style bottom tab bar ───────────────────────────────────
    m_tabBar = new QTabBar();
    m_tabBar->setObjectName("viewTabBar");
    m_tabBar->setStyleSheet(
        "QTabBar          { background:#161b22; border-top:1px solid #30363d; }"
        "QTabBar::tab     { background:#161b22; color:#8b949e;"
        "                   border:none; border-right:1px solid #30363d;"
        "                   padding:4px 20px; font-size:9pt; }"
        "QTabBar::tab:selected     { background:#0d1117; color:#58a6ff;"
        "                           border-top:2px solid #1f6feb; }"
        "QTabBar::tab:hover:!selected { background:#21262d; color:#c9d1d9; }");
    m_tabBar->addTab(tr("Text"));
    m_tabBar->addTab(tr("2d"));
    m_tabBar->addTab(tr("3d"));
    m_tabBar->addTab("—");
    root->addWidget(m_tabBar);

    // Friendly empty-state card — overlays the view stack while the active
    // project has no maps. Built once and parented to m_viewStack so it floats
    // above the hex/2D/3D widgets without occupying a layout slot.
    buildEmptyState();

    // ── Connections ───────────────────────────────────────────────────
    connect(m_tabBar, &QTabBar::currentChanged, this, [this](int idx) {
        if (idx < 3) { switchView(idx); emit viewSwitched(idx); }
    });

    connect(m_versionCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ProjectView::onVersionChanged);

    // Sprint H: right-click the Version combo to open any version
    // (incl. "Current") side-by-side in a new MDI subwindow.
    m_versionCombo->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_versionCombo, &QWidget::customContextMenuRequested,
            this, [this](const QPoint &pos) {
        if (!m_project) return;
        QMenu menu(m_versionCombo);
        menu.setStyleSheet(
            "QMenu { background:#21262d; color:#e6edf3; "
            "  border:1px solid #30363d; }"
            "QMenu::item:selected { background:#1f6feb; }");
        // -1 == Current (working).  0..N-1 map onto Project::versions[i].
        const int n = m_project->versions.size();
        if (n > 0) {
            menu.addAction(tr("Open \"Current (working)\" in new window"),
                           this, [this]() {
                emit cloneVersionRequested(m_project, -1);
            });
            menu.addSeparator();
            for (int i = 0; i < n; ++i) {
                const QString lbl = m_project->versions[i].name.isEmpty()
                    ? tr("Version %1").arg(i + 1)
                    : m_project->versions[i].name;
                const int idx = i;
                menu.addAction(tr("Open \"%1\" in new window").arg(lbl),
                               this, [this, idx]() {
                    emit cloneVersionRequested(m_project, idx);
                });
            }
        } else {
            QAction *a = menu.addAction(tr("(no extra versions)"));
            a->setEnabled(false);
        }
        menu.exec(m_versionCombo->mapToGlobal(pos));
    });

    connect(m_btnAddVer, &QPushButton::clicked, this, &ProjectView::onAddVersion);

    connect(m_cmpCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        updateComparisonSource();
    });

    connect(m_hexWidget, &HexWidget::offsetSelected,
            this,        &ProjectView::onOffsetSelected);
    connect(m_hexWidget, &HexWidget::mapClickedInHex, this, [this](uint32_t address) {
        if (!m_project) return;
        for (const MapInfo &map : m_project->maps) {
            if (map.cellDataStart() == address) {
                m_isSelectingFromHex = true;
                showMap(map);
                emit mapActivated(map, m_project);
                m_isSelectingFromHex = false;
                break;
            }
        }
    });
    connect(m_hexWidget, &HexWidget::dataModified,
            this,        &ProjectView::onDataModified);
    connect(m_hexWidget, &HexWidget::bytesModified, this,
            [this](int start, int length) {
        if (!m_project || start < 0 || length <= 0) return;
        const QByteArray hexData = m_hexWidget->getData();
        if (start + length > hexData.size()) return;
        m_project->applyRomPatches(
            {{start, hexData.mid(start, length)}}, tr("Hex edit"));
    });

    connect(m_waveWidget, &WaveformWidget::mapClicked,
            this, [this](const MapInfo &map) {
        showMap(map);
        emit mapActivated(map, m_project);
    });

    auto runCreateMap = [this](uint32_t address, int length) {
        if (!m_project) return;
        CreateMapDlg dlg(address, length, 2, this);
        if (dlg.exec() == QDialog::Accepted) {
            MapInfo newMap = dlg.resultMap();
            m_project->maps.append(newMap);
            m_project->modified = true;
            m_waveWidget->setMaps(m_project->maps);
            emit m_project->dataChanged(); // refreshes the left panel tree
            emit mapActivated(newMap, m_project);
        }
    };
    connect(m_waveWidget, &WaveformWidget::selectionToMapRequested,
            this, runCreateMap);
    connect(m_hexWidget, &HexWidget::selectionToMapRequested,
            this, runCreateMap);

    // ── Waveform editing → shared Project ROM transaction ──────────
    connect(m_waveWidget, &WaveformWidget::dataModified, this, [this](int start, int end) {
        if (!m_project) return;
        const QByteArray &waveData = m_waveWidget->romData();
        if (waveData.size() != m_project->currentData.size()) return;
        const int len = qMin(end, waveData.size()) - start;
        if (len <= 0 || start < 0) return;
        m_project->applyRomPatches(
            {{start, waveData.mid(start, len)}}, tr("Waveform edit"));
    });

    connect(m_addressInput, &QLineEdit::returnPressed, this, [this]() {
        QString val = m_addressInput->text().trimmed();
        if (val.isEmpty()) return;
        // Strip optional "0x" / "0X" prefix; always parse as hex — this is an
        // address field, hex is the natural base. Bare "1A000" or "0x1A000"
        // should both work. Fall back to decimal only if hex parse fails (e.g.
        // the user typed a pure-decimal byte offset like "4096").
        if (val.startsWith("0x", Qt::CaseInsensitive))
            val = val.mid(2);
        bool ok = false;
        uint32_t rawAddr = val.toUInt(&ok, 16);
        if (!ok) rawAddr = val.toUInt(&ok, 10);
        if (!ok) return;
        switchView(0);   // ensure hex/text view is visible
        goToAddress(rawAddr);
    });

    // ── View-switching shortcuts: Ctrl+1/2/3 ─────────────────────────
    auto *s1 = new QShortcut(QKeySequence(tr("Ctrl+1")), this);
    connect(s1, &QShortcut::activated, this, [this]() { switchView(0); });
    auto *s2 = new QShortcut(QKeySequence(tr("Ctrl+2")), this);
    connect(s2, &QShortcut::activated, this, [this]() { switchView(1); });
    auto *s3 = new QShortcut(QKeySequence(tr("Ctrl+3")), this);
    connect(s3, &QShortcut::activated, this, [this]() { switchView(2); });
    // ── Escape: clear selection in active view ───────────────────────
    auto *sEsc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(sEsc, &QShortcut::activated, this, [this]() {
        if (m_viewStack->currentIndex() == 1 && m_waveWidget
            && m_waveWidget->hasSelection()) {
            m_waveWidget->clearSelection();
        }
    });
}

// ── Public API ────────────────────────────────────────────────────────────────

void ProjectView::loadProject(Project *project)
{
    m_project = project;
    if (!project) {
        m_titleLabel->setText(tr("No project"));
        updateEmptyState();
        return;
    }

    m_titleLabel->setText(project->fullTitle());
    m_hexWidget->setProject(project);
    m_waveWidget->setProject(project);
    m_hexWidget->loadData(project->currentData, project->originalData,
                          project->baseAddress);
    m_hexWidget->setAnnotationStore(project->annotations());
    m_waveWidget->setAnnotationStore(project->annotations());

    // Show ROM data in waveform widget
    if (!project->currentData.isEmpty()) {
        m_waveWidget->showROM(project->currentData, project->originalData);
    }

    if (!project->maps.isEmpty()) {
        QVector<MapRegion> regions;
        for (const auto &m : project->maps)
            regions.append(cellDataRegion(m));
        m_hexWidget->setMapRegions(regions);
        m_waveWidget->setMaps(project->maps);
    } else {
        // No A2L-derived maps yet — keep the waveform clean.
        m_waveWidget->setMaps({});
    }

    // Auto-detected overlays render as a fallback until an A2L is imported.
    // setAutoDetectedMaps() is always called so that switching projects or
    // clearing the auto-scan keeps the widget in sync. The widget itself
    // only draws these when project->maps is empty.
    if (!project->hideAutoDetectedMaps)
        m_waveWidget->setAutoDetectedMaps(project->autoDetectedMaps);
    else
        m_waveWidget->setAutoDetectedMaps({});

    rebuildVersionCombo();
    rebuildComparisonCombo();
    updateEmptyState();

    connect(project, &Project::dataChanged, this, [this]() {
        if (m_project) {
            m_hexWidget->refreshData(m_project->currentData);
            m_titleLabel->setText(m_project->fullTitle());
            QVector<MapRegion> regions;
            regions.reserve(m_project->maps.size());
            for (const auto &map : m_project->maps)
                regions.append(cellDataRegion(map));
            m_hexWidget->setMapRegions(regions);
            // Re-push both map lists so an async auto-scan result or an A2L
            // import triggers a repaint without the caller having to touch
            // the waveform directly.
            m_waveWidget->setMaps(m_project->maps);
            m_waveWidget->setAutoDetectedMaps(m_project->hideAutoDetectedMaps
                                                  ? QVector<MapInfo>{}
                                                  : m_project->autoDetectedMaps);
            updateEmptyState();
        }
    });
    connect(project, &Project::romPatched, this,
            [this](int start, int end) {
        if (!m_project) return;
        m_hexWidget->refreshData(m_project->currentData);
        m_waveWidget->refreshRomData(m_project->currentData);
        const uint32_t mapStart = m_selectedMap.cellDataStart();
        const uint64_t mapEnd = uint64_t(mapStart)
                              + uint64_t(m_selectedMap.cellDataLength());
        if (m_map3d && m_selectedMap.cellDataLength() > 0
            && start < mapEnd && end > int(mapStart))
            m_map3d->showMap(m_project->currentData, m_selectedMap);
    });
    connect(project, &Project::versionsChanged,
            this, &ProjectView::rebuildVersionCombo);
    connect(project, &Project::versionsChanged,
            this, &ProjectView::rebuildComparisonCombo);
}

void ProjectView::showMap(const MapInfo &map)
{
    m_selectedMap = map;
    m_waveWidget->setCurrentMap(map);
    m_hexWidget->setActiveMap(map);

    // A map-list click behaves like WinOLS: select the map's data in the
    // hexdump, without opening an editor window.  Inline axes precede the
    // actual cell grid, so select only the editable map cells.
    const uint32_t dataOffset = map.cellDataStart();
    if (!m_isSelectingFromHex) {
        m_hexWidget->selectRange(dataOffset, map.cellDataLength());
    }
    switchView(0);

    const uint32_t displayAddress = map.rawAddress ? map.rawAddress
        : (m_project ? m_project->baseAddress + dataOffset : dataOffset);
    m_addressInput->setText(
        QString("0x%1").arg(displayAddress, 8, 16, QChar('0')).toUpper());
}

void ProjectView::goToAddress(uint32_t addr)
{
    // Convert physical/raw address → file offset by subtracting the project's
    // base address, matching the inline Go-to input's behavior. Without this,
    // addresses like 0x80100000 on a Tricore ECU exceed m_data.size() and
    // HexWidget::goToAddress silently returns — the "Go to" appears broken.
    const uint32_t base = m_project ? m_project->baseAddress : 0;
    const uint32_t fileOffset = (addr >= base && base > 0) ? addr - base : addr;
    m_hexWidget->goToAddress(fileOffset);
    m_addressInput->setText(QString("0x%1").arg(addr, 8, 16, QChar('0')).toUpper());
}

void ProjectView::switchToView(int index)
{
    if (m_viewStack && index >= 0 && index < m_viewStack->count()) {
        switchView(index);  // use switchView to sync scroll + update tab bar
        emit viewSwitched(index);
    }
}

void ProjectView::goToMap(const MapInfo &map)
{
    if (!m_project) return;
    m_selectedMap = map;

    // Ensure waveform has ROM data and maps
    if (!m_project->currentData.isEmpty())
        m_waveWidget->showROM(m_project->currentData, m_hexWidget->getOriginalData());
    m_waveWidget->setMaps(m_project->maps);
    m_waveWidget->setCurrentMap(map);

    // Switch to 2D view (updates tab bar via switchView)
    switchView(1);

    // Navigate to the map address in both views
    m_waveWidget->goToAddress(map.address);
    m_hexWidget->goToAddress(map.address);
}

void ProjectView::setDisplayParams(int cellSize, ByteOrder bo, int displayFmt, bool isSigned)
{
    m_hexWidget->setDisplayParams(cellSize, bo, displayFmt, isSigned);
}

void ProjectView::setFontSize(int pt)
{
    m_hexWidget->setFontSize(pt);
}

// ── Version management ────────────────────────────────────────────────────────

void ProjectView::rebuildVersionCombo()
{
    if (!m_project) return;
    m_versionCombo->blockSignals(true);
    m_versionCombo->clear();
    for (const auto &v : m_project->versions)
        m_versionCombo->addItem(
            QString("%1  (%2)").arg(v.name)
                .arg(v.created.toString("yyyy-MM-dd HH:mm")));
    m_versionCombo->addItem(tr("★  Current (working)"));
    m_versionCombo->setCurrentIndex(m_versionCombo->count() - 1);
    m_versionCombo->blockSignals(false);
}

void ProjectView::onVersionChanged(int index)
{
    if (!m_project) return;
    if (index < 0 || index >= m_project->versions.size()) return;

    QMessageBox mb(this);
    mb.setWindowTitle(tr("Restore Version"));
    mb.setIcon(QMessageBox::Warning);
    mb.setText(tr("Restore snapshot <b>%1</b>?")
                   .arg(m_project->versions[index].name));
    mb.setInformativeText(tr("Unsaved changes to the current ROM will be lost."));
    auto *btnRestore = mb.addButton(tr("Discard && Restore"), QMessageBox::DestructiveRole);
    mb.addButton(QMessageBox::Cancel);
    mb.setDefaultButton(qobject_cast<QPushButton *>(mb.button(QMessageBox::Cancel)));
    mb.exec();

    if (mb.clickedButton() != btnRestore) {
        m_versionCombo->blockSignals(true);
        m_versionCombo->setCurrentIndex(m_versionCombo->count() - 1);
        m_versionCombo->blockSignals(false);
        return;
    }
    m_project->restoreVersion(index);
}

void ProjectView::onAddVersion()
{
    if (!m_project) return;
    bool ok;
    QString name = QInputDialog::getText(
        this, tr("Save Version"),
        tr("Enter a name for this snapshot:"),
        QLineEdit::Normal,
        QString("v%1").arg(m_project->versions.size() + 1),
        &ok);
    if (ok && !name.trimmed().isEmpty())
        m_project->snapshotVersion(name.trimmed());
}

// ── Address / data events ─────────────────────────────────────────────────────

void ProjectView::onOffsetSelected(uint32_t offset, uint8_t /*value*/)
{
    m_addressInput->setText(
        QString("0x%1").arg(offset, 8, 16, QChar('0')).toUpper());
    emit statusMessage(
        QString("Cursor: 0x%1  |  %2")
            .arg(offset, 8, 16, QChar('0')).toUpper()
            .arg(m_project ? m_project->fullTitle() : ""));
}

void ProjectView::onDataModified(int count)
{
    if (!m_project) return;
    m_project->modified = count > 0;
    m_titleLabel->setText(count > 0
        ? tr("%1  [%2 modified]").arg(m_project->fullTitle()).arg(count)
        : m_project->fullTitle());
}

void ProjectView::rebuildComparisonCombo()
{
    m_cmpCombo->blockSignals(true);

    // Preserve the user's current selection across rebuilds.
    // We store Project* pointers as item data (via quintptr); the special
    // sentinel values 0 and 1 represent "None" and "Original".
    const quintptr prevData = (m_cmpCombo->count() > 0)
        ? m_cmpCombo->currentData().value<quintptr>()
        : 0;   // default to "None"

    m_cmpCombo->clear();

    if (!m_project) {
        m_cmpCombo->blockSignals(false);
        return;
    }

    // Item 0: "None" — clears comparison data  (sentinel 0)
    m_cmpCombo->addItem(tr("None"), QVariant::fromValue<quintptr>(0));

    // Item 1: "Original" — this project's own original/unmodified data  (sentinel 1)
    m_cmpCombo->addItem(tr("Original"), QVariant::fromValue<quintptr>(1));

    // All other open projects (excluding self)
    for (Project *p : m_allProjects) {
        if (p == m_project) continue;
        m_cmpCombo->addItem(p->listLabel(),
                            QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(p)));
    }

    // Version snapshots from the current project.
    // Tagged with small sentinel values: VERSION_TAG_BASE + versionIndex.
    // These won't collide with real heap pointers (which are large addresses).
    constexpr quintptr VERSION_TAG_BASE = 0x100;
    if (m_project && !m_project->versions.isEmpty()) {
        m_cmpCombo->insertSeparator(m_cmpCombo->count());
        for (int i = 0; i < m_project->versions.size(); ++i) {
            const auto &v = m_project->versions[i];
            QString label = tr("[Version] %1  (%2)")
                .arg(v.name)
                .arg(v.created.toString("yyyy-MM-dd HH:mm"));
            m_cmpCombo->addItem(label,
                                QVariant::fromValue<quintptr>(VERSION_TAG_BASE + i));
        }
    }

    // Restore the previous selection if it still exists
    int restoreIdx = 0;  // default to "None"
    for (int i = 0; i < m_cmpCombo->count(); i++) {
        if (m_cmpCombo->itemData(i).value<quintptr>() == prevData) {
            restoreIdx = i;
            break;
        }
    }
    m_cmpCombo->setCurrentIndex(restoreIdx);

    m_cmpCombo->blockSignals(false);
    updateComparisonSource();   // Apply comparison data
}

void ProjectView::updateComparisonSource()
{
    if (!m_project || m_cmpCombo->currentIndex() < 0) {
        m_waveWidget->setComparisonData(QByteArray());
        m_hexWidget->setComparisonData(QByteArray());
        return;
    }

    const quintptr val = m_cmpCombo->currentData().value<quintptr>();
    QByteArray cmpData;

    constexpr quintptr VERSION_TAG_BASE = 0x100;
    constexpr quintptr VERSION_TAG_MAX  = VERSION_TAG_BASE + 0xFFFF;

    if (val == 0) {
        // "None" — no comparison
        cmpData = QByteArray();
    } else if (val == 1) {
        // "Original" — this project's own unmodified data
        cmpData = m_project->originalData;
    } else if (val >= VERSION_TAG_BASE && val <= VERSION_TAG_MAX) {
        // Version snapshot from the current project
        int vIdx = static_cast<int>(val - VERSION_TAG_BASE);
        if (m_project && vIdx >= 0 && vIdx < m_project->versions.size())
            cmpData = m_project->versions[vIdx].data;
    } else {
        // Another open project — use its currentData
        auto *otherProject = reinterpret_cast<Project *>(val);
        cmpData = otherProject->currentData;
    }

    m_waveWidget->setComparisonData(cmpData);
    m_hexWidget->setComparisonData(cmpData);
}

// ── Available projects (cross-project comparison) ────────────────────────────

void ProjectView::setAvailableProjects(const QVector<Project*> &projects)
{
    m_allProjects = projects;
    rebuildComparisonCombo();
}

// ── Retranslation ─────────────────────────────────────────────────────────────

void ProjectView::retranslateUi()
{
    if (!m_project)
        m_titleLabel->setText(tr("No project"));
    if (m_versionLabel) m_versionLabel->setText(tr("Version:"));
    if (m_btnAddVer)    m_btnAddVer->setText(tr("+ Version"));
    if (m_cmpLabel)     m_cmpLabel->setText(tr("Compare with:"));
    if (m_gotoLabel)    m_gotoLabel->setText(tr("Go to:"));
    if (m_tabBar) {
        m_tabBar->setTabText(0, tr("Text"));
        m_tabBar->setTabText(1, tr("2d"));
        m_tabBar->setTabText(2, tr("3d"));
        // tab 3 "—" is a decorative separator, no translation
    }
    if (m_emptyTitle)     m_emptyTitle->setText(tr("No maps in this project yet"));
    if (m_emptyBody)      m_emptyBody->setText(
        tr("Import an A2L file to get characteristic-named maps, or run "
           "Auto-detect Maps to scan the ROM for likely candidates."));
    if (m_emptyImportBtn) m_emptyImportBtn->setText(tr("Import A2L…"));
    if (m_emptyBody)
        m_emptyBody->setText(tr(
            "Import an A2L file to get characteristic-named maps. "
            "Auto-detection is already running in the background — its "
            "results will appear here as they're found."));
    // m_emptyAutoBtn intentionally null — the auto-detect prompt was
    // removed; nothing to retranslate.
    if (m_emptyState && m_emptyState->isVisible())
        positionEmptyState();
    if (m_waveWidget) m_waveWidget->retranslateUi();
    if (m_hexWidget) m_hexWidget->retranslateUi();
}

// ── View switching ────────────────────────────────────────────────────────────

void ProjectView::switchView(int index)
{
    const int prevIndex = m_viewStack->currentIndex();

    // ── Sync scroll position between Text ↔ 2D ──────────────────────────
    // Convert the current view's scroll position to a byte offset,
    // then apply it to the target view.
    if (prevIndex != index && m_project && !m_project->currentData.isEmpty()) {
        if (prevIndex == 0 && index == 1) {
            // Text → 2D: carry hex scroll position to waveform
            int row = m_hexWidget->verticalScrollBar()->value();
            uint32_t byteOff = (uint32_t)row * m_hexWidget->bytesPerRow();
            m_waveWidget->showROM(m_project->currentData, m_hexWidget->getOriginalData());
            m_waveWidget->goToAddress(byteOff);
        } else if (prevIndex == 1 && index == 0) {
            // 2D → Text: carry waveform scroll position to hex view
            uint32_t byteOff = (uint32_t)m_waveWidget->scrollOffset();
            m_hexWidget->goToAddress(byteOff);
        }
    }

    m_viewStack->setCurrentIndex(index);
    m_tabBar->blockSignals(true);
    m_tabBar->setCurrentIndex(index);
    m_tabBar->blockSignals(false);

    if (index == 1 && m_project && !m_project->currentData.isEmpty()) {
        // Only reload ROM if we haven't already done it in the sync block above
        if (prevIndex != 0)
            m_waveWidget->showROM(m_project->currentData, m_hexWidget->getOriginalData());
    }
    if (index == 2 && m_selectedMap.dimensions.x > 0 && m_project) {
        m_map3d->setOriginalData(m_project->originalData);
        m_map3d->showMap(m_project->currentData, m_selectedMap);
    }
}

// ── Empty-state overlay ───────────────────────────────────────────────────────
//
// Shown over the view stack while the active project has no maps. The card is
// parented to m_viewStack (not added to a layout) so it floats above whichever
// view is current; positionEmptyState() keeps it centred on resize. The two
// buttons trigger MainWindow's existing actImportA2L / actAutoDetectMaps slots
// via QMetaObject — projectview.h must not depend on mainwindow.h.

void ProjectView::buildEmptyState()
{
    if (!m_viewStack) return;

    m_emptyState = new QWidget(m_viewStack);
    m_emptyState->setObjectName("pvEmptyState");
    m_emptyState->setStyleSheet(
        "#pvEmptyState { background:#161b22; border:1px solid #30363d; border-radius:10px; }"
        "QLabel { background:transparent; }");
    m_emptyState->setAttribute(Qt::WA_StyledBackground, true);
    m_emptyState->hide();

    auto *card = new QVBoxLayout(m_emptyState);
    card->setContentsMargins(28, 28, 28, 28);
    card->setSpacing(12);
    card->setAlignment(Qt::AlignCenter);

    // Top row: optional × dismiss button anchored top-right.
    // Clicking it sets project->noMapsHintDismissed = true and persists
    // on the next save, so the prompt won't reappear for this project.
    {
        auto *topRow = new QHBoxLayout();
        topRow->setContentsMargins(0, 0, 0, 0);
        topRow->addStretch(1);
        auto *closeBtn = new QPushButton(QStringLiteral("\u2715"));
        closeBtn->setToolTip(tr("Dismiss — don't show this hint again for this project"));
        closeBtn->setCursor(Qt::PointingHandCursor);
        closeBtn->setFixedSize(24, 24);
        closeBtn->setStyleSheet(
            "QPushButton { background:transparent; color:#7d8590; border:none;"
            "  border-radius:12px; font-size:11pt; font-weight:bold; padding:0; }"
            "QPushButton:hover { background:#30363d; color:#e6edf3; }");
        connect(closeBtn, &QPushButton::clicked, this, [this]() {
            if (!m_project) return;
            m_project->noMapsHintDismissed = true;
            m_project->modified = true;
            emit m_project->dataChanged();   // triggers debounced autosave
            updateEmptyState();
        });
        topRow->addWidget(closeBtn);
        card->addLayout(topRow);
    }

    auto *icon = new QLabel(QStringLiteral("\u2B22"));   // hexagon
    icon->setAlignment(Qt::AlignCenter);
    {
        QFont f = icon->font();
        f.setPointSize(36);
        icon->setFont(f);
    }
    icon->setStyleSheet("color:#8b949e; background:transparent;");
    card->addWidget(icon);

    m_emptyTitle = new QLabel(tr("No maps in this project yet"));
    m_emptyTitle->setAlignment(Qt::AlignCenter);
    {
        QFont f = m_emptyTitle->font();
        f.setPointSize(13);
        f.setBold(true);
        m_emptyTitle->setFont(f);
    }
    m_emptyTitle->setStyleSheet("color:#e6edf3; background:transparent;");
    card->addWidget(m_emptyTitle);

    m_emptyBody = new QLabel(
        tr("Import an A2L file to get characteristic-named maps. "
           "Auto-detection is already running in the background — its "
           "results will appear here as they're found."));
    m_emptyBody->setAlignment(Qt::AlignCenter);
    m_emptyBody->setWordWrap(true);
    m_emptyBody->setStyleSheet("color:#8b949e; background:transparent; font-size:10pt;");
    card->addWidget(m_emptyBody);

    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(10);
    btnRow->setAlignment(Qt::AlignCenter);

    m_emptyImportBtn = new QPushButton(tr("Import A2L…"));
    m_emptyImportBtn->setCursor(Qt::PointingHandCursor);
    m_emptyImportBtn->setMinimumHeight(34);
    m_emptyImportBtn->setMinimumWidth(150);
    m_emptyImportBtn->setStyleSheet(
        "QPushButton { background:#1f6feb; color:#fff; border:none;"
        "  border-radius:6px; padding:8px 18px; font-weight:bold; font-size:10pt; }"
        "QPushButton:hover { background:#388bfd; }");
    connect(m_emptyImportBtn, &QPushButton::clicked, this, [this]() {
        triggerMainWindowAction("actImportA2L",
            QStringList{ tr("Import A2L…"), QStringLiteral("Import A2L…") });
    });
    btnRow->addWidget(m_emptyImportBtn);

    // Auto-detect button removed per UX feedback: the scan kicks off
    // automatically on raw-ROM import (see runMapAutoDetectOnImport in
    // mainwindow.cpp). Asking the user to trigger it again from here was
    // redundant and confusing — the empty state now just offers A2L.
    m_emptyAutoBtn = nullptr;

    card->addLayout(btnRow);

    m_emptyState->adjustSize();
    positionEmptyState();
}

void ProjectView::updateEmptyState()
{
    if (!m_emptyState) return;
    // Hide the "No maps yet" prompt when EITHER real maps OR auto-detected
    // overlays are present. Showing the "import A2L / scan for maps"
    // helper while autoDetectedMaps already lists 1400+ candidates was
    // confusing — the user was being asked to do something that had
    // already happened automatically.
    const bool hasRealMaps  = m_project && !m_project->maps.isEmpty();
    const bool hasAutoMaps  = m_project && !m_project->autoDetectedMaps.isEmpty();
    const bool dismissed    = m_project && m_project->noMapsHintDismissed;
    if (!m_project || hasRealMaps || hasAutoMaps || dismissed) {
        m_emptyState->hide();
    } else {
        positionEmptyState();
        m_emptyState->show();
        m_emptyState->raise();
    }
}

void ProjectView::positionEmptyState()
{
    if (!m_emptyState || !m_viewStack) return;
    const int maxW = qMin(420, qMax(260, m_viewStack->width() - 80));
    m_emptyState->setMaximumWidth(maxW);
    m_emptyState->adjustSize();
    const QSize sz = m_emptyState->sizeHint();
    const int x = qMax(0, (m_viewStack->width()  - sz.width())  / 2);
    const int y = qMax(0, (m_viewStack->height() - sz.height()) / 2);
    m_emptyState->setGeometry(x, y, sz.width(), sz.height());
}

void ProjectView::triggerMainWindowAction(const char *slotName,
                                          const QStringList &fallbackTextHints)
{
    // Walk up the parent chain to find the QMainWindow that owns this view.
    QObject *o = parent();
    QMainWindow *mw = nullptr;
    while (o) {
        if (auto *w = qobject_cast<QMainWindow*>(o)) { mw = w; break; }
        o = o->parent();
    }
    if (!mw) {
        for (QWidget *top : QApplication::topLevelWidgets()) {
            if ((mw = qobject_cast<QMainWindow*>(top))) break;
        }
    }
    if (!mw) return;

    // Preferred: invoke the named slot directly. Falls through to the
    // text-based QAction lookup if invokeMethod fails (e.g. private method).
    if (slotName && QMetaObject::invokeMethod(mw, slotName, Qt::QueuedConnection))
        return;

    // Fallback: scan QActions for one whose text matches any hint. Mnemonic
    // ampersands are stripped before comparison to avoid locale drift.
    auto strip = [](QString s) { return s.remove(QLatin1Char('&')); };
    for (const QString &hint : fallbackTextHints) {
        const QString want = strip(hint);
        for (QAction *act : mw->findChildren<QAction*>()) {
            if (strip(act->text()).compare(want, Qt::CaseInsensitive) == 0) {
                act->trigger();
                return;
            }
        }
    }
}

void ProjectView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_emptyState && m_emptyState->isVisible())
        positionEmptyState();
}

void ProjectView::setShowOriginalDiffOverlay(bool on)
{
    // Sprint B — push the overlay flag down to each child widget.  Each
    // widget owns its own rendering; here we only plumb the bit.
    if (m_hexWidget)  m_hexWidget->setShowOriginalDiffOverlay(on);
    if (m_waveWidget) m_waveWidget->setShowOriginalDiffOverlay(on);
    if (m_map3d)      m_map3d->setShowOriginalDiffOverlay(on);
}
