/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QMainWindow>
#include <QDockWidget>
#include <QTreeWidget>
#include <QLineEdit>
#include <QLabel>
#include <QProgressBar>
#include <QToolButton>
#include <QPushButton>
#include <QComboBox>
#include <QMenu>
#include <QSplitter>
#include <QStackedWidget>
#include <QHBoxLayout>
#include <QToolBar>
#include <QAction>
#include <QActionGroup>
#include <QMap>
#include <QHash>
#include <QSet>
#include <QPointer>
#include <QTimer>
#include <QTranslator>
#include <QFutureWatcher>
#include "io/ols/MapAutoDetect.h"   // ols::MapCandidate
#include "romcomparedlg.h"
#include "projectview.h"
#include "mapeditorview.h"
#include "a2lparser.h"
#include "a2limportdialog.h"
#include "project.h"
#include "newprojectdialog.h"
#include "romlinkdialog.h"
#include "featuregate.h"
#include "accountwidget.h"
#include "apiclient.h"

class AIAssistant;
class UpdateChecker;
class CommandPalette;
class DiffPanel;
class SavepointManager;
class SavepointsPanel;
class QDockWidget;
class QScreen;
class QShowEvent;
namespace ads {
class CDockManager;
class CDockWidget;
}
class ModulePanel;
struct PaletteEntry;
#ifdef RX14_DEBUG_RPC
class DebugRpc;
class QJsonObject;
class QJsonArray;
#endif

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    enum class PanelFilter { All, Modified, Starred, Recent, TypeValue, TypeCurve, TypeMap };

    // ── Sprint L Lua bridge accessors (need public visibility) ──
    Project *luaActiveProject() const;     // public wrapper over private activeProject()
    void     luaNewProject();              // public wrapper over private actNewProject()
    int      luaSaveAllProjects();         // returns count of projects saved
    int      luaCloseAllProjects();        // returns count of sub-windows closed
    // Non-interactive close — delegates to the existing eventFilter +
    // finalizeClosedProject deferred-cleanup machinery so back-to-back Lua
    // calls don't trip the access-violation race documented at
    // finalizeClosedProject().  Returns true if a project was closed.
    bool     luaCloseActiveProject(bool deleteFile);
    void     luaRememberLastCreatedMap(Project *project, int mapIndex);
    struct MapInfo *luaLastCreatedMap(Project *project) const;
    void     luaClearLastCreatedMap(Project *project = nullptr);
    // Session-local indices for the legacy Lua window API. They refer to Hex
    // docks only, preserving its historical "one window per project" model.
    int      luaWindowCount() const;
    int      luaActiveWindowIndex() const;
    bool     luaActivateWindow(int zeroBasedIndex);

