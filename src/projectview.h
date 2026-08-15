/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QWidget>
#include <QStackedWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QTabBar>
#include "hexwidget.h"
#include "waveformwidget.h"
#include "map3dwidget.h"
#include "project.h"

// ProjectView — the MDI content widget for one project.
// Contains: version bar, hex/waveform/3D view stack, bottom tab bar.
// Map selection is handled externally by MainWindow's left panel.

class ProjectView : public QWidget {
    Q_OBJECT

public:
    explicit ProjectView(QWidget *parent = nullptr);

    void loadProject(Project *project);
    Project        *project()        const { return m_project; }
    WaveformWidget *waveformWidget() const { return m_waveWidget; }
    HexWidget      *hexWidget()      const { return m_hexWidget; }
    Map3DWidget    *map3dWidget()    const { return m_map3d; }
    /// The map currently shown in the 2D/3D views (set via showMap/goToMap).
    /// Used by Sprint A's "operate on whole map when no byte selection
    /// exists" fallback in MainWindow's edit-op dispatcher.
    const MapInfo  &selectedMap()    const { return m_selectedMap; }
    int             currentViewIndex() const { return m_viewStack ? m_viewStack->currentIndex() : 0; }

    // Called by MainWindow when a map is selected in the left panel
    void showMap(const MapInfo &map);
    void goToAddress(uint32_t addr);
    void switchToView(int index); // 0=Hex, 1=2D, 2=3D
    void goToMap(const MapInfo &map);
    // Forward display parameters to the hex widget
    void setDisplayParams(int cellSize, ByteOrder bo, int displayFmt, bool isSigned);
    void setFontSize(int pt);

    // Called by MainWindow when the set of open projects changes
    void setAvailableProjects(const QVector<Project*> &projects);

    // Called by MainWindow after a language change
    void retranslateUi();

signals:
    void mapActivated(const MapInfo &map, Project *project);
    void statusMessage(const QString &msg);
    void viewSwitched(int index);   // emitted when user changes tab (0=Text 1=2D 2=3D)

    /// Sprint H: user right-clicked the version combo and asked to open
    /// a specific version in a separate MDI subwindow (side-by-side view).
    /// @p versionIndex follows ProjectView convention:
    ///   -1  → "Current (working)" (Project::currentData)
    ///    0  → first ProjectVersion snapshot (Project::versions[0])
    ///    n  → Project::versions[n]
    /// MainWindow clones the project, swaps currentData, opens new window.
    void cloneVersionRequested(Project *project, int versionIndex);

public slots:
    void switchView(int index); // 0=Hex  1=2D  2=3D

    /// Sprint B: forward "Diff vs Original" overlay flag to all three
    /// child widgets (hex / waveform / 3D map).  Each widget renders the
    /// flag in its own way.  Called by MainWindow when the user toggles
    /// the View → Differences vs Original action.
    void setShowOriginalDiffOverlay(bool on);
    void onAddVersion();

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onVersionChanged(int index);
    void onOffsetSelected(uint32_t offset, uint8_t value);
    void onDataModified(int count);
    void updateComparisonSource();

private:
    void rebuildVersionCombo();
    void rebuildComparisonCombo();

    // Top bar
    QLabel      *m_titleLabel   = nullptr;
    QLabel      *m_versionLabel = nullptr;
    QPushButton *m_btnAddVer    = nullptr;
    QLabel      *m_cmpLabel     = nullptr;
    QComboBox   *m_cmpCombo     = nullptr;
    QLabel      *m_gotoLabel    = nullptr;
    QComboBox   *m_versionCombo = nullptr;
    QLineEdit   *m_addressInput = nullptr;

    // View
    QStackedWidget *m_viewStack  = nullptr;
    HexWidget      *m_hexWidget  = nullptr;
    WaveformWidget *m_waveWidget = nullptr;
    Map3DWidget    *m_map3d      = nullptr;
    bool            m_isSelectingFromHex = false;

    // Empty-state overlay shown when the active project has no maps.
    // Built once in the constructor and re-positioned via resizeEvent so it
    // stays centred over the view stack. Hidden as soon as the project gains
    // at least one map (A2L import, auto-detect, manual create, etc).
    QWidget        *m_emptyState     = nullptr;
    QLabel         *m_emptyTitle     = nullptr;
    QLabel         *m_emptyBody      = nullptr;
    QPushButton    *m_emptyImportBtn = nullptr;
    QPushButton    *m_emptyAutoBtn   = nullptr;
    void buildEmptyState();
    void updateEmptyState();
    void positionEmptyState();
    void triggerMainWindowAction(const char *slotName, const QStringList &fallbackTextHints);

    // bottom tab bar
    QTabBar *m_tabBar = nullptr;

    Project    *m_project = nullptr;
    MapInfo     m_selectedMap;

    // All open projects (set by MainWindow), used to populate the comparison dropdown
    QVector<Project*> m_allProjects;
};