#ifdef RX14_DEBUG_RPC
    // ── Debug RPC entry points ────────────────────────────────────────────
    // Called from DebugRpc on the Qt main thread.  All return QJsonObject /
    // bool so the RPC layer can forward results without re-implementing
    // window management in the server.
    QJsonObject rpcGetWorkspaceInfo() const;
    bool rpcActivateWindow(const QString &windowId);
    bool rpcCloseWindow(const QString &windowId);
    QJsonArray rpcGetOpenProjects() const;
    QJsonObject rpcGetProjectInfo(const QString &projPath) const;
    bool rpcCloseProject(const QString &projPath);
    QJsonObject debugStateSnapshot() const;
    bool        debugTakeScreenshot(const QString &target,
                                    QString *outPath,
                                    QString *outErr);
    bool        debugTriggerAction(const QString &name,
                                   bool *outChecked,
                                   QString *outErr);
    bool        debugSetScroll(const QString &target, int subIdx, int value,
                               QString *outErr);
    bool        debugSwitchView(int subIdx, int viewIdx, QString *outErr);
    /// Headless ROM load — bypasses ProjectPropertiesDialog so the dev
    /// harness can drive end-to-end tests over RPC.  Same effect as the
    /// user picking File → New Project and clicking through.
    bool        debugLoadRom(const QString &romPath, QString *outErr);
    /// Apply an edit operation directly without going through UI dialogs.
    /// Used by RPC harness for end-to-end testing.  @p op is one of the
    /// EditOp enum values cast to int.  @p start/@p end are byte range.
    /// @p value is used by Absolute (set to v) and Relative (delta v).
    bool        debugApplyEdit(int subIdx, int op, int start, int end,
                               double value, QString *outErr);
    /// Add an annotation programmatically. @p length defaults to 1.
    /// Empty @p text means a pure marker.
    bool        debugAddAnnotation(int subIdx, qint64 addr, const QString &text,
                                   qint64 length, QString *outErr);
    /// Snapshot the active project's annotation list for verification.
    QJsonArray  debugAnnotationList(int subIdx) const;
    /// Sprint D — invoke MapListExporter without going through QFileDialog.
    bool        debugExportMapList(int subIdx, bool csv,
                                   const QString &path, QString *outErr);
    /// Sprint F — fingerprint a map by name and return matches above @p threshold.
    QJsonArray  debugFindSimilar(int subIdx, const QString &refMapName,
                                 double threshold) const;
    /// Open a saved .rx14proj from disk through the same path the user
    /// would take via File → Open.
    bool        debugOpenProject(const QString &path, QString *outErr);
    /// First N maps of the active project as JSON array (name, address, dims).
    QJsonArray  debugMapList(int subIdx, int limit) const;
    /// Read up to 256 bytes of currentData starting at @p addr — used by the
    /// E2E harness to verify edit operations actually changed the bytes.
    QString     debugReadBytes(int subIdx, qint64 addr, int len) const;
    /// Apply an edit op to *each* of the given map names without going through
    /// the QMessageBox::question UI confirmation. Same code path as runBulkEdit.
    bool        debugBulkEdit(int subIdx, const QStringList &mapNames,
                              int op, double v, QString *outErr);
    /// Trigger one undo step on the active view's WaveformEditor.
    bool        debugUndo(int subIdx, QString *outErr);
    /// Remove all annotations whose addr equals @p addr.
    bool        debugRemoveAnnotation(int subIdx, qint64 addr, QString *outErr);
#endif

protected:
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void closeEvent(QCloseEvent *e) override;
    void showEvent(QShowEvent *e) override;
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:
    // UI-scale: offer a rescale-restart when the window lands on a screen
    // whose recommended scale differs from the one applied at startup.
    void onScreenChanged(QScreen *scr);
    QSet<QString> m_scalePromptedScreens;   // prompted once per screen/session
    bool          m_screenWatchConnected = false;

    // Restyle the ADS dock chrome (tabs, title bars, containers) from the
    // current AppConfig colors; called at startup and on every theme change.
    void applyDockManagerTheme();

private slots:
    // Project menu
    void actProjectManager();
    void actNewProject();
    void actOpenProject();
    void actSaveProject();
    void actSaveProjectAs();
    void actCloseProject();
    void actImportA2L(const QString &droppedPath = {});
    void actImportKP(const QString &droppedPath = {});
    void actImportOlsProject();
    void actAddVersion();
    void actExportROM();
    void actExportOlsProject();
    void actVerifyChecksum();
    void actCorrectChecksum();
    void actOBDUnlock();

    // Export-time checksum guard. Verifies the bytes about to be written and,
    // on a mismatch, prompts the user to correct them (correcting `data` in
    // place) or export anyway. Returns false only if the user cancels.
    bool confirmChecksumBeforeExport(class Project *proj, QByteArray &data);

    // View / Window
    void actGoHome();       // close all projects (save prompt) → welcome page
    void actTileWindows();
    void actCascadeWindows();
    void actSaveWorkspacePerspective();
    void actRestoreWorkspaceLayout();
    void actLoadWorkspacePerspective();
    void actCompareProjects();

    // ROM linking / compare
    void actLinkRom();
    void actImportLinkedVersion();
    void actCompareRoms();
    void actCompareHex();
    void actImportMapPack();
    void actOpenPatchEditor();

    // Navigation
    void actPrevMap();
    void actNextMap();

    // VSCode-style command palette (Cmd/Ctrl+K). Builds a fresh entry list
    // from the current m_projects + ProjectRegistry + every QAction registered
    // on this window, then shows the popup.
    void actShowCommandPalette();

    // Internal
    void onWorkspaceDockActivated();
    void onMapActivated(const MapInfo &map, Project *project);
    void onStatusMessage(const QString &msg);
    void onCloneVersionRequested(Project *parent, int versionIndex);
    /// Sprint I — open Find Similar Files dialog using the active
    /// project's currentData (or originalData if available) as the
    /// query.  Falls back to a status-bar message if no project open.
    void actFindSimilarFiles();
    /// Sprint I — explicit "Index WinOLS catalog now" entry that
    /// runs the full fingerprint scan over every configured scan
    /// root.  Useful when user wants to build the index without
    /// first opening a project (the "Find Similar Files" entry
    /// requires an active project for the query side).
    void actBuildSimilarityIndex();
    void onTreeItemClicked(QTreeWidgetItem *item, int col);
    void activateTreeMapItem(QTreeWidgetItem *item);
    void refreshRecentMapsStrip();

    // Toolbar display-format toggles
    void onDataSizeChanged();
    void onDisplayFmtChanged();
    void onByteOrderChanged();
    void onSignChanged();

    // 2D view sync
    void onWaveSyncScroll(int scrollOffset);
    void onWaveSyncZoom(int sliderValue);   // fan out zoom slider across views
    void onHexSyncScroll(int byteOffset);
    void onSyncViewSwitch(int index);

private:
    void buildActions();
    void buildMenuBar();
    void buildToolBars();
    void buildLeftPanel();
    void buildWelcomePage();
    void updateCentralPage();
    void loadLanguage(const QString &lang);
    void retranslateUi();

    ads::CDockWidget *openProject(Project *project);
    void loadROMIntoProject(Project *project, const QString &romPath);
    void loadA2LIntoProject(Project *project, const QString &a2lPath);
    void runMapAutoDetectOnImport(Project *project);
    void cancelMapScan(Project *project);
    // Refresh the "Saved · Ns ago / Modified" status-bar indicator.
    void updateAutoSaveStatus();
    void refreshProjectTree();     // thin wrapper — arms debounce timer
    void refreshProjectTreeNow();  // actual rebuild
    void updateMapTreeEditorAccents();

    struct MapEditorKey {
        // A display/A2L identifier is not guaranteed unique: a calibration
        // may legitimately contain several maps with the same name or id.
        // Cell data start is the stable per-map discriminator inside a project.
        QString mapId;
        uint32_t cellDataStart = 0;

        bool operator==(const MapEditorKey &other) const {
            return mapId == other.mapId && cellDataStart == other.cellDataStart;
        }
        friend size_t qHash(const MapEditorKey &key, size_t seed = 0) noexcept {
            return ::qHash(key.mapId, seed) ^ ::qHash(key.cellDataStart, seed);
        }
    };
    enum class CloseIntent { UnreferencedViewClose, ExplicitProjectClose };
    struct WorkspaceViewRecord {
        enum class Kind { HexView, MapEditor };
        Project *project = nullptr;
        QPointer<ads::CDockWidget> dock;
        QPointer<QWidget> content;
        Kind kind = Kind::HexView;
        MapEditorKey mapKey;
    };

    static MapEditorKey mapEditorKey(const MapInfo &map);
    ads::CDockWidget *projectDock(Project *project) const;
    QList<ProjectView *> projectViews() const;
    ProjectView *projectViewAt(int index) const;
    ProjectView *activeProjectView() const;
    QList<MapEditorView *> mapEditorViews(Project *project = nullptr) const;
    MapEditorView *activeMapEditor() const;
    ads::CDockWidget *findMapEditorDock(Project *project, const MapInfo &map) const;
    void updateMapEditorKey(Project *project, MapEditorView *editor,
                            const MapInfo &map);
    void registerWorkspaceDock(Project *project, ads::CDockWidget *dock,
                               QWidget *content, WorkspaceViewRecord::Kind kind,
                               const MapEditorKey &mapKey);
    void unregisterWorkspaceDock(ads::CDockWidget *dock);
    // Wayland compositor title-bar drags cannot be observed by ADS. This is
    // the deterministic counterpart to drag-to-dock for an already-floating
    // document and keeps the Float Active Window command reversible.
    void redockWorkspaceDock(ads::CDockWidget *dock);
    // ADS emits closeRequested before it destroys a DeleteOnClose dock.
    // Keep the document-save policy here rather than in a QWidget close event:
    // CDockWidget::closeDockWidget() removes the dock directly and does not
    // send QEvent::Close to the dock widget.
    bool closeWorkspaceDock(ads::CDockWidget *dock);
    void requestProjectFinalization(Project *project, CloseIntent intent);

    ProjectView *activeView()    const;
    Project     *activeProject() const;
    bool hasOpenMapEditors(Project *project) const;
    bool hasVisibleProjectView(Project *project) const;
    void finalizeProjectWhenUnreferenced(Project *project);
    void broadcastAvailableProjects();   // push m_projects to all ProjectViews
    void finalizeClosedProject(Project *p); // deferred cleanup after sub close
    void spawnLinkedRomSubwindowsFor(Project *parent);  // re-open child windows after load
    QSet<Project *> m_expandAllOnNextBuild;             // projects that should auto-expand all map groups on the next tree rebuild

    void editProjectInfo();
    void actAutoDetectMaps();   // Misc → Auto-detect Maps… (OLS-style scanner)
    void actAutoDetectEcu();    // Misc → Auto-detect ECU… (73-detector chain port)
    void applyDisplayFormat();

    void autoRetryFailed(const QVector<QPair<QString,QString>> &failed,
                         const QString &lang,
                         const QVector<QPair<QString,QString>> &allItems,
                         int prevApplied, int totalMaps);
    void applyTreeFilter();    // push current format state → overlay
    void applyUiTheme();       // rebuild stylesheets from AppConfig UI colors
    void retileWindows();      // reset the dynamic document arrangement
    void openMapEditor(const QByteArray &romData, const MapInfo &map,
                       ByteOrder byteOrder, Project *project = nullptr);
    void openMapViewer(Project *project, const MapInfo &map);
    // Returns true after bringing the sole editor for this project/map to
    // front.  Map editors are widgets hosted by ADS, never dialogs.
    bool activateMapEditor(Project *project, const MapInfo &map);
    ads::CDockWidget *mapEditorDock(const MapEditorView *editor) const;
    void closeMapEditor(MapEditorView *editor);
    void closeMapEditors(Project *project, const MapInfo *map = nullptr);
    void saveWorkspacePerspective(const QString &name);
    void restoreWorkspacePerspective(const QString &name);

    // ── Translators ───────────────────────────────────────────────────
    QTranslator m_qtTr;
    QTranslator m_appTr;

    // ── Layout ────────────────────────────────────────────────────────
    QSplitter   *m_mainSplitter    = nullptr;
    QWidget     *m_leftPanel       = nullptr;
    QLabel      *m_leftPanelTitle  = nullptr;
    QTreeWidget *m_projectTree     = nullptr;
    QHash<QString, QTreeWidgetItem *> m_mapTreeItems;
    QWidget     *m_recentMapsStrip = nullptr;
    QHBoxLayout *m_recentMapsRow   = nullptr;
    QLabel      *m_recentMapsEmpty = nullptr;
    QLabel      *m_recentMapsTitle = nullptr;
    QLineEdit   *m_filterEdit           = nullptr;
    QPushButton *m_filterChangedBtn     = nullptr;   // "changed only" toggle
    ads::CDockManager *m_dockManager     = nullptr;
    QStackedWidget *m_centralStack  = nullptr;
    QWidget        *m_welcomePage   = nullptr;
    QToolBar       *m_formatToolbar  = nullptr;
    QToolBar       *m_projectToolbar = nullptr;
    QAction        *m_actAutoScanOnLoad = nullptr;
    QHash<Project*, QFutureWatcher<QVector<ols::MapCandidate>>*>
        m_mapScanWatchers;
    QSet<Project *> m_finalizationScheduled;
    QSet<Project *> m_forceProjectClose;
    QWidget        *m_scanStatusWidget = nullptr;
    QLabel         *m_scanStatusLabel  = nullptr;
    QProgressBar   *m_scanStatusBar    = nullptr;
    // VSCode-style auto-save indicator + state.
    QLabel         *m_saveStatusLabel  = nullptr;
    QDateTime       m_lastAutoSaveTime;
    QTimer         *m_saveStatusTickTimer = nullptr;
    QSet<Project*>  m_promptedFirstSave;
    // Auto-save mode persisted in QSettings under "autoSaveMode":
    //   "off"             — no auto-save (manual Ctrl+S only)
    //   "afterDelay"      — debounced after each edit (default)
    //   "onFocusChange"   — when project view loses focus
    //   "onWindowDeactivate" — when application loses focus
    QAction        *m_actAutoSaveOff      = nullptr;
    QAction        *m_actAutoSaveDelay    = nullptr;
    QAction        *m_actAutoSaveFocus    = nullptr;
    QAction        *m_actAutoSaveDeact    = nullptr;

    // ── Panel filter ──────────────────────────────────────────────────
    PanelFilter m_panelFilter = PanelFilter::All;
    QVector<QPair<Project*, QString>> m_recentMaps;  // newest first, max 20
    // Filter chip buttons (stored for styling updates)
    QPushButton *m_chipAll      = nullptr;
    QPushButton *m_chipModified = nullptr;
    QPushButton *m_chipStarred  = nullptr;
    QPushButton *m_chipRecent   = nullptr;
    QPushButton *m_chipValue    = nullptr;
    QPushButton *m_chipCurve    = nullptr;
    QPushButton *m_chipMap      = nullptr;

    // ── Menus (stored for retranslation) ──────────────────────────────
    QMenu *m_menuProject = nullptr;
    QMenu *m_menuEdit    = nullptr;
    QMenu *m_menuView    = nullptr;
    QMenu *m_menuSel     = nullptr;
    QMenu *m_menuFind    = nullptr;
    QMenu *m_menuMisc    = nullptr;
    QMenu *m_menuDatalog = nullptr;
    QMenu *m_menuLang    = nullptr;
    QMenu *m_menuWindow  = nullptr;
    QMenu *m_menuHelp    = nullptr;

    // ── Datalog menu actions / state ──────────────────────────────────
    void openDatalog();
    void compareDatalogs();
    void rebuildDatalogMenu();
    QStringList datalogRecent() const;
    void datalogPushRecent(const QString &path);

    // Map-editor dock ownership lives exclusively in m_workspaceViews.
    QHash<Project *, QPointer<ads::CDockWidget>> m_projectDocks;
    QList<WorkspaceViewRecord> m_workspaceViews;
    QPointer<ads::CDockWidget> m_activeWorkspaceDock;

    // ── A2L parser ────────────────────────────────────────────────────
    A2LParser *m_parser          = nullptr;
    Project   *m_parsingProject  = nullptr;

    // ── Auto-save ──────────────────────────────────────────────────────
    QTimer *m_autoSaveTimer = nullptr;
    void    autoSaveAll();
    void    scheduleAutoSave();

    // ── Debounced project-tree refresh ─────────────────────────────────
    QTimer *m_treeRefreshTimer = nullptr;

    // ── Projects / navigation state ───────────────────────────────────
    QVector<Project *> m_projects;
    // A ROM patch already drives hex/wave/map repaint through romPatched().
    // Keep its following dataChanged() for title/autosave/diff work, but do
    // not rebuild and scroll the project tree for every byte transaction.
    QSet<Project *> m_projectsWithPendingRomPatch;
    int                m_currentMapIdx = -1; // index into activeProject->maps
    QPointer<Project>  m_luaLastCreatedMapProject;
    int                m_luaLastCreatedMapIndex = -1;

    // ── Font size ─────────────────────────────────────────────────────
    int        m_fontSize       = 10;
    QLabel    *m_fontSizeLabel  = nullptr;
    int        m_treeFontSize   = 10;
    QLabel    *m_treeFontLabel  = nullptr;

    // ── Display format state (reflected in toolbar toggles) ───────────
    int       m_dataSize    = 1;      // 1/2/4 bytes
    bool      m_dataFloat   = false;
    ByteOrder m_byteOrder   = ByteOrder::BigEndian;
    bool      m_signed      = false;
    int       m_displayFmt  = 0;      // 0=dec 1=hex 2=bin 3=pct
    bool      m_showDiff    = false;
    bool      m_heightColors= true;

    // ── File / Project actions ─────────────────────────────────────────
    QAction *m_actProjectMgr = nullptr;
    QAction *m_actNew        = nullptr;
    QAction *m_actOpen       = nullptr;
    QAction *m_actSave       = nullptr;
    QAction *m_actSaveAs     = nullptr;
    QAction *m_actClose      = nullptr;
    QAction *m_actHome       = nullptr;
    QAction *m_actImportA2L  = nullptr;
    QAction *m_actImportKP     = nullptr;
    QAction *m_actImportXDF     = nullptr;
    QAction *m_actImportFRF     = nullptr;
    // Single OLS-import action — toolbar OLS button + Project menu
    // "Import OLS…" entry both reference this. Replaces the previous
    // m_actImportOLS / m_actImportOLS pair which routed to the same
    // implementation anyway.
    QAction *m_actImportOLS = nullptr;
    QAction *m_actAddVersion = nullptr;
    QAction *m_actExport        = nullptr;
    QAction *m_actExportOLS  = nullptr;

    // ── ROM linking / compare ──────────────────────────────────────────
    QAction *m_actLinkRom         = nullptr;
    QAction *m_actImportLinkedVer = nullptr;
    QAction *m_actCompareRoms     = nullptr;
    QAction *m_actCompareHex      = nullptr;
    QAction *m_actImportMapPack   = nullptr;
    QAction *m_actImportCsvList   = nullptr;  // toolbar CSV map list import
    QAction *m_actPatchEditor     = nullptr;
    QAction *m_actDtcManager      = nullptr;
    QAction *m_actDtcWizard       = nullptr;
    QAction *m_actAIFunctions     = nullptr;
    QAction *m_actVerifyChecksum  = nullptr;
    QAction *m_actCorrectChecksum = nullptr;
    QAction *m_actOBDUnlock       = nullptr;

    // ── Window actions ─────────────────────────────────────────────────
    QAction *m_actTile        = nullptr;
    QAction *m_actCascade     = nullptr;
    QAction *m_actCompare     = nullptr;
    QAction *m_actFloatWindow = nullptr;
    QAction *m_actLoadWorkspaceLayout = nullptr;

    // ── Navigation actions ─────────────────────────────────────────────
    QAction *m_actPrevMap     = nullptr;
    QAction *m_actNextMap     = nullptr;
    QAction *m_actSyncCursors = nullptr;

    // ── Selection / map editing operations (Sprint A) ──────────────────
    // Funnels the active view's selection through the WaveformEditor of
    // its ProjectView so all three views (hex / waveform / 3D) share one
    // undo stack per project.  See applyEditOp() in mainwindow.cpp.
    enum class EditOp {
        ValuePlus1, ValueMinus1,
        Absolute, Relative, Slider, RoundLimit,
        Original, Interpolate, Smooth, Flatten,
    };
    struct EditParams {
        double value     = 0.0;     // Absolute, Slider
        double delta     = 0.0;     // Relative add
        double scaleVal  = 1.0;     // Relative multiply
        bool   isScale   = false;   // distinguishes add vs multiply
        int    multiple  = 1;       // RoundLimit
        double minVal    = 0.0;     // RoundLimit / Slider
        double maxVal    = 0.0;     // RoundLimit / Slider
    };
    QAction *m_actValPlus1    = nullptr;
    QAction *m_actValMinus1   = nullptr;
    QAction *m_actUndoRom     = nullptr;
    QAction *m_actRedoRom     = nullptr;
    QAction *m_actChangeAbs   = nullptr;
    QAction *m_actChangeRel   = nullptr;
    QAction *m_actChangeSlider = nullptr;
    QAction *m_actRoundLimit  = nullptr;
    QAction *m_actOriginalVal = nullptr;
    QAction *m_actInterpolate = nullptr;
    QAction *m_actSmooth      = nullptr;
    QAction *m_actFlatten     = nullptr;
    QAction *m_actAgain       = nullptr;
    EditOp     m_lastEditOp = EditOp::ValuePlus1;
    EditParams m_lastEditParams;
    bool       m_haveLastEdit = false;
    void applyEditOp(EditOp op, const EditParams &p);
    void onEditOpFromMenu(EditOp op);    // helper that pops dialog if needed
    void updateRomUndoActions();
    /// Slot connected to HexWidget::editOpRequested / Map3DWidget::editOpRequested.
    /// Wrapped as a real member function (not a lambda) because
    /// Qt::UniqueConnection refuses lambdas — using a lambda crashes with
    /// "Unique connection requires the slot to be a pointer to a member
    /// function of a QObject subclass." (qobject.h:263)
    void onEditOpRequestedFromView(int code);

    // ── Command palette (Cmd/Ctrl+K) ───────────────────────────────────
    QAction        *m_actCmdPalette = nullptr;
    QAction        *m_actPreferences = nullptr;
    CommandPalette *m_cmdPalette    = nullptr;  // lazily constructed

    // ── Map operation actions ──────────────────────────────────────────
    QAction *m_actOptimize   = nullptr;
    QAction *m_actDifference = nullptr;
    QAction *m_actIgnore     = nullptr;
    QAction *m_actFactor     = nullptr;
    QAction *m_actOrigFactor = nullptr;

    // ── Data size group (exclusive checkable) ──────────────────────────
    QAction *m_act8bit  = nullptr;
    QAction *m_act16bit = nullptr;
    QAction *m_act32bit = nullptr;
    QAction *m_actFloat = nullptr;
    QActionGroup *m_grpDataSize = nullptr;

    // ── Byte order group ───────────────────────────────────────────────
    QAction *m_actLo = nullptr;
    QAction *m_actHi = nullptr;
    QActionGroup *m_grpByteOrder = nullptr;

    // ── Sign group ─────────────────────────────────────────────────────
    QAction *m_actSigned   = nullptr;
    QAction *m_actUnsigned = nullptr;
    QActionGroup *m_grpSign = nullptr;

    // ── Display format group ───────────────────────────────────────────
    QAction *m_actDec = nullptr;
    QAction *m_actHex = nullptr;
    QAction *m_actBin = nullptr;
    QAction *m_actPct = nullptr;
    QActionGroup *m_grpDisplayFmt = nullptr;

    // ── Visual toggles ─────────────────────────────────────────────────
    QAction *m_actHeightColors = nullptr;

    // ── Differences panel (View → Differences) ─────────────────────────
    QAction     *m_actToggleDiff = nullptr;
    QDockWidget *m_diffDock      = nullptr;
    QDockWidget *m_moduleDock    = nullptr;
    ModulePanel *m_modulePanel   = nullptr;
    void         refreshModulePanel();          // populate from active project
    DiffPanel   *m_diffPanel     = nullptr;

    // ── Differences-to-Original overlay (View → Diff vs Original) ──────
    // Sprint B: toggle that highlights every byte that differs from
    // Project::originalData across hex / waveform / 3D map.  Persisted
    // in QSettings under "view/showOriginalDiff".
    QAction *m_actDiffOriginal = nullptr;
    bool     m_showOriginalDiff = false;
    void onToggleDiffOriginal(bool on);

    // ── Annotations (Sprint C) ──────────────────────────────────────────
    QAction *m_actInsertComment = nullptr;
    QAction *m_actInsertMarker  = nullptr;
    QAction *m_actDeleteComment = nullptr;
    QAction *m_actNextMarker    = nullptr;
    QAction *m_actPrevMarker    = nullptr;
    void onInsertComment();
    void onInsertMarker();
    void onDeleteComment();
    void onJumpMarker(bool forward);
    void exportMapListCsv();
    void exportMapListJson();
    void exportTuningReport();
    void actImportXdf(const QString &droppedPath = {});
    void actImportFrf();
    // Open a decoded VAG flash block (raw bytes) as a fresh project.
    void openExtractedRom(const QByteArray &romBytes, const QString &suggestedName);
    // WinOLS-style numeric value search across the active ROM.
    void actFindValue();
    void actExportXdf();
    void runBulkEdit(const QVector<MapInfo> &maps);
    void runFindSimilar(const MapInfo &reference);
    struct AnnoCtx {
        class AnnotationStore *store = nullptr;
        qint64 offset = -1;
        Project *project = nullptr;
        ProjectView *view = nullptr;
        bool ok = false;
    };
    AnnoCtx resolveAnnoCtx();

    // ── Tuning Branches / Savepoints (View → Tuning Branches) ──────────
    QAction          *m_actToggleSavepoints = nullptr;
    QDockWidget      *m_savepointsDock      = nullptr;
    SavepointsPanel  *m_savepointsPanel     = nullptr;
    SavepointManager *m_savepoints          = nullptr;
    void onSavepointsSwitchProject();
    void onDiffRowActivated(quint32 address);
    void onProjectDataChangedForDiff();   // refreshes DiffPanel when any
                                          // open project's bytes change
    void onDiffAlignmentChanged();        // re-aligns B/C views from A's
                                          // current scroll position
    void onDiffComparisonChanged();       // pushes B's bytes (offset-shifted)
                                          // into A's hex/waveform as comparison
                                          // data so colour overlays show up

    // ── AI Assistant ───────────────────────────────────────────────────
    QAction      *m_actToggleAI  = nullptr;
    AIAssistant  *m_aiAssistant  = nullptr;

    // ── Account ────────────────────────────────────────────────────────
    AccountWidget *m_accountWidget  = nullptr;
    QAction       *m_actAccount     = nullptr;

    // ── AI Translation ─────────────────────────────────────────────────
    QPushButton   *m_btnTranslateAll = nullptr;
    QComboBox     *m_langCombo       = nullptr;
    // In-memory session cache: map name → TranslationResult
    QMap<QString, TranslationResult> m_translations;

    // ── Update checker ──────────────────────────────────────────────────
    UpdateChecker *m_updateChecker = nullptr;
    QFrame        *m_updateBar     = nullptr;
    QLabel        *m_updateLabel   = nullptr;
    QProgressBar  *m_updateProgress = nullptr;
    QPushButton   *m_updateBtn     = nullptr;
    QString        m_updateUrl;
    QString        m_updateVersion;
    QString        m_updateChangelog;

#ifdef RX14_DEBUG_RPC
    // ── Debug RPC server (TCP 127.0.0.1:48714) ────────────────────────────
    DebugRpc *m_debugRpc = nullptr;
#endif
};
