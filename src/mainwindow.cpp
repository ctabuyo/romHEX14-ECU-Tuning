#include "mainwindow.h"
#include "appconstants.h"
#include "lua/LuaEngine.h"
#include "hexcomparedlg.h"
#include "diffpanel.h"
#include "savepoints/SavepointManager.h"
#include "annotations/AnnotationStore.h"
#include "io/MapListExporter.h"
#include "io/TuneReport.h"
#include "io/XdfIo.h"
#include "io/ols/KpExporter.h"
#include "edit/FindSimilarMapsDlg.h"
#include "edit/MapFingerprint.h"
#include "io/winols/SimilarFilesDlg.h"
#include "io/winols/BuildIndexProgressDlg.h"
#include "io/winols/SimilarityIndex.h"
#include "io/winols/WinOlsConfig.h"
#include "io/winols/OlsCfgParser.h"
#include "io/legion/LegionDlg.h"
#include "module/ModuleRegistry.h"
#include "module/ModulePanel.h"
#include "io/legion/Legion.h"
#include <QCryptographicHash>
#include "savepoints/SavepointsPanel.h"
#include "debug/DebugLog.h"
#include <QDockWidget>
#include <QElapsedTimer>
#include <QScrollBar>
#ifdef RX14_DEBUG_RPC
#  include "debug/DebugRpc.h"
#  include "logger.h"
#  include <QJsonObject>
#  include <QJsonArray>
#  include <QPixmap>
#  include <QDir>
#endif
#include "updatechecker.h"
#include "olsparser.h"
#include "kpparser.h"
#include "kpimportdlg.h"
#include "frfimportdlg.h"
#include "valuesearchdlg.h"
#include "createmapdlg.h"
#include "mappropertiesdlg.h"
#include "io/ols/OlsImporter.h"
#include "io/ols/OlsProjectBuilder.h"
#include "io/ols/OlsExporter.h"
#include "io/ols/MapAutoDetect.h"
#include "io/ols/EcuAutoDetect.h"
#include "io/ols/KpImporter.h"
#include <QDesktopServices>
#include <QProcess>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include "projectpropertiesdlg.h"
#include "projectmanagerdlg.h"
#include "projectregistry.h"
#include "appconfig.h"
#include "configdialog.h"
#include "aboutdialog.h"
#include "logindialog.h"
#include "apiclient.h"
#include "accountwidget.h"
#include "ecudetector.h"
#include "romparser.h"
#include "mappackdlg.h"
#include "patcheditordlg.h"
#include "dtcdialog.h"
#include "cloud/CloudToolsDlg.h"
#include "checksummanager.h"
#include "checksumelectdlg.h"
#include "aifunctionsdlg.h"
#include "aiassistant.h"
#include "uiwidgets.h"
#include "brandlogo.h"
#include "commandpalette.h"
#include "projectregistry.h"
#include <QStackedWidget>
#include <QListWidget>
#include <QListWidgetItem>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QStyle>
#include <QMouseEvent>
#include <QPointer>
#include <QPainterPath>
#include <QRegularExpression>
#include <cstring>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QFileDialog>
#include <QMenuBar>
#include <QToolBar>
#include <QToolTip>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QInputDialog>
#include <QCloseEvent>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QEventLoop>
#include <QProgressDialog>
#include <QTimer>
#include <QHeaderView>
#include <QMenu>
#include <QLabel>
#include <QSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QDialog>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QPainter>
#include <QFont>
#include <QLibraryInfo>
#include <QSettings>
#include <QLocale>
#include <QDateTime>
#include <QScrollArea>
#include <QGraphicsDropShadowEffect>
#include <QRadialGradient>
#include <QLinearGradient>
#include <QUrl>
#include <functional>

static const int kTreeFolderRole = Qt::UserRole + 5;

// ── Icon factory ──────────────────────────────────────────────────────────────
// Creates a 22×22 icon by rendering a short symbol string in the given colour.
// Used for all icon-only toolbar buttons.

static int s_iconSize = 28;

// Filesystem-safe default basename for the project's .rx14proj file.
//
// For OLS-imported projects we want `OLS_<brand>_<model>_<ecu>` so the
// saved file is self-describing in Finder. For non-OLS projects we keep
// the existing `displayName()` behaviour.
//
// Spaces collapse to `_` and a small whitelist of FS-hostile characters
// (/, \, :, *, ?, ", <, >, |) is replaced with `_` so the user is never
// prompted to fix the name on Windows or macOS.
static QString suggestedProjectBasename(const Project *p)
{
    auto sanitize = [](QString s) {
        s = s.trimmed();
        static const QRegularExpression bad(QStringLiteral("[ /\\\\:\\*\\?\"<>|]+"));
        s.replace(bad, QStringLiteral("_"));
        // Collapse runs of underscores
        static const QRegularExpression dup(QStringLiteral("_+"));
        s.replace(dup, QStringLiteral("_"));
        // Strip leading/trailing underscores
        while (s.startsWith('_')) s.remove(0, 1);
        while (s.endsWith('_'))   s.chop(1);
        return s;
    };
    if (!p) return QStringLiteral("project");

    // OLS-origin projects carry captured per-segment metadata; non-empty
    // olsSegments == "imported from .ols".
    const bool fromOls = !p->olsSegments.isEmpty();

    if (fromOls) {
        QStringList parts;
        parts << QStringLiteral("OLS");
        if (!p->brand.isEmpty())   parts << sanitize(p->brand);
        if (!p->model.isEmpty())   parts << sanitize(p->model);
        if (!p->ecuType.isEmpty()) parts << sanitize(p->ecuType);
        // Drop empties from sanitize (e.g. brand was just whitespace).
        parts.removeAll(QString());
        if (parts.size() == 1) {
            // Brand/model/ecu all empty — fall back to displayName so user
            // gets *something* recognisable, still prefixed with OLS.
            const QString fallback = sanitize(p->displayName());
            if (!fallback.isEmpty()) parts << fallback;
        }
        return parts.join(QLatin1Char('_'));
    }
    return sanitize(p->displayName());
}

static QIcon makeIcon(const QString &sym, const QColor &color, int ptSize = -1)
{
    const int S = s_iconSize;
    const qreal dpr = qApp->devicePixelRatio();
    const int pxS = qRound(S * dpr);

    QPixmap pm(pxS, pxS);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setRenderHint(QPainter::Antialiasing);

    // Detect CJK: if any character is > U+2E80, it's CJK
    bool hasCJK = false;
    for (const QChar &ch : sym)
        if (ch.unicode() > 0x2E80) { hasCJK = true; break; }

    // CJK gets bigger font (80% of S), Latin gets smaller (60% of S)
    int pxFont = (ptSize > 0) ? ptSize : (hasCJK ? (S * 8 / 10) : (S * 6 / 10));
    QFont f("Segoe UI", 10, QFont::Bold);
    f.setStyleHint(QFont::SansSerif);
    f.setPixelSize(pxFont);
    QFontMetrics fm(f);
    while (fm.horizontalAdvance(sym) > S - 2 && pxFont > 8) {
        pxFont--;
        f.setPixelSize(pxFont);
        fm = QFontMetrics(f);
    }

    p.setFont(f);
    p.setPen(color);
    p.drawText(QRectF(0, 0, S, S), Qt::AlignCenter, sym);
    return QIcon(pm);
}

// Arrow icon for Prev / Next map (triangle + "M" label)
static QIcon makeNavIcon(bool prevDir, const QColor &col)
{
    const int S = s_iconSize;
    const qreal dpr = qApp->devicePixelRatio();
    const int pxS = qRound(S * dpr);
    QPixmap pm(pxS, pxS);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(col);
    p.setPen(Qt::NoPen);

    QPolygon tri;
    if (prevDir) tri << QPoint(15,3) << QPoint(6,11) << QPoint(15,19);
    else         tri << QPoint(7,3)  << QPoint(16,11) << QPoint(7,19);
    p.drawPolygon(tri);

    p.setPen(col.lighter(170));
    QFont f("Segoe UI", 5, QFont::Bold);
    p.setFont(f);
    QRect textR = prevDir ? QRect(15,8,6,7) : QRect(1,8,6,7);
    p.drawText(textR, Qt::AlignCenter, "M");
    return QIcon(pm);
}

// Gradient-filled rectangle icon for Height Colors
static QIcon makeGradientIcon()
{
    const int S = s_iconSize;
    const qreal dpr = qApp->devicePixelRatio();
    const int pxS = qRound(S * dpr);
    QPixmap pm(pxS, pxS);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    QLinearGradient g(2, 8, 20, 8);
    g.setColorAt(0.0,  QColor(0x00,0x40,0xff));
    g.setColorAt(0.33, QColor(0x00,0xcc,0x00));
    g.setColorAt(0.66, QColor(0xff,0xaa,0x00));
    g.setColorAt(1.0,  QColor(0xff,0x22,0x00));
    p.setBrush(g);
    p.setPen(QPen(QColor("" + AppConfig::instance().colors.uiBorder.name() + ""), 1));
    p.drawRoundedRect(2, 7, 18, 8, 2, 2);
    return QIcon(pm);
}

static QString fmtSize(qint64 b)
{
    if (b < 1024)    return QString::number(b) + " B";
    if (b < 1048576) return QString::number(b / 1024.0, 'f', 1) + " KB";
    return QString::number(b / 1048576.0, 'f', 1) + " MB";
}

// ══════════════════════════════════════════════════════════════════════════════



MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setAcceptDrops(true);

    m_parser  = new A2LParser(this);

    // ── Central: left panel | MDI ─────────────────────────────────────
    m_mainSplitter = new QSplitter(Qt::Horizontal);
    m_mainSplitter->setHandleWidth(3);

    buildLeftPanel();
    m_mainSplitter->addWidget(m_leftPanel);

    m_mdi = new QMdiArea();
    m_mdi->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_mdi->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_mdi->setBackground(QBrush(QColor(0x08, 0x0b, 0x10)));
    m_mainSplitter->addWidget(m_mdi);

    // Sprint L Iter 1 — initialize embedded Lua engine.
    // Safe to call here: MDI exists, MainWindow itself is `this`.
    lua::LuaEngine::instance().initialize(this);

    // Lua scripts run on a worker thread; keep destructive project UI gated
    // while one is active so user actions do not compete with scripted edits.
    connect(&lua::LuaEngine::instance(),
            &lua::LuaEngine::scriptRunningChanged,
            this, [this](bool running) {
        if (m_actClose) m_actClose->setEnabled(!running);
        if (running) {
            statusBar()->showMessage(
                tr("Lua script running..."),
                0);
        } else {
            statusBar()->clearMessage();
        }
    });

    // ── AI Assistant right panel ──────────────────────────────────────
    m_aiAssistant = new AIAssistant(this);
    m_aiAssistant->setMinimumWidth(280);
    m_aiAssistant->setMaximumWidth(600);
    m_aiAssistant->hide();  // hidden by default; toggle via action
    m_mainSplitter->addWidget(m_aiAssistant);

    m_mainSplitter->setStretchFactor(0, 0);
    m_mainSplitter->setStretchFactor(1, 1);
    m_mainSplitter->setStretchFactor(2, 0);
    m_mainSplitter->setSizes({360, 1040, 0});

    connect(m_aiAssistant, &AIAssistant::projectModified, this, [this]() {
        // Don't gate refresh on activeProject(): when the AI panel has focus,
        // QMdiArea::activeSubWindow() returns null and the modified-map filter
        // was silently keeping its stale UserRole+3 values. Refresh the tree
        // unconditionally and nudge every open project view so each one recomputes
        // its per-map "changed" state against originalData.
        for (auto *sw : m_mdi->subWindowList()) {
            if (auto *pv = qobject_cast<ProjectView *>(sw->widget()))
                if (auto *p = pv->project())
                    emit p->dataChanged();
        }
        refreshProjectTree();
    });

    buildActions();
    buildMenuBar();
    buildToolBars();

    // Ensure Preferences is ALWAYS visible in Misc menu — guard on every show
    connect(m_menuMisc, &QMenu::aboutToShow, this, [this]() {
        if (m_actPreferences && !m_menuMisc->actions().contains(m_actPreferences)) {
            m_menuMisc->addSeparator();
            m_menuMisc->addAction(m_actPreferences);
        }
    });

    connect(m_mdi, &QMdiArea::subWindowActivated,
            this,  &MainWindow::onSubWindowActivated);

    connect(m_parser, &A2LParser::progress, this, [this](const QString &msg, int pct) {
        statusBar()->showMessage(
            tr("Parsing A2L…  %1  (%2%)").arg(msg).arg(pct));
    });

    statusBar()->showMessage(tr("Ready  —  Open a ROM file or project to begin."));

    // Persistent map-scan indicator in the status bar. Hidden by default,
    // shown from runMapAutoDetectOnImport() while a scan is in flight.
    // Gives the user visible confirmation that something's happening in
    // the background — previously the scan was silent and invisible.
    m_scanStatusWidget = new QWidget(this);
    {
        auto *lay = new QHBoxLayout(m_scanStatusWidget);
        lay->setContentsMargins(8, 0, 8, 0);
        lay->setSpacing(6);
        m_scanStatusLabel = new QLabel(tr("Scanning ROM for maps…"),
                                       m_scanStatusWidget);
        m_scanStatusLabel->setStyleSheet("color:#d29922;font-weight:bold");
        m_scanStatusBar = new QProgressBar(m_scanStatusWidget);
        m_scanStatusBar->setRange(0, 0);              // indeterminate busy
        m_scanStatusBar->setTextVisible(false);
        m_scanStatusBar->setFixedHeight(12);
        m_scanStatusBar->setFixedWidth(140);
        lay->addWidget(m_scanStatusLabel);
        lay->addWidget(m_scanStatusBar);
        m_scanStatusWidget->hide();
    }
    statusBar()->addPermanentWidget(m_scanStatusWidget);

    // VSCode-style auto-save indicator: "✓ Saved 12s ago" / "● Modified".
    // Refreshes on every save event and via a 1Hz ticker so the elapsed
    // time stays current without the user having to do anything.
    m_saveStatusLabel = new QLabel(this);
    m_saveStatusLabel->setStyleSheet("color:#3fb950;");
    statusBar()->addPermanentWidget(m_saveStatusLabel);
    m_saveStatusTickTimer = new QTimer(this);
    m_saveStatusTickTimer->setInterval(1000);
    connect(m_saveStatusTickTimer, &QTimer::timeout,
            this, &MainWindow::updateAutoSaveStatus);
    m_saveStatusTickTimer->start();

    m_accountWidget = new AccountWidget(this);
    statusBar()->addPermanentWidget(m_accountWidget);

    resize(1440, 880);

    // ── Load persisted config (colors etc.) ───────────────────────────
    AppConfig::instance().load();
    applyUiTheme();
    connect(&AppConfig::instance(), &AppConfig::colorsChanged,
            this, [this]() {
        applyUiTheme();
        // DON'T rebuild the left panel — it causes tree state loss
        // and layout glitches. The global QSS handles backgrounds.
        // Just force repaint on the tree and its children.
        if (m_projectTree) {
            m_projectTree->update();
            m_projectTree->viewport()->update();
        }
        // Rebuild welcome page
        if (m_welcomePage && m_centralStack) {
            const int prevIdx = m_centralStack->currentIndex();
            const int wIdx = m_centralStack->indexOf(m_welcomePage);
            if (wIdx >= 0) m_centralStack->removeWidget(m_welcomePage);
            m_welcomePage->deleteLater();
            m_welcomePage = nullptr;
            buildWelcomePage();
            m_centralStack->insertWidget(0, m_welcomePage);
            m_centralStack->setCurrentIndex(prevIdx);
        }
        // Close and reopen all MDI subwindows so they reconstruct
        // with fresh AppConfig color reads
        QList<QMdiSubWindow*> subs = m_mdi->subWindowList();
        for (auto *sub : subs) {
            if (sub->widget())
                sub->widget()->update();
            for (auto *child : sub->widget()->findChildren<QWidget*>())
                child->update();
        }
    });

    // ── Update checker ─────────────────────────────────────────────────
    m_updateBar = new QFrame();
    m_updateBar->setStyleSheet(
        "QFrame{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 rgba(31,111,235,.15), stop:0.5 rgba(124,58,237,.12), stop:1 rgba(31,111,235,.15));"
        "border-bottom:1px solid rgba(31,111,235,.4);padding:0}");
    m_updateBar->hide();
    auto *ubLay = new QHBoxLayout(m_updateBar);
    ubLay->setContentsMargins(12,6,12,6);
    ubLay->setSpacing(10);
    // Animated pulse dot
    auto *dotLabel = new QLabel("\u2B24");
    dotLabel->setStyleSheet("color:#22c55e;font-size:7pt");
    ubLay->addWidget(dotLabel);
    // Pulsing animation for dot
    auto *dotAnim = new QPropertyAnimation(dotLabel, "maximumWidth");
    Q_UNUSED(dotAnim); // just for the pulse effect via timer
    auto *pulseTimer = new QTimer(dotLabel);
    connect(pulseTimer, &QTimer::timeout, dotLabel, [dotLabel]() {
        static bool bright = false;
        dotLabel->setStyleSheet(bright ? "color:#22c55e;font-size:7pt" : "color:#4ade80;font-size:7pt");
        bright = !bright;
    });
    pulseTimer->start(800);

    m_updateLabel = new QLabel();
    m_updateLabel->setStyleSheet("color:" + AppConfig::instance().colors.uiText.name() + ";font-size:9pt;font-weight:500");
    ubLay->addWidget(m_updateLabel, 1);
    m_updateProgress = new QProgressBar();
    m_updateProgress->setFixedWidth(160);
    m_updateProgress->setFixedHeight(6);
    m_updateProgress->setRange(0, 100);
    m_updateProgress->setTextVisible(false);
    m_updateProgress->setStyleSheet(
        "QProgressBar{background:rgba(255,255,255,.08);border:none;border-radius:3px}"
        "QProgressBar::chunk{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 " + AppConfig::instance().colors.uiAccent.name() + ",stop:1 #7c3aed);border-radius:3px}");
    m_updateProgress->hide();
    ubLay->addWidget(m_updateProgress);
    m_updateBtn = new QPushButton(tr("Update Now"));
    auto *ubBtn = m_updateBtn;
    ubBtn->setCursor(Qt::PointingHandCursor);
    ubBtn->setStyleSheet(
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 " + AppConfig::instance().colors.uiAccent.name() + ",stop:1 #7c3aed);color:white;border:none;border-radius:4px;"
        "padding:5px 20px;font-weight:bold;font-size:9pt}"
        "QPushButton:hover{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 " + AppConfig::instance().colors.uiAccent.lighter(120).name() + ",stop:1 #9b5de5)}");
    ubLay->addWidget(ubBtn);
    auto *ubClose = new QPushButton("\u2715");
    ubClose->setFixedSize(28,28);
    ubClose->setCursor(Qt::PointingHandCursor);
    ubClose->setStyleSheet("QPushButton{background:none;color:" + AppConfig::instance().colors.uiTextDim.name() + ";border:none;font-size:11pt}"
                           "QPushButton:hover{color:" + AppConfig::instance().colors.uiText.name() + "}");
    ubLay->addWidget(ubClose);
    // Insert update bar above the central widget
    auto *centralWrap = new QWidget();
    auto *cwLay = new QVBoxLayout(centralWrap);
    cwLay->setContentsMargins(0,0,0,0);
    cwLay->setSpacing(0);
    cwLay->addWidget(m_updateBar);

    // Stack: [0] welcome page, [1] MDI workspace with sidebars
    m_centralStack = new QStackedWidget();
    buildWelcomePage();
    m_centralStack->addWidget(m_welcomePage);  // page 0
    m_centralStack->addWidget(m_mainSplitter); // page 1
    cwLay->addWidget(m_centralStack, 1);
    setCentralWidget(centralWrap);
    updateCentralPage();

    // Rebuild the welcome page whenever login state flips so the account
    // ribbon appears/disappears live without requiring a restart.
    auto rebuildWelcome = [this]() {
        if (!m_centralStack || !m_welcomePage) return;
        const int prevIdx = m_centralStack->currentIndex();
        const int wIdx = m_centralStack->indexOf(m_welcomePage);
        if (wIdx >= 0) m_centralStack->removeWidget(m_welcomePage);
        m_welcomePage->deleteLater();
        m_welcomePage = nullptr;
        buildWelcomePage();
        m_centralStack->insertWidget(0, m_welcomePage);
        m_centralStack->setCurrentIndex(prevIdx);
    };
    connect(&ApiClient::instance(), &ApiClient::loginStateChanged,
            this, rebuildWelcome);
    // Also rebuild when the ProjectRegistry changes (new save, open, rename,
    // unregister) so the "Recent Projects" tiles stay fresh without restart.
    connect(&ProjectRegistry::instance(), &ProjectRegistry::changed,
            this, rebuildWelcome);

    connect(ubClose, &QPushButton::clicked, m_updateBar, &QFrame::hide);

    m_updateChecker = new UpdateChecker(this);
    connect(m_updateChecker, &UpdateChecker::updateAvailable,
            this, [this](const QString &ver, const QString &changelog, const QString &url) {
        m_updateUrl = url;
        m_updateVersion = ver;
        m_updateChangelog = changelog;
        m_updateLabel->setText(tr("Update available: <b>v%1</b> — %2").arg(ver, changelog));
        m_updateBar->show();
    });
    connect(m_updateChecker, &UpdateChecker::noUpdateAvailable,
            this, [this]() {
        if (!m_updateChecker->property("silent").toBool()) {
            auto *dlg = new QDialog(this);
            dlg->setWindowTitle(tr("Updates"));
            dlg->setFixedSize(380, 200);
            dlg->setStyleSheet("QDialog{background:" + AppConfig::instance().colors.uiBg.name() + ";border:1px solid " + AppConfig::instance().colors.buttonBg.name() + "}"
                "QLabel{color:" + AppConfig::instance().colors.uiText.name() + "}QPushButton{background:" + AppConfig::instance().colors.uiAccent.name() + ";color:white;"
                "border:none;border-radius:6px;padding:8px 24px;font-weight:bold}"
                "QPushButton:hover{background:" + AppConfig::instance().colors.uiAccent.lighter(120).name() + "}");
            auto *lay = new QVBoxLayout(dlg);
            lay->setAlignment(Qt::AlignCenter);
            lay->setSpacing(12);
            auto *icon = new QLabel("\u2705");
            icon->setStyleSheet("font-size:36pt");
            icon->setAlignment(Qt::AlignCenter);
            lay->addWidget(icon);
            auto *msg = new QLabel(tr("You are running the latest version (v%1).")
                .arg(UpdateChecker::currentVersion()));
            msg->setStyleSheet("font-size:11pt;color:" + AppConfig::instance().colors.uiTextDim.name() + "");
            msg->setAlignment(Qt::AlignCenter);
            msg->setWordWrap(true);
            lay->addWidget(msg);
            auto *ok = new QPushButton(tr("OK"));
            ok->setFixedWidth(100);
            connect(ok, &QPushButton::clicked, dlg, &QDialog::accept);
            lay->addWidget(ok, 0, Qt::AlignCenter);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->exec();
        }
    });
    connect(m_updateChecker, &UpdateChecker::checkFailed,
            this, [this](const QString &err) {
        if (!m_updateChecker->property("silent").toBool()) {
            auto *dlg = new QDialog(this);
            dlg->setWindowTitle(tr("Update Check Failed"));
            dlg->setFixedSize(400, 200);
            dlg->setStyleSheet("QDialog{background:" + AppConfig::instance().colors.uiBg.name() + ";border:1px solid " + AppConfig::instance().colors.buttonBg.name() + "}"
                "QLabel{color:" + AppConfig::instance().colors.uiText.name() + "}QPushButton{background:#da3633;color:white;"
                "border:none;border-radius:6px;padding:8px 24px;font-weight:bold}"
                "QPushButton:hover{background:#f85149}");
            auto *lay = new QVBoxLayout(dlg);
            lay->setAlignment(Qt::AlignCenter);
            lay->setSpacing(12);
            auto *icon = new QLabel("\u26A0");
            icon->setStyleSheet("font-size:36pt");
            icon->setAlignment(Qt::AlignCenter);
            lay->addWidget(icon);
            auto *msg = new QLabel(err);
            msg->setStyleSheet("font-size:10pt;color:#f59e0b");
            msg->setAlignment(Qt::AlignCenter);
            msg->setWordWrap(true);
            lay->addWidget(msg);
            auto *ok = new QPushButton(tr("OK"));
            ok->setFixedWidth(100);
            connect(ok, &QPushButton::clicked, dlg, &QDialog::accept);
            lay->addWidget(ok, 0, Qt::AlignCenter);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->exec();
        }
    });
    connect(ubBtn, &QPushButton::clicked, this, [this]() {
        if (m_updateUrl.isEmpty()) return;
#ifdef Q_OS_WIN
        // Windows: download installer to temp and launch it
        m_updateLabel->setText(tr("Downloading update…"));
        m_updateProgress->setValue(0);
        m_updateProgress->show();
        auto *nam = new QNetworkAccessManager(this);
        QUrl dlUrl(m_updateUrl);
        QNetworkRequest req{dlUrl};
        req.setTransferTimeout(60000);
        auto *reply = nam->get(req);
        connect(reply, &QNetworkReply::downloadProgress, this,
                [this](qint64 received, qint64 total) {
            if (total > 0)
                m_updateProgress->setValue((int)(received * 100 / total));
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, nam]() {
            reply->deleteLater();
            nam->deleteLater();
            // GFW-friendly failure UX. The Romhex update CDN is blocked
            // for some users in mainland China; rather than leave a cryptic
            // status-bar message, surface a popup that explains the issue
            // and offers a one-click fallback to the browser (which can
            // route via the user's VPN / proxy / mirror more easily than
            // the in-app QNetworkAccessManager).
            auto showDlFailureDialog = [this](const QString &reason) {
                QMessageBox box(this);
                box.setIcon(QMessageBox::Warning);
                box.setWindowTitle(tr("Update download failed"));
                box.setTextFormat(Qt::RichText);
                box.setText(QString("<b>%1</b>").arg(
                    tr("The installer couldn't be downloaded.")));
                box.setInformativeText(
                    tr("Reason: %1\n\n"
                       "If you are in mainland China, the GFW (\u9632\u706b\u957f\u57ce) "
                       "may be blocking the update server. Try a VPN "
                       "(\u68af\u5b50) and retry, or click \"Open in browser\" "
                       "below to download the installer directly through "
                       "your browser (which can use your system proxy).")
                        .arg(reason));
                auto *openBtn = box.addButton(tr("Open in browser"),
                                              QMessageBox::AcceptRole);
                box.addButton(QMessageBox::Close);
                box.exec();
                if (box.clickedButton() == openBtn && !m_updateUrl.isEmpty())
                    QDesktopServices::openUrl(QUrl(m_updateUrl));
            };
            if (reply->error() != QNetworkReply::NoError) {
                m_updateLabel->setText(tr("Download failed: %1").arg(reply->errorString()));
                showDlFailureDialog(reply->errorString());
                return;
            }
            QByteArray data = reply->readAll();
            if (data.size() < 1024) { // too small to be a real installer
                const QString msg = tr("Server returned only %1 bytes — likely "
                                       "blocked or rate-limited.").arg(data.size());
                m_updateLabel->setText(tr("Download failed: file too small (%1 bytes)").arg(data.size()));
                showDlFailureDialog(msg);
                return;
            }
            QString tmpDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/rx14-update";
            QDir().mkpath(tmpDir);
            QString tmpPath = tmpDir + "/RX14-Setup-" + QString::number(QRandomGenerator::global()->generate(), 16) + ".exe";
            QFile f(tmpPath);
            if (!f.open(QIODevice::WriteOnly)) {
                m_updateLabel->setText(tr("Failed to write installer to disk."));
                return;
            }
            f.write(data);
            f.close();
            m_updateLabel->setText(tr("Launching installer…"));
            if (!QProcess::startDetached(tmpPath, {"/SILENT", "/UPDATE"})) {
                m_updateLabel->setText(tr("Failed to launch installer."));
                return;
            }
            qApp->quit();
        });
#else
        // macOS/Linux: open browser to download page
        QDesktopServices::openUrl(QUrl(m_updateUrl));
#endif
    });
    // Check on startup (silent)
    QTimer::singleShot(3000, m_updateChecker, [this]() {
        m_updateChecker->setProperty("silent", true);
        m_updateChecker->checkForUpdates(true);
    });

    // ── Refresh entitlements on startup ─────────────────────────────────
    QTimer::singleShot(2000, this, []() {
        if (ApiClient::instance().isLoggedIn())
            ApiClient::instance().refreshEntitlements();
    });

    // ── Load saved language (retranslates UI if not English) ──────────
    QString lang = rx14::appSettings()
                   .value("language",
                          QLocale::system().name().section('_', 0, 0))
                   .toString();
    // Migrate old "zh" code to "zh_CN" (filename is rx14_zh_CN.qm)
    if (lang == "zh") lang = "zh_CN";
    loadLanguage(lang);

    // No intro wizard — the welcome empty-state in the central stack is shown
    // when no project is open, so the app opens straight to a usable surface.

    // ── Auto-save (debounced ~1.5s after last change) ────────────────────
    m_autoSaveTimer = new QTimer(this);
    m_autoSaveTimer->setSingleShot(true);
    m_autoSaveTimer->setInterval(5000); // 5s debounce — long enough for
                                         // mid-edit pauses, short enough
                                         // to feel "always saved".
    connect(m_autoSaveTimer, &QTimer::timeout, this, &MainWindow::autoSaveAll);

    // Auto-save mode: onWindowDeactivate — fire when romHEX14 loses focus.
    connect(qApp, &QApplication::applicationStateChanged,
            this, [this](Qt::ApplicationState s) {
        if (s == Qt::ApplicationInactive
            && rx14::appSettings().value("autoSaveMode", "afterDelay")
                   .toString() == "onWindowDeactivate") {
            autoSaveAll();
        }
    });

    // ── Debounce project-tree refreshes (150 ms) ─────────────────────────
    m_treeRefreshTimer = new QTimer(this);
    m_treeRefreshTimer->setSingleShot(true);
    m_treeRefreshTimer->setInterval(150);
    connect(m_treeRefreshTimer, &QTimer::timeout,
            this, [this](){ this->refreshProjectTreeNow(); });

    // ── Stale-sidecar cleanup (one-time migration) ───────────────────────
    // The old autosave system wrote to <project>.rx14proj.autosave files.
    // VSCode-style auto-save (Option A) writes the real .rx14proj
    // atomically, so any leftover .autosave from previous versions is
    // junk — remove it on startup so the user isn't haunted by it.
    QTimer::singleShot(1500, this, [this]() {
        const QString autoSaveDir = ProjectRegistry::defaultProjectDir() + "/autosave";
        QDir dir(autoSaveDir);
        for (const QString &f : dir.entryList({"*.autosave"}, QDir::Files))
            QFile::remove(autoSaveDir + "/" + f);
        for (const auto &entry : ProjectRegistry::instance().entries())
            QFile::remove(entry.path + ".autosave");
    });

    // ── Differences panel (right-side QDockWidget) ─────────────────────────
    // Compare two open projects byte-by-byte, optionally copy diffs into a
    // third "target" project.  Toggled by m_actToggleDiff (View menu, Ctrl+D).
    m_diffPanel = new DiffPanel(this);
    m_diffDock = new QDockWidget(tr("Differences"), this);
    m_diffDock->setObjectName("diffDock");
    m_diffDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_diffDock->setFeatures(QDockWidget::DockWidgetMovable
                            | QDockWidget::DockWidgetClosable
                            | QDockWidget::DockWidgetFloatable);
    m_diffDock->setWidget(m_diffPanel);
    m_diffDock->setMinimumWidth(320);
    m_diffDock->hide();
    addDockWidget(Qt::RightDockWidgetArea, m_diffDock);

    if (m_actToggleDiff) {
        connect(m_actToggleDiff, &QAction::toggled,
                m_diffDock, &QDockWidget::setVisible);
        connect(m_diffDock, &QDockWidget::visibilityChanged,
                m_actToggleDiff, &QAction::setChecked);
        // First time it's opened, push the current project list so something
        // is visible immediately.
        connect(m_diffDock, &QDockWidget::visibilityChanged, this,
                [this](bool on) {
            if (!on) return;
            m_diffPanel->setActiveProject(activeProject());
            m_diffPanel->setProjects(m_projects);
            m_diffPanel->setDisplayParams(m_dataSize, m_byteOrder,
                                          m_displayFmt, m_signed);
        });
    }
    connect(m_diffPanel, &DiffPanel::rowActivated,
            this, &MainWindow::onDiffRowActivated);
    connect(m_diffPanel, &DiffPanel::alignmentChanged,
            this, &MainWindow::onDiffAlignmentChanged);
    connect(m_diffPanel, &DiffPanel::copyApplied, this,
            [this](Project *target, int n) {
        statusBar()->showMessage(
            tr("Copied %1 word(s) into %2")
                .arg(n)
                .arg(target ? target->name : QString()),
            4000);
    });

    // ── Tuning Branches dock ──────────────────────────────────────────────
    // Per-project named savepoints (Ctrl+B).  Manager is owned by
    // MainWindow but rebound to the active project on every focus change
    // so the panel always reflects the project the user is editing.
    m_savepoints      = new SavepointManager(this);
    m_savepointsPanel = new SavepointsPanel(this);
    m_savepointsPanel->setManager(m_savepoints);

    m_savepointsDock = new QDockWidget(tr("Tuning Branches"), this);
    m_savepointsDock->setObjectName("savepointsDock");
    m_savepointsDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_savepointsDock->setFeatures(QDockWidget::DockWidgetMovable
                                  | QDockWidget::DockWidgetClosable
                                  | QDockWidget::DockWidgetFloatable);
    m_savepointsDock->setWidget(m_savepointsPanel);
    m_savepointsDock->setMinimumWidth(300);
    m_savepointsDock->hide();
    addDockWidget(Qt::RightDockWidgetArea, m_savepointsDock);

    // ── Control-module panel (BCM) — hidden until a module dump is detected,
    // then auto-revealed and pinned on the right. Read-only in this build. ──
    m_modulePanel = new ModulePanel(this);
    m_moduleDock  = new QDockWidget(tr("Module"), this);
    m_moduleDock->setObjectName("moduleDock");
    m_moduleDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_moduleDock->setFeatures(QDockWidget::DockWidgetMovable
                              | QDockWidget::DockWidgetClosable
                              | QDockWidget::DockWidgetFloatable);
    m_moduleDock->setWidget(m_modulePanel);
    m_moduleDock->setMinimumWidth(360);
    m_moduleDock->hide();
    addDockWidget(Qt::RightDockWidgetArea, m_moduleDock);
    refreshModulePanel();   // initial (no-op until a module dump is active)

    if (m_actToggleSavepoints) {
        connect(m_actToggleSavepoints, &QAction::toggled,
                m_savepointsDock, &QDockWidget::setVisible);
        connect(m_savepointsDock, &QDockWidget::visibilityChanged,
                m_actToggleSavepoints, &QAction::setChecked);
    }

#ifdef RX14_DEBUG_RPC
    // ── Debug RPC server ───────────────────────────────────────────────────
    // localhost-only TCP JSON-line interface for inspecting & driving the UI
    // from external dev tools.  See src/debug/DebugRpc.h.
    m_debugRpc = new DebugRpc(this, this);
    m_debugRpc->start(48714);
#endif
}

// ── Language loading ──────────────────────────────────────────────────────────

void MainWindow::loadLanguage(const QString &lang)
{
    qApp->removeTranslator(&m_qtTr);
    qApp->removeTranslator(&m_appTr);

    m_qtTr.load("qt_" + lang,
                QLibraryInfo::path(QLibraryInfo::TranslationsPath));
    // Try resource first, then filesystem next to exe, then source dir
    bool appOk = m_appTr.load(":/i18n/rx14_" + lang + ".qm");
    if (!appOk) {
        QString exeDir = QCoreApplication::applicationDirPath();
        appOk = m_appTr.load(exeDir + "/rx14_" + lang + ".qm");
    }
    if (!appOk) {
        appOk = m_appTr.load(QCoreApplication::applicationDirPath()
                             + "/../../translations/rx14_" + lang + ".qm");
    }
    qWarning() << "loadLanguage:" << lang << "loaded:" << appOk;

    qApp->installTranslator(&m_qtTr);
    qApp->installTranslator(&m_appTr);

    rx14::appSettings().setValue("language", lang);

    retranslateUi();

    // Also retranslate any open ProjectViews
    for (auto *sw : m_mdi->subWindowList()) {
        if (auto *pv = qobject_cast<ProjectView *>(sw->widget()))
            pv->retranslateUi();
    }
    for (auto &ov : std::as_const(m_overlays))
        if (ov) ov->retranslateUi();
    if (m_accountWidget)
        m_accountWidget->retranslateUi();
    if (m_aiAssistant)
        m_aiAssistant->retranslateUi();

    // Re-parse + rebuild the module (BCM) panel so its data labels and its own
    // strings pick up the new language (both are tr()'d at parse/build time).
    refreshModulePanel();

    // ── Set font per language ────────────────────────────────────────
    QFont baseFont = qApp->font();
    if (lang == "zh_CN" || lang == "zh") {
        // High-quality CJK fonts per platform
#ifdef Q_OS_WIN
        QFont zhFont("Microsoft YaHei UI", baseFont.pointSize());
#elif defined(Q_OS_MACOS)
        QFont zhFont("PingFang SC", baseFont.pointSize());
#else
        QFont zhFont("Noto Sans CJK SC", baseFont.pointSize());
#endif
        zhFont.setStyleStrategy(QFont::PreferAntialias);
        qApp->setFont(zhFont);
    } else if (lang == "th") {
#ifdef Q_OS_WIN
        QFont thFont("Leelawadee UI", baseFont.pointSize());
#elif defined(Q_OS_MACOS)
        QFont thFont("Thonburi", baseFont.pointSize());
#else
        QFont thFont("Noto Sans Thai", baseFont.pointSize());
#endif
        thFont.setStyleStrategy(QFont::PreferAntialias);
        qApp->setFont(thFont);
    } else {
        QFont defaultFont("Segoe UI", baseFont.pointSize());
        defaultFont.setStyleStrategy(QFont::PreferAntialias);
        qApp->setFont(defaultFont);
    }
}

// ── Left panel ────────────────────────────────────────────────────────────────

void MainWindow::buildLeftPanel()
{
    m_leftPanel = new QWidget();
    m_leftPanel->setMinimumWidth(260);
    m_leftPanel->setMaximumWidth(1100);
    auto *lay = new QVBoxLayout(m_leftPanel);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // Title bar
    auto *titleBar = new QWidget();
    titleBar->setStyleSheet("background:" + AppConfig::instance().colors.uiPanel.name() + "; border-bottom:1px solid " + AppConfig::instance().colors.uiBorder.name() + ";");
    auto *titleLay = new QHBoxLayout(titleBar);
    titleLay->setContentsMargins(8, 5, 8, 5);
    m_leftPanelTitle = new QLabel(tr("Map Selection"));
    m_leftPanelTitle->setStyleSheet(
        "color:" + AppConfig::instance().colors.uiTextDim.name() + "; font-size:8pt; font-weight:bold;"
        "letter-spacing:0.5px; text-transform:uppercase; background:transparent;");
    titleLay->addWidget(m_leftPanelTitle);
    titleLay->addStretch();

    // Font size control — –  9pt  +
    const QString fsBtnSS =
        "QPushButton { background:" + AppConfig::instance().colors.buttonBg.name() + "; color:" + AppConfig::instance().colors.uiTextDim.name() + "; border:1px solid " + AppConfig::instance().colors.uiBorder.name() + ";"
        " border-radius:3px; font-size:11pt; font-weight:bold; padding:0; }"
        "QPushButton:hover  { background:" + AppConfig::instance().colors.uiBorder.name() + "; color:" + AppConfig::instance().colors.uiText.name() + "; }"
        "QPushButton:pressed{ background:" + AppConfig::instance().colors.uiAccent.name() + "; color:#fff; border-color:" + AppConfig::instance().colors.uiAccent.name() + "; }";

    auto *btnFsMinus = new QPushButton("−");
    btnFsMinus->setFixedSize(28, 28);
    btnFsMinus->setToolTip(tr("Decrease list font size"));
    btnFsMinus->setStyleSheet(fsBtnSS);

    m_treeFontLabel = new QLabel(QString("%1").arg(m_treeFontSize));
    m_treeFontLabel->setFixedWidth(28);
    m_treeFontLabel->setAlignment(Qt::AlignCenter);
    m_treeFontLabel->setStyleSheet("color:" + AppConfig::instance().colors.uiTextDim.name() + "; font-size:7.5pt; background:transparent;");

    auto *btnFsPlus = new QPushButton("+");
    btnFsPlus->setFixedSize(28, 28);
    btnFsPlus->setToolTip(tr("Increase list font size"));
    btnFsPlus->setStyleSheet(fsBtnSS);

    titleLay->addWidget(btnFsMinus);
    titleLay->addWidget(m_treeFontLabel);
    titleLay->addWidget(btnFsPlus);

    connect(btnFsPlus, &QPushButton::clicked, this, [this]() {
        if (m_treeFontSize >= 16) return;
        ++m_treeFontSize;
        m_treeFontLabel->setText(QString("%1").arg(m_treeFontSize));
        refreshProjectTree();
    });
    connect(btnFsMinus, &QPushButton::clicked, this, [this]() {
        if (m_treeFontSize <= 7) return;
        --m_treeFontSize;
        m_treeFontLabel->setText(QString("%1").arg(m_treeFontSize));
        refreshProjectTree();
    });

    // Hidden language memory — not shown in UI, just remembers last pick
    m_langCombo = new QComboBox();
    m_langCombo->addItem("English", QStringLiteral("en"));
    m_langCombo->addItem("中文",     QStringLiteral("zh_CN"));
    m_langCombo->hide(); // never shown; dialog creates its own visible combo

    // AI Translate All button — lives in the title bar, right-aligned
    m_btnTranslateAll = new QPushButton(tr("✦ AI Translate"));
    m_btnTranslateAll->setFixedHeight(20);
    m_btnTranslateAll->setStyleSheet(
        "QPushButton { background:#1a2a45; color:" + AppConfig::instance().colors.uiAccent.lighter(140).name() + "; border:1px solid " + AppConfig::instance().colors.uiAccent.name() + ";"
        " border-radius:3px; font-size:7pt; font-weight:bold; padding:0 7px; }"
        "QPushButton:hover { background:" + AppConfig::instance().colors.uiAccent.name() + "; color:#fff; }"
        "QPushButton:disabled { background:transparent; color:" + AppConfig::instance().colors.uiBorder.name() + "; border-color:" + AppConfig::instance().colors.buttonBg.name() + "; }");
    m_btnTranslateAll->setToolTip(tr("Sign in to use AI map translation"));
    m_btnTranslateAll->hide();
    titleLay->addWidget(m_btnTranslateAll);

    // Update button state on login change
    auto updateTxBtn = [this]() {
        const auto &api = ApiClient::instance();
        const bool loggedIn  = api.isLoggedIn();
        const bool hasModule = api.hasModule(QStringLiteral("translation"));
        m_btnTranslateAll->setVisible(loggedIn);
        m_btnTranslateAll->setEnabled(hasModule);
        if (loggedIn && !hasModule)
            m_btnTranslateAll->setToolTip(tr("AI Translation module not active"));
        else if (loggedIn)
            m_btnTranslateAll->setToolTip(tr("Translate all map names using AI"));
    };
    updateTxBtn();
    connect(&ApiClient::instance(), &ApiClient::loginStateChanged, this, updateTxBtn);

    // ── Translate All click handler ────────────────────────────────────
    connect(m_btnTranslateAll, &QPushButton::clicked, this, [this]() {
        auto *proj = activeProject();
        if (!proj || proj->maps.isEmpty()) return;

        // Collect all maps
        QVector<QPair<QString,QString>> items;
        items.reserve(proj->maps.size());
        for (const auto &m : proj->maps)
            items.append({m.name, m.description});

        // ── Pre-flight dialog ──────────────────────────────────────────────
        QDialog ask(this);
        ask.setWindowTitle(tr("AI Translation"));
        ask.setMinimumWidth(340);
        auto *askLay = new QVBoxLayout(&ask);
        askLay->setSpacing(12);

        auto *askLabel = new QLabel(
            tr("<b>Translate %1 maps</b>").arg(items.size()), &ask);
        auto *askSub = new QLabel(
            tr("Run a sample of 25 maps first to verify quality, or translate everything now."), &ask);
        askSub->setWordWrap(true);
        askSub->setStyleSheet("color:" + AppConfig::instance().colors.uiTextDim.name() + ";");

        // Language row
        auto *langRow = new QHBoxLayout();
        auto *langLabel = new QLabel(tr("Language:"), &ask);
        auto *langCombo = new QComboBox(&ask);
        langCombo->addItem("English",  QStringLiteral("en"));
        langCombo->addItem("中文",      QStringLiteral("zh_CN"));
        langCombo->setCurrentIndex(m_langCombo->currentIndex() < langCombo->count()
                                   ? m_langCombo->currentIndex() : 0);
        langRow->addWidget(langLabel);
        langRow->addWidget(langCombo, 1);

        // Buttons row
        auto *btnRow  = new QHBoxLayout();
        auto *btnCancel = new QPushButton(tr("Cancel"), &ask);
        auto *btnSample = new QPushButton(tr("Sample (25)"), &ask);
        auto *btnAll    = new QPushButton(tr("Translate All"), &ask);
        btnAll->setDefault(true);
        btnRow->addWidget(btnCancel);
        btnRow->addStretch();
        btnRow->addWidget(btnSample);
        btnRow->addWidget(btnAll);

        askLay->addWidget(askLabel);
        askLay->addWidget(askSub);
        askLay->addLayout(langRow);
        askLay->addLayout(btnRow);

        bool sampleOnly = false;
        bool accepted   = false;
        connect(btnCancel, &QPushButton::clicked, &ask, &QDialog::reject);
        connect(btnSample, &QPushButton::clicked, &ask, [&]() { sampleOnly = true;  accepted = true; ask.accept(); });
        connect(btnAll,    &QPushButton::clicked, &ask, [&]() { sampleOnly = false; accepted = true; ask.accept(); });
        if (ask.exec() != QDialog::Accepted || !accepted) return;

        const QString lang = langCombo->currentData().toString();
        // Remember choice for next time
        for (int i = 0; i < m_langCombo->count(); ++i)
            if (m_langCombo->itemData(i).toString() == lang) { m_langCombo->setCurrentIndex(i); break; }

        // Keep the full list so we can continue after a sample run
        const QVector<QPair<QString,QString>> allItems = items;
        if (sampleOnly) items = items.mid(0, 25);

        const int total = items.size();

        // Progress dialog — modal but processes events so UI stays alive
        auto *progress = new QProgressDialog(
            tr("Translating %1 maps…").arg(total),
            tr("Cancel"), 0, total, this);
        progress->setWindowTitle(tr("AI Translation"));
        progress->setWindowModality(Qt::WindowModal);
        progress->setMinimumDuration(0);
        progress->setAutoClose(false);
        progress->setAutoReset(false);
        progress->setValue(0);
        progress->setMinimumWidth(360);

        // Shared cancel flag — lets in-flight requests finish but drops results
        auto cancelled = std::make_shared<bool>(false);
        connect(progress, &QProgressDialog::canceled, this, [cancelled]() {
            *cancelled = true;
        });

        m_btnTranslateAll->setEnabled(false);

        ApiClient::instance().translateMapsBatch(
            items, lang,
            // progressCb — called from Qt event loop (network replies)
            [progress, cancelled](int done, int total) {
                if (*cancelled) return;
                progress->setValue(done);
                progress->setLabelText(
                    QObject::tr("Translating maps…  %1 / %2").arg(done).arg(total));
            },
            // doneCb — called once everything is done
            [this, progress, cancelled, proj, sampleOnly, allItems, lang]
            (const QVector<TranslationResult> &results) {
                progress->close();
                progress->deleteLater();

                m_btnTranslateAll->setEnabled(
                    ApiClient::instance().hasModule(QStringLiteral("translation")));

                int applied = 0;
                for (const auto &r : results) {
                    if (!r.translation.isEmpty()) {
                        m_translations[r.name] = r;
                        ++applied;
                    }
                }
                if (applied > 0) {
                    // Save expanded state of all groups before refresh
                    QSet<QString> expandedGroups;
                    std::function<void(QTreeWidgetItem*)> collectExp;
                    collectExp = [&collectExp, &expandedGroups](QTreeWidgetItem *it) {
                        if (it->childCount() > 0 && !(it->flags() & Qt::ItemIsSelectable) && it->isExpanded()) {
                            expandedGroups.insert(it->text(0).section("  (", 0, 0));
                        }
                        for (int i = 0; i < it->childCount(); ++i)
                            collectExp(it->child(i));
                    };
                    for (int i = 0; i < m_projectTree->topLevelItemCount(); ++i)
                        collectExp(m_projectTree->topLevelItem(i));

                    refreshProjectTree();

                    // Restore expanded state
                    std::function<void(QTreeWidgetItem*)> restoreExp;
                    restoreExp = [&restoreExp, &expandedGroups](QTreeWidgetItem *it) {
                        if (it->childCount() > 0 && !(it->flags() & Qt::ItemIsSelectable)) {
                            QString gname = it->text(0).section("  (", 0, 0);
                            if (expandedGroups.contains(gname)) {
                                it->setExpanded(true);
                            }
                        }
                        for (int i = 0; i < it->childCount(); ++i)
                            restoreExp(it->child(i));
                    };
                    for (int i = 0; i < m_projectTree->topLevelItemCount(); ++i)
                        restoreExp(m_projectTree->topLevelItem(i));
                }

                if (*cancelled) return;

                if (sampleOnly) {
                    // Show sample results and ask whether to continue
                    const int remaining = allItems.size() - 25;
                    QMessageBox confirm(this);
                    confirm.setWindowTitle(tr("AI Translation – Sample Complete"));
                    confirm.setText(
                        tr("Sample translated <b>%1 of %2</b> maps successfully.")
                            .arg(applied).arg(results.size()));
                    confirm.setInformativeText(
                        remaining > 0
                            ? tr("Does the quality look good? Click \"Continue\" to translate "
                                 "the remaining %1 maps, or \"Done\" to keep only the sample.")
                                  .arg(remaining)
                            : tr("All maps were already in the sample."));
                    QPushButton *btnContinue = nullptr;
                    if (remaining > 0)
                        btnContinue = confirm.addButton(
                            tr("Continue (%1 maps)").arg(remaining), QMessageBox::AcceptRole);
                    confirm.addButton(
                        remaining > 0 ? tr("Done (keep sample)") : tr("Done"),
                        QMessageBox::RejectRole);
                    confirm.exec();

                    if (!btnContinue || confirm.clickedButton() != btnContinue) {
                        statusBar()->showMessage(
                            tr("Sample complete: %1 of %2 maps translated.")
                                .arg(applied).arg(results.size()), 8000);
                        return;
                    }

                    // ── Continue with the remaining maps ───────────────────
                    auto remaining_items = allItems.mid(25);
                    const int remTotal   = remaining_items.size();

                    auto *prog2 = new QProgressDialog(
                        tr("Translating %1 maps…").arg(remTotal),
                        tr("Cancel"), 0, remTotal, this);
                    prog2->setWindowTitle(tr("AI Translation"));
                    prog2->setWindowModality(Qt::WindowModal);
                    prog2->setMinimumDuration(0);
                    prog2->setAutoClose(false);
                    prog2->setAutoReset(false);
                    prog2->setValue(0);
                    prog2->setMinimumWidth(360);

                    auto cancelled2 = std::make_shared<bool>(false);
                    connect(prog2, &QProgressDialog::canceled, this, [cancelled2]() {
                        *cancelled2 = true;
                    });

                    m_btnTranslateAll->setEnabled(false);

                    ApiClient::instance().translateMapsBatch(
                        remaining_items, lang,
                        [prog2, cancelled2](int done, int total2) {
                            if (*cancelled2) return;
                            prog2->setValue(done);
                            prog2->setLabelText(
                                QObject::tr("Translating maps…  %1 / %2").arg(done).arg(total2));
                        },
                        [this, prog2, cancelled2, remaining_items, lang]
                        (const QVector<TranslationResult> &res2) {
                            prog2->close();
                            prog2->deleteLater();
                            m_btnTranslateAll->setEnabled(
                                ApiClient::instance().hasModule(QStringLiteral("translation")));
                            int applied2 = 0;
                            QVector<QPair<QString,QString>> failed2;
                            for (const auto &r : res2) {
                                if (!r.translation.isEmpty()) {
                                    m_translations[r.name] = r;
                                    ++applied2;
                                } else {
                                    failed2.append({r.name, {}});
                                }
                            }
                            if (applied2 > 0)
                                refreshProjectTree();
                            if (*cancelled2) return;
                            autoRetryFailed(failed2, lang, remaining_items, applied2, res2.size());
                        });
                } else {
                    // Collect failed maps and auto-retry without prompting
                    QVector<QPair<QString,QString>> failed;
                    for (const auto &r : results)
                        if (r.translation.isEmpty())
                            failed.append({r.name, {}});
                    autoRetryFailed(failed, lang, allItems, applied, results.size());
                }
            });
    });

    lay->addWidget(titleBar);

    // ── Search bar ────────────────────────────────────────────────────────
    auto *filterBar = new QWidget();
    filterBar->setStyleSheet("background:" + AppConfig::instance().colors.uiBg.name() + "; border-bottom:1px solid " + AppConfig::instance().colors.buttonBg.name() + ";");
    auto *filterLay = new QHBoxLayout(filterBar);
    filterLay->setContentsMargins(8, 6, 8, 6);
    filterLay->setSpacing(6);
    m_filterEdit = new QLineEdit();
    m_filterEdit->setPlaceholderText(tr("筛选地图…"));
    m_filterEdit->setClearButtonEnabled(true);
    m_filterEdit->setMinimumHeight(26);
    m_filterEdit->setStyleSheet(
        "QLineEdit { background:" + AppConfig::instance().colors.uiPanel.name() + "; color:" + AppConfig::instance().colors.uiText.name() + "; border:1px solid " + AppConfig::instance().colors.uiBorder.name() + ";"
        " border-radius:5px; padding:0 8px; font-size:9pt; }"
        "QLineEdit:focus { border-color:" + AppConfig::instance().colors.uiAccent.name() + "; }");
    filterLay->addWidget(m_filterEdit, 1);

    m_filterChangedBtn = new QPushButton();
    m_filterChangedBtn->setIcon(makeIcon("●", QColor("#7d8590")));
    m_filterChangedBtn->setCheckable(true);
    m_filterChangedBtn->setFixedSize(28, 28);
    m_filterChangedBtn->setIconSize(QSize(16, 16));
    m_filterChangedBtn->setToolTip(tr("Show only modified maps"));
    m_filterChangedBtn->setStyleSheet(
        "QPushButton { background:" + AppConfig::instance().colors.uiPanel.name() + "; border:1px solid " + AppConfig::instance().colors.uiBorder.name() + "; border-radius:5px; }"
        "QPushButton:checked { border-color:#ff7b72; background:#2d1215; }"
        "QPushButton:hover   { border-color:" + AppConfig::instance().colors.uiTextDim.name() + "; }"
        "QPushButton:focus   { border:2px solid " + AppConfig::instance().colors.uiAccent.lighter(140).name() + "; }");
    connect(m_filterChangedBtn, &QPushButton::toggled, this, [this](bool on) {
        m_filterChangedBtn->setIcon(makeIcon("●", on ? QColor("#ff7b72") : QColor("#7d8590")));
    });
    filterLay->addWidget(m_filterChangedBtn);
    lay->addWidget(filterBar);

    // ── Filter chip bar — two rows ────────────────────────────────────────
    auto *chipBar = new QWidget();
    chipBar->setStyleSheet("background:" + AppConfig::instance().colors.uiBg.name() + "; border-bottom:1px solid " + AppConfig::instance().colors.buttonBg.name() + ";");
    auto *chipGrid = new QVBoxLayout(chipBar);
    chipGrid->setContentsMargins(6, 5, 6, 5);
    chipGrid->setSpacing(4);

    const QString chipSS =
        "QPushButton { background:" + AppConfig::instance().colors.uiPanel.name() + "; color:" + AppConfig::instance().colors.uiText.name() + "; border:1px solid " + AppConfig::instance().colors.uiBorder.name() + ";"
        " border-radius:4px; font-size:8.5pt; padding:2px 0; text-align:center; min-width:0; min-height:0; }"
        "QPushButton:checked { background:#1a2a45; color:" + AppConfig::instance().colors.uiAccent.lighter(140).name() + "; border-color:" + AppConfig::instance().colors.uiAccent.name() + "; font-weight:bold; }"
        "QPushButton:hover:!checked { background:#1c2128; color:" + AppConfig::instance().colors.uiText.name() + "; border-color:#7d8590; }"
        "QPushButton:focus { border:2px solid " + AppConfig::instance().colors.uiAccent.lighter(140).name() + "; }";

    auto makeChip = [&](const QString &sym, const QColor &iconCol,
                        const QString &label, QPushButton *&out,
                        const QString &extraSS = {}) {
        out = new QPushButton(label);
        out->setIcon(makeIcon(sym, iconCol, sym.size() > 1 ? 7 : 10));
        out->setIconSize(QSize(16, 16));
        out->setCheckable(true);
        out->setFixedHeight(24);
        out->setStyleSheet(chipSS + extraSS);
    };

    // Row 1: All | Modified | Starred | Recent
    auto *row1 = new QHBoxLayout();
    row1->setSpacing(4);
    makeChip("≡",  QColor("" + AppConfig::instance().colors.uiTextDim.name() + ""), tr("All"),      m_chipAll);
    makeChip("Δ",  QColor("#f0883e"), tr("Modified"),  m_chipModified);
    makeChip("★",  QColor("#d4a017"), tr("Starred"),   m_chipStarred);
    makeChip("◷",  QColor("" + AppConfig::instance().colors.uiAccent.lighter(140).name() + ""), tr("Recent"),    m_chipRecent);
    row1->addWidget(m_chipAll,      1);
    row1->addWidget(m_chipModified, 2);
    row1->addWidget(m_chipStarred,  2);
    row1->addWidget(m_chipRecent,   2);

    // Row 2: Values | Curves | Maps
    auto *row2 = new QHBoxLayout();
    row2->setSpacing(4);
    makeChip("●", QColor("#3fb950"), tr("Values"), m_chipValue,
        "QPushButton:checked{background:#0d2a18;color:#3fb950;border-color:#2ea043;font-weight:bold;}");
    makeChip("∿", QColor("#bc8cff"), tr("Curves"), m_chipCurve,
        "QPushButton:checked{background:#1e1535;color:#bc8cff;border-color:#8957e5;font-weight:bold;}");
    makeChip("▦", QColor("" + AppConfig::instance().colors.uiAccent.lighter(120).name() + ""), tr("Maps"),   m_chipMap,
        "QPushButton:checked{background:#0d1f3c;color:#79c0ff;border-color:" + AppConfig::instance().colors.uiAccent.lighter(120).name() + ";font-weight:bold;}");
    row2->addWidget(m_chipValue, 1);
    row2->addWidget(m_chipCurve, 1);
    row2->addWidget(m_chipMap,   1);

    chipGrid->addLayout(row1);
    chipGrid->addLayout(row2);
    m_chipAll->setChecked(true);
    lay->addWidget(chipBar);

    // Wire chips
    auto onChip = [this](QPushButton *pressed, PanelFilter mode) {
        for (auto *ch : {m_chipAll, m_chipModified, m_chipStarred, m_chipRecent,
                         m_chipValue, m_chipCurve, m_chipMap})
            ch->setChecked(false);
        pressed->setChecked(true);
        m_panelFilter = mode;
        applyTreeFilter();
    };
    connect(m_chipAll,      &QPushButton::clicked, this, [this, onChip]{ onChip(m_chipAll,      PanelFilter::All); });
    connect(m_chipModified, &QPushButton::clicked, this, [this, onChip]{ onChip(m_chipModified, PanelFilter::Modified); });
    connect(m_chipStarred,  &QPushButton::clicked, this, [this, onChip]{ onChip(m_chipStarred,  PanelFilter::Starred); });
    connect(m_chipRecent,   &QPushButton::clicked, this, [this, onChip]{ onChip(m_chipRecent,   PanelFilter::Recent); });
    connect(m_chipValue,    &QPushButton::clicked, this, [this, onChip]{ onChip(m_chipValue,    PanelFilter::TypeValue); });
    connect(m_chipCurve,    &QPushButton::clicked, this, [this, onChip]{ onChip(m_chipCurve,    PanelFilter::TypeCurve); });
    connect(m_chipMap,      &QPushButton::clicked, this, [this, onChip]{ onChip(m_chipMap,      PanelFilter::TypeMap); });

    // ── Tree — configurable table columns, with the hierarchy in Name ─────
    m_projectTree = new QTreeWidget();
    m_projectTree->setColumnCount(5);
    m_projectTree->setHeaderLabels({tr("Name"), tr("Address"), tr("ID"),
                                    tr("Type"), tr("Size")});
    m_projectTree->setHeaderHidden(false);
    // Sprint E — multi-select for bulk edit (Ctrl+click / Shift+click).
    m_projectTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_projectTree->setRootIsDecorated(true);
    m_projectTree->setExpandsOnDoubleClick(false);  // we handle double-click manually
    m_projectTree->setAlternatingRowColors(false);
    m_projectTree->setUniformRowHeights(true);
    m_projectTree->setIndentation(14);
    m_projectTree->setIconSize(QSize(14, 14));
    m_projectTree->setTextElideMode(Qt::ElideRight);
    m_projectTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_projectTree->setAllColumnsShowFocus(true);
    auto *treeHeader = m_projectTree->header();
    treeHeader->setSectionsMovable(true);
    treeHeader->setStretchLastSection(false);
    treeHeader->setSectionResizeMode(QHeaderView::Interactive);
    treeHeader->resizeSection(0, 260);
    treeHeader->resizeSection(1, 96);
    treeHeader->resizeSection(2, 260);
    treeHeader->resizeSection(3, 72);
    treeHeader->resizeSection(4, 72);

    const QString treeHeaderStateKey = QStringLiteral("projectTree/headerStateV3");
    const QByteArray savedHeaderState = rx14::appSettings()
                                            .value(treeHeaderStateKey)
                                            .toByteArray();
    if (!savedHeaderState.isEmpty())
        treeHeader->restoreState(savedHeaderState);

    auto saveTreeHeaderState = [treeHeader, treeHeaderStateKey]() {
        rx14::appSettings().setValue(treeHeaderStateKey, treeHeader->saveState());
    };
    connect(treeHeader, &QHeaderView::sectionResized, this,
            [saveTreeHeaderState](int, int, int) { saveTreeHeaderState(); });
    connect(treeHeader, &QHeaderView::sectionMoved, this,
            [saveTreeHeaderState](int, int, int) { saveTreeHeaderState(); });
    treeHeader->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(treeHeader, &QHeaderView::customContextMenuRequested, this,
            [this, treeHeader, treeHeaderStateKey, saveTreeHeaderState](const QPoint &pos) {
        QMenu menu(treeHeader);
        for (int column = 0; column < m_projectTree->columnCount(); ++column) {
            auto *action = menu.addAction(
                m_projectTree->headerItem()->text(column));
            action->setCheckable(true);
            action->setChecked(!m_projectTree->isColumnHidden(column));
            if (column == 0) {
                action->setEnabled(false); // hierarchy must remain visible
                continue;
            }
            connect(action, &QAction::toggled, &menu,
                    [this, column, treeHeaderStateKey, saveTreeHeaderState](bool visible) {
                m_projectTree->setColumnHidden(column, !visible);
                saveTreeHeaderState();
            });
        }
        menu.exec(treeHeader->mapToGlobal(pos));
    });
    m_projectTree->setStyleSheet(
        "QTreeWidget { background:" + AppConfig::instance().colors.uiBg.name() + "; color:" + AppConfig::instance().colors.uiText.name() + "; border:none; }"
        "QHeaderView::section { background:" + AppConfig::instance().colors.uiPanel.name() + "; color:" + AppConfig::instance().colors.uiTextDim.name() + "; border:0; border-right:1px solid " + AppConfig::instance().colors.uiBorder.name() + "; border-bottom:1px solid " + AppConfig::instance().colors.uiBorder.name() + "; padding:4px 6px; font-weight:600; }"
        "QTreeWidget::item { padding:3px 6px; min-height:24px; }"
        "QTreeWidget::item:selected { background:#1f3a6e; color:#ffffff; }"
        "QTreeWidget::item:hover:!selected { background:" + AppConfig::instance().colors.uiPanel.name() + "; }"
        "QTreeWidget::branch { background:" + AppConfig::instance().colors.uiBg.name() + "; }"
        "QTreeWidget:focus { border:1px solid " + AppConfig::instance().colors.uiAccent.lighter(140).name() + "; }"
        "QScrollBar:vertical { background:" + AppConfig::instance().colors.uiBg.name() + "; width:8px; border:none; }"
        "QScrollBar::handle:vertical { background:#7d8590; border-radius:4px; min-height:24px; }"
        "QScrollBar::groove:vertical { background:" + AppConfig::instance().colors.uiBg.name() + "; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:" + AppConfig::instance().colors.uiBg.name() + "; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"
        "QScrollBar:horizontal { background:" + AppConfig::instance().colors.uiBg.name() + "; height:8px; border:none; }"
        "QScrollBar::handle:horizontal { background:#7d8590; border-radius:4px; min-width:24px; }"
        "QScrollBar::groove:horizontal { background:" + AppConfig::instance().colors.uiBg.name() + "; }"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background:" + AppConfig::instance().colors.uiBg.name() + "; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; }");

    // Folder navigation nodes use the platform's open/closed folder icons.
    // Map leaves retain their colored type icon instead.
    connect(m_projectTree, &QTreeWidget::itemExpanded, this,
            [](QTreeWidgetItem *item) {
        if (item->data(0, kTreeFolderRole).toBool())
            item->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_DirOpenIcon));
    });
    connect(m_projectTree, &QTreeWidget::itemCollapsed, this,
            [](QTreeWidgetItem *item) {
        if (item->data(0, kTreeFolderRole).toBool())
            item->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_DirClosedIcon));
    });

    // ── Recent Maps strip (above the tree) ────────────────────────────────
    m_recentMapsStrip = new QWidget();
    m_recentMapsStrip->setStyleSheet(
        "background:" + AppConfig::instance().colors.uiBg.name() + "; border-bottom:1px solid " + AppConfig::instance().colors.buttonBg.name() + ";");
    auto *recentLay = new QVBoxLayout(m_recentMapsStrip);
    recentLay->setContentsMargins(8, 5, 8, 5);
    recentLay->setSpacing(3);

    m_recentMapsTitle = new QLabel(tr("Recent Maps"));
    m_recentMapsTitle->setStyleSheet(
        "color:" + AppConfig::instance().colors.uiTextDim.name() + "; font-size:9pt; font-weight:600;"
        "letter-spacing:0.5px; background:transparent;");
    recentLay->addWidget(m_recentMapsTitle);

    auto *recentRowWrap = new QWidget(m_recentMapsStrip);
    recentRowWrap->setStyleSheet("background:transparent;");
    m_recentMapsRow = new QHBoxLayout(recentRowWrap);
    m_recentMapsRow->setContentsMargins(0, 0, 0, 0);
    m_recentMapsRow->setSpacing(4);
    recentLay->addWidget(recentRowWrap);

    m_recentMapsEmpty = new QLabel(tr("No recent maps yet"), recentRowWrap);
    m_recentMapsEmpty->setStyleSheet(
        "color:#6e7681; font-size:8pt; font-style:italic; background:transparent;");
    m_recentMapsRow->addWidget(m_recentMapsEmpty);
    m_recentMapsRow->addStretch();

    lay->addWidget(m_recentMapsStrip);

    lay->addWidget(m_projectTree, 1);

    // Initial build of the strip (likely empty on startup)
    refreshRecentMapsStrip();

    // Filter logic — both controls call the same method
    connect(m_filterEdit,       &QLineEdit::textChanged,
            this, &MainWindow::applyTreeFilter);
    connect(m_filterChangedBtn, &QPushButton::toggled,
            this, &MainWindow::applyTreeFilter);

    // Single-click on a map leaf → select its byte range in the hex viewer.
    connect(m_projectTree, &QTreeWidget::itemClicked,
            this,          &MainWindow::onTreeItemClicked);

    // Double-click handling
    connect(m_projectTree, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem *item, int col) {
        Q_UNUSED(col);

        // Project root → collapse/expand only if its window is currently visible
        auto projRootVar = item->data(0, Qt::UserRole);
        auto mapVar      = item->data(0, Qt::UserRole + 2);
        if (projRootVar.isValid() && !mapVar.isValid()) {
            auto *proj = static_cast<Project *>(projRootVar.value<void *>());
            if (!proj) return;
            bool windowOpen = false;
            for (auto *sub : m_mdi->subWindowList()) {
                auto *pv = qobject_cast<ProjectView *>(sub->widget());
                if (pv && pv->project() == proj && sub->isVisible()) {
                    windowOpen = true; break;
                }
            }
            if (windowOpen)
                item->setExpanded(!item->isExpanded());
            return;
        }

        // Map leaf → open a new editable map window. Single-click has already
        // selected the corresponding byte range in the hex viewer.
        if (!mapVar.isValid()) return;
        auto map = mapVar.value<MapInfo>();
        auto *proj = static_cast<Project*>(item->data(0, Qt::UserRole + 1).value<void*>());
        if (!proj) return;
        openProject(proj);
        if (auto *pv = activeView()) pv->showMap(map);
        onMapActivated(map, proj);
        openMapViewer(proj, map);
    });

    // Right-click context menu: expand/collapse groups + map comments
    m_projectTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_projectTree, &QTreeWidget::customContextMenuRequested,
            this, [this](const QPoint &pos) {
        QTreeWidgetItem *item = m_projectTree->itemAt(pos);
        if (item && (item->flags() & Qt::ItemIsSelectable) && !item->isSelected()) {
            m_projectTree->clearSelection();
            item->setSelected(true);
            m_projectTree->setCurrentItem(item);
        }

        QMenu menu(m_projectTree);
        struct SelectedMapEntry { Project *project; MapInfo map; };
        QVector<SelectedMapEntry> selectedMapEntries;
        for (auto *selected : m_projectTree->selectedItems()) {
            const auto mapVar = selected->data(0, Qt::UserRole + 2);
            auto *project = static_cast<Project *>(
                selected->data(0, Qt::UserRole + 1).value<void *>());
            if (mapVar.isValid() && project)
                selectedMapEntries.append({project, mapVar.value<MapInfo>()});
        }
        const int selectedCount = selectedMapEntries.size();

        // ── Map actions (only for map leaf items) ───────────────────────
        QAction *actOpenMap       = nullptr;
        QAction *actViewInHex     = nullptr;
        QAction *actSearchForName = nullptr;
        QAction *actCopyMapNames  = nullptr;
        QAction *actCloseSelectedMapViews = nullptr;
        QAction *actCloseMapViews = nullptr;
        QAction *actStarMap       = nullptr;
        QAction *actDuplicateMap  = nullptr;
        QAction *actSelectAllMaps = nullptr;
        QAction *actClearMapSelection = nullptr;
        QAction *actSelectStarredMaps = nullptr;
        QAction *actNewFolder     = nullptr;
        QAction *actMoveToFolder  = nullptr;
        QAction *actEditComment   = nullptr;
        QAction *actClearComment  = nullptr;
        QAction *actMapProperties = nullptr;
        QAction *actDeleteMap     = nullptr;
        Project *menuMapProject   = nullptr;
        MapInfo menuMap;
        if (item) {
            auto mapVar = item->data(0, Qt::UserRole + 2);
            if (mapVar.isValid()) {
                menuMap = mapVar.value<MapInfo>();
                menuMapProject = static_cast<Project *>(
                    item->data(0, Qt::UserRole + 1).value<void *>());

                actOpenMap       = menu.addAction(tr("Open"));
                actViewInHex     = menu.addAction(tr("View in hexdump"));
                actSearchForName = menu.addAction(tr("Search for name"));
                actCopyMapNames  = menu.addAction(tr("Copy name"));
                if (selectedCount > 1)
                    actCloseSelectedMapViews = menu.addAction(
                        tr("Close selected map windows"));
                actCloseMapViews = menu.addAction(tr("Close all map windows"));
                menu.addSeparator();
                const bool allStarred = !selectedMapEntries.isEmpty()
                    && std::all_of(selectedMapEntries.cbegin(), selectedMapEntries.cend(),
                        [](const SelectedMapEntry &entry) {
                            return isProjectMapStarred(*entry.project, entry.map);
                        });
                actStarMap = menu.addAction(allStarred
                    ? (selectedCount > 1 ? tr("Unstar %1 maps").arg(selectedCount)
                                         : tr("Unstar map"))
                    : (selectedCount > 1 ? tr("Star %1 maps").arg(selectedCount)
                                         : tr("Star map")));
                actDuplicateMap  = menu.addAction(selectedCount > 1
                    ? tr("Duplicate %1 maps").arg(selectedCount)
                    : tr("Duplicate"));
                auto *selectMenu = menu.addMenu(tr("Select…"));
                actSelectAllMaps = selectMenu->addAction(tr("All maps"));
                actSelectStarredMaps = selectMenu->addAction(tr("Starred maps"));
                actClearMapSelection = selectMenu->addAction(tr("Clear selection"));
                menu.addSeparator();
                actNewFolder    = menu.addAction(tr("New folder with selected maps…"));
                actNewFolder->setEnabled(selectedCount == 1);
                actMoveToFolder = menu.addAction(tr("Move to folder…"));
                menu.addSeparator();

                if (selectedCount == 1) {
                    actEditComment  = menu.addAction(
                        menuMap.userNotes.isEmpty()
                        ? tr("Add Comment…")
                        : tr("Edit Comment…"));
                    if (!menuMap.userNotes.isEmpty())
                        actClearComment = menu.addAction(tr("Clear Comment"));
                    actMapProperties = menu.addAction(tr("Properties…"));
                }
                actDeleteMap = menu.addAction(selectedCount > 1
                    ? tr("Delete %1 maps…").arg(selectedMapEntries.size())
                    : tr("Delete Map…"));
                menu.addSeparator();
            }
        }

        // ── Rename action (Project / sub-project / ProjectVersion) ─────
        // Multi-Version .ols imports create sub-project Project* entries AND
        // mirror them as ProjectVersion snapshots inside each sibling's
        // versions[]. Renaming a sub-project has to update BOTH: the tree
        // label (project->name) and every sibling's versions[].name so the
        // dropdown stays consistent.
        QAction *actRename = nullptr;
        Project *renameTarget = nullptr;
        if (item) {
            auto projVar = item->data(0, Qt::UserRole);
            auto mapVar  = item->data(0, Qt::UserRole + 2);
            if (projVar.isValid() && !mapVar.isValid()) {
                auto *proj = static_cast<Project *>(projVar.value<void *>());
                // Only offer rename for real Project items — not parent shells
                // (those get their name from the source filename) and not
                // group headers (those have no UserRole).
                if (proj && !(proj->currentData.isEmpty()
                              && !proj->subProjects.isEmpty())) {
                    actRename = menu.addAction(tr("Rename…"));
                    renameTarget = proj;
                }
            }
        }

        QAction *actExpandAll   = menu.addAction(tr("Expand All Groups"));
        QAction *actCollapseAll = menu.addAction(tr("Collapse All Groups"));

        // Per-project expand/collapse when multiple projects exist
        QAction *actExpandProj   = nullptr;
        QAction *actCollapseProj = nullptr;
        if (m_projectTree->topLevelItemCount() > 1) {
            // Find the project root for the clicked item
            QTreeWidgetItem *projRoot = item;
            while (projRoot && projRoot->parent())
                projRoot = projRoot->parent();
            if (projRoot && projRoot->childCount() > 0) {
                QString projName = projRoot->text(0).section("  ", 0, 0); // strip size suffix
                menu.addSeparator();
                actExpandProj   = menu.addAction(tr("Expand \"%1\"").arg(projName));
                actCollapseProj = menu.addAction(tr("Collapse \"%1\"").arg(projName));
            }
        }

        // Per-group expand/collapse for clicked group node
        QAction *actExpandThis   = nullptr;
        QAction *actCollapseThis = nullptr;
        QAction *actTranslateGroup = nullptr;
        if (item && item->childCount() > 0 && item->parent()
            && !(item->flags() & Qt::ItemIsSelectable)) {
            menu.addSeparator();
            actExpandThis   = menu.addAction(tr("Expand \"%1\"").arg(item->text(0).section("  ", 0, 0)));
            actCollapseThis = menu.addAction(tr("Collapse \"%1\"").arg(item->text(0).section("  ", 0, 0)));

            // ── AI Translate Group (only for group header items with translation module) ──
            if (ApiClient::instance().hasModule(QStringLiteral("translation"))) {
                menu.addSeparator();
                actTranslateGroup = menu.addAction(tr("✦ AI Translate Group…"));
            }
        }

        // Sprint E — bulk edit when 2+ map rows are selected
        QAction *actBulkEdit = nullptr;
        QVector<MapInfo> bulkSelection;
        {
            for (auto *it : m_projectTree->selectedItems()) {
                auto v = it->data(0, Qt::UserRole + 2);
                if (v.isValid()) bulkSelection.append(v.value<MapInfo>());
            }
            if (bulkSelection.size() >= 2) {
                menu.addSeparator();
                actBulkEdit = menu.addAction(
                    tr("Bulk edit %1 maps…").arg(bulkSelection.size()));
            }
        }

        // Sprint F — Find similar maps when exactly one map row is selected
        QAction *actFindSimilar = nullptr;
        MapInfo  findSimilarRef;
        if (bulkSelection.size() == 1
            || (item && item->data(0, Qt::UserRole + 2).isValid()
                && bulkSelection.size() <= 1)) {
            if (item) {
                auto v = item->data(0, Qt::UserRole + 2);
                if (v.isValid()) {
                    findSimilarRef = v.value<MapInfo>();
                    menu.addSeparator();
                    actFindSimilar = menu.addAction(tr("Find similar maps…"));
                }
            }
        }

        QAction *chosen = menu.exec(m_projectTree->mapToGlobal(pos));
        if (!chosen) return;

        if (actBulkEdit && chosen == actBulkEdit) {
            runBulkEdit(bulkSelection);
            return;
        }
        if (actFindSimilar && chosen == actFindSimilar) {
            runFindSimilar(findSimilarRef);
            return;
        }

        if (actSearchForName && chosen == actSearchForName) {
            m_filterEdit->setText(menuMap.name);
            m_filterEdit->setFocus();
            applyTreeFilter();
            return;
        }

        if (actCopyMapNames && chosen == actCopyMapNames) {
            QStringList names;
            names.reserve(selectedMapEntries.size());
            for (const auto &entry : selectedMapEntries)
                names.append(entry.map.name);
            QApplication::clipboard()->setText(names.join(QLatin1Char('\n')));
            statusBar()->showMessage(
                tr("Copied %1 map name(s)").arg(names.size()), 2500);
            return;
        }

        if (actOpenMap && chosen == actOpenMap && menuMapProject) {
            for (const auto &entry : selectedMapEntries) {
                openProject(entry.project);
                if (auto *pv = activeView()) pv->showMap(entry.map);
                onMapActivated(entry.map, entry.project);
                openMapViewer(entry.project, entry.map);
            }
            return;
        }

        if (actViewInHex && chosen == actViewInHex && menuMapProject) {
            openProject(menuMapProject);
            if (auto *pv = activeView()) pv->showMap(menuMap);
            onMapActivated(menuMap, menuMapProject);
            return;
        }

        if (actCloseMapViews && chosen == actCloseMapViews && menuMapProject) {
            QSet<Project *> projects;
            for (const auto &entry : selectedMapEntries) projects.insert(entry.project);
            for (auto ov : m_overlays)
                if (ov && projects.contains(ov->targetProject())) ov->close();
            m_overlays.removeAll(QPointer<MapOverlay>());
            return;
        }

        if (actCloseSelectedMapViews && chosen == actCloseSelectedMapViews) {
            for (const auto &entry : selectedMapEntries) {
                for (auto ov : m_overlays) {
                    if (ov && ov->targetProject() == entry.project
                        && ov->displaysMap(entry.map)) ov->close();
                }
            }
            m_overlays.removeAll(QPointer<MapOverlay>());
            return;
        }

        if (actStarMap && chosen == actStarMap && menuMapProject) {
            const bool shouldStar = !std::all_of(
                selectedMapEntries.cbegin(), selectedMapEntries.cend(),
                [](const SelectedMapEntry &entry) {
                    return isProjectMapStarred(*entry.project, entry.map);
                });
            QSet<Project *> changedProjects;
            for (const auto &entry : selectedMapEntries) {
                setProjectMapStarred(*entry.project, entry.map, shouldStar);
                entry.project->modified = true;
                changedProjects.insert(entry.project);
            }
            for (Project *project : changedProjects)
                emit project->dataChanged();
            refreshProjectTree();
            return;
        }

        if (actDuplicateMap && chosen == actDuplicateMap && menuMapProject) {
            QSet<Project *> changedProjects;
            for (const auto &entry : selectedMapEntries) {
                MapInfo duplicate = entry.map;
                const QString baseName = entry.map.name.isEmpty() ? tr("Map") : entry.map.name;
                duplicate.name = baseName + tr(" copy");
                for (int suffix = 2; ; ++suffix) {
                    const bool exists = std::any_of(entry.project->maps.cbegin(),
                        entry.project->maps.cend(), [&duplicate](const MapInfo &candidate) {
                            return candidate.name == duplicate.name;
                        });
                    if (!exists) break;
                    duplicate.name = baseName + tr(" copy %1").arg(suffix);
                }
                entry.project->maps.append(duplicate);
                entry.project->modified = true;
                changedProjects.insert(entry.project);
            }
            for (Project *project : changedProjects)
                emit project->dataChanged();
            refreshProjectTree();
            return;
        }

        if (actSelectAllMaps && chosen == actSelectAllMaps && menuMapProject) {
            m_projectTree->clearSelection();
            std::function<void(QTreeWidgetItem *)> selectMaps =
                [&](QTreeWidgetItem *node) {
                const auto mapVar = node->data(0, Qt::UserRole + 2);
                const auto *project = static_cast<Project *>(
                    node->data(0, Qt::UserRole + 1).value<void *>());
                if (mapVar.isValid() && project == menuMapProject)
                    node->setSelected(true);
                for (int i = 0; i < node->childCount(); ++i)
                    selectMaps(node->child(i));
            };
            for (int i = 0; i < m_projectTree->topLevelItemCount(); ++i)
                selectMaps(m_projectTree->topLevelItem(i));
            return;
        }

        if (actSelectStarredMaps && chosen == actSelectStarredMaps && menuMapProject) {
            m_projectTree->clearSelection();
            std::function<void(QTreeWidgetItem *)> selectStarred =
                [&](QTreeWidgetItem *node) {
                const auto mapVar = node->data(0, Qt::UserRole + 2);
                auto *project = static_cast<Project *>(
                    node->data(0, Qt::UserRole + 1).value<void *>());
                if (mapVar.isValid() && project == menuMapProject
                    && isProjectMapStarred(*project, mapVar.value<MapInfo>()))
                    node->setSelected(true);
                for (int i = 0; i < node->childCount(); ++i)
                    selectStarred(node->child(i));
            };
            for (int i = 0; i < m_projectTree->topLevelItemCount(); ++i)
                selectStarred(m_projectTree->topLevelItem(i));
            return;
        }

        if (actClearMapSelection && chosen == actClearMapSelection) {
            m_projectTree->clearSelection();
            return;
        }

        if ((actNewFolder && chosen == actNewFolder)
            || (actMoveToFolder && chosen == actMoveToFolder)) {
            if (!menuMapProject) return;
            QVector<MapInfo> mapsToMove;
            for (auto *selected : m_projectTree->selectedItems()) {
                const auto mapVar = selected->data(0, Qt::UserRole + 2);
                const auto *project = static_cast<Project *>(
                    selected->data(0, Qt::UserRole + 1).value<void *>());
                if (mapVar.isValid() && project == menuMapProject)
                    mapsToMove.append(mapVar.value<MapInfo>());
            }
            if (mapsToMove.isEmpty()) mapsToMove.append(menuMap);

            QString folder;
            if (chosen == actNewFolder) {
                const QString suggestion = menuMap.folderPath.isEmpty()
                    ? tr("New folder") : menuMap.folderPath + QStringLiteral("/") + tr("New folder");
                bool ok = false;
                folder = QInputDialog::getText(this, tr("New Folder"),
                    tr("Folder path:"), QLineEdit::Normal, suggestion, &ok);
                if (!ok) return;
            } else {
                QStringList folders;
                for (const auto &candidate : menuMapProject->maps) {
                    if (!candidate.folderPath.isEmpty() && !folders.contains(candidate.folderPath))
                        folders.append(candidate.folderPath);
                }
                std::sort(folders.begin(), folders.end());
                folders.prepend(tr("(Project root)"));
                bool ok = false;
                folder = QInputDialog::getItem(this, tr("Move to Folder"),
                    tr("Folder path:"), folders, 0, true, &ok);
                if (!ok) return;
                if (folder == tr("(Project root)")) folder.clear();
            }

            folder = folder.trimmed();
            folder.replace(QLatin1Char('\\'), QLatin1Char('/'));
            while (folder.contains(QStringLiteral("//")))
                folder.replace(QStringLiteral("//"), QStringLiteral("/"));
            while (folder.startsWith(QLatin1Char('/'))) folder.removeFirst();
            while (folder.endsWith(QLatin1Char('/'))) folder.chop(1);
            if (chosen == actNewFolder && folder.isEmpty()) return;

            for (const auto &source : mapsToMove) {
                const auto it = std::find(menuMapProject->maps.begin(),
                    menuMapProject->maps.end(), source);
                if (it != menuMapProject->maps.end()) it->folderPath = folder;
            }
            menuMapProject->modified = true;
            emit menuMapProject->dataChanged();
            refreshProjectTree();
            return;
        }

        if (actMapProperties && chosen == actMapProperties && item) {
            const auto map = item->data(0, Qt::UserRole + 2).value<MapInfo>();
            auto *proj = static_cast<Project *>(
                item->data(0, Qt::UserRole + 1).value<void *>());
            if (!proj) return;

            MapPropertiesDialog dlg(map, proj->byteOrder, this);
            if (dlg.exec() != QDialog::Accepted) return;

            const auto it = std::find(proj->maps.begin(), proj->maps.end(), map);
            if (it == proj->maps.end()) return;
            MapInfo updated = dlg.result();
            updated.cellBigEndian = dlg.byteOrder() == ByteOrder::BigEndian;
            *it = updated;
            proj->modified = true;
            emit proj->dataChanged();
            refreshProjectTree();
            return;
        }

        if (actDeleteMap && chosen == actDeleteMap && menuMapProject) {
            const int count = selectedMapEntries.size();
            if (QMessageBox::question(
                    this, tr("Delete Map"),
                    count > 1
                    ? tr("Delete %1 selected map definitions?").arg(count)
                    : tr("Delete the map definition \"%1\"?").arg(menuMap.name),
                    QMessageBox::Yes | QMessageBox::Cancel,
                    QMessageBox::Cancel) != QMessageBox::Yes)
                return;

            QSet<Project *> changedProjects;
            for (const auto &entry : selectedMapEntries) {
                for (auto ov : m_overlays)
                    if (ov && ov->targetProject() == entry.project
                        && ov->displaysMap(entry.map)) ov->close();
                const auto it = std::find(entry.project->maps.begin(),
                    entry.project->maps.end(), entry.map);
                if (it == entry.project->maps.end()) continue;
                entry.project->maps.erase(it);
                entry.project->modified = true;
                changedProjects.insert(entry.project);
            }
            m_overlays.removeAll(QPointer<MapOverlay>());
            for (Project *project : changedProjects)
                emit project->dataChanged();
            refreshProjectTree();
            return;
        }

        // ── Handle rename ──────────────────────────────────────────────
        if (actRename && chosen == actRename && renameTarget) {
            bool ok = false;
            const QString oldName = renameTarget->name;
            const QString newName = QInputDialog::getText(
                this, tr("Rename"),
                tr("New name:"),
                QLineEdit::Normal, oldName, &ok).trimmed();
            if (!ok || newName.isEmpty() || newName == oldName) return;
            renameTarget->name = newName;
            renameTarget->modified = true;
            // Cascade to sibling sub-projects' versions[] so every open
            // window's Versions dropdown reflects the new label.
            if (renameTarget->isSubProject && renameTarget->parentProject) {
                const int idx = renameTarget->subProjectIndex;
                for (Project *sib : renameTarget->parentProject->subProjects) {
                    if (!sib) continue;
                    if (idx < 0 || idx >= sib->versions.size()) continue;
                    sib->versions[idx].name = newName;
                    emit sib->versionsChanged();
                }
            } else {
                // Top-level project rename: mirror to any snapshots that match
                // the old name (no subProjects here, so this is a no-op for
                // plain projects, but it keeps the flow symmetric).
                for (auto &v : renameTarget->versions)
                    if (v.name == oldName) v.name = newName;
                emit renameTarget->versionsChanged();
            }
            emit renameTarget->dataChanged();   // triggers MDI title + tree refresh
            refreshProjectTree();
            return;
        }

        // ── Handle comment actions ──────────────────────────────────────
        if (chosen == actEditComment || chosen == actClearComment) {
            auto mapVar = item->data(0, Qt::UserRole + 2);
            auto map    = mapVar.value<MapInfo>();
            auto *proj  = static_cast<Project *>(
                              item->data(0, Qt::UserRole + 1).value<void *>());
            if (!proj) return;

            QString newNote;
            if (chosen == actEditComment) {
                bool ok;
                newNote = QInputDialog::getMultiLineText(
                    this, tr("Map Comment"),
                    tr("Comment for  \"%1\":").arg(map.name),
                    map.userNotes, &ok);
                if (!ok) return;
            }
            // Update the map in the project
            for (auto &m : proj->maps) {
                if (m.name == map.name) {
                    m.userNotes = newNote.trimmed();
                    break;
                }
            }
            proj->modified = true;
            refreshProjectTree();
            return;
        }

        // ── Handle expand/collapse actions ─────────────────────────────
        auto forEachGroup = [this](auto fn) {
            std::function<void(QTreeWidgetItem*)> walk = [&](QTreeWidgetItem *it) {
                if (it->childCount() > 0 && !(it->flags() & Qt::ItemIsSelectable))
                    fn(it);
                for (int i = 0; i < it->childCount(); ++i)
                    walk(it->child(i));
            };
            for (int i = 0; i < m_projectTree->topLevelItemCount(); ++i)
                walk(m_projectTree->topLevelItem(i));
        };

        if (chosen == actExpandAll)
            forEachGroup([](QTreeWidgetItem *it){ it->setExpanded(true); });
        else if (chosen == actCollapseAll)
            forEachGroup([](QTreeWidgetItem *it){ it->setExpanded(false); });
        else if (actExpandProj && chosen == actExpandProj) {
            // Expand all children of this project
            QTreeWidgetItem *projRoot = item;
            while (projRoot && projRoot->parent()) projRoot = projRoot->parent();
            std::function<void(QTreeWidgetItem*)> expandAll = [&](QTreeWidgetItem *it) {
                it->setExpanded(true);
                for (int i = 0; i < it->childCount(); ++i) expandAll(it->child(i));
            };
            if (projRoot) expandAll(projRoot);
        }
        else if (actCollapseProj && chosen == actCollapseProj) {
            QTreeWidgetItem *projRoot = item;
            while (projRoot && projRoot->parent()) projRoot = projRoot->parent();
            std::function<void(QTreeWidgetItem*)> collapseAll = [&](QTreeWidgetItem *it) {
                it->setExpanded(false);
                for (int i = 0; i < it->childCount(); ++i) collapseAll(it->child(i));
            };
            if (projRoot) collapseAll(projRoot);
        }
        else if (actExpandThis  && chosen == actExpandThis)
            item->setExpanded(true);
        else if (actCollapseThis && chosen == actCollapseThis)
            item->setExpanded(false);
        else if (actTranslateGroup && chosen == actTranslateGroup) {
            // Collect maps from group children
            QVector<QPair<QString,QString>> groupItems;
            Project *proj = nullptr;
            for (int i = 0; i < item->childCount(); i++) {
                QTreeWidgetItem *child = item->child(i);
                auto mapVar = child->data(0, Qt::UserRole + 2);
                if (!mapVar.isValid()) continue;
                auto map = mapVar.value<MapInfo>();
                if (!proj) proj = static_cast<Project *>(
                    child->data(0, Qt::UserRole + 1).value<void *>());
                groupItems.append({map.name, map.description});
            }
            if (groupItems.isEmpty() || !proj) return;

            const QString groupName = item->text(0).section("  (", 0, 0).trimmed();

            // ── Pre-flight dialog ──────────────────────────────────────────────
            QDialog ask(this);
            ask.setWindowTitle(tr("AI Translation"));
            ask.setMinimumWidth(340);
            auto *askLay = new QVBoxLayout(&ask);
            askLay->setSpacing(12);

            auto *askLabel = new QLabel(tr("<b>Translate group \"%1\" (%2 maps)</b>")
                .arg(groupName).arg(groupItems.size()), &ask);
            askLabel->setTextFormat(Qt::RichText);

            auto *langRow  = new QHBoxLayout();
            auto *langLabel = new QLabel(tr("Language:"), &ask);
            auto *langCombo = new QComboBox(&ask);
            langCombo->addItem("English",  QStringLiteral("en"));
            langCombo->addItem("中文",      QStringLiteral("zh_CN"));
            langRow->addWidget(langLabel);
            langRow->addWidget(langCombo, 1);

            auto *btnRow    = new QHBoxLayout();
            auto *btnCancel = new QPushButton(tr("Cancel"), &ask);
            auto *btnOk     = new QPushButton(tr("Translate"), &ask);
            btnOk->setDefault(true);
            btnRow->addWidget(btnCancel);
            btnRow->addStretch();
            btnRow->addWidget(btnOk);

            askLay->addWidget(askLabel);
            askLay->addLayout(langRow);
            askLay->addLayout(btnRow);

            connect(btnCancel, &QPushButton::clicked, &ask, &QDialog::reject);
            connect(btnOk,     &QPushButton::clicked, &ask, &QDialog::accept);
            if (ask.exec() != QDialog::Accepted) return;

            const QString lang = langCombo->currentData().toString();
            const int total = groupItems.size();

            // ── Progress dialog ────────────────────────────────────────────────
            auto *progress = new QProgressDialog(
                tr("Translating group \"%1\"…").arg(groupName),
                tr("Cancel"), 0, total, this);
            progress->setWindowTitle(tr("AI Translation"));
            progress->setWindowModality(Qt::WindowModal);
            progress->setMinimumDuration(0);
            progress->setAutoClose(false);
            progress->setAutoReset(false);
            progress->setValue(0);
            progress->setMinimumWidth(360);

            auto cancelled = std::make_shared<bool>(false);
            connect(progress, &QProgressDialog::canceled, this, [cancelled]() {
                *cancelled = true;
            });

            ApiClient::instance().translateMapsBatch(
                groupItems, lang,
                // progressCb — called from Qt event loop (network replies)
                [progress, cancelled](int done, int total) {
                    if (*cancelled) return;
                    progress->setValue(done);
                    progress->setLabelText(
                        QObject::tr("Translating maps…  %1 / %2").arg(done).arg(total));
                },
                // doneCb — called once everything is done
                [this, progress, cancelled, groupName]
                (const QVector<TranslationResult> &results) {
                    // Check if user cancelled FIRST, before showing any dialogs
                    if (*cancelled) {
                        progress->close();
                        progress->deleteLater();
                        return;
                    }

                    progress->close();
                    progress->deleteLater();

                    int applied = 0;
                    int totalReceived = results.size();
                    for (const auto &r : results) {
                        // Store all results regardless of whether translation is empty
                        // (empty translations might indicate cached or failed entries)
                        if (!r.translation.isEmpty()) {
                            m_translations[r.name] = r;
                            ++applied;
                        }
                    }

                    // Show status message first (before checking for active project)
                    if (applied > 0) {
                        // Status-bar feedback only — non-destructive success doesn't need a modal.
                        statusBar()->showMessage(
                            tr("Translated %1 maps in group \"%2\".").arg(applied).arg(groupName), 5000);
                    } else if (totalReceived > 0) {
                        // Got results but no translations - show debug info
                        statusBar()->showMessage(
                            tr("API returned %1 results but no translations for group \"%2\" (possible network/API issue)").arg(totalReceived).arg(groupName), 8000);
                        QMessageBox::warning(this, tr("Translation Issue"),
                            tr("API returned %1 results but no translations were generated.\n\nThis usually means:\n- The maps already have translations cached\n- Or there was an API error\n\nResults received: %1\nApplied: %2").arg(totalReceived).arg(applied));
                        return;
                    } else {
                        // No results at all
                        statusBar()->showMessage(
                            tr("No translation results for group \"%1\" (API error or network issue)").arg(groupName), 8000);
                        QMessageBox::critical(this, tr("Translation Failed"),
                            tr("No translation results for group \"%1\"\n\nThe API did not return any data.\nPossible issues:\n- Network error\n- API error\n- Invalid map names").arg(groupName));
                        return;
                    }

                    auto *proj = activeProject();
                    if (!proj) return;

                    // Save expanded state of all groups before refresh
                    QSet<QString> expandedGroups;
                    std::function<void(QTreeWidgetItem*)> collectExpanded;
                    collectExpanded = [&collectExpanded, &expandedGroups](QTreeWidgetItem *it) {
                        if (it->childCount() > 0 && !(it->flags() & Qt::ItemIsSelectable) && it->isExpanded()) {
                            expandedGroups.insert(it->text(0).section("  (", 0, 0));
                        }
                        for (int i = 0; i < it->childCount(); ++i)
                            collectExpanded(it->child(i));
                    };
                    for (int i = 0; i < m_projectTree->topLevelItemCount(); ++i)
                        collectExpanded(m_projectTree->topLevelItem(i));

                    proj->modified = true;
                    refreshProjectTree();

                    // Restore expanded state
                    std::function<void(QTreeWidgetItem*)> restoreExpanded;
                    restoreExpanded = [&restoreExpanded, &expandedGroups](QTreeWidgetItem *it) {
                        if (it->childCount() > 0 && !(it->flags() & Qt::ItemIsSelectable)) {
                            QString groupName = it->text(0).section("  (", 0, 0);
                            if (expandedGroups.contains(groupName)) {
                                it->setExpanded(true);
                            }
                        }
                        for (int i = 0; i < it->childCount(); ++i)
                            restoreExpanded(it->child(i));
                    };
                    for (int i = 0; i < m_projectTree->topLevelItemCount(); ++i)
                        restoreExpanded(m_projectTree->topLevelItem(i));
                });
        }
    });
}

// ── Actions ───────────────────────────────────────────────────────────────────

void MainWindow::autoRetryFailed(const QVector<QPair<QString,QString>> &failed,
                                  const QString &lang,
                                  const QVector<QPair<QString,QString>> &allItems,
                                  int prevApplied, int totalMaps)
{
    if (failed.isEmpty()) {
        statusBar()->showMessage(
            tr("AI translation complete: %1 of %2 maps translated.")
                .arg(prevApplied).arg(totalMaps), 8000);
        return;
    }

    // Build description lookup from allItems
    QMap<QString,QString> descOf;
    for (const auto &p : allItems) descOf[p.first] = p.second;

    QVector<QPair<QString,QString>> retryItems;
    retryItems.reserve(failed.size());
    for (const auto &p : failed)
        retryItems.append({p.first, descOf.value(p.first)});

    statusBar()->showMessage(tr("Retrying %1 maps…").arg(retryItems.size()));
    m_btnTranslateAll->setEnabled(false);

    ApiClient::instance().translateMapsBatch(
        retryItems, lang,
        [](int, int) {},   // silent — no progress bar for retry
        [this, prevApplied, totalMaps, retryItems, lang, allItems]
        (const QVector<TranslationResult> &res) {
            m_btnTranslateAll->setEnabled(
                ApiClient::instance().hasModule(QStringLiteral("translation")));
            int applied = prevApplied;
            QVector<QPair<QString,QString>> stillFailed;
            for (const auto &r : res) {
                if (!r.translation.isEmpty()) {
                    m_translations[r.name] = r;
                    ++applied;
                } else {
                    stillFailed.append({r.name, {}});
                }
            }
            refreshProjectTree();
            // Recurse until nothing fails (server-side retries handle rate limits,
            // so this should converge quickly)
            autoRetryFailed(stillFailed, lang, allItems, applied, totalMaps);
        });
}

void MainWindow::buildActions()
{
    // File / project
    m_actProjectMgr = new QAction(tr("Project Manager…"),       this);
    m_actProjectMgr->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
    m_actNew        = new QAction(tr("New Project…"),           this);
    m_actOpen       = new QAction(tr("Open Project…"),          this);
    m_actSave       = new QAction(tr("Save"),                   this);
    m_actSaveAs     = new QAction(tr("Save As…"),               this);
    m_actClose      = new QAction(tr("Close Project"),          this);
    m_actHome       = new QAction(tr("Home"),                   this);
    m_actImportA2L  = new QAction(tr("Import A2L…"),            this);
    m_actImportKP   = new QAction(tr("Import KP…"),             this);
    m_actImportKP->setToolTip(tr("Import a .kp map pack and apply map labels to the current project"));
    m_actImportXDF  = new QAction(tr("Import XDF…"),            this);
    m_actImportXDF->setToolTip(tr("Import a TunerPro .xdf definition and apply its maps to the current project"));
    m_actImportFRF  = new QAction(tr("Import FRF / ODX…"),      this);
    m_actImportFRF->setToolTip(tr("Extract a VAG .frf / .sgo / .odx flash container to a ROM binary"));
    // Single OLS-import action — replaces the previous Import OLS / Import
    // WinOLS Project pair (both routed to the same actImportOlsProject
    // slot anyway). Toolbar OLS button + Project menu both point here.
    m_actImportOLS = new QAction(tr("Import OLS…"), this);
    m_actAddVersion = new QAction(tr("Save Version Snapshot…"), this);
    m_actExport        = new QAction(tr("Export ROM…"),            this);
    m_actExportOLS  = new QAction(tr("Export WinOLS Project (.ols)…"), this);

    m_actNew->setShortcut(QKeySequence::New);
    m_actOpen->setShortcut(QKeySequence::Open);
    m_actSave->setShortcut(QKeySequence::Save);
    m_actSaveAs->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    m_actExport->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));

    // ROM linking / compare
    m_actLinkRom         = new QAction(tr("Link ROM to Project…"),       this);
    m_actImportLinkedVer = new QAction(tr("Import ROM as Version…"),     this);
    m_actCompareRoms     = new QAction(tr("Compare ROM / Version…"),     this);
    m_actCompareHex      = new QAction(tr("Compare Hex…"),                this);
    m_actImportMapPack   = new QAction(tr("Import Map Pack…"),           this);
    m_actImportCsvList   = new QAction(tr("Import Map List (CSV)…"),     this);
    m_actPatchEditor     = new QAction(tr("Open Patch Script…"),         this);
    m_actDtcManager      = new QAction(tr("DTC Manager (A2L)…"),         this);
    m_actDtcWizard       = new QAction(tr("Cloud Tools (DTC && Features)…"), this);
    m_actAIFunctions     = new QAction(tr("AI Functions…"), this);
    m_actVerifyChecksum  = new QAction(tr("Verify Checksum"),   this);
    m_actCorrectChecksum = new QAction(tr("Correct Checksum…"), this);
    m_actVerifyChecksum->setToolTip(tr("Verify the ROM checksum using the ECU-specific algorithm"));
    m_actCorrectChecksum->setToolTip(tr("Recalculate and write the correct ROM checksum"));
    m_actLinkRom->setToolTip(tr("Link another ROM file to this project and auto-locate all maps"));
    m_actImportLinkedVer->setToolTip(tr("Import a ROM file as a new version snapshot of this project"));
    m_actCompareRoms->setToolTip(tr("Compare current ROM against a linked ROM or saved version"));
    m_actImportMapPack->setToolTip(tr("Import a .rxpack map pack and apply selected maps to the current ROM"));
    m_actImportCsvList->setToolTip(tr("Import a CSV map list (Address;Name;Size) and add the map definitions to the project"));
    m_actPatchEditor->setToolTip(tr("Open a .rxpatch script file in the patch editor"));

    // Window
    m_actTile    = new QAction(tr("Tile Windows"),      this);
    m_actCascade = new QAction(tr("Cascade Windows"),   this);
    m_actCompare = new QAction(tr("Compare Projects…"), this);

    // Navigation
    m_actPrevMap     = new QAction(this);
    m_actNextMap     = new QAction(this);
    m_actSyncCursors = new QAction(this);
    m_actSyncCursors->setCheckable(true);
    m_actPrevMap->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Left));
    m_actNextMap->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Right));
    m_actPrevMap->setToolTip(tr("Move cursor to previous map  (Ctrl+←)"));
    m_actNextMap->setToolTip(tr("Move cursor to next map  (Ctrl+→)"));
    m_actSyncCursors->setToolTip(tr("Sync 2D view scroll across all open projects"));

    // VSCode-style command palette. Cmd+K on macOS, Ctrl+K elsewhere
    // (Qt::CTRL maps to ⌘ on macOS automatically). Added to the window
    // (and to the Misc menu via retranslateUi) so the shortcut works
    // even when no menu is open.
    m_actCmdPalette = new QAction(tr("Command Palette\u2026"), this);
    m_actCmdPalette->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
    m_actCmdPalette->setShortcutContext(Qt::ApplicationShortcut);
    addAction(m_actCmdPalette);
    connect(m_actCmdPalette, &QAction::triggered,
            this, &MainWindow::actShowCommandPalette);

    m_actPreferences = new QAction(tr("Settings\u2026"), this);
    m_actPreferences->setMenuRole(QAction::NoRole);
    connect(m_actPreferences, &QAction::triggered, this, [this]() {
        ConfigDialog dlg(this);
        dlg.exec();
        for (auto *sub : m_mdi->subWindowList())
            sub->widget()->update();
    });

    m_actToggleAI = new QAction(tr("AI Assistant"), this);
    m_actToggleAI->setCheckable(true);
    m_actToggleAI->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Backslash));
    m_actToggleAI->setToolTip(tr("Show / hide the AI Assistant panel  (Ctrl+\\)"));
    connect(m_actToggleAI, &QAction::toggled, this, [this](bool on) {
        if (!m_aiAssistant) return;
        m_aiAssistant->setVisible(on);
        if (on) {
            // Give it a reasonable width if it was previously hidden
            QList<int> sizes = m_mainSplitter->sizes();
            if (sizes.size() >= 3 && sizes[2] < 50) {
                sizes[2] = 360;
                sizes[1] = qMax(400, sizes[1] - 360);
                m_mainSplitter->setSizes(sizes);
            }
            auto *proj = activeProject();
            if (proj) m_aiAssistant->setProject(proj);
            m_aiAssistant->setAllProjects(m_projects);
        }
        // Retile after splitter settles so windows fill the new MDI area exactly
        QTimer::singleShot(50, this, [this]() { retileWindows(); });
    });

    // Map operations
    m_actOptimize   = new QAction(this);
    m_actDifference = new QAction(this);
    m_actIgnore     = new QAction(this);
    m_actFactor     = new QAction(this);
    m_actOrigFactor = new QAction(this);
    m_actOptimize->setToolTip(tr("Optimize value range — fit colour scale to current map"));
    m_actDifference->setCheckable(true);
    m_actDifference->setToolTip(tr("Show difference to original ROM data"));
    m_actIgnore->setCheckable(true);
    m_actIgnore->setToolTip(tr("Ignore this map (exclude from operations)"));
    m_actFactor->setToolTip(tr("Apply custom scaling factor"));
    m_actOrigFactor->setToolTip(tr("Reset to original scaling factor"));

    // Data size group (exclusive)
    m_act8bit  = new QAction(this); m_act8bit->setCheckable(true);
    m_act16bit = new QAction(this); m_act16bit->setCheckable(true);
    m_act32bit = new QAction(this); m_act32bit->setCheckable(true);
    m_actFloat = new QAction(this); m_actFloat->setCheckable(true);
    m_act8bit->setToolTip(tr("8-bit cell width"));
    m_act16bit->setToolTip(tr("16-bit cell width"));
    m_act32bit->setToolTip(tr("32-bit cell width"));
    m_actFloat->setToolTip(tr("32-bit float cell width"));
    m_act8bit->setChecked(true);
    m_grpDataSize = new QActionGroup(this);
    m_grpDataSize->addAction(m_act8bit);
    m_grpDataSize->addAction(m_act16bit);
    m_grpDataSize->addAction(m_act32bit);
    m_grpDataSize->addAction(m_actFloat);
    m_grpDataSize->setExclusive(true);

    // Byte order group (exclusive)
    m_actLo = new QAction(this); m_actLo->setCheckable(true);
    m_actHi = new QAction(this); m_actHi->setCheckable(true);
    m_actLo->setToolTip(tr("Low byte first (Little Endian)"));
    m_actHi->setToolTip(tr("High byte first (Big Endian)"));
    m_actHi->setChecked(true);
    m_grpByteOrder = new QActionGroup(this);
    m_grpByteOrder->addAction(m_actLo);
    m_grpByteOrder->addAction(m_actHi);
    m_grpByteOrder->setExclusive(true);

    // Sign group (exclusive)
    m_actSigned   = new QAction(this); m_actSigned->setCheckable(true);
    m_actUnsigned = new QAction(this); m_actUnsigned->setCheckable(true);
    m_actSigned->setToolTip(tr("Signed integer interpretation"));
    m_actUnsigned->setToolTip(tr("Unsigned integer interpretation"));
    m_actUnsigned->setChecked(true);
    m_grpSign = new QActionGroup(this);
    m_grpSign->addAction(m_actSigned);
    m_grpSign->addAction(m_actUnsigned);
    m_grpSign->setExclusive(true);

    // Display format group (exclusive)
    m_actDec = new QAction(this); m_actDec->setCheckable(true);
    m_actHex = new QAction(this); m_actHex->setCheckable(true);
    m_actBin = new QAction(this); m_actBin->setCheckable(true);
    m_actPct = new QAction(this); m_actPct->setCheckable(true);
    m_actDec->setToolTip(tr("Display values as decimal"));
    m_actHex->setToolTip(tr("Display values as hexadecimal"));
    m_actBin->setToolTip(tr("Display values as binary"));
    m_actPct->setToolTip(tr("Display values as percentage"));
    m_actDec->setChecked(true);
    m_grpDisplayFmt = new QActionGroup(this);
    m_grpDisplayFmt->addAction(m_actDec);
    m_grpDisplayFmt->addAction(m_actHex);
    m_grpDisplayFmt->addAction(m_actBin);
    m_grpDisplayFmt->addAction(m_actPct);
    m_grpDisplayFmt->setExclusive(true);

    // Visual
    m_actHeightColors = new QAction(this);
    m_actHeightColors->setCheckable(true);
    m_actHeightColors->setChecked(true);
    m_actHeightColors->setToolTip(tr("Turn height colours on / off"));

    // Differences panel toggle (View → Differences, Ctrl+D)
    m_actToggleDiff = new QAction(tr("Differences"), this);
    m_actToggleDiff->setCheckable(true);
    m_actToggleDiff->setShortcut(QKeySequence("Ctrl+D"));
    m_actToggleDiff->setToolTip(
        tr("Show/hide the Differences panel (compare two projects byte-by-byte)"));

    // Differences-to-Original overlay (Sprint B) — Ctrl+Shift+O.
    // Lights up every byte that differs from Project::originalData in
    // hex / waveform / 3D simultaneously.
    m_actDiffOriginal = new QAction(tr("Differences vs Original"), this);
    m_actDiffOriginal->setCheckable(true);
    m_actDiffOriginal->setShortcut(QKeySequence("Ctrl+Shift+O"));
    m_actDiffOriginal->setToolTip(
        tr("Highlight every cell that differs from the project's original ROM"));
    m_showOriginalDiff = rx14::appSettings()
                            .value("view/showOriginalDiff", false).toBool();
    m_actDiffOriginal->setChecked(m_showOriginalDiff);
    connect(m_actDiffOriginal, &QAction::toggled,
            this, &MainWindow::onToggleDiffOriginal);

    // Annotations (Sprint C) — comments + markers at ROM offsets.
    m_actInsertComment = new QAction(tr("Insert comment…"), this);
    m_actInsertComment->setShortcut(QKeySequence("Shift+Return"));
    m_actInsertComment->setToolTip(
        tr("Attach a free-text comment to the current ROM offset"));
    connect(m_actInsertComment, &QAction::triggered,
            this, &MainWindow::onInsertComment);

    m_actInsertMarker = new QAction(tr("Insert marker"), this);
    m_actInsertMarker->setShortcut(QKeySequence("Ctrl+F5"));
    m_actInsertMarker->setToolTip(
        tr("Pin a quick marker at the current ROM offset (no text)"));
    connect(m_actInsertMarker, &QAction::triggered,
            this, &MainWindow::onInsertMarker);

    m_actDeleteComment = new QAction(tr("Delete annotation here"), this);
    m_actDeleteComment->setToolTip(
        tr("Remove the comment / marker at the current ROM offset"));
    connect(m_actDeleteComment, &QAction::triggered,
            this, &MainWindow::onDeleteComment);

    m_actNextMarker = new QAction(tr("Next marker"), this);
    m_actNextMarker->setShortcut(QKeySequence(Qt::Key_F5));
    connect(m_actNextMarker, &QAction::triggered,
            this, [this]() { onJumpMarker(true); });

    m_actPrevMarker = new QAction(tr("Previous marker"), this);
    m_actPrevMarker->setShortcut(QKeySequence("Shift+F5"));
    connect(m_actPrevMarker, &QAction::triggered,
            this, [this]() { onJumpMarker(false); });

    // Tuning Branches / Savepoints (View → Tuning Branches, Ctrl+B)
    m_actToggleSavepoints = new QAction(tr("Tuning Branches"), this);
    m_actToggleSavepoints->setCheckable(true);
    m_actToggleSavepoints->setShortcut(QKeySequence("Ctrl+B"));
    m_actToggleSavepoints->setToolTip(
        tr("Named snapshots of the active project — save current state, "
           "switch back, compare trials"));

    // ── Selection / map editing operations (Sprint A) ──────────────────
    m_actValPlus1     = new QAction(tr("Value +1"), this);
    m_actValPlus1->setShortcut(QKeySequence(Qt::Key_Plus));
    m_actValMinus1    = new QAction(tr("Value −1"), this);
    m_actValMinus1->setShortcut(QKeySequence(Qt::Key_Minus));
    m_actChangeAbs    = new QAction(tr("Change absolute…"), this);
    m_actChangeAbs->setShortcut(QKeySequence(Qt::Key_Equal));
    m_actChangeRel    = new QAction(tr("Change relative…"), this);
    m_actChangeRel->setShortcut(QKeySequence(Qt::Key_Percent));
    m_actChangeSlider = new QAction(tr("Change by slider…"), this);
    m_actRoundLimit   = new QAction(tr("Round / limit values…"), this);
    m_actOriginalVal  = new QAction(tr("Restore original value"), this);
    m_actOriginalVal->setShortcut(QKeySequence(Qt::Key_F11));
    m_actInterpolate  = new QAction(tr("Interpolate"), this);
    m_actSmooth       = new QAction(tr("Smooth"), this);
    m_actFlatten      = new QAction(tr("Flatten (set to mean)"), this);
    m_actAgain        = new QAction(tr("Again"), this);
    m_actAgain->setShortcut(QKeySequence(Qt::Key_F4));
    m_actAgain->setToolTip(
        tr("Re-apply the last edit operation on the current selection"));

    auto wireEdit = [this](QAction *a, EditOp op) {
        connect(a, &QAction::triggered, this, [this, op]() {
            onEditOpFromMenu(op);
        });
    };
    wireEdit(m_actValPlus1,     EditOp::ValuePlus1);
    wireEdit(m_actValMinus1,    EditOp::ValueMinus1);
    wireEdit(m_actChangeAbs,    EditOp::Absolute);
    wireEdit(m_actChangeRel,    EditOp::Relative);
    wireEdit(m_actChangeSlider, EditOp::Slider);
    wireEdit(m_actRoundLimit,   EditOp::RoundLimit);
    wireEdit(m_actOriginalVal,  EditOp::Original);
    wireEdit(m_actInterpolate,  EditOp::Interpolate);
    wireEdit(m_actSmooth,       EditOp::Smooth);
    wireEdit(m_actFlatten,      EditOp::Flatten);
    connect(m_actAgain, &QAction::triggered, this, [this]() {
        if (m_haveLastEdit) applyEditOp(m_lastEditOp, m_lastEditParams);
    });

    // Connect slots
    connect(m_actProjectMgr, &QAction::triggered, this, &MainWindow::actProjectManager);
    connect(m_actNew,        &QAction::triggered, this, &MainWindow::actNewProject);
    connect(m_actOpen,       &QAction::triggered, this, &MainWindow::actOpenProject);
    connect(m_actSave,       &QAction::triggered, this, &MainWindow::actSaveProject);
    connect(m_actSaveAs,     &QAction::triggered, this, &MainWindow::actSaveProjectAs);
    connect(m_actClose,      &QAction::triggered, this, &MainWindow::actCloseProject);
    connect(m_actHome,       &QAction::triggered, this, &MainWindow::actGoHome);
    connect(m_actImportA2L,  &QAction::triggered, this, [this]{ actImportA2L(); });
    connect(m_actImportKP,      &QAction::triggered, this, [this]{ actImportKP(); });
    connect(m_actImportXDF,     &QAction::triggered, this, [this]{ actImportXdf(); });
    connect(m_actImportFRF,     &QAction::triggered, this, &MainWindow::actImportFrf);
    connect(m_actImportOLS, &QAction::triggered, this, &MainWindow::actImportOlsProject);
    connect(m_actAddVersion,   &QAction::triggered, this, &MainWindow::actAddVersion);
    connect(m_actExport,     &QAction::triggered, this, &MainWindow::actExportROM);
    connect(m_actExportOLS, &QAction::triggered, this, &MainWindow::actExportOlsProject);
    connect(m_actLinkRom,         &QAction::triggered, this, &MainWindow::actLinkRom);
    connect(m_actImportLinkedVer, &QAction::triggered, this, &MainWindow::actImportLinkedVersion);
    connect(m_actCompareRoms,     &QAction::triggered, this, &MainWindow::actCompareRoms);
    connect(m_actCompareHex,      &QAction::triggered, this, &MainWindow::actCompareHex);
    connect(m_actImportMapPack,   &QAction::triggered, this, &MainWindow::actImportMapPack);
    connect(m_actImportCsvList,   &QAction::triggered, this, [this]() {
        Project *proj = activeProject();
        if (!proj) {
            QMessageBox::information(this, tr("No project"),
                tr("Open a project before importing a map list."));
            return;
        }
        MapPackDlg::importPack(proj, this, /*preferCsv=*/true);
    });
    connect(m_actPatchEditor,     &QAction::triggered, this, &MainWindow::actOpenPatchEditor);
    connect(m_actDtcWizard,      &QAction::triggered, this, [this]() {
        auto *proj = activeProject();
        if (!proj || proj->currentData.isEmpty()) {
            QMessageBox::information(this, tr("No project"),
                tr("Open a project with ROM data first."));
            return;
        }
        auto *dlg = new CloudToolsDlg(proj, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    });
    connect(m_actDtcManager,     &QAction::triggered, this, [this]() {
        auto *proj = activeProject();
        if (!proj) {
            QMessageBox::information(this, tr("No project"), tr("Open a project with A2L maps first."));
            return;
        }
        // Check if project has any DFC maps (A2L-driven path)
        bool hasDfc = false;
        for (const auto &m : proj->maps) {
            if (m.name.contains("DFC_CtlMsk")) { hasDfc = true; break; }
        }
        if (!hasDfc) {
            QMessageBox::information(this, tr("No DTCs Found"),
                tr("No DFC_CtlMsk maps found in this project.\n"
                   "DTC Manager (A2L) needs DFC definitions from an imported A2L file.\n\n"
                   "Use “Disable DTC && Features…” for ROM-based (signature-driven) detection."));
            return;
        }
        auto *dlg = new DtcDialog(proj, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    });
    connect(m_actAIFunctions, &QAction::triggered, this, [this]() {
        auto *proj = activeProject();
        if (!proj) {
            QMessageBox::information(this, tr("No project"), tr("Open a project first."));
            return;
        }
        if (proj->maps.size() < 5) {
            QMessageBox::information(this, tr("AI Functions"),
                tr("AI Functions requires map definitions.\nImport an A2L file first to define ECU maps."));
            return;
        }
        if (!ApiClient::instance().isLoggedIn() || !ApiClient::instance().hasModule(QStringLiteral("ai_functions"))) {
            QMessageBox::information(this, tr("AI Functions"),
                tr("AI Functions requires a Pro account.\nPurchase from romhex14.com to unlock."));
            return;
        }
        auto *dlg = new AIFunctionsDlg(m_projects, proj, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    });
    connect(m_actVerifyChecksum, &QAction::triggered, this, &MainWindow::actVerifyChecksum);
    connect(m_actCorrectChecksum, &QAction::triggered, this, &MainWindow::actCorrectChecksum);
    connect(m_actTile,       &QAction::triggered, this, &MainWindow::actTileWindows);
    connect(m_actCascade,    &QAction::triggered, this, &MainWindow::actCascadeWindows);
    connect(m_actCompare,    &QAction::triggered, this, &MainWindow::actCompareProjects);
    connect(m_actPrevMap,    &QAction::triggered, this, &MainWindow::actPrevMap);
    connect(m_actNextMap,    &QAction::triggered, this, &MainWindow::actNextMap);
    connect(m_actSyncCursors, &QAction::toggled, this, [this](bool on) {
        // Re-wire every open view when sync is toggled
        for (auto *sub : m_mdi->subWindowList()) {
            auto *pv = qobject_cast<ProjectView *>(sub->widget());
            if (!pv) continue;
            auto *ww = pv->waveformWidget();
            auto *hw = pv->hexWidget();
            if (on) {
                connect(ww, &WaveformWidget::scrollSynced, this, &MainWindow::onWaveSyncScroll,
                        Qt::UniqueConnection);
                connect(ww, &WaveformWidget::zoomSynced, this, &MainWindow::onWaveSyncZoom,
                        Qt::UniqueConnection);
                if (hw) connect(hw, &HexWidget::scrollSynced, this, &MainWindow::onHexSyncScroll,
                                Qt::UniqueConnection);
                connect(pv, &ProjectView::viewSwitched, this, &MainWindow::onSyncViewSwitch,
                        Qt::UniqueConnection);
            } else {
                disconnect(ww, &WaveformWidget::scrollSynced, this, &MainWindow::onWaveSyncScroll);
                disconnect(ww, &WaveformWidget::zoomSynced,   this, &MainWindow::onWaveSyncZoom);
                if (hw) disconnect(hw, &HexWidget::scrollSynced, this, &MainWindow::onHexSyncScroll);
                disconnect(pv, &ProjectView::viewSwitched,    this, &MainWindow::onSyncViewSwitch);
            }
        }
        // When turning sync ON, snap all non-active waveforms + hex views
        // to the active project's scroll position.
        if (on) {
            auto *av = activeView();
            if (av) {
                auto *activeWW = av->waveformWidget();
                auto *activeHW = av->hexWidget();
                const int waveOff = activeWW ? activeWW->scrollOffset() : 0;
                const int hexOff  = activeHW
                    ? activeHW->verticalScrollBar()->value() * activeHW->bytesPerRow()
                    : 0;
                for (auto *sub : m_mdi->subWindowList()) {
                    auto *pv = qobject_cast<ProjectView *>(sub->widget());
                    if (!pv) continue;
                    auto *ww = pv->waveformWidget();
                    auto *hw = pv->hexWidget();
                    if (activeWW && ww && ww != activeWW)
                        ww->syncScrollTo(waveOff);
                    if (activeHW && hw && hw != activeHW)
                        hw->syncScrollTo(hexOff);
                }
            }
        }
    });

    connect(m_grpDataSize,   &QActionGroup::triggered, this, &MainWindow::onDataSizeChanged);
    connect(m_grpDisplayFmt, &QActionGroup::triggered, this, &MainWindow::onDisplayFmtChanged);
    connect(m_grpByteOrder,  &QActionGroup::triggered, this, &MainWindow::onByteOrderChanged);
    connect(m_grpSign,       &QActionGroup::triggered, this, &MainWindow::onSignChanged);
    connect(m_actHeightColors,&QAction::toggled, this, [this](bool on) {
        m_heightColors = on;
        applyDisplayFormat();
    });
    connect(m_actDifference, &QAction::toggled, this, [this](bool on) {
        m_showDiff = on;
        applyDisplayFormat();
    });
}

// ── Menu bar (WinOLS naming) ───────────────────────────────────────────────────

void MainWindow::buildMenuBar()
{
    m_menuProject = menuBar()->addMenu("");
    m_menuEdit    = menuBar()->addMenu("");
    m_menuView    = menuBar()->addMenu("");
    m_menuSel     = menuBar()->addMenu("");
    m_menuFind    = menuBar()->addMenu("");
    m_menuMisc    = menuBar()->addMenu("");
    m_menuDatalog = menuBar()->addMenu("");
    m_menuWindow  = menuBar()->addMenu("");
    m_menuHelp    = menuBar()->addMenu("&?");
    // Add Preferences immediately so it's always in the menu
    if (m_actPreferences) m_menuMisc->addAction(m_actPreferences);
}

// ── retranslateUi — rebuilds all menus and updates all translatable strings ───

void MainWindow::retranslateUi()
{
    if (!m_menuProject) return;

    // ── Stored action tooltips/texts ──────────────────────────────────
    m_actProjectMgr->setText(tr("Project Manager…"));
    m_actNew->setText(tr("New Project…"));
    m_actOpen->setText(tr("Open Project…"));
    m_actSave->setText(tr("Save"));
    m_actSaveAs->setText(tr("Save As…"));
    m_actClose->setText(tr("Close Project"));
    m_actHome->setText(tr("Home"));
    m_actImportA2L->setText(tr("Import A2L…"));
    m_actImportKP->setText(tr("Import KP…"));
    m_actImportXDF->setText(tr("Import XDF…"));
    if (m_actImportFRF) m_actImportFRF->setText(tr("Import FRF / ODX…"));
    m_actImportOLS->setText(tr("Import OLS…"));
    m_actAddVersion->setText(tr("Save Version Snapshot…"));
    m_actExport->setText(tr("Export ROM…"));
    m_actExportOLS->setText(tr("Export WinOLS Project (.ols)…"));
    m_actLinkRom->setText(tr("Link ROM to Project…"));
    m_actImportLinkedVer->setText(tr("Import ROM as Version…"));
    m_actCompareRoms->setText(tr("Compare ROM / Version…"));
    m_actCompareHex->setText(tr("Compare Hex…"));
    m_actImportMapPack->setText(tr("Import Map Pack…"));
    m_actImportCsvList->setText(tr("Import Map List (CSV)…"));
    m_actPatchEditor->setText(tr("Open Patch Script…"));
    m_actDtcManager->setText(tr("DTC Manager (A2L)…"));
    m_actDtcWizard->setText(tr("Cloud Tools (DTC && Features)…"));
    m_actAIFunctions->setText(tr("AI Functions…"));
    m_actVerifyChecksum->setText(tr("Verify Checksum"));
    m_actCorrectChecksum->setText(tr("Correct Checksum…"));
    if (m_actCmdPalette) m_actCmdPalette->setText(tr("Command Palette\u2026"));
    if (m_actPreferences) m_actPreferences->setText(tr("Settings\u2026"));
    m_actTile->setText(tr("Tile Windows"));
    m_actCascade->setText(tr("Cascade Windows"));
    m_actCompare->setText(tr("Compare Projects…"));
    m_actPrevMap->setToolTip(tr("Move cursor to previous map  (Ctrl+←)"));
    m_actNextMap->setToolTip(tr("Move cursor to next map  (Ctrl+→)"));
    m_actSyncCursors->setToolTip(tr("Sync 2D view scroll across all open projects"));
    m_actOptimize->setToolTip(tr("Optimize value range — fit colour scale to current map"));
    m_actDifference->setToolTip(tr("Show difference to original ROM data"));
    m_actIgnore->setToolTip(tr("Ignore this map (exclude from operations)"));
    m_actFactor->setToolTip(tr("Apply custom scaling factor"));
    m_actOrigFactor->setToolTip(tr("Reset to original scaling factor"));
    m_act8bit->setToolTip(tr("8-bit cell width"));
    m_act16bit->setToolTip(tr("16-bit cell width"));
    m_act32bit->setToolTip(tr("32-bit cell width"));
    m_actFloat->setToolTip(tr("32-bit float cell width"));
    m_actLo->setToolTip(tr("Low byte first (Little Endian)"));
    m_actHi->setToolTip(tr("High byte first (Big Endian)"));
    m_actSigned->setToolTip(tr("Signed integer interpretation"));
    m_actUnsigned->setToolTip(tr("Unsigned integer interpretation"));
    m_actDec->setToolTip(tr("Display values as decimal"));
    m_actHex->setToolTip(tr("Display values as hexadecimal"));
    m_actBin->setToolTip(tr("Display values as binary"));
    m_actPct->setToolTip(tr("Display values as percentage"));
    m_actHeightColors->setToolTip(tr("Turn height colours on / off"));

    // ── PR3 feature actions ───────────────────────────────────────────
    if (m_actToggleAI)        m_actToggleAI->setText(tr("AI Assistant"));
    if (m_actToggleDiff)      m_actToggleDiff->setText(tr("Differences"));
    if (m_actToggleSavepoints) m_actToggleSavepoints->setText(tr("Tuning Branches"));
    if (m_actDiffOriginal)    m_actDiffOriginal->setText(tr("Differences vs Original"));
    if (m_actValPlus1)        m_actValPlus1->setText(tr("Value +1"));
    if (m_actValMinus1)       m_actValMinus1->setText(tr("Value \u22121"));
    if (m_actChangeAbs)       m_actChangeAbs->setText(tr("Change absolute\u2026"));
    if (m_actChangeRel)       m_actChangeRel->setText(tr("Change relative\u2026"));
    if (m_actChangeSlider)    m_actChangeSlider->setText(tr("Change by slider\u2026"));
    if (m_actRoundLimit)      m_actRoundLimit->setText(tr("Round / limit values\u2026"));
    if (m_actOriginalVal)     m_actOriginalVal->setText(tr("Restore original value"));
    if (m_actInterpolate)     m_actInterpolate->setText(tr("Interpolate"));
    if (m_actSmooth)          m_actSmooth->setText(tr("Smooth"));
    if (m_actFlatten)         m_actFlatten->setText(tr("Flatten (set to mean)"));
    if (m_actAgain)           m_actAgain->setText(tr("Again"));
    if (m_actInsertComment)   m_actInsertComment->setText(tr("Insert comment\u2026"));
    if (m_actInsertMarker)    m_actInsertMarker->setText(tr("Insert marker"));
    if (m_actDeleteComment)   m_actDeleteComment->setText(tr("Delete annotation here"));
    if (m_actNextMarker)      m_actNextMarker->setText(tr("Next marker"));
    if (m_actPrevMarker)      m_actPrevMarker->setText(tr("Previous marker"));
    if (m_diffDock)           m_diffDock->setWindowTitle(tr("Differences"));
    if (m_savepointsDock)     m_savepointsDock->setWindowTitle(tr("Tuning Branches"));

    // ── Menu titles ───────────────────────────────────────────────────
    m_menuProject->setTitle(tr("&Project"));
    m_menuEdit->setTitle(tr("&Edit"));
    m_menuView->setTitle(tr("&View"));
    m_menuSel->setTitle(tr("&Selection"));
    m_menuFind->setTitle(tr("&Find"));
    m_menuMisc->setTitle(tr("&Miscellaneous"));
    if (m_menuDatalog) m_menuDatalog->setTitle(tr("&Datalog"));
    m_menuWindow->setTitle(tr("&Window"));
    // m_menuHelp stays "&?"

    // QToolBar's windowTitle is what shows in the main-window right-click
    // "show/hide toolbar" context menu. Must be retranslated on language
    // switch — bare addToolBar("...") leaves them stuck in the source lang.
    if (m_projectToolbar) m_projectToolbar->setWindowTitle(tr("Project"));
    if (m_formatToolbar)  m_formatToolbar->setWindowTitle(tr("Format"));

    // ── Project menu ──────────────────────────────────────────────────
    m_menuProject->clear();
    m_menuProject->addAction(m_actProjectMgr);
    m_menuProject->addSeparator();
    m_menuProject->addAction(m_actNew);
    m_menuProject->addAction(m_actOpen);
    m_menuProject->addSeparator();
    m_menuProject->addAction(m_actSave);
    m_menuProject->addAction(m_actSaveAs);
    m_menuProject->addSeparator();
    m_menuProject->addAction(m_actImportA2L);
    m_menuProject->addAction(m_actImportOLS);   // "Import OLS…"
    m_menuProject->addAction(m_actImportKP);
    m_menuProject->addAction(m_actImportXDF);
    m_menuProject->addAction(m_actImportFRF);
    m_menuProject->addAction(m_actExport);
    m_menuProject->addAction(m_actExportOLS);
    m_menuProject->addSeparator();
    m_menuProject->addAction(m_actAddVersion);
    {
        // Sprint H — Open Version in new window submenu (dynamic).
        // Populated lazily from the active project's versions[] every
        // time the menu is about to show, so it always reflects the
        // current state.
        QMenu *openVerMenu = m_menuProject->addMenu(tr("Open Version in New Window"));
        connect(openVerMenu, &QMenu::aboutToShow, this, [this, openVerMenu]() {
            openVerMenu->clear();
            Project *p = activeProject();
            if (!p) {
                QAction *a = openVerMenu->addAction(tr("(no active project)"));
                a->setEnabled(false);
                return;
            }
            QAction *cur = openVerMenu->addAction(tr("Current (working)"));
            connect(cur, &QAction::triggered, this, [this, p]() {
                onCloneVersionRequested(p, -1);
            });
            if (p->versions.isEmpty()) {
                QAction *a = openVerMenu->addAction(tr("(no extra versions)"));
                a->setEnabled(false);
                return;
            }
            openVerMenu->addSeparator();
            for (int i = 0; i < p->versions.size(); ++i) {
                const QString lbl = p->versions[i].name.isEmpty()
                    ? tr("Version %1").arg(i + 1)
                    : p->versions[i].name;
                QAction *a = openVerMenu->addAction(lbl);
                const int idx = i;
                connect(a, &QAction::triggered, this, [this, p, idx]() {
                    onCloneVersionRequested(p, idx);
                });
            }
        });
    }
    m_menuProject->addSeparator();
    m_menuProject->addAction(m_actLinkRom);
    m_menuProject->addAction(m_actImportLinkedVer);
#ifdef RX14_PRO_BUILD
    m_menuProject->addAction(m_actCompareRoms);
    m_menuProject->addAction(m_actCompareHex);
#endif
    m_menuProject->addSeparator();
    m_menuProject->addAction(m_actImportMapPack);
    m_menuProject->addAction(m_actImportCsvList);
    m_menuProject->addAction(m_actPatchEditor);
    m_menuProject->addAction(m_actDtcWizard);
    m_menuProject->addAction(m_actDtcManager);
#ifdef RX14_PRO_BUILD
    m_menuProject->addAction(m_actAIFunctions);
    m_menuProject->addAction(m_actVerifyChecksum);
    m_menuProject->addAction(m_actCorrectChecksum);
#endif
    m_menuProject->addSeparator();
    // Sprint D — map list export
    m_menuProject->addAction(tr("Export map list as &CSV…"),
                             this, [this]() { exportMapListCsv(); });
    m_menuProject->addAction(tr("Export map list as &JSON…"),
                             this, [this]() { exportMapListJson(); });
    m_menuProject->addAction(tr("Export XD&F (TunerPro)…"),
                             this, [this]() { actExportXdf(); });
    m_menuProject->addAction(tr("Export &KP map pack…"),
                             this, [this]() { actExportKp(); });
    m_menuProject->addAction(tr("Export &Tuning Report…"),
                             this, [this]() { exportTuningReport(); });
    m_menuProject->addSeparator();
    m_menuProject->addAction(m_actClose);
    m_menuProject->addSeparator();
    m_menuProject->addAction(tr("E&xit"), QKeySequence::Quit, this, &QWidget::close);

    // ── Edit menu ─────────────────────────────────────────────────────
    m_menuEdit->clear();
    m_menuEdit->addAction(m_actPrevMap);
    m_menuEdit->addAction(m_actNextMap);
    m_menuEdit->addSeparator();
    m_menuEdit->addAction(tr("&Find Map…"), QKeySequence::Find, this, [this]() {
        if (m_filterEdit) { m_filterEdit->setFocus(); m_filterEdit->selectAll(); }
    });

    // ── View menu ─────────────────────────────────────────────────────
    m_menuView->clear();
    m_menuView->addAction(tr("&Hex Editor"), this, [this]() {
        if (auto *v = activeView()) v->switchView(0); });
    m_menuView->addAction(tr("&Waveform"), this, [this]() {
        if (auto *v = activeView()) v->switchView(1); });
    m_menuView->addAction(tr("&3D Map"), this, [this]() {
        if (auto *v = activeView()) v->switchView(2); });
    m_menuView->addSeparator();
    m_menuView->addAction(m_actToggleAI);
    if (m_actToggleDiff)
        m_menuView->addAction(m_actToggleDiff);
    if (m_actToggleSavepoints)
        m_menuView->addAction(m_actToggleSavepoints);
    if (m_actDiffOriginal)
        m_menuView->addAction(m_actDiffOriginal);
    m_menuView->addSeparator();
    m_menuView->addAction(tr("Zoom &In"), QKeySequence::ZoomIn, this, [this]() {
        if (m_fontSize < 24 && m_fontSizeLabel) {
            ++m_fontSize;
            m_fontSizeLabel->setText(QString("%1pt").arg(m_fontSize));
            for (auto *sub : m_mdi->subWindowList())
                if (auto *pv = qobject_cast<ProjectView *>(sub->widget()))
                    pv->setFontSize(m_fontSize);
        }
    });
    m_menuView->addAction(tr("Zoom &Out"), QKeySequence::ZoomOut, this, [this]() {
        if (m_fontSize > 7 && m_fontSizeLabel) {
            --m_fontSize;
            m_fontSizeLabel->setText(QString("%1pt").arg(m_fontSize));
            for (auto *sub : m_mdi->subWindowList())
                if (auto *pv = qobject_cast<ProjectView *>(sub->widget()))
                    pv->setFontSize(m_fontSize);
        }
    });

    // ── Selection menu ────────────────────────────────────────────────
    m_menuSel->clear();
    m_menuSel->addAction(m_actSyncCursors);
    m_menuSel->addSeparator();
    m_menuSel->addAction(m_actCompare);
    m_menuSel->addSeparator();
    // Sprint A — map editing operations.  All entries operate on the
    // active view's selection (waveform / hex) or whole-map (3D).
    m_menuSel->addAction(m_actValPlus1);
    m_menuSel->addAction(m_actValMinus1);
    m_menuSel->addAction(m_actChangeAbs);
    m_menuSel->addAction(m_actChangeRel);
    m_menuSel->addAction(m_actChangeSlider);
    m_menuSel->addAction(m_actRoundLimit);
    m_menuSel->addAction(m_actOriginalVal);
    m_menuSel->addAction(m_actInterpolate);
    m_menuSel->addAction(m_actSmooth);
    m_menuSel->addAction(m_actFlatten);
    m_menuSel->addSeparator();
    m_menuSel->addAction(m_actAgain);

    // ── Find menu ─────────────────────────────────────────────────────
    m_menuFind->clear();
    m_menuFind->addAction(tr("Find &Address…"), this, [this]() {
        auto *v = activeView();
        if (!v) return;
        bool ok;
        QString s = QInputDialog::getText(this, tr("Go to Address"),
                                          tr("Address (hex or dec):"),
                                          QLineEdit::Normal, {}, &ok);
        if (!ok || s.isEmpty()) return;
        uint32_t addr = s.startsWith("0x", Qt::CaseInsensitive)
                        ? s.toUInt(nullptr, 16) : s.toUInt(nullptr, 0);
        v->goToAddress(addr);
    });
    m_menuFind->addAction(tr("Find &Value…"), this, &MainWindow::actFindValue);

    // Sprint C — annotations / markers
    m_menuFind->addSeparator();
    m_menuFind->addAction(tr("Find &Similar Files…"),
                          this, &MainWindow::actFindSimilarFiles);
    m_menuFind->addAction(tr("Catalog Tune &Suggestions…"), this, [this]() {
        Project *p = activeProject();
        if (!p || p->currentData.isEmpty()) {
            statusBar()->showMessage(
                tr("Open a project first to scan the catalog."), 4000);
            return;
        }
        auto *dlg = new legion::LegionDlg(p, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &QDialog::accepted, this, [this, dlg, p]() {
            const auto verdicts = dlg->selectedVerdicts();
            if (verdicts.isEmpty()) return;

            // Bounding box of touched bytes — single undo entry covers it.
            uint32_t lo = std::numeric_limits<uint32_t>::max(), hi = 0;
            for (const auto &v : verdicts) {
                if (v.startAddr < lo) lo = v.startAddr;
                if (v.endAddr   > hi) hi = v.endAddr;
            }
            if (hi < lo || hi >= uint32_t(p->currentData.size())) {
                statusBar()->showMessage(
                    tr("No applicable catalog suggestions for this ROM."), 4000);
                return;
            }
            const int spanLen = int(hi - lo + 1);
            const QByteArray before = p->currentData.mid(int(lo), spanLen);
            int totalChanged = 0;
            for (const auto &v : verdicts) {
                totalChanged += legion::applyVerdict(p->currentData, v);
            }
            const QByteArray after = p->currentData.mid(int(lo), spanLen);

            // Each view keeps its OWN byte buffer:
            //   - Project::currentData (we just wrote into this)
            //   - WaveformWidget::m_data (waveform-editor backing store)
            //   - HexWidget::m_data + m_modifications set
            //
            // ProjectView's existing WaveformWidget::dataModified handler
            // copies wave→project on every edit; if we skip resyncing
            // wave first, that copy would clobber the LEGION writes with
            // STALE wave bytes.  HexWidget also needs an explicit refresh
            // so the new diff-vs-original highlights (m_modifications) get
            // recomputed.
            // #4: the dialog is modeless, so the user may have switched to a
            // different project's window since it opened.  Resolve the view
            // that actually owns `p` instead of activeView(), or the refresh /
            // undo / dataChanged would land on — and could clobber — the wrong
            // project.  If p's view was closed meanwhile, pv stays null and we
            // skip the view refresh (the data write to p->currentData already
            // happened correctly).
            ProjectView *pv = nullptr;
            for (auto *sub : m_mdi->subWindowList()) {
                if (auto *cand = qobject_cast<ProjectView *>(sub->widget())) {
                    if (cand->project() == p) { pv = cand; break; }
                }
            }
            WaveformWidget *ww = pv ? pv->waveformWidget() : nullptr;
            WaveformEditor *ed = ww ? ww->editor() : nullptr;

            if (ww) ww->showROM(p->currentData, p->originalData);
            if (pv && pv->hexWidget())
                pv->hexWidget()->refreshData(p->currentData);

            // Route the undo entry through the editor so Ctrl+Z reverts
            // the whole LEGION batch in one shot.  emitModified fires
            // dataModified, but with wave now in sync the ProjectView
            // handler's memcmp short-circuits — no clobber.
            if (ed) {
                ed->submitExternal(int(lo), before, after);
            }
            p->modified = true;
            emit p->dataChanged();
            statusBar()->showMessage(
                tr("Applied %1 catalog suggestions (%2 bytes changed). Ctrl+Z to undo.")
                .arg(verdicts.size()).arg(totalChanged), 6000);
        });
        dlg->show();
        dlg->raise();
        dlg->activateWindow();
    });
    m_menuFind->addSeparator();
    m_menuFind->addAction(m_actInsertComment);
    m_menuFind->addAction(m_actInsertMarker);
    m_menuFind->addAction(m_actDeleteComment);
    m_menuFind->addAction(m_actNextMarker);
    m_menuFind->addAction(m_actPrevMarker);

    // ── Datalog menu ───────────────────────────────────────────────────
    rebuildDatalogMenu();

    // ── Miscellaneous menu ────────────────────────────────────────────
        m_menuMisc->clear();
    m_menuMisc->addAction(tr("Project &Info…"), this, &MainWindow::editProjectInfo);
    m_menuMisc->addSeparator();
    m_menuMisc->addAction(m_actAddVersion);
    m_menuMisc->addAction(m_actOptimize);
    m_menuMisc->addSeparator();
    // ── Auto-detect Maps (WinOLS-style scanner) ────────────────────────
    // NOTE: parallel agents may also add items to this menu (e.g.
    //       "Auto-detect ECU"). If you're touching this region, leave
    //       this comment marker so commits don't collide.
    m_menuMisc->addAction(tr("Auto-detect &Maps\u2026"), this,
                          &MainWindow::actAutoDetectMaps);
#ifdef RX14_PRO_BUILD
    m_menuMisc->addAction(tr("Auto-detect &ECU\u2026"), this,
                          &MainWindow::actAutoDetectEcu);
#endif
    m_actAutoScanOnLoad = m_menuMisc->addAction(tr("Auto-scan &ROM on import"));
    m_actAutoScanOnLoad->setCheckable(true);
    m_actAutoScanOnLoad->setChecked(
        rx14::appSettings().value("autoDetectMapsOnLoad", true).toBool());
    connect(m_actAutoScanOnLoad, &QAction::toggled, this, [](bool on) {
        rx14::appSettings().setValue("autoDetectMapsOnLoad", on);
    });
    m_menuMisc->addSeparator();
    // Preferences and Command Palette — placed BEFORE submenus
    if (m_actCmdPalette) m_menuMisc->addAction(m_actCmdPalette);
    if (m_actPreferences) m_menuMisc->addAction(m_actPreferences);
    m_menuMisc->addSeparator();

    // ── Auto Save submenu (VSCode-style modes) ──────────────────────────
    {
        QMenu *autoSaveMenu = m_menuMisc->addMenu(tr("Auto &Save"));
        auto *grp = new QActionGroup(autoSaveMenu);
        grp->setExclusive(true);
        const QString cur = rx14::appSettings()
                               .value("autoSaveMode", "afterDelay").toString();
        auto add = [&](const QString &label, const QString &mode,
                       const QString &tip, QAction **slot) {
            QAction *a = autoSaveMenu->addAction(label);
            a->setCheckable(true);
            a->setChecked(cur == mode);
            a->setToolTip(tip);
            grp->addAction(a);
            connect(a, &QAction::toggled, this, [this, mode](bool on) {
                if (!on) return;
                rx14::appSettings().setValue("autoSaveMode", mode);
                if (mode == "off" && m_autoSaveTimer)
                    m_autoSaveTimer->stop();
                updateAutoSaveStatus();
            });
            *slot = a;
        };
        add(tr("Off"),                       "off",
            tr("Manual save only (Ctrl+S)"),
            &m_actAutoSaveOff);
        add(tr("After Delay"),               "afterDelay",
            tr("Save 5 s after the last edit (recommended)"),
            &m_actAutoSaveDelay);
        add(tr("On Focus Change"),           "onFocusChange",
            tr("Save when switching projects"),
            &m_actAutoSaveFocus);
        add(tr("On Window Deactivate"),      "onWindowDeactivate",
            tr("Save when romHEX14 loses focus"),
            &m_actAutoSaveDeact);
    }
    m_menuMisc->addSeparator();

    // Language submenu (recreated on each retranslate to update checked state)
    m_menuLang = m_menuMisc->addMenu(tr("&Language"));
    struct LangEntry { QString code; QString label; };
    const LangEntry langs[] = {
        { "en",    "English"                                         },
        { "zh_CN", "\u7B80\u4F53\u4E2D\u6587 (Chinese Simplified)"  },
        { "es",    "Espa\u00F1ol (Spanish)"                          },
        { "th",    "\u0E20\u0E32\u0E29\u0E32\u0E44\u0E17\u0E22 (Thai)" },
    };
    auto *langGroup = new QActionGroup(m_menuLang);
    langGroup->setExclusive(true);
    const QString curLang = rx14::appSettings().value("language", "en").toString();
    for (const auto &l : langs) {
        auto *a = m_menuLang->addAction(l.label);
        a->setCheckable(true);
        a->setChecked(l.code == curLang);
        langGroup->addAction(a);
        const QString code = l.code;
        connect(a, &QAction::triggered, this, [this, code]() {
            loadLanguage(code);
        });
    }



    // ── Window menu ───────────────────────────────────────────────────
    m_menuWindow->clear();
    m_menuWindow->addAction(m_actTile);
    m_menuWindow->addAction(m_actCascade);
    m_menuWindow->addSeparator();
    m_menuWindow->addAction(m_actCompare);

    // ── Help menu ─────────────────────────────────────────────────────
    m_menuHelp->clear();
    m_menuHelp->addAction(tr("&About RX14"), this, [this]() {
        AboutDialog dlg(this);
        dlg.exec();
    });
#ifdef RX14_PRO_BUILD
    m_menuHelp->addAction(tr("Check for &Updates\u2026"), this, [this]() {
        m_updateChecker->setProperty("silent", false);
        m_updateChecker->checkForUpdates(false);
    });
    m_menuHelp->addSeparator();
    if (!m_actAccount) {
        m_actAccount = new QAction(this);
        connect(m_actAccount, &QAction::triggered, this, [this]() {
            LoginDialog dlg(this);
            dlg.exec();
        });
        connect(&ApiClient::instance(), &ApiClient::loginStateChanged,
                this, [this]() {
            const auto &api = ApiClient::instance();
            if (api.isLoggedIn())
                m_actAccount->setText(tr("&Account: %1").arg(api.userEmail()));
            else
                m_actAccount->setText(tr("&Account / Sign in\u2026"));
        });
    }
    {
        const auto &api = ApiClient::instance();
        if (api.isLoggedIn())
            m_actAccount->setText(tr("&Account: %1").arg(api.userEmail()));
        else
            m_actAccount->setText(tr("&Account / Sign in\u2026"));
    }
    m_menuHelp->addAction(m_actAccount);
#endif

    // ── Left panel ────────────────────────────────────────────────────
    if (m_leftPanelTitle)
        m_leftPanelTitle->setText(tr("Map Selection"));
    if (m_filterEdit)
        m_filterEdit->setPlaceholderText(tr("Filter maps…"));
    if (m_projectTree)
        m_projectTree->setHeaderLabels({tr("Name"), tr("Address"), tr("ID"),
                                        tr("Type"), tr("Size")});

    // ── AI Translate button ───────────────────────────────────────────
    if (m_btnTranslateAll)
        m_btnTranslateAll->setText(tr("✦ AI Translate"));

    // ── Filter chips ──────────────────────────────────────────────────
    if (m_chipAll)      m_chipAll->setText(tr("All"));
    if (m_chipModified) m_chipModified->setText(tr("Modified"));
    if (m_chipStarred)  m_chipStarred->setText(tr("Starred"));
    if (m_chipRecent)   m_chipRecent->setText(tr("Recent"));
    if (m_chipValue)    m_chipValue->setText(tr("Values"));
    if (m_chipCurve)    m_chipCurve->setText(tr("Curves"));
    if (m_chipMap)      m_chipMap->setText(tr("Maps"));

    // ── Translatable toolbar icons ────────────────────────────────────
    // These icon labels change with language (e.g. "V+" → "版本+" in Chinese)
    {
        const QColor cFile("" + AppConfig::instance().colors.uiText.name() + ""), cNav("" + AppConfig::instance().colors.uiAccent.lighter(140).name() + ""), cEnd("#d2a641");
        //: Toolbar icon label for "New Project" (keep very short, 2-3 chars)
        m_actNew->setIcon(makeIcon(tr("N+"), cFile));
        //: Toolbar icon label for "Add Version" (keep very short, 2-3 chars)
        m_actAddVersion->setIcon(makeIcon(tr("V+", "toolbar icon"), cNav));
        //: Toolbar icon label for "Export ROM" (keep very short, 2-3 chars)
        m_actExport->setIcon(makeIcon(tr("EXP", "toolbar icon"), cFile));
        //: Toolbar icon label for "Little Endian" byte order (keep very short)
        m_actLo->setIcon(makeIcon(tr("LE", "toolbar icon"), cEnd));
        //: Toolbar icon label for "Big Endian" byte order (keep very short)
        m_actHi->setIcon(makeIcon(tr("BE", "toolbar icon"), cEnd));
    }

    refreshProjectTree();

    // Update banner — retranslate live (skipped while downloading, to preserve
    // the transient progress/status text shown during the install flow).
    if (m_updateBtn)
        m_updateBtn->setText(tr("Update Now"));
    if (m_updateLabel && !m_updateVersion.isEmpty()
        && m_updateProgress && !m_updateProgress->isVisible()) {
        m_updateLabel->setText(tr("Update available: <b>v%1</b> — %2")
                               .arg(m_updateVersion, m_updateChangelog));
    }

    // Status bar — reset to localized idle message. Any transient status
    // (e.g. "Project saved: …") is ephemeral and fine to replace on language
    // switch; long-running operations re-post their own progress on next tick.
    statusBar()->showMessage(tr("Ready  —  Open a ROM file or project to begin."));

    // Welcome page — built before loadLanguage() during construction, so its
    // tr() strings were captured untranslated. Rebuild it whenever language
    // changes so it always reflects the active locale. Cheap (only called on
    // language switch and once at startup).
    if (m_centralStack) {
        const int prevIdx = m_centralStack->currentIndex();
        if (m_welcomePage) {
            const int wIdx = m_centralStack->indexOf(m_welcomePage);
            if (wIdx >= 0) m_centralStack->removeWidget(m_welcomePage);
            m_welcomePage->deleteLater();
            m_welcomePage = nullptr;
        }
        buildWelcomePage();
        m_centralStack->insertWidget(0, m_welcomePage);
        m_centralStack->setCurrentIndex(prevIdx);
    }
}

// ── Toolbars (icon-only, tooltip on hover) ─────────────────────────────────────

void MainWindow::buildToolBars()
{
    const QColor cFile ("" + AppConfig::instance().colors.uiText.name() + "");  // file ops — neutral
    const QColor cNav  ("" + AppConfig::instance().colors.uiAccent.lighter(140).name() + "");  // navigation — blue
    const QColor cSize ("#e3b341");  // data size   — amber
    const QColor cEnd  ("#f0883e");  // endian      — orange
    const QColor cSign ("#3fb950");  // sign        — green
    const QColor cFmt  ("#79c0ff");  // display fmt — light blue
    const QColor cOp   ("#f85149");  // operations  — red
    const QColor cVis  ("#bc8cff");  // visual      — purple

    // Assign icons — all use uniform 11pt, icon width adapts to text
    m_actHome->setIcon(makeIcon("\u2302", cFile));   // ⌂ house glyph
    m_actNew->setIcon(makeIcon("N+", cFile));
    m_actOpen->setIcon(makeIcon("\u25B6", cFile));
    m_actProjectMgr->setIcon(makeIcon("\u25A4", cFile));
    m_actSave->setIcon(makeIcon("💾", cFile));
    m_actSaveAs->setIcon(makeIcon("💾+", cFile));
    m_actClose->setIcon(makeIcon("✕", cFile));
    m_actImportA2L->setIcon(makeIcon("A2L", cNav));
    m_actImportOLS->setIcon(makeIcon("OLS", cNav));   // toolbar OLS button
    m_actImportKP->setIcon(makeIcon("KP", cNav));
    m_actImportXDF->setIcon(makeIcon("XDF", cNav));
    m_actImportCsvList->setIcon(makeIcon("CSV", cNav));
    m_actAddVersion->setIcon(makeIcon("V+", cNav));
    m_actExport->setIcon(makeIcon("EXP", cFile));
    m_actTile->setIcon(makeIcon("⊞", cFile));
    m_actCascade->setIcon(makeIcon("⧉", cFile));

    m_actPrevMap->setIcon(makeNavIcon(true,  cNav));
    m_actNextMap->setIcon(makeNavIcon(false, cNav));
    m_actSyncCursors->setIcon(makeIcon("⇔", cNav));
    m_actToggleAI->setIcon(makeIcon("AI", QColor("#a371f7")));
    m_actOptimize->setIcon(makeIcon("↕", cOp));
    m_actDifference->setIcon(makeIcon("Δ", cOp));
    m_actIgnore->setIcon(makeIcon("IGN", cOp));
    m_actFactor->setIcon(makeIcon("×K", cVis));
    m_actOrigFactor->setIcon(makeIcon("×1", cVis));
    m_actHeightColors->setIcon(makeGradientIcon());

    m_act8bit->setIcon(makeIcon("8", cSize));
    m_act16bit->setIcon(makeIcon("16", cSize));
    m_act32bit->setIcon(makeIcon("32", cSize));
    m_actFloat->setIcon(makeIcon("F", cSize));
    m_actLo->setIcon(makeIcon("LE", cEnd));
    m_actHi->setIcon(makeIcon("BE", cEnd));
    m_actSigned->setIcon(makeIcon("±", cSign));
    m_actUnsigned->setIcon(makeIcon("+", cSign));
    m_actDec->setIcon(makeIcon("10", cFmt));
    m_actHex->setIcon(makeIcon("0x", cFmt));
    m_actBin->setIcon(makeIcon("01", cFmt));
    m_actPct->setIcon(makeIcon("%", cFmt, 22));

    // ── Toolbar 1: File / Project + Navigation ─────────────────────────
    auto *tb1 = addToolBar(tr("Project"));
    m_projectToolbar = tb1;
    tb1->setObjectName("tb1");
    tb1->setMovable(false);
    tb1->setIconSize(QSize(s_iconSize, s_iconSize));
    tb1->setIconSize(QSize(s_iconSize, s_iconSize));

    tb1->addAction(m_actHome);
    tb1->addAction(m_actNew);
    tb1->addAction(m_actProjectMgr);
    tb1->addAction(m_actSave);
    tb1->addAction(m_actSaveAs);
    tb1->addSeparator();
    tb1->addAction(m_actImportA2L);
    tb1->addAction(m_actImportOLS);   // toolbar OLS button → unified import
    tb1->addAction(m_actImportKP);
    tb1->addAction(m_actImportXDF);
    tb1->addAction(m_actImportCsvList);
    tb1->addAction(m_actAddVersion);
    tb1->addSeparator();
    tb1->addAction(m_actExport);
    tb1->addSeparator();
    tb1->addAction(m_actClose);
    tb1->addSeparator();
    tb1->addAction(m_actTile);
    tb1->addAction(m_actCascade);
    tb1->addSeparator();
    tb1->addAction(m_actPrevMap);
    tb1->addAction(m_actNextMap);
    tb1->addAction(m_actSyncCursors);
    tb1->addSeparator();
    tb1->addAction(m_actToggleAI);
    tb1->addSeparator();
    tb1->addAction(m_actOptimize);


    // ── Toolbar 2: Data format ─────────────────────────────────────────
    addToolBarBreak();
    auto *tb2 = addToolBar(tr("Format"));
    m_formatToolbar = tb2;
    tb2->setObjectName("tb2");
    tb2->setMovable(false);
    tb2->setIconSize(QSize(s_iconSize, s_iconSize));
    tb2->setIconSize(QSize(s_iconSize, s_iconSize));

    // Data size
    tb2->addAction(m_act8bit);
    tb2->addAction(m_act16bit);
    tb2->addAction(m_act32bit);
    tb2->addAction(m_actFloat);
    tb2->addSeparator();

    // Byte order
    tb2->addAction(m_actLo);
    tb2->addAction(m_actHi);
    tb2->addSeparator();

    // Sign
    tb2->addAction(m_actSigned);
    tb2->addAction(m_actUnsigned);
    tb2->addSeparator();

    // Display format
    tb2->addAction(m_actDec);
    tb2->addAction(m_actHex);
    tb2->addAction(m_actBin);
    tb2->addAction(m_actPct);
    tb2->addSeparator();

    // Operations
    tb2->addAction(m_actDifference);
    tb2->addAction(m_actIgnore);
    tb2->addAction(m_actFactor);
    tb2->addAction(m_actOrigFactor);
    tb2->addSeparator();

    // Visual
    tb2->addAction(m_actHeightColors);
    tb2->addSeparator();

    // ── Font size spinner ─────────────────────────────────────────────
    auto *fontSpin = new QSpinBox;
    fontSpin->setRange(7, 24);
    fontSpin->setValue(m_fontSize);
    fontSpin->setSuffix("pt");
    fontSpin->setFixedWidth(54);
    fontSpin->setFixedHeight(22);
    fontSpin->setToolTip(tr("Hex editor font size"));
    fontSpin->setStyleSheet(
        "QSpinBox { background:" + AppConfig::instance().colors.uiPanel.name() + "; color:" + AppConfig::instance().colors.uiText.name() + ";"
        " border:1px solid " + AppConfig::instance().colors.uiBorder.name() + "; border-radius:3px; font-size:8pt; padding-left:3px; }"
        "QSpinBox:hover { border-color:" + AppConfig::instance().colors.uiAccent.lighter(140).name() + "; }"
        "QSpinBox::up-button, QSpinBox::down-button {"
        " width:14px; border:none; background:#1c2230; }"
        "QSpinBox::up-button:hover, QSpinBox::down-button:hover { background:" + AppConfig::instance().colors.uiAccent.name() + "; }"
        "QSpinBox::up-arrow   { image:none; width:0; height:0;"
        " border-left:4px solid transparent; border-right:4px solid transparent;"
        " border-bottom:5px solid " + AppConfig::instance().colors.uiText.name() + "; }"
        "QSpinBox::down-arrow { image:none; width:0; height:0;"
        " border-left:4px solid transparent; border-right:4px solid transparent;"
        " border-top:5px solid " + AppConfig::instance().colors.uiText.name() + "; }");
    tb2->addWidget(fontSpin);

    // Keep m_fontSizeLabel as a thin alias (zoom menu still uses it)
    m_fontSizeLabel = new QLabel; // unused visually, kept for zoom-menu sync
    m_fontSizeLabel->hide();

    connect(fontSpin, &QSpinBox::valueChanged, this, [this, fontSpin](int val) {
        m_fontSize = val;
        m_fontSizeLabel->setText(QString("%1pt").arg(val));
        for (auto *sub : m_mdi->subWindowList())
            if (auto *pv = qobject_cast<ProjectView *>(sub->widget()))
                pv->setFontSize(m_fontSize);
    });
}

// ── Project management ────────────────────────────────────────────────────────

QMdiSubWindow *MainWindow::openProject(Project *project)
{
    QElapsedTimer __op_t; __op_t.start();
    auto __step = [&__op_t](const char *what) {
        qCInfo(catFind) << "  openProject:" << what << __op_t.restart() << "ms";
    };
    // Re-show existing window if project already loaded
    for (auto *sub : m_mdi->subWindowList()) {
        auto *pv = qobject_cast<ProjectView *>(sub->widget());
        if (pv && pv->project() == project) {
            sub->show();
            m_mdi->setActiveSubWindow(sub);
            return sub;
        }
    }

    if (!m_projects.contains(project))
        m_projects.append(project);

    // Make the QMdiArea page visible BEFORE adding+showing the subwindow.
    // On the 0->1 transition the central stack is still on the placeholder
    // page, so the QMdiArea is hidden and zero-sized; adding+show()ing a
    // subwindow into it and only then flipping the stack triggers a resize
    // storm on a freshly-shown subwindow that has crashed inside
    // QMdiArea::resizeEvent.  Switching the page first gives the area a real
    // geometry up front.
    updateCentralPage();

    auto *view = new ProjectView();
    view->loadProject(project);
    __step("loadProject");

    auto *sw = m_mdi->addSubWindow(view);
    sw->setAttribute(Qt::WA_DeleteOnClose, false);
    sw->installEventFilter(this);
    sw->setWindowTitle(project->fullTitle());
    sw->resize(920, 640);
    sw->show();
    __step("addSubWindow+show");

    connect(view, &ProjectView::mapActivated,
            this, &MainWindow::onMapActivated);
    connect(view, &ProjectView::statusMessage,
            this, &MainWindow::onStatusMessage);
    connect(view, &ProjectView::cloneVersionRequested,
            this, &MainWindow::onCloneVersionRequested);
    QPointer<QMdiSubWindow> safeSw(sw);
    QPointer<Project> safeProject(project);
    connect(project, &Project::dataChanged, sw, [this, safeSw, safeProject]() {
        if (!safeSw || !safeProject) return;
        safeSw->setWindowTitle(safeProject->modified
            ? safeProject->fullTitle() + "  *"
            : safeProject->fullTitle());
        refreshProjectTree();
    });

    // Debounced autosave on any mutation (data edits, version snapshots,
    // linked-ROM additions). Qt::UniqueConnection guards against duplicate
    // wiring if openProject() is invoked more than once for the same project.
    connect(project, &Project::dataChanged,       this, &MainWindow::scheduleAutoSave, Qt::UniqueConnection);
    connect(project, &Project::versionsChanged,   this, &MainWindow::scheduleAutoSave, Qt::UniqueConnection);
    connect(project, &Project::linkedRomsChanged, this, &MainWindow::scheduleAutoSave, Qt::UniqueConnection);

    // Differences panel: re-run the diff scan whenever this project's data
    // changes.  Must be a member function so Qt::UniqueConnection can verify
    // duplicate detection — lambdas trigger an assertion failure here.
    connect(project, &Project::dataChanged,
            this, &MainWindow::onProjectDataChangedForDiff,
            Qt::UniqueConnection);

    // Linked-ROM child → parent sync. When this project is a linked ROM whose
    // parent is still alive in m_projects, mirror every byte change back into
    // the parent's linkedRoms[idx].data. This is what makes "Save parent
    // project" actually persist the user's edits to a linked ROM.
    //
    // No Qt::UniqueConnection — Qt forbids that flag with lambda slots
    // (asserts in debug). The early-return at the top of openProject() that
    // re-shows existing subwindows prevents this branch from running twice
    // for the same project, so duplicate connections aren't possible.
    if (project->isLinkedRom && project->parentProject
        && project->parentLinkedIndex >= 0) {
        connect(project, &Project::dataChanged, this, [project]() {
            Project *parent = project->parentProject;
            const int idx   = project->parentLinkedIndex;
            if (!parent) return;
            if (idx < 0 || idx >= parent->linkedRoms.size()) return;
            if (parent->linkedRoms[idx].data == project->currentData) return;
            parent->linkedRoms[idx].data = project->currentData;
            parent->modified = true;
            emit parent->linkedRomsChanged();   // triggers parent autosave
        });
    }

    // If sync is already on, wire this new project's signals immediately
    if (m_actSyncCursors && m_actSyncCursors->isChecked()) {
        connect(view->waveformWidget(), &WaveformWidget::scrollSynced,
                this, &MainWindow::onWaveSyncScroll, Qt::UniqueConnection);
        connect(view->waveformWidget(), &WaveformWidget::zoomSynced,
                this, &MainWindow::onWaveSyncZoom, Qt::UniqueConnection);
        if (auto *hw = view->hexWidget())
            connect(hw, &HexWidget::scrollSynced,
                    this, &MainWindow::onHexSyncScroll, Qt::UniqueConnection);
        connect(view, &ProjectView::viewSwitched,
                this, &MainWindow::onSyncViewSwitch, Qt::UniqueConnection);
    }

    // Hex / 3D view right-click "Edit ..." submenus emit editOpRequested(int)
    // — route through the same dispatcher used by the global Selection
    // menu so all paths converge on one applyEditOp() / one undo stack.
    // NOTE: must be a member-function slot, not a lambda.  Qt::UniqueConnection
    // requires it (qobject.h:263) and a lambda there crashes with
    // QtFatalMsg.
    if (auto *hw = view->hexWidget()) {
        connect(hw, &HexWidget::editOpRequested,
                this, &MainWindow::onEditOpRequestedFromView,
                Qt::UniqueConnection);
    }
    if (auto *m3 = view->map3dWidget()) {
        connect(m3, &Map3DWidget::editOpRequested,
                this, &MainWindow::onEditOpRequestedFromView,
                Qt::UniqueConnection);
    }

    // Inherit current "Diff vs Original" overlay state — Sprint B.
    view->setShowOriginalDiffOverlay(m_showOriginalDiff);
    __step("connect+overlay");

    refreshProjectTree();
    __step("refreshProjectTree");
    refreshRecentMapsStrip();
    __step("refreshRecentMapsStrip");
    broadcastAvailableProjects();
    __step("broadcastAvailableProjects");

    // Mark this project for full group-expansion on the next tree rebuild.
    // refreshProjectTreeNow() consumes the flag so the FIRST rebuild after
    // open shows everything expanded; subsequent rebuilds preserve whatever
    // the user has manually collapsed/expanded since.
    if (!project->isLinkedRom)
        m_expandAllOnNextBuild.insert(project);

    // When opening a parent project that has previously-saved linked ROMs,
    // auto-spawn each one as its own MDI subwindow so the user sees the same
    // workspace they had when they last saved. Skip if this IS a linked ROM
    // (recursive linking is not a concept here) or if any spawned linked ROM
    // is already open in a sibling subwindow.
    if (!project->isLinkedRom && !project->linkedRoms.isEmpty()) {
        // Defer to next event loop iteration so the parent's subwindow is
        // fully shown before we tile + resize.
        QTimer::singleShot(0, this, [this, project]() {
            spawnLinkedRomSubwindowsFor(project);
            // After children are added, retile so all subwindows fill the MDI
            // area side-by-side (mirrors the behavior of actLinkRom). A second
            // singleShot lets all addSubWindow / show calls flush first.
            QTimer::singleShot(0, this, [this]() {
                retileWindows();
                if (m_actSyncCursors && !m_actSyncCursors->isChecked())
                    m_actSyncCursors->setChecked(true);
            });
        });
    }

    return sw;
}

void MainWindow::spawnLinkedRomSubwindowsFor(Project *parent)
{
    if (!parent || parent->isLinkedRom) return;

    // For each persisted linked-ROM entry, build a child Project (mirroring
    // the construction in actLinkRom) and openProject() it — unless one is
    // already open for this parent at this index.
    for (int i = 0; i < parent->linkedRoms.size(); ++i) {
        const LinkedRom &lr = parent->linkedRoms[i];

        // Skip if a child for this parent + index is already open.
        bool alreadyOpen = false;
        for (Project *p : m_projects) {
            if (p->isLinkedRom
                && p->parentProject == parent
                && p->parentLinkedIndex == i) {
                alreadyOpen = true;
                break;
            }
        }
        if (alreadyOpen) continue;

        auto *child = new Project(this);
        child->name              = lr.label;
        child->brand             = parent->brand;
        child->model             = parent->model;
        child->ecuType           = parent->ecuType;
        child->displacement      = parent->displacement;
        child->year              = parent->year;
        child->notes             = parent->notes;
        child->byteOrder         = parent->byteOrder;
        child->baseAddress       = parent->baseAddress;
        child->currentData       = lr.data;
        child->originalData      = lr.data;
        child->a2lContent        = parent->a2lContent;
        child->groups            = parent->groups;
        child->isLinkedRom       = true;
        child->isLinkedReference = lr.isReference;
        child->linkedToProjectPath = parent->filePath;
        child->linkedFromData    = parent->currentData;
        child->parentProject     = parent;
        child->parentLinkedIndex = i;

        // Build per-map info using the saved offsets if present.
        for (const MapInfo &m : parent->maps) {
            MapInfo nm = m;
            nm.linkConfidence = lr.mapConfidence.value(m.name, 0);
            if (lr.mapOffsets.contains(m.name)) {
                const uint32_t linkedAddr = lr.mapOffsets[m.name];
                const int64_t  delta = (int64_t)linkedAddr - (int64_t)m.address;
                nm.address    = linkedAddr;
                nm.rawAddress = (uint32_t)((int64_t)m.rawAddress + delta);
            }
            child->maps.append(nm);
        }
        // child is freshly loaded from disk-snapshot — not modified.
        child->modified = false;

        m_projects.append(child);
        openProject(child);   // also wires the child→parent sync
    }
}

void MainWindow::broadcastAvailableProjects()
{
    for (auto *sub : m_mdi->subWindowList()) {
        auto *pv = qobject_cast<ProjectView *>(sub->widget());
        if (pv)
            pv->setAvailableProjects(m_projects);
    }
    if (m_diffPanel)
        m_diffPanel->setProjects(m_projects);
}

void MainWindow::loadROMIntoProject(Project *project, const QString &romPath)
{
    const QString fileName = QFileInfo(romPath).fileName();

    // ── Progress dialog ───────────────────────────────────────────────────────
    auto *progress = new QProgressDialog(this);
    progress->setWindowTitle(tr("Loading ROM"));
    progress->setLabelText(tr("Reading  %1…").arg(fileName));
    progress->setCancelButton(nullptr);          // ROM load can't be cancelled
    progress->setRange(0, 0);                    // indeterminate spinner
    progress->setMinimumWidth(400);
    progress->setWindowModality(Qt::WindowModal);
    progress->setWindowFlags(progress->windowFlags() & ~Qt::WindowContextHelpButtonHint);
    progress->show();

    // Cycle the label text through stages so the user knows work is happening
    const QStringList stages = {
        tr("Reading  %1…").arg(fileName),
        tr("Parsing ROM format…"),
        tr("Detecting ECU type…"),
        tr("Almost done…")
    };
    int stageIdx = 0;
    auto *stageTimer = new QTimer(progress);
    stageTimer->setInterval(600);
    connect(stageTimer, &QTimer::timeout, progress, [&stageIdx, &stages, progress] {
        stageIdx = (stageIdx + 1) % stages.size();
        progress->setLabelText(stages[stageIdx]);
    });
    stageTimer->start();

    // ── Run parsing on a background thread ───────────────────────────────────
    QFuture<ParsedROM> future = QtConcurrent::run([romPath]() {
        return parseROMFile(romPath);
    });

    QFutureWatcher<ParsedROM> watcher;
    QEventLoop loop;
    connect(&watcher, &QFutureWatcher<ParsedROM>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(future);
    loop.exec();   // blocks this function but keeps event loop alive (spinner animates)

    stageTimer->stop();
    progress->close();
    progress->deleteLater();

    // ── Apply result ─────────────────────────────────────────────────────────
    ParsedROM parsed = watcher.result();
    if (!parsed.ok) {
        QMessageBox::critical(this, tr("Error"),
            tr("Cannot load ROM file:\n%1\n\n%2").arg(romPath).arg(parsed.error));
        return;
    }
    project->currentData = parsed.data;
    // Snapshot original only on first ROM load (never overwrite if already set)
    if (project->originalData.isEmpty())
        project->originalData = parsed.data;
    project->baseAddress = parsed.baseAddress;
    project->romPath     = romPath;
    if (project->name.isEmpty())
        project->name = QFileInfo(romPath).baseName();
    project->modified = false;

    // Auto-detect ECU type and set byte order
    ECUDetection det = detectECU(project->currentData);
    project->byteOrder = det.byteOrder;

    // Control-module dump detection (BCM). When recognized, prefill the
    // vehicle/module metadata and reveal the pinned, read-only Module panel.
    if (module::ModuleRegistry::instance().detect(project->currentData)) {
        const module::ModuleInfo mi =
            module::ModuleRegistry::instance().parse(project->currentData);
        auto fill = [](QString &dst, const QString &src) {
            if (dst.trimmed().isEmpty() && !src.isEmpty()) dst = src;
        };
        fill(project->brand,       mi.brand);
        fill(project->model,       mi.vehicleModel);
        fill(project->vin,         mi.vin);
        fill(project->ecuProducer, mi.manufacturer);
        fill(project->ecuType,     mi.ecuLabel);
        statusBar()->showMessage(
            tr("%1 detected (%2)").arg(mi.moduleType, mi.variant), 8000);
        QMetaObject::invokeMethod(this, [this]{ refreshModulePanel(); },
                                  Qt::QueuedConnection);
    }

    // If the ROM parser gave us a base address from HEX/SREC, trust it over
    // the ECU detector heuristic (the HEX address is authoritative).
    if (parsed.baseAddress != 0)
        det.baseAddr = parsed.baseAddress;
    else
        project->baseAddress = det.baseAddr;

    // Run the 73-detector chain (ols::EcuAutoDetect) and seed any blank
    // ECU metadata fields with the result. This gives raw HEX/BIN imports
    // the same Make/ECU/HW/SW prefill that .ols imports get from the
    // project-block metadata. Detection failure is silent — empty fields
    // stay empty and the user can fill them in via the properties dialog.
    {
        // project->currentData is already the decoded flat binary (parseROMFile
        // stripped any HEX/SREC envelope). Do NOT call decodeRom again — that
        // re-decodes based on the .hex filename hint and mangles the binary,
        // moving the P010 ID block to a bogus offset so HW/SW extraction fails.
        ols::EcuDetectionResult ecuRes =
            ols::EcuAutoDetect::detect(project->currentData);
        if (ecuRes.ok) {
            // 'slots' is reserved as a Qt keyword (Q_OBJECT), so use 'fields'
            ols::EcuMetadataFields fields;
            fields.producer     = &project->ecuProducer;
            fields.ecuName      = &project->ecuType;
            fields.hwNumber     = &project->ecuNrEcu;
            fields.swNumber     = &project->ecuSwNumber;
            fields.swVersion    = &project->ecuSwVersion;
            fields.productionNo = &project->ecuNrProd;
            fields.engineCode   = &project->engineCode;
            ols::EcuAutoDetect::applyToFields(ecuRes, fields, /*overwrite=*/false);
            statusBar()->showMessage(tr("ECU detected: %1").arg(ecuRes.family), 5000);
        }
    }

    statusBar()->showMessage(
        QString("ROM loaded  |  %1 (%2)  |  Base: 0x%3  |  %4")
            .arg(det.identifier)
            .arg(parsed.format)
            .arg(project->baseAddress, 8, 16, QChar('0')).toUpper()
            .arg(det.byteOrder == ByteOrder::LittleEndian ? "LE" : "BE"),
        8000);

    // Sync toolbar byte-order toggles
    m_byteOrder = det.byteOrder;
    m_actLo->blockSignals(true); m_actHi->blockSignals(true);
    m_actLo->setChecked(det.byteOrder == ByteOrder::LittleEndian);
    m_actHi->setChecked(det.byteOrder == ByteOrder::BigEndian);
    m_actLo->blockSignals(false); m_actHi->blockSignals(false);

    // ── Automatic map auto-detection on raw ROM import ──────────────────
    // Runs the WinOLS-style MapAutoDetect scanner on a background thread,
    // populates project->autoDetectedMaps (separate from project->maps).
    // Waveform widget renders those as dashed/translucent overlays until an
    // A2L import populates project->maps — see waveformwidget.cpp's
    // m_autoMaps handling. First run shows an opt-out wizard.
    runMapAutoDetectOnImport(project);

    emit project->dataChanged();
}

// Cancel any in-flight async map scan for the given project. Used when
// the user cancels a new-project import (we don't want a background scan
// holding CPU after the project's been deleted) and also called by the
// A2L import path before populating real maps.
void MainWindow::cancelMapScan(Project *project)
{
    if (!project) return;
    if (auto *w = m_mapScanWatchers.take(project)) {
        w->disconnect(this);
        // Don't bother waiting — the orphan future runs to completion in
        // the background pool and its result is thrown away by the
        // disconnected handler.
        w->deleteLater();
    }
    if (m_scanStatusWidget && m_mapScanWatchers.isEmpty())
        m_scanStatusWidget->hide();
}

// ── Auto-scan on raw ROM import ───────────────────────────────────────────
// Lives as a member function so we can reach m_menuMisc and settings without
// plumbing pointers through. Called unconditionally from loadROMIntoProject;
// internally gated by the QSettings toggle + opt-out wizard.
void MainWindow::runMapAutoDetectOnImport(Project *project)
{
    if (!project || project->currentData.isEmpty()) return;

    QSettings s("CT14", "RX14");
    const bool wizardShown = s.value("autoDetectWizardShown", false).toBool();
    if (!wizardShown) {
        // First-run opt-out wizard.
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Auto-detect Maps"));
        dlg.setMinimumWidth(440);
        auto *lay = new QVBoxLayout(&dlg);
        lay->setSpacing(12);
        auto *body = new QLabel(
            tr("romHEX14 can automatically scan every ROM file you open and "
               "highlight the maps it finds in the 2D waveform view.\n\n"
               "This is a fallback while you haven't imported an A2L — once "
               "an A2L is loaded, the auto-detected overlays disappear and "
               "the real maps take over."),
            &dlg);
        body->setWordWrap(true);
        lay->addWidget(body);
        auto *check = new QCheckBox(
            tr("Enable automatic map scanning for every ROM I open"), &dlg);
        check->setChecked(true);
        lay->addWidget(check);
        auto *btn = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
        lay->addWidget(btn);
        QObject::connect(btn, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        dlg.exec();
        s.setValue("autoDetectMapsOnLoad", check->isChecked());
        s.setValue("autoDetectWizardShown", true);
        // Update the toggle menu entry's checked state if it exists.
        if (m_actAutoScanOnLoad)
            m_actAutoScanOnLoad->setChecked(check->isChecked());
    }
    if (!s.value("autoDetectMapsOnLoad", true).toBool()) return;

    // If a previous scan is still running for this project (e.g. user picked
    // a different ROM mid-scan), don't double-enqueue — the stale result
    // will be discarded in onMapScanFinished anyway, but also skipping the
    // re-launch avoids confusing status-bar messages.
    if (m_mapScanWatchers.contains(project)) return;

    // FULLY ASYNC: kick off the scan on a background thread and return
    // immediately. The ROM and MDI window open right away; when the
    // QFutureWatcher fires `finished()`, onMapScanFinished applies the
    // results to project->autoDetectedMaps and refreshes the tree. If an
    // A2L is imported before the scan completes, loadA2LIntoProject cancels
    // the watcher so we don't clobber real maps with stale auto-detected
    // overlays. This is "the WinOLS way" per the user's request — never
    // block the UI on a full-ROM scan.
    const QByteArray rom  = project->currentData;
    const quint32    base = project->baseAddress;
    auto *watcher =
        new QFutureWatcher<QVector<ols::MapCandidate>>(this);
    m_mapScanWatchers.insert(project, watcher);
    // Light up the permanent status-bar indicator so the user sees the
    // scan is happening. Text includes the project name to disambiguate
    // multiple concurrent scans.
    if (m_scanStatusWidget) {
        m_scanStatusLabel->setText(
            tr("Scanning  %1  for maps…")
                .arg(project->displayName()));
        m_scanStatusWidget->show();
    }
    QPointer<Project> projPtr(project);
    Project *projectKey = project;
    QFuture<QVector<ols::MapCandidate>> fut = QtConcurrent::run(
        [rom, base]() {
            ols::MapAutoDetectOptions opts;
            opts.topN = 500;   // strict mode: don't flood the overlay list (#25)
            return ols::MapAutoDetect::scan(rom, base, opts);
        });
    watcher->setFuture(fut);
    connect(watcher,
            &QFutureWatcher<QVector<ols::MapCandidate>>::finished,
            this, [this, projPtr, watcher, projectKey]() {
        m_mapScanWatchers.remove(projectKey);
        // Hide the status-bar spinner once no scans remain.
        if (m_scanStatusWidget && m_mapScanWatchers.isEmpty())
            m_scanStatusWidget->hide();
        watcher->deleteLater();
        // Project gone (closed mid-scan) or A2L already arrived → discard.
        if (!projPtr || !m_projects.contains(projPtr.data())) return;
        if (!projPtr->maps.isEmpty()) return;

        const QVector<ols::MapCandidate> candidates = watcher->result();
        QVector<MapInfo> auto_maps;
        auto_maps.reserve(candidates.size());
        const quint32 base = projPtr->baseAddress;
        for (const auto &c : candidates) {
            MapInfo m;
            m.name           = c.name;
            m.description    = tr("Auto-detected: %1")
                                   .arg(c.reason);
            m.type           = (c.height <= 1) ? QStringLiteral("CURVE")
                                               : QStringLiteral("MAP");
            m.rawAddress     = c.romAddress;
            m.address        = (c.romAddress >= base)
                                 ? c.romAddress - base : c.romAddress;
            m.dimensions.x   = int(c.width);
            m.dimensions.y   = int(c.height);
            m.dataSize       = c.cellBytes;
            m.dataSigned     = c.cellSigned;
            m.length         = m.dimensions.x * m.dimensions.y * m.dataSize;
            m.cellDataType   = (c.cellBytes == 1) ? 1
                                                  : (c.bigEndian ? 2 : 3);
            m.cellBigEndian  = c.bigEndian;
            m.linkConfidence = 100;
            auto_maps.append(m);
        }
        projPtr->autoDetectedMaps = auto_maps;
        statusBar()->showMessage(
            tr("Auto-detected %n map(s) — visible in 2D view until an A2L is imported",
               "", auto_maps.size()),
            8000);
        // Refresh tree + waveform so the new overlays show immediately.
        emit projPtr->dataChanged();
        refreshProjectTree();
    });
    statusBar()->showMessage(
        tr("Scanning ROM for maps in the background…"), 4000);
}

void MainWindow::loadA2LIntoProject(Project *project, const QString &a2lPath)
{
    QFile f(a2lPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Error"), tr("Cannot open A2L file:\n") + a2lPath);
        return;
    }
    QByteArray rawA2l = f.readAll();
    f.close();

    // DAMOS files that are actually ASAP2/A2L text parse fine here. The Bosch
    // *binary* DAMOS container has no public parser, and the industry path is
    // to convert it to A2L first — so if the file has no ASAP2 markers and
    // looks binary, guide the user instead of silently importing nothing.
    {
        const bool hasA2lMarkers = rawA2l.contains("CHARACTERISTIC")
            || rawA2l.contains("ASAP2_VERSION") || rawA2l.contains("/begin");
        int nonText = 0;
        const int probe = qMin(4096, rawA2l.size());
        for (int i = 0; i < probe; ++i) {
            const auto c = static_cast<unsigned char>(rawA2l.at(i));
            if (c == 0 || (c < 0x09) || (c > 0x0D && c < 0x20 && c != 0x1B)) ++nonText;
        }
        const bool looksBinary = probe > 0 && nonText * 20 > probe;   // >5% control bytes
        if (!hasA2lMarkers && looksBinary) {
            QMessageBox::information(this, tr("Binary DAMOS not supported"),
                tr("This looks like a binary DAMOS file, which has no open format and "
                   "cannot be read directly.\n\nConvert it to A2L (ASAP2) first — e.g. with "
                   "your OLS/DAMOS toolchain's \"DAMOS/ASAP2 export\" — then import the "
                   ".a2l here. Text-based DAMOS/A2L files import directly."));
            return;
        }
    }

    QString text = QString::fromUtf8(rawA2l);

    // Any in-flight map auto-scan would produce results AFTER real A2L maps
    // land on project->maps. The finished() handler already discards when
    // maps is non-empty, but explicitly dropping the watcher here also frees
    // us from waiting for the background future; the orphaned future runs
    // to completion and its result is thrown away.
    cancelMapScan(project);
    // Auto-detected overlays become noise the moment we have real maps.
    project->autoDetectedMaps.clear();

    m_parsingProject = project;
    m_actImportA2L->setEnabled(false);
    statusBar()->showMessage(tr("Parsing A2L file…"));

    // ── Progress dialog ───────────────────────────────────────────────────────
    const QString a2lName = QFileInfo(a2lPath).fileName();
    auto *progress = new QProgressDialog(this);
    progress->setWindowTitle(tr("Importing A2L"));
    progress->setLabelText(tr("Parsing  %1…").arg(a2lName));
    progress->setCancelButton(nullptr);
    progress->setRange(0, 0);
    progress->setMinimumWidth(420);
    progress->setWindowModality(Qt::WindowModal);
    progress->setWindowFlags(progress->windowFlags() & ~Qt::WindowContextHelpButtonHint);
    progress->show();

    // Animated stage labels
    const QStringList a2lStages = {
        tr("Parsing  %1…").arg(a2lName),
        tr("Reading RECORD_LAYOUTs…"),
        tr("Parsing CHARACTERISTIC blocks…"),
        tr("Resolving COMPU_METHODs…"),
        tr("Building map list…"),
        tr("Almost done…")
    };
    auto *a2lTimer = new QTimer(progress);
    a2lTimer->setInterval(700);
    // stage index stored in a shared_ptr so both the timer slot and the
    // finished handler can access it safely without dangling references.
    auto stageCounter = std::make_shared<int>(0);
    connect(a2lTimer, &QTimer::timeout, progress,
            [stageCounter, a2lStages = a2lStages, progress]() mutable {
        *stageCounter = (*stageCounter + 1) % a2lStages.size();
        progress->setLabelText(a2lStages[*stageCounter]);
    });
    a2lTimer->start();

    int romSize = project->currentData.size();
    // Don't pass existing base to A2L parser — let A2L detect its own base
    // The existing base may come from a BIN/HEX parser guess that doesn't match the A2L
    uint32_t knownBase = 0;

    // Use QtConcurrent but with a custom thread pool that has a large stack
    auto *pool = new QThreadPool(this);
    pool->setMaxThreadCount(1);
    pool->setStackSize(64 * 1024 * 1024); // 64 MB stack for large A2L files

    auto *watcher = new QFutureWatcher<void>(this);

    connect(watcher, &QFutureWatcher<void>::finished, this,
            [this, project, a2lPath, rawA2l, watcher, progress, a2lTimer, pool]()
    {
        a2lTimer->stop();
        progress->close();
        progress->deleteLater();
        pool->deleteLater();

        m_actImportA2L->setEnabled(true);
        m_parsingProject = nullptr;

        qDebug() << "A2L parse finished, building map list...";
        QVector<MapInfo> allMaps;
        try {
            allMaps = m_parser->getMapList();
        } catch (...) {
            qWarning() << "Exception in getMapList()";
            watcher->deleteLater();
            return;
        }
        qDebug() << QString("A2L: %1 maps found, base=0x%2")
            .arg(allMaps.size()).arg(m_parser->baseAddress(), 0, 16);

        A2LImportDialog dlg(allMaps, m_parser->groups(),
                            m_parser->baseAddress(), this);
        if (dlg.exec() != QDialog::Accepted) {
            statusBar()->showMessage(tr("A2L import cancelled."));
            watcher->deleteLater();
            return;
        }

        qDebug() << QString("A2L import accepted: %1 maps selected").arg(dlg.selectedMaps().size());

        // ── Validate maps against ROM ─────────────────────────────────
        QVector<MapInfo> selectedMaps = dlg.selectedMaps();
        const int romSize = project->currentData.size();
        const uint32_t base = m_parser->baseAddress();
        const char *romData = project->currentData.constData();
        int outOfBounds = 0;
        int nMaps = 0, nCurves = 0, nValues = 0;
        QVector<MapInfo> validMaps;
        validMaps.reserve(selectedMaps.size());
        for (auto &m : selectedMaps) {
            if (m.rawAddress < base) { outOfBounds++; continue; }
            int64_t fileOffset = (int64_t)m.rawAddress - (int64_t)base;
            int len = m.length > 0 ? m.length : m.dimensions.x * m.dimensions.y * m.dataSize;
            if (len <= 0) len = m.dataSize;
            if (fileOffset < 0 || (fileOffset + len) > romSize) { outOfBounds++; continue; }
            m.address = (uint32_t)fileOffset;
            validMaps.append(m);
            if (m.type == "MAP") nMaps++;
            else if (m.type == "CURVE") nCurves++;
            else nValues++;
        }

        // ── Multi-factor compatibility scoring ───────────────────────
        int scorePoints = 0;  // accumulated weighted points
        int scoreMax = 0;     // max possible points
        QString scoreDetails;

        // Check 1: EPK (ECU Production Key) — weight: 40 points
        // This is the most reliable indicator. Exact string match in ROM.
        {
            scoreMax += 40;
            QRegularExpression epkRe(R"re(EPK\s+"([^"]+)")re");
            auto epkM = epkRe.match(QString::fromLatin1(rawA2l.left(200000)));
            if (epkM.hasMatch()) {
                QByteArray epkBytes = epkM.captured(1).toLatin1();
                if (project->currentData.contains(epkBytes)) {
                    scorePoints += 40;
                    scoreDetails += tr("EPK \"%1\" found in ROM").arg(epkM.captured(1)) + " \u2713\n";
                } else {
                    // EPK exists but doesn't match — strong negative signal
                    scoreDetails += tr("EPK \"%1\" NOT found in ROM").arg(epkM.captured(1)) + " \u2717\n";
                }
            } else {
                scoreDetails += tr("No EPK in A2L (skipped)") + "\n";
                scoreMax -= 40;
            }
        }

        // Check 2: STD_AXIS axis count headers — weight: 30 points (proportional)
        // For STD_AXIS maps, the first byte at the map address should be the axis count.
        // Score proportionally based on match percentage.
        {
            scoreMax += 30;
            int axisMatches = 0, axisSamples = 0;
            for (const auto &m : validMaps) {
                if (m.type != "MAP" && m.type != "CURVE") continue;
                if (m.dimensions.x <= 1) continue;
                if (m.mapDataOffset == 0) continue;
                int off = (int)m.address;
                if (off + 2 > romSize) continue;
                uint8_t b0 = (uint8_t)romData[off];
                if (b0 == m.dimensions.x || b0 == m.dimensions.y) axisMatches++;
                axisSamples++;
                if (axisSamples >= 200) break;
            }
            if (axisSamples > 10) {
                int pct = 100 * axisMatches / axisSamples;
                scorePoints += 30 * pct / 100; // proportional
                scoreDetails += tr("Axis header check: %1% match (%2/%3)")
                    .arg(pct).arg(axisMatches).arg(axisSamples);
                scoreDetails += (pct > 50) ? " \u2713\n" : " \u2717\n";
            } else {
                scoreDetails += tr("Axis header check: skipped (no STD_AXIS maps)") + "\n";
                scoreMax -= 30;
            }
        }

        // Check 3: MAP data smoothness — weight: 30 points (proportional)
        // Real calibration data has gradual transitions between adjacent cells.
        // Random code bytes at wrong addresses will be chaotic.
        {
            scoreMax += 30;
            int smoothMaps = 0, sampledMaps = 0;
            for (const auto &m : validMaps) {
                if (m.type != "MAP" || m.dimensions.x < 4 || m.dimensions.y < 4) continue;
                int off = (int)(m.address + m.mapDataOffset);
                int cells = m.dimensions.x * m.dimensions.y;
                int len = cells * m.dataSize;
                if (off + len > romSize) continue;
                // Check smoothness: count how many adjacent cell pairs have small differences
                int smooth = 0, total = 0;
                for (int i = 0; i < cells - 1 && i < 64; i++) {
                    int64_t v0 = 0, v1 = 0;
                    for (int b = 0; b < m.dataSize; b++) {
                        v0 |= ((uint8_t)romData[off + i * m.dataSize + b]) << (8 * b);
                        v1 |= ((uint8_t)romData[off + (i+1) * m.dataSize + b]) << (8 * b);
                    }
                    int64_t diff = v1 - v0;
                    if (diff < 0) diff = -diff;
                    int64_t maxVal = (1LL << (m.dataSize * 8)) - 1;
                    if (diff < maxVal / 4) smooth++; // adjacent values within 25% of range
                    total++;
                }
                if (total > 0 && smooth * 100 / total > 60) smoothMaps++;
                sampledMaps++;
                if (sampledMaps >= 50) break;
            }
            if (sampledMaps > 5) {
                int pct = 100 * smoothMaps / sampledMaps;
                scorePoints += 30 * pct / 100; // proportional
                scoreDetails += tr("MAP data smoothness: %1% (%2/%3 maps)")
                    .arg(pct).arg(smoothMaps).arg(sampledMaps);
                scoreDetails += (pct > 50) ? " \u2713\n" : " \u2717\n";
            } else {
                scoreDetails += tr("MAP data smoothness: skipped (not enough MAPs)") + "\n";
                scoreMax -= 30;
            }
        }

        // Calculate overall score
        int matchScore = scoreMax > 0 ? (100 * scorePoints / scoreMax) : 0;
        bool mismatch = matchScore < 50;

        // ── Import results dialog ────────────────────────────────────────
        {
            int total = selectedMaps.size();
            int valid = validMaps.size();
            int failPct = total > 0 ? (int)(100.0 * outOfBounds / total) : 0;
            int okPct = total > 0 ? (100 - failPct) : 0;
            // mismatch already computed from matchScore above

            QDialog resultDlg(this);
            resultDlg.setWindowTitle(tr("A2L Import Results"));
            resultDlg.setMinimumWidth(460);
            auto *rLay = new QVBoxLayout(&resultDlg);
            rLay->setSpacing(12);

            // Icon + title
            auto *iconLabel = new QLabel(mismatch
                ? QString::fromUtf8("\u26A0")   // warning
                : QString::fromUtf8("\u2705")); // checkmark
            iconLabel->setStyleSheet(mismatch
                ? "font-size:36pt; color:#ff7b72;"
                : "font-size:36pt; color:#3fb950;");
            iconLabel->setAlignment(Qt::AlignCenter);
            rLay->addWidget(iconLabel);

            auto *titleLabel = new QLabel(mismatch
                ? tr("<b>A2L does not match this ROM</b>")
                : tr("<b>A2L Import Complete</b>"));
            titleLabel->setAlignment(Qt::AlignCenter);
            titleLabel->setStyleSheet("font-size:14pt;");
            rLay->addWidget(titleLabel);

            // Stats table
            QString stats;
            stats += "<table style='margin:8px auto; font-size:10pt;'>";
            stats += tr("<tr><td style='padding:2px 12px;'>Total maps in A2L:</td><td><b>%1</b></td></tr>").arg(total);
            stats += tr("<tr><td style='padding:2px 12px; color:#3fb950;'>Valid (in ROM):</td><td><b>%1</b> (%2%)</td></tr>").arg(valid).arg(okPct);
            stats += tr("<tr><td style='padding:2px 12px; color:#ff7b72;'>Out of bounds:</td><td><b>%1</b> (%2%)</td></tr>").arg(outOfBounds).arg(failPct);
            stats += "</table>";
            if (valid > 0) {
                stats += "<table style='margin:4px auto; font-size:9pt; color:" + AppConfig::instance().colors.uiTextDim.name() + ";'>";
                stats += tr("<tr><td style='padding:1px 8px;'>MAPs:</td><td>%1</td>"
                           "<td style='padding:1px 8px;'>CURVEs:</td><td>%2</td>"
                           "<td style='padding:1px 8px;'>VALUEs:</td><td>%3</td></tr>")
                    .arg(nMaps).arg(nCurves).arg(nValues);
                stats += "</table>";
            }
            auto *statsLabel = new QLabel(stats);
            statsLabel->setAlignment(Qt::AlignCenter);
            rLay->addWidget(statsLabel);

            // Compatibility score
            {
                QString scoreColor = matchScore >= 75 ? "#3fb950" : matchScore >= 50 ? "#d29a22" : "#ff7b72";
                QString scoreEmoji = matchScore >= 75 ? "\u2705" : matchScore >= 50 ? "\u26A0" : "\u274C";
                auto *scoreWidget = new QLabel(
                    tr("<div style='background:rgba(255,255,255,0.03); border:1px solid rgba(255,255,255,0.08); "
                       "border-radius:8px; padding:12px; margin:4px 0;'>"
                       "<b style='font-size:11pt;'>%1 %2: %3%</b><br>"
                       "<pre style='color:%5; font-size:8pt; margin-top:6px;'>%4</pre>"
                       "</div>")
                    .arg(scoreEmoji)
                    .arg(tr("Compatibility"))
                    .arg(QString("<span style='color:%1'>%2</span>").arg(scoreColor).arg(matchScore))
                    .arg(scoreDetails.trimmed().toHtmlEscaped().replace("\n", "<br>")).arg(AppConfig::instance().colors.uiTextDim.name()));
                scoreWidget->setWordWrap(true);
                rLay->addWidget(scoreWidget);
            }

            // Mismatch recommendation
            if (mismatch) {
                auto *tipLabel = new QLabel(
                    tr("<div style='background:rgba(255,123,114,0.1); border:1px solid rgba(255,123,114,0.3); "
                       "border-radius:8px; padding:12px; margin:8px 0;'>"
                       "<b>%1</b><br><br>"
                       "%2<br><br>"
                       "<b>%3</b> %4"
                       "</div>")
                    .arg(tr("This A2L file does not match the loaded ROM."))
                    .arg(tr("Most map addresses point outside the ROM data, which means "
                            "this A2L was created for a different firmware version."))
                    .arg(tr("Recommendation:"))
                    .arg(tr("Import the A2L into the <i>original</i> matching ROM file first, "
                            "then use <b>Link ROM</b> to transfer the maps to this file.")));
                tipLabel->setWordWrap(true);
                rLay->addWidget(tipLabel);
            }

            // Buttons
            auto *btnRow = new QHBoxLayout();
            btnRow->addStretch();
            if (mismatch && valid > 0) {
                auto *btnForce = new QPushButton(tr("Import %1 valid maps anyway").arg(valid));
                btnForce->setStyleSheet(
                    "QPushButton{background:rgba(255,123,114,0.15);color:#ff7b72;border:1px solid rgba(255,123,114,0.3);"
                    "border-radius:8px;padding:8px 20px;font-weight:600}"
                    "QPushButton:hover{background:rgba(255,123,114,0.25)}");
                connect(btnForce, &QPushButton::clicked, &resultDlg, &QDialog::accept);
                btnRow->addWidget(btnForce);
            }
            if (mismatch) {
                auto *btnCancel = new QPushButton(tr("Cancel import"));
                btnCancel->setDefault(true);
                btnCancel->setStyleSheet(
                    "QPushButton{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #3a91d0,stop:1 #2563eb);"
                    "color:white;border:none;border-radius:8px;padding:8px 24px;font-weight:700}"
                    "QPushButton:hover{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #4da8e8,stop:1 #3b7bf5)}");
                connect(btnCancel, &QPushButton::clicked, &resultDlg, &QDialog::reject);
                btnRow->addWidget(btnCancel);
            } else {
                auto *btnOk = new QPushButton(tr("OK"));
                btnOk->setDefault(true);
                btnOk->setStyleSheet(
                    "QPushButton{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #3a91d0,stop:1 #2563eb);"
                    "color:white;border:none;border-radius:8px;padding:8px 32px;font-weight:700}"
                    "QPushButton:hover{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #4da8e8,stop:1 #3b7bf5)}");
                connect(btnOk, &QPushButton::clicked, &resultDlg, &QDialog::accept);
                btnRow->addWidget(btnOk);
            }
            rLay->addLayout(btnRow);

            if (resultDlg.exec() != QDialog::Accepted) {
                watcher->deleteLater();
                return;
            }

            if (validMaps.isEmpty()) {
                statusBar()->showMessage(tr("No valid maps to import."), 5000);
                watcher->deleteLater();
                return;
            }
        }

        project->maps        = validMaps;
        project->groups      = m_parser->groups();
        project->byteOrder   = m_parser->byteOrder();
        project->baseAddress = m_parser->baseAddress();
        project->a2lPath     = a2lPath;
        project->a2lContent  = rawA2l;   // embed full A2L for portability

        // Mark the project as modified + notify listeners — VSCode-style
        // autosave (Option A) will write the real .rx14proj on the next
        // debounced tick, no eager-save needed.
        project->modified = true;
        emit project->dataChanged();

        // Sync byte-order toolbar to imported setting
        m_byteOrder = project->byteOrder;
        m_actLo->setChecked(m_byteOrder == ByteOrder::LittleEndian);
        m_actHi->setChecked(m_byteOrder == ByteOrder::BigEndian);

        qDebug() << "Loading maps into project view...";
        for (auto *sub : m_mdi->subWindowList()) {
            auto *pv = qobject_cast<ProjectView *>(sub->widget());
            if (pv && pv->project() == project) {
                pv->loadProject(project);
                break;
            }
        }

        qDebug() << "Refreshing project tree...";
        refreshProjectTree();

        // Expand all map group folders in the tree
        std::function<void(QTreeWidgetItem*)> expandAll = [&](QTreeWidgetItem *item) {
            item->setExpanded(true);
            for (int i = 0; i < item->childCount(); i++)
                expandAll(item->child(i));
        };
        for (int i = 0; i < m_projectTree->topLevelItemCount(); i++)
            expandAll(m_projectTree->topLevelItem(i));

        // Switch project view to 2D mode and center on first map
        for (auto *sub : m_mdi->subWindowList()) {
            auto *pv = qobject_cast<ProjectView *>(sub->widget());
            if (pv && pv->project() == project) {
                pv->switchToView(1); // 1 = 2D waveform view
                if (!project->maps.isEmpty()) {
                    // Center on the first MAP-type entry, or first map if no MAPs
                    const MapInfo *firstMap = nullptr;
                    for (const auto &m : project->maps) {
                        if (m.type == "MAP") { firstMap = &m; break; }
                    }
                    if (!firstMap) firstMap = &project->maps.first();
                    pv->goToMap(*firstMap);
                }
                break;
            }
        }

        statusBar()->showMessage(
            QString("A2L imported: %1 of %2 maps loaded.")
                .arg(project->maps.size()).arg(allMaps.size()));

        watcher->deleteLater();
    });

    watcher->setFuture(QtConcurrent::run(pool, [this, text, romSize, knownBase]() {
        try {
            m_parser->parse(text, romSize, knownBase);
        } catch (const std::exception &e) {
            qWarning("A2L parse exception: %s", e.what());
        } catch (...) {
            qWarning("A2L parse unknown exception");
        }
    }));
}

// Returns true if any of the map's ROM bytes differ from the original snapshot
// .kp = WinOLS Kennfeldpaket (map pack). Unlike .ols files (full project
// containers that BECOME a new project), a .kp is a bag of map labels that
// should be APPLIED ON TOP of the currently-open project — same UX shape
// as Import A2L. Previous wiring routed this through actImportOlsProject
// which created a brand-new project, which is wrong.
void MainWindow::actImportKP(const QString &droppedPath)
{
    Project *proj = activeProject();
    if (!proj || proj->currentData.isEmpty()) {
        QMessageBox::information(this, tr("Import KP"),
            tr("Open a project with ROM data first. KP map packs are added "
               "on top of an existing project (the same way A2L files are)."));
        return;
    }

    QString path = droppedPath;
    if (path.isEmpty())
        path = QFileDialog::getOpenFileName(this,
            tr("Import KP map pack"), {},
            tr("KP map packs (*.kp);;All files (*)"));
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("Error"),
            tr("Cannot open file: %1").arg(path));
        return;
    }
    const QByteArray data = f.readAll();
    f.close();

    auto result = ols::KpImporter::importFromBytes(
        data, proj->baseAddress,
        static_cast<uint32_t>(qMax(0, proj->currentData.size())));
    if (!result.error.isEmpty()) {
        QMessageBox::critical(this, tr("Import Error"), result.error);
        return;
    }
    if (result.maps.isEmpty()) {
        QMessageBox::information(this, tr("Import KP"),
            tr("No maps found in this .kp file."));
        return;
    }

    // ── Review dialog: show the decoded maps and let the user pick which to
    // add (table + select-all/none + ROM overview), the same as before. The
    // deterministic parser already resolves addresses/dimensions, so the
    // dialog's offset defaults to 0 and passes the maps through unchanged. ──
    KPVehicleInfo kpInfo;
    kpInfo.romByteSize = static_cast<uint32_t>(proj->currentData.size());
    KPImportDlg reviewDlg(kpInfo, result.maps,
                          static_cast<int>(proj->currentData.size()),
                          proj->currentData, this);
    if (reviewDlg.exec() != QDialog::Accepted)
        return;                              // user cancelled — nothing added
    const QVector<MapInfo> chosenMaps = reviewDlg.selectedMaps();
    if (chosenMaps.isEmpty()) {
        statusBar()->showMessage(tr("Import KP: no maps selected."), 4000);
        return;
    }

    // De-dupe against existing maps by (name + address). New maps are
    // appended; same-name+addr maps are skipped so a second KP import on
    // the same project doesn't double-stack labels.
    QSet<QPair<QString, uint32_t>> existing;
    for (const auto &m : proj->maps)
        existing.insert({m.name, m.address});

    int added = 0, skipped = 0;
    QStringList addedNames;
    addedNames.reserve(chosenMaps.size());
    for (const auto &m : chosenMaps) {
        if (existing.contains({m.name, m.address})) { ++skipped; continue; }
        proj->maps.append(m);
        existing.insert({m.name, m.address});
        addedNames.append(m.name);
        ++added;
    }

    if (added == 0) {
        QMessageBox::information(this, tr("Import KP"),
            tr("All %1 selected maps were already present in the project.")
                .arg(chosenMaps.size()));
        return;
    }

    // ── Folder grouping for KP-imported maps ─────────────────────────────
    //
    // The .kp wire format does not carry per-map folder info, so we derive
    // groups heuristically by name prefix.  Scope is the newly-imported KP
    // maps only — existing proj->groups (from a prior A2L/OLS import) are
    // preserved and we append new buckets to them rather than replacing
    // the whole list.  Issue #23.
    if (!addedNames.isEmpty()) {
        QHash<QString, QStringList>        bucketsCanonical;   // key → names
        QHash<QString, QHash<QString,int>> bucketDisplays;     // key → (display→count)

        auto firstToken = [](const QString &s) -> QString {
            for (int i = 0; i < s.size(); ++i) {
                if (!s[i].isLetter())
                    return s.left(i);
            }
            return s;
        };

        for (const QString &name : addedNames) {
            const QString first = firstToken(name).trimmed();
            const QString key = first.isEmpty() ? QStringLiteral("__other__")
                                                : first.toLower();
            bucketsCanonical[key].append(name);
            if (!first.isEmpty())
                ++bucketDisplays[key][first];
        }

        // Skip group names that already exist in proj->groups so a second KP
        // import doesn't shadow / duplicate buckets the user already has.
        QSet<QString> existingGroupNames;
        for (const auto &g : proj->groups)
            existingGroupNames.insert(g.name.toLower());

        QStringList singletons;
        QVector<A2LGroup> newGroups;
        for (auto it = bucketsCanonical.constBegin();
             it != bucketsCanonical.constEnd(); ++it) {
            const QString &key = it.key();
            const QStringList &names = it.value();
            if (key == QStringLiteral("__other__") || names.size() < 2) {
                singletons += names;
                continue;
            }
            const auto &displays = bucketDisplays[key];
            QString best = key;
            int bestCount = 0;
            for (auto dit = displays.constBegin(); dit != displays.constEnd(); ++dit) {
                if (dit.value() > bestCount) {
                    bestCount = dit.value();
                    best = dit.key();
                }
            }
            if (existingGroupNames.contains(best.toLower())) {
                singletons += names;
                continue;
            }
            A2LGroup g;
            g.name = best;
            g.characteristics = names;
            newGroups.append(std::move(g));
        }
        std::sort(newGroups.begin(), newGroups.end(),
                  [](const A2LGroup &a, const A2LGroup &b) {
                      return a.name.toLower() < b.name.toLower();
                  });
        if (!singletons.isEmpty()) {
            std::sort(singletons.begin(), singletons.end(),
                      [](const QString &a, const QString &b) {
                          return a.toLower() < b.toLower();
                      });
            // Append singletons to existing "Other" if one is already there,
            // otherwise create a fresh "Other" bucket.
            A2LGroup *otherGroup = nullptr;
            for (auto &g : proj->groups) {
                if (g.name.compare(tr("Other"), Qt::CaseInsensitive) == 0) {
                    otherGroup = &g;
                    break;
                }
            }
            if (otherGroup) {
                otherGroup->characteristics += singletons;
            } else {
                A2LGroup other;
                other.name = tr("Other");
                other.characteristics = singletons;
                newGroups.append(std::move(other));
            }
        }
        proj->groups += newGroups;
    }

    proj->modified = true;
    emit proj->dataChanged();   // tree refresh + autosave

    QString msg = tr("Imported %1 maps from %2")
                      .arg(added).arg(QFileInfo(path).fileName());
    if (skipped > 0)
        msg += tr(" (%1 already present, skipped)").arg(skipped);
    statusBar()->showMessage(msg, 6000);
}

// ── WinOLS unified import (replaces legacy actImportOLS / actImportKP) ───────
void MainWindow::actImportOlsProject()
{
    QString path = QFileDialog::getOpenFileName(this,
        tr("Import WinOLS Project"), {},
        tr("WinOLS files (*.ols *.kp);;All files (*)"));
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("Error"), tr("Cannot open file: %1").arg(path));
        return;
    }
    QByteArray fileData = f.readAll();
    f.close();

    // Import via the structured pipeline
    ols::OlsImportResult result = ols::OlsImporter::importFromBytes(fileData);
    if (!result.error.isEmpty()) {
        QMessageBox::critical(this, tr("Import Error"), result.error);
        return;
    }
    if (!result.warnings.isEmpty()) {
        statusBar()->showMessage(tr("Import completed with %1 warning(s)").arg(result.warnings.size()));
    }

    // ── Multi-Version sub-project tree ──────────────────────────────────
    // OLD behaviour: pop a "Select Version" dialog and load only the chosen
    // one. NEW behaviour (see project.h "Multi-Version sub-project hierarchy"
    // for design rationale): build one parent shell + N child sub-projects
    // — never prompt the user. Single-Version files still create one flat
    // top-level project for backward compatibility.
    QVector<Project*> projects =
        ols::buildProjectsFromOlsImport(result, path, this);
    if (projects.isEmpty()) {
        QMessageBox::critical(this, tr("Import Error"),
            tr("No Versions found in WinOLS file."));
        return;
    }

    // Sync toolbar byte-order from the imported project.
    Project *primary = projects.first();
    m_byteOrder = primary->byteOrder;
    if (m_actLo) {
        m_actLo->blockSignals(true);
        m_actLo->setChecked(m_byteOrder == ByteOrder::LittleEndian);
        m_actLo->blockSignals(false);
    }
    if (m_actHi) {
        m_actHi->blockSignals(true);
        m_actHi->setChecked(m_byteOrder == ByteOrder::BigEndian);
        m_actHi->blockSignals(false);
    }

    // ── ECU auto-detect (opportunistic blank-fill, per project) ──────────
    // The .ols project block frequently has empty HwNumber/SwNumber for
    // older files. Run the 73-detector chain on each project's assembled
    // ROM and fill in blanks. Existing non-empty values are preserved
    // (overwrite=false). For multi-Version imports the parent shell has
    // no ROM bytes, so we only detect on children (where currentData
    // lives). [Coordination marker for ECU auto-detect agent: this block
    // now runs per sub-project rather than once on a single Project.]
    bool anyEcuDetected = false;
    QString detectedFamily;
    for (Project *proj : projects) {
        if (proj->currentData.isEmpty()) continue;
        ols::EcuDetectionResult ecuRes =
            ols::EcuAutoDetect::detect(proj->currentData);
        if (!ecuRes.ok) continue;
        // 'slots' is reserved as a Qt keyword (Q_OBJECT) — use 'fields'
        ols::EcuMetadataFields fields;
        fields.producer     = &proj->ecuProducer;
        fields.ecuName      = &proj->ecuType;
        fields.hwNumber     = &proj->ecuNrEcu;
        fields.swNumber     = &proj->ecuSwNumber;
        fields.swVersion    = &proj->ecuSwVersion;
        fields.productionNo = &proj->ecuNrProd;
        fields.engineCode   = &proj->engineCode;
        const int filled =
            ols::EcuAutoDetect::applyToFields(ecuRes, fields,
                                                 /*overwrite=*/false);
        if (filled > 0 && !anyEcuDetected) {
            anyEcuDetected = true;
            detectedFamily = ecuRes.family;
        }
    }
    if (anyEcuDetected)
        statusBar()->showMessage(tr("ECU detected: %1").arg(detectedFamily), 5000);

    // buildProjectsFromOlsImport returns exactly 1 Project (Version 0 = main
    // ROM; Versions 1..N-1 live inside its versions[] as snapshots).
    for (Project *p : projects)
        if (!m_projects.contains(p)) m_projects.append(p);
    if (primary) openProject(primary);
    refreshProjectTree();

    const int totalMaps = primary ? primary->maps.size() : 0;
    const int extraVersions = primary ? primary->versions.size() : 0;
    if (extraVersions > 0) {
        statusBar()->showMessage(
            tr("Imported WinOLS project: %1 — %2 maps, %3 extra version(s)")
                .arg(QFileInfo(path).fileName())
                .arg(totalMaps).arg(extraVersions));
    } else {
        statusBar()->showMessage(
            tr("Imported WinOLS project: %1 — %2 maps loaded")
                .arg(QFileInfo(path).fileName()).arg(totalMaps));
    }
}

static bool mapHasChanges(const Project *proj, const MapInfo &m)
{
    if (proj->originalData.isEmpty() || proj->currentData.isEmpty()) return false;
    const int len = m.length > 0 ? m.length
                  : m.dimensions.x * m.dimensions.y * m.dataSize;
    if (len <= 0 || m.address == 0) return false;
    if ((int)(m.address + len) > proj->currentData.size()) return false;
    if ((int)(m.address + len) > proj->originalData.size()) return false;
    return std::memcmp(proj->currentData.constData()  + m.address,
                       proj->originalData.constData() + m.address, len) != 0;
}

// ─── Welcome page v3 — local helpers (file-scope statics) ──────────────────
namespace {

// Paint a soft radial glow at the top-center of the widget — depth without
// weight. Also paints a thin bottom fade into bgRoot. The new welcome layout
// keeps using this as its root widget so the ambient backdrop carries through.
class WelcomeBackdrop : public QWidget {
public:
    using QWidget::QWidget;
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        // Base fill
        p.fillRect(rect(), QColor(Theme::bgRoot()));
        // Radial glow just below the top bar
        QRadialGradient g(QPointF(width() / 2.0, 120.0), 620.0);
        g.setColorAt(0.0, QColor(31, 111, 235, 38));   // ~15% alpha
        g.setColorAt(0.55, QColor(31, 111, 235, 10));
        g.setColorAt(1.0, QColor(31, 111, 235, 0));
        p.fillRect(rect(), g);
        // Bottom fade into bgRoot for clean scroll edge
        QLinearGradient fade(0, height() - 80, 0, height());
        fade.setColorAt(0.0, QColor(13, 17, 23, 0));
        fade.setColorAt(1.0, QColor(13, 17, 23, 230));
        p.fillRect(QRect(0, height() - 80, width(), 80), fade);
    }
};

// Click-to-trigger event filter for any widget. Mouse-release inside the
// widget rect fires the callback. Used by the drop zone, recent-project
// cards and the account chip.
class WelcomeClickRelay : public QObject {
public:
    std::function<void()> fn;
    explicit WelcomeClickRelay(QObject *p, std::function<void()> f)
        : QObject(p), fn(std::move(f)) {}
protected:
    bool eventFilter(QObject *o, QEvent *e) override {
        if (e->type() == QEvent::MouseButtonRelease) {
            auto *me = static_cast<QMouseEvent *>(e);
            if (me->button() == Qt::LeftButton) {
                auto *w = qobject_cast<QWidget *>(o);
                if (w && w->rect().contains(me->pos())) {
                    fn();
                    return true;
                }
            }
        }
        return QObject::eventFilter(o, e);
    }
};

// Resize-driven adaptive grid rebuilder. Installed on the welcome page
// widget so the recent-projects QGridLayout reflows with the window. The
// rebuild closure captures the column-count math + the cards so we just
// need to invoke it on QEvent::Resize.
class WelcomeResizeFilter : public QObject {
public:
    std::function<void(int)> rebuild;
    explicit WelcomeResizeFilter(QObject *p, std::function<void(int)> f)
        : QObject(p), rebuild(std::move(f)) {}
protected:
    bool eventFilter(QObject *o, QEvent *e) override {
        if (e->type() == QEvent::Resize) {
            auto *w = qobject_cast<QWidget *>(o);
            if (w && rebuild) rebuild(w->width());
        }
        return QObject::eventFilter(o, e);
    }
};

} // namespace

void MainWindow::buildWelcomePage()
{
    // WelcomeBackdrop paints a soft radial glow + bottom fade; root widget
    // for the page so the ambient backdrop stays consistent with v2.
    m_welcomePage = new WelcomeBackdrop();
    m_welcomePage->setAutoFillBackground(false);
    m_welcomePage->setStyleSheet(
        QString("WelcomeBackdrop { background:%1; }").arg(Theme::bgRoot()));

    auto *root = new QVBoxLayout(m_welcomePage);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── 0. Slim top bar (36px) — KEPT FROM v1 ──────────────────────────────
    auto *topBar = new QFrame();
    topBar->setFixedHeight(36);
    topBar->setStyleSheet(QString(
        "QFrame {"
        "  background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "    stop:0 " + AppConfig::instance().colors.uiPanel.name() + ", stop:1 " + AppConfig::instance().colors.uiBg.name() + ");"
        "  border:none; border-bottom:1px solid %1;"
        "}").arg(Theme::borderSubtle()));
    auto *topLay = new QHBoxLayout(topBar);
    topLay->setContentsMargins(Theme::spaceM, 0, Theme::spaceS, 0);
    topLay->setSpacing(Theme::spaceS);

    // Hex glow logo (24x24)
    auto *hexTile = new QLabel(QStringLiteral("\u2B22"));
    hexTile->setFixedSize(24, 24);
    hexTile->setAlignment(Qt::AlignCenter);
    hexTile->setStyleSheet(QString(
        "QLabel { background:%1; color:white;"
        "         border:1px solid rgba(88,166,255,0.6); border-radius:6px;"
        "         font-size:11pt; font-weight:bold; }")
        .arg(Theme::primary()));
    {
        auto *glow = new QGraphicsDropShadowEffect(hexTile);
        glow->setBlurRadius(16);
        glow->setColor(QColor(31, 111, 235, 160));
        glow->setOffset(0, 0);
        hexTile->setGraphicsEffect(glow);
    }
    topLay->addWidget(hexTile);

    // Wordmark
    auto *wordmark = new QLabel(QStringLiteral("romHEX14"));
    {
        QFont f = wordmark->font();
        f.setPointSize(11);
        f.setWeight(QFont::DemiBold);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 0.2);
        wordmark->setFont(f);
    }
    wordmark->setStyleSheet(
        QString("color:%1; background:transparent;").arg(Theme::textBright()));
    topLay->addWidget(wordmark);

    // Version pill (neutral)
    auto *versionPill = UI::makePill(QString("v") + qApp->applicationVersion(),
                                     QStringLiteral("neutral"));
    {
        QFont f = versionPill->font();
        f.setPointSize(9);
        versionPill->setFont(f);
    }
    topLay->addWidget(versionPill);

    topLay->addStretch(1);

#ifdef RX14_PRO_BUILD
    // Account chip / Sign-in button. When signed in, give the chip a subtle
    // green gradient + 1px success border so the bar feels alive.
    // Pro-only: the community edition has no account system, so no sign-in UI.
    {
        auto &api = ApiClient::instance();
        if (api.isLoggedIn()) {
            const QString email = api.userEmail();
            const QStringList mods = api.modules();
            const int modCount = mods.size();
            // Show the FULL email — there's plenty of horizontal real estate
            // in the top bar and ellipsising "test@ct14garage.com" down to
            // "test@…" hid the actually-useful info. Only truncate if the
            // email exceeds 32 chars (rare in practice).
            QString displayEmail = email;
            if (displayEmail.size() > 32)
                displayEmail = displayEmail.left(29) + QStringLiteral("\u2026");
            const QString modsText = (modCount > 0)
                ? tr("%n modules", "", modCount)
                : tr("Free");
            const QString chipText = QStringLiteral("\u25CF  ") + displayEmail
                + QStringLiteral(" \u00B7 ") + modsText;

            auto *acctBtn = new QPushButton(chipText);
            acctBtn->setCursor(Qt::PointingHandCursor);
            acctBtn->setToolTip(email);
            acctBtn->setFlat(true);
            acctBtn->setFocusPolicy(Qt::NoFocus);
            {
                QFont f = acctBtn->font();
                f.setPointSize(10);             // bumped 9→10 for legibility
                f.setWeight(QFont::DemiBold);
                acctBtn->setFont(f);
            }
            // Brighter fills + green text (#3fb950) so the chip reads at a
            // glance against the dark backdrop. Previous gradient was so
            // subtle the user couldn't see it at all.
            acctBtn->setStyleSheet(QString(
                "QPushButton {"
                "  background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
                "    stop:0 rgba(63,185,80,0.30), stop:1 rgba(63,185,80,0.12));"
                "  color:%1; border:1px solid %1; border-radius:14px;"
                "  padding:4px 12px;"
                "}"
                "QPushButton:hover {"
                "  background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
                "    stop:0 rgba(63,185,80,0.45), stop:1 rgba(63,185,80,0.20));"
                "  color:#ffffff;"
                "}"
                "QPushButton:pressed { background:rgba(63,185,80,0.40); }")
                .arg(Theme::success));
            connect(acctBtn, &QPushButton::clicked, this, [this]() {
                LoginDialog dlg(this);
                dlg.exec();
            });
            topLay->addWidget(acctBtn);
        } else {
            auto *signInBtn = UI::makeFlatButton(tr("Sign in"));
            signInBtn->setCursor(Qt::PointingHandCursor);
            signInBtn->setToolTip(tr("Sign in to your account"));
            signInBtn->setFocusPolicy(Qt::NoFocus);
            {
                QFont f = signInBtn->font();
                f.setPointSize(10);
                signInBtn->setFont(f);
            }
            connect(signInBtn, &QPushButton::clicked, this, [this]() {
                LoginDialog dlg(this);
                dlg.exec();
            });
            topLay->addWidget(signInBtn);
        }
    }
#endif // RX14_PRO_BUILD

    // Top-bar icon buttons: small inline pushbuttons. We do NOT use
    // makeFlatButton here because its #flat QSS adds 6×14px padding, which
    // would leave a 28×28 button with 0px content room and the icon-glyph
    // gets clipped to nothing. Inline style with no padding + larger size.
    auto makeTopBarIconBtn = [this](const QString &glyph,
                                    const QString &tip,
                                    std::function<void()> action) -> QPushButton * {
        auto *b = new QPushButton(glyph);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(tip);
        b->setFixedSize(32, 32);
        b->setFocusPolicy(Qt::NoFocus);  // no focus ring on welcome chrome
        b->setStyleSheet(QString(
            "QPushButton {"
            "  background:transparent; color:%1; border:1px solid transparent;"
            "  border-radius:6px; padding:0; font-size:14pt;"
            "}"
            "QPushButton:hover {"
            "  background:rgba(31,111,235,0.18); color:%2;"
            "  border:1px solid rgba(31,111,235,0.5);"
            "}"
            "QPushButton:pressed { background:rgba(31,111,235,0.30); }")
            .arg(Theme::textBright(), Theme::accent()));
        connect(b, &QPushButton::clicked, this, std::move(action));
        return b;
    };

    auto *prefsBtn = makeTopBarIconBtn(
        QStringLiteral("\u2699"), tr("Preferences"), [this]() {
            ConfigDialog dlg(this);
            dlg.exec();
            for (auto *sub : m_mdi->subWindowList())
                sub->widget()->update();
        });
    topLay->addWidget(prefsBtn);

    auto *helpBtn = makeTopBarIconBtn(
        QStringLiteral("?"), tr("Help"), []() {
            QString lang = rx14::appSettings().value("language", "en").toString();
            if (lang == QLatin1String("zh_CN") || lang == QLatin1String("zh"))
                lang = QStringLiteral("zh");
            QDesktopServices::openUrl(QUrl(
                QStringLiteral("https://romhex14.com/docs/?lang=%1").arg(lang)));
        });
    topLay->addWidget(helpBtn);

    root->addWidget(topBar);

    // ── Scrollable single-column content host ──────────────────────────────
    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(
        "QScrollArea { background:transparent; border:none; }"
        "QScrollArea > QWidget > QWidget { background:transparent; }");
    scroll->viewport()->setAutoFillBackground(false);

    auto *scrollHost = new QWidget();
    scrollHost->setAttribute(Qt::WA_TranslucentBackground);
    auto *hostLay = new QVBoxLayout(scrollHost);
    hostLay->setContentsMargins(Theme::spaceXL, 0, Theme::spaceXL, Theme::spaceL);
    hostLay->setSpacing(0);

    // ── Helper: thin horizontal divider with breathing room above + below ──
    auto addSectionDivider = [&hostLay](int spaceAbove = 16, int spaceBelow = 16) {
        hostLay->addSpacing(spaceAbove);
        auto *div = new QFrame();
        div->setFrameShape(QFrame::NoFrame);
        div->setFixedHeight(1);
        div->setMaximumWidth(640);
        div->setStyleSheet(QString("background:%1; border:none;")
                           .arg(Theme::borderSubtle()));
        auto *divWrap = new QHBoxLayout();
        divWrap->setContentsMargins(0, 0, 0, 0);
        divWrap->addStretch(1);
        divWrap->addWidget(div);
        divWrap->addStretch(1);
        hostLay->addLayout(divWrap);
        hostLay->addSpacing(spaceBelow);
    };

    // ── 1. Hero block (logo + title + subtitle + drop zone) ────────────────
    hostLay->addSpacing(56);

    auto *hexLogo = new QLabel(QStringLiteral("\u2B22"));
    hexLogo->setAlignment(Qt::AlignCenter);
    hexLogo->setAttribute(Qt::WA_TranslucentBackground);
    {
        QFont f = hexLogo->font();
        f.setPointSize(56);
        hexLogo->setFont(f);
    }
    hexLogo->setStyleSheet(
        QString("color:%1; background:transparent;").arg(Theme::accent()));
    {
        // Soft brand-blue glow around the hero hex
        auto *glow = new QGraphicsDropShadowEffect(hexLogo);
        glow->setBlurRadius(24);
        glow->setColor(QColor(88, 166, 255, 100));
        glow->setOffset(0, 0);
        hexLogo->setGraphicsEffect(glow);
    }
    hostLay->addWidget(hexLogo);

    hostLay->addSpacing(14);

    auto *titleLbl = new QLabel(tr("romHEX 14"));
    titleLbl->setAlignment(Qt::AlignCenter);
    titleLbl->setAttribute(Qt::WA_TranslucentBackground);
    {
        QFont f = titleLbl->font();
        f.setPointSize(32);
        f.setWeight(QFont::DemiBold);
        titleLbl->setFont(f);
    }
    titleLbl->setStyleSheet(
        QString("color:%1; background:transparent;").arg(Theme::textBright()));
    hostLay->addWidget(titleLbl);

    hostLay->addSpacing(4);

    auto *subtitleLbl = new QLabel(tr("AI-assisted ECU calibration"));
    subtitleLbl->setAlignment(Qt::AlignCenter);
    subtitleLbl->setAttribute(Qt::WA_TranslucentBackground);
    {
        QFont f = subtitleLbl->font();
        f.setPointSize(13);
        subtitleLbl->setFont(f);
    }
    subtitleLbl->setStyleSheet(
        QString("color:%1; background:transparent;").arg(Theme::textMuted()));
    hostLay->addWidget(subtitleLbl);

    hostLay->addSpacing(28);

    // Drop zone — primary action. Tightened to 180px so Quick Actions sits
    // visible above the fold on a 14" laptop.
    auto *dropZone = new QFrame();
    dropZone->setObjectName("welcomeDropZone");
    dropZone->setCursor(Qt::PointingHandCursor);
    dropZone->setFixedHeight(180);
    dropZone->setMaximumWidth(640);
    dropZone->setStyleSheet(QString(
        "QFrame#welcomeDropZone {"
        "  background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
        "    stop:0 transparent, stop:1 rgba(31,111,235,0.05));"
        "  border:2px dashed %1;"
        "  border-radius:%2px;"
        "}"
        "QFrame#welcomeDropZone:hover {"
        "  border:2px dashed %3;"
        "  background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
        "    stop:0 rgba(88,166,255,0.08), stop:1 rgba(31,111,235,0.16));"
        "}")
        .arg(Theme::accent())
        .arg(Theme::radiusCard)
        .arg(Theme::accentDim()));

    auto *dzLay = new QVBoxLayout(dropZone);
    dzLay->setContentsMargins(Theme::spaceL, Theme::spaceL,
                              Theme::spaceL, Theme::spaceL);
    dzLay->setSpacing(0);
    dzLay->setAlignment(Qt::AlignCenter);

    auto *dzIcon = new QLabel(QStringLiteral("\U0001F4E5"));
    dzIcon->setAlignment(Qt::AlignCenter);
    dzIcon->setAttribute(Qt::WA_TransparentForMouseEvents);
    {
        QFont f = dzIcon->font();
        f.setPointSize(32);
        dzIcon->setFont(f);
    }
    dzIcon->setStyleSheet(
        QString("color:%1; background:transparent;").arg(Theme::accent()));
    dzLay->addWidget(dzIcon);

    dzLay->addSpacing(8);

    auto *dzTitle = new QLabel(tr("Drop a ROM file to start"));
    dzTitle->setAlignment(Qt::AlignCenter);
    dzTitle->setAttribute(Qt::WA_TransparentForMouseEvents);
    {
        QFont f = dzTitle->font();
        f.setPointSize(15);
        f.setWeight(QFont::DemiBold);
        dzTitle->setFont(f);
    }
    dzTitle->setStyleSheet(
        QString("color:%1; background:transparent;").arg(Theme::textBright()));
    dzLay->addWidget(dzTitle);

    auto *dzSub = new QLabel(tr("or click to browse"));
    dzSub->setAlignment(Qt::AlignCenter);
    dzSub->setAttribute(Qt::WA_TransparentForMouseEvents);
    {
        QFont f = dzSub->font();
        f.setPointSize(11);
        f.setItalic(true);
        dzSub->setFont(f);
    }
    dzSub->setStyleSheet(
        QString("color:%1; background:transparent;").arg(Theme::textMuted()));
    dzLay->addWidget(dzSub);

    dzLay->addSpacing(8);

    auto *dzFmt = new QLabel(
        tr("Supported: .hex .bin .rom .ori .s19 .mpc"));
    dzFmt->setAlignment(Qt::AlignCenter);
    dzFmt->setAttribute(Qt::WA_TransparentForMouseEvents);
    {
        QFont f = dzFmt->font();
        f.setPointSize(10);
        dzFmt->setFont(f);
    }
    dzFmt->setStyleSheet(
        QString("color:%1; background:transparent; border:none;")
            .arg(Theme::textDim()));
    dzLay->addWidget(dzFmt);

    dropZone->installEventFilter(new WelcomeClickRelay(dropZone, [this]() {
        actNewProject();
    }));

    auto *dzOuter = new QHBoxLayout();
    dzOuter->setContentsMargins(0, 0, 0, 0);
    dzOuter->addStretch(1);
    dzOuter->addWidget(dropZone);
    dzOuter->addStretch(1);
    hostLay->addLayout(dzOuter);

    // ── Divider: Hero / QuickActions ───────────────────────────────────────
    addSectionDivider(24, 24);

    // ── 2. Quick Actions row (7 icon tiles) ────────────────────────────────
    // Each tile is its own QFrame; we install hover/press paint via stylesheet
    // and add a coloured drop-shadow on hover via QGraphicsDropShadowEffect.
    struct QATile {
        QString  glyph;
        QString  label;
        std::function<void()> action;
    };
    QVector<QATile> tiles;
    tiles.reserve(7);
    tiles.push_back({QStringLiteral("\U0001F4C2"), tr("Open"),
                     [this]() { actOpenProject(); }});
    tiles.push_back({QStringLiteral("\u2795"),     tr("New"),
                     [this]() { actNewProject(); }});
    tiles.push_back({QStringLiteral("\U0001F4CA"), tr("Manager"),
                     [this]() { actProjectManager(); }});
    tiles.push_back({QStringLiteral("\U0001F50D"), tr("Find"),
                     [this]() { actShowCommandPalette(); }});
    tiles.push_back({QStringLiteral("\u2699"),     tr("Preferences"),
                     [this]() { ConfigDialog dlg(this); dlg.exec(); }});
    tiles.push_back({QStringLiteral("\U0001F4DA"), tr("Documentation"),
                     []() {
                         QString lang = rx14::appSettings()
                             .value("language", "en").toString();
                         if (lang == QLatin1String("zh_CN")
                             || lang == QLatin1String("zh"))
                             lang = QStringLiteral("zh");
                         QDesktopServices::openUrl(QUrl(QStringLiteral(
                             "https://romhex14.com/docs/?lang=%1").arg(lang)));
                     }});
    tiles.push_back({QStringLiteral("\u2139"),     tr("About"),
                     [this]() { AboutDialog dlg(this); dlg.exec(); }});

    auto makeQuickActionTile = [this](const QATile &t) -> QFrame * {
        auto *tile = new QFrame();
        // ObjectName-scoped QSS — without this, the QFrame { ... } selector
        // cascades to every QLabel inside (QLabel IS-A QFrame in Qt's class
        // hierarchy) and each label gains the tile's border + background,
        // rendering as a small box around the icon and another around the
        // label text. Scoping to "#qaTile" stops the cascade dead.
        tile->setObjectName("qaTile");
        tile->setCursor(Qt::PointingHandCursor);
        tile->setAttribute(Qt::WA_Hover, true);
        tile->setFocusPolicy(Qt::NoFocus);   // welcome chrome — no focus ring
        // 88×88 — large enough for the 24pt icon to read but tight enough
        // that 7 tiles in a row fit comfortably within the 640px content
        // column. (96×96 last pass was too chunky; 80×80 felt cramped.)
        tile->setFixedSize(88, 88);
        tile->setStyleSheet(QString(
            "QFrame#qaTile {"
            "  background:%1; border:1px solid %2; border-radius:12px;"
            "}"
            "QFrame#qaTile:hover {"
            "  background:#1c2128; border:1px solid %3;"
            "}").arg(Theme::bgCard(), Theme::borderSubtle(), Theme::accent()));

        // Subtle resting shadow that intensifies on hover via Qt's hover state
        // changes (the effect itself is constant — re-paint changes are driven
        // by the QSS hover rule above; lift "feel" comes from the shadow).
        {
            auto *fx = new QGraphicsDropShadowEffect(tile);
            fx->setBlurRadius(14);
            fx->setColor(QColor(0, 0, 0, 90));
            fx->setOffset(0, 4);
            tile->setGraphicsEffect(fx);
        }

        auto *vl = new QVBoxLayout(tile);
        // Tighter padding so icon+label fill the 88px tile without floating
        // in dead space; centred so the visual mass sits just above the
        // mathematical centre (icon-heavy stack reads more balanced that way).
        vl->setContentsMargins(6, 8, 6, 8);
        vl->setSpacing(4);
        vl->setAlignment(Qt::AlignCenter);

        auto *icon = new QLabel(t.glyph);
        icon->setAlignment(Qt::AlignCenter);
        icon->setAttribute(Qt::WA_TransparentForMouseEvents);
        {
            QFont f = icon->font();
            f.setPointSize(24);    // 24pt: clear without dwarfing the label
            icon->setFont(f);
        }
        icon->setStyleSheet(QString("color:%1; background:transparent;")
                            .arg(Theme::accent()));
        {
            // Tiny coloured glow under the icon — soft, not garish
            auto *glow = new QGraphicsDropShadowEffect(icon);
            glow->setBlurRadius(8);
            glow->setColor(QColor(88, 166, 255, 100));
            glow->setOffset(0, 0);
            icon->setGraphicsEffect(glow);
        }
        vl->addWidget(icon, 0, Qt::AlignHCenter);

        auto *lbl = new QLabel(t.label);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setAttribute(Qt::WA_TransparentForMouseEvents);
        {
            QFont f = lbl->font();
            f.setPointSize(9);
            f.setWeight(QFont::DemiBold);
            lbl->setFont(f);
        }
        lbl->setStyleSheet(QString("color:%1; background:transparent;")
                           .arg(Theme::textBright()));
        vl->addWidget(lbl, 0, Qt::AlignHCenter);

        auto fn = t.action;
        tile->installEventFilter(new WelcomeClickRelay(tile, std::move(fn)));
        return tile;
    };

    QVector<QFrame *> qaTiles;
    qaTiles.reserve(tiles.size());
    for (const auto &t : tiles) qaTiles.push_back(makeQuickActionTile(t));

    // Two HBox rows; reflow logic moves the trailing 3 between them based on
    // viewport width. Wide → all in row1; narrow (<720) → 4 in row1, 3 in row2.
    auto *qaHost = new QWidget();
    qaHost->setAttribute(Qt::WA_TranslucentBackground);
    // 7 tiles × 88px + 6 × 10px gutters = 676px, plus a 24px slack so
    // QHBoxLayout's centered alignment can't clip the rightmost tile when
    // the parent column shrinks slightly. Drop-zone above stays at 640.
    qaHost->setMaximumWidth(720);
    auto *qaCol = new QVBoxLayout(qaHost);
    qaCol->setContentsMargins(0, 0, 0, 0);
    qaCol->setSpacing(10);

    auto *qaRow1 = new QHBoxLayout();
    qaRow1->setContentsMargins(0, 0, 0, 0);
    qaRow1->setSpacing(10);
    qaRow1->setAlignment(Qt::AlignHCenter);
    auto *qaRow2 = new QHBoxLayout();
    qaRow2->setContentsMargins(0, 0, 0, 0);
    qaRow2->setSpacing(10);
    qaRow2->setAlignment(Qt::AlignHCenter);

    auto *qaRow2Wrap = new QWidget();
    qaRow2Wrap->setAttribute(Qt::WA_TranslucentBackground);
    auto *qaRow2WrapLay = new QVBoxLayout(qaRow2Wrap);
    qaRow2WrapLay->setContentsMargins(0, 0, 0, 0);
    qaRow2WrapLay->setSpacing(0);
    qaRow2WrapLay->addLayout(qaRow2);

    qaCol->addLayout(qaRow1);
    qaCol->addWidget(qaRow2Wrap);

    auto qaReflow = [qaTiles, qaRow1, qaRow2, qaRow2Wrap](int viewportW) {
        // Wrap to a 4+3 split when the viewport narrower than the
        // single-row footprint (7 tiles + gutters + a little breathing
        // room). Bumped 720→760 to match the new tile size.
        const bool wrap = (viewportW < 760);
        // Strip both rows of widgets without destroying the tiles.
        auto strip = [](QHBoxLayout *row) {
            while (row->count() > 0) {
                QLayoutItem *it = row->takeAt(0);
                if (!it) continue;
                if (QWidget *w = it->widget()) w->setParent(nullptr);
                delete it;
            }
        };
        strip(qaRow1);
        strip(qaRow2);

        const int splitAt = wrap ? 4 : qaTiles.size();
        for (int i = 0; i < qaTiles.size(); ++i) {
            QHBoxLayout *target = (i < splitAt) ? qaRow1 : qaRow2;
            target->addWidget(qaTiles[i]);
        }
        qaRow2Wrap->setVisible(wrap);
    };
    qaReflow(m_welcomePage->width());

    auto *qaOuter = new QHBoxLayout();
    qaOuter->setContentsMargins(0, 0, 0, 0);
    qaOuter->addStretch(1);
    qaOuter->addWidget(qaHost);
    qaOuter->addStretch(1);
    hostLay->addLayout(qaOuter);

    // Resize filter for Quick Actions (separate from the grid one — they may
    // wrap at different breakpoints).
    m_welcomePage->installEventFilter(
        new WelcomeResizeFilter(m_welcomePage, qaReflow));

    // ── Divider: QuickActions / Recent Projects ────────────────────────────
    addSectionDivider(24, 16);

    // ── 3. Recent projects (always rendered; empty state if no entries) ────
    const auto entries = ProjectRegistry::instance().entries();
    if (!entries.isEmpty()) {
        // Header row (640px max width matching drop zone)
        auto *recentHdrWrap = new QWidget();
        recentHdrWrap->setAttribute(Qt::WA_TranslucentBackground);
        recentHdrWrap->setMaximumWidth(640);
        auto *hdrLay = new QHBoxLayout(recentHdrWrap);
        hdrLay->setContentsMargins(0, 0, 0, 0);
        hdrLay->setSpacing(0);

        auto *recentTitle = new QLabel(tr("Recent projects"));
        recentTitle->setAttribute(Qt::WA_TranslucentBackground);
        {
            QFont f = recentTitle->font();
            f.setPointSize(14);
            f.setWeight(QFont::DemiBold);
            recentTitle->setFont(f);
        }
        recentTitle->setStyleSheet(
            QString("color:%1; background:transparent;").arg(Theme::textBright()));
        hdrLay->addWidget(recentTitle);

        hdrLay->addStretch(1);

        auto *viewAll = new QLabel(tr("View all  \u2192"));
        viewAll->setCursor(Qt::PointingHandCursor);
        viewAll->setAttribute(Qt::WA_TranslucentBackground);
        {
            QFont f = viewAll->font();
            f.setPointSize(11);
            viewAll->setFont(f);
        }
        viewAll->setStyleSheet(QString(
            "QLabel { color:%1; background:transparent; }"
            "QLabel:hover { color:%2; }")
            .arg(Theme::accent(), Theme::accentDim()));
        viewAll->installEventFilter(
            new WelcomeClickRelay(viewAll, [this]() { actProjectManager(); }));
        hdrLay->addWidget(viewAll);

        auto *hdrOuter = new QHBoxLayout();
        hdrOuter->setContentsMargins(0, 0, 0, 0);
        hdrOuter->addStretch(1);
        hdrOuter->addWidget(recentHdrWrap);
        hdrOuter->addStretch(1);
        hostLay->addLayout(hdrOuter);

        hostLay->addSpacing(12);

        // Sort + cap to last 12
        QVector<ProjectEntry> sorted = entries;
        std::sort(sorted.begin(), sorted.end(),
                  [](const ProjectEntry &a, const ProjectEntry &b) {
                      return a.changedAt > b.changedAt;
                  });
        if (sorted.size() > 12) sorted.resize(12);

        // Relative-time formatter (today / yesterday / N days/weeks/months ago)
        auto relativeTime = [this](const QDateTime &when) -> QString {
            if (!when.isValid()) return QString();
            const qint64 secs = when.secsTo(QDateTime::currentDateTime());
            const qint64 days = secs / 86400;
            if (days <= 0) return tr("today");
            if (days == 1) return tr("yesterday");
            if (days < 7) return tr("%n days ago", "", int(days));
            if (days < 30) {
                const int w = int(days / 7);
                return tr("%n weeks ago", "", w);
            }
            const int m = int(days / 30);
            return tr("%n months ago", "", m);
        };

        // Card factory — 240x140 tile with brand logo
        auto makeProjectCard = [this, &relativeTime](
                                   const ProjectEntry &e) -> QFrame *
        {
            auto *card = new QFrame();
            // Same scoping trick as quick-action tiles: prevent the QFrame
            // border/background from cascading to every QLabel child (which
            // would render visible boxes around the project name, ECU,
            // brand-logo placeholder, and "today" footer).
            card->setObjectName("recentCard");
            card->setCursor(Qt::PointingHandCursor);
            card->setAttribute(Qt::WA_Hover, true);
            card->setFocusPolicy(Qt::NoFocus);   // welcome chrome — no focus ring
            card->setFixedSize(260, 150);        // bumped 240×140→260×150 for bigger logo
            card->setStyleSheet(QString(
                "QFrame#recentCard {"
                "  background:%1; border:1px solid %2; border-radius:12px;"
                "}"
                "QFrame#recentCard:hover {"
                "  background:#1c2128; border:1px solid %3;"
                "}").arg(Theme::bgCard(), Theme::borderSubtle(), Theme::accent()));

            // Hover-lift drop shadow (3px feel via dy=5 + larger blur)
            {
                auto *fx = new QGraphicsDropShadowEffect(card);
                fx->setBlurRadius(18);
                fx->setColor(QColor(0, 0, 0, 100));
                fx->setOffset(0, 5);
                card->setGraphicsEffect(fx);
            }

            auto *cl = new QVBoxLayout(card);
            cl->setContentsMargins(14, 14, 14, 14);
            cl->setSpacing(0);

            // Top row: brand logo (40x40) | name + subtitle stack
            auto *topRow = new QHBoxLayout();
            topRow->setContentsMargins(0, 0, 0, 0);
            topRow->setSpacing(12);

            // Brand logo placeholder — letter avatar fallback while async load
            const QString brandKey = e.brand.trimmed();
            const QString letter = brandKey.isEmpty()
                ? QStringLiteral("?")
                : brandKey.left(1).toUpper();

            auto *logo = new QLabel(letter);
            logo->setAttribute(Qt::WA_TransparentForMouseEvents);
            logo->setFixedSize(56, 56);          // bumped 40→56 for legibility
            logo->setAlignment(Qt::AlignCenter);
            logo->setScaledContents(false);
            {
                QFont f = logo->font();
                f.setPointSize(22);              // bumped 16→22 to match 56px size
                f.setWeight(QFont::DemiBold);
                logo->setFont(f);
            }
            // Soft circular placeholder — no harsh border. The accent letter
            // sits centered on a slightly-lighter chip; once the real brand
            // logo arrives via async fetch, the chip gets replaced.
            logo->setStyleSheet(QString(
                "QLabel { background:%1; color:%2; border:none; border-radius:8px; }")
                .arg(Theme::bgRoot(), Theme::accent()));
            topRow->addWidget(logo, 0, Qt::AlignTop);

            // Async fetch real brand logo. Use a QPointer so the callback is
            // safe if the welcome page is rebuilt before the fetch completes.
            if (!brandKey.isEmpty()) {
                QPointer<QLabel> safe = logo;
                BrandLogo::instance().requestLogo(brandKey,
                    [safe](const QPixmap &pm) {
                        if (!safe || pm.isNull()) return;
                        QPixmap scaled = pm.scaled(
                            56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                        // Round corners 8px to match the placeholder
                        QPixmap rounded(scaled.size());
                        rounded.fill(Qt::transparent);
                        QPainter p(&rounded);
                        p.setRenderHint(QPainter::Antialiasing);
                        QPainterPath pp;
                        pp.addRoundedRect(QRectF(0, 0, scaled.width(),
                                                 scaled.height()), 8, 8);
                        p.setClipPath(pp);
                        p.drawPixmap(0, 0, scaled);
                        p.end();
                        safe->setText(QString());
                        safe->setPixmap(rounded);
                        safe->setStyleSheet(
                            "QLabel { background:transparent; border:none; }");
                    });
            }

            auto *txt = new QVBoxLayout();
            txt->setContentsMargins(0, 0, 0, 0);
            txt->setSpacing(2);

            const QString tail = QFileInfo(e.path).fileName();
            const QString name = e.name.isEmpty() ? tail : e.name;
            auto *nameLbl = new QLabel();
            nameLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
            {
                QFont f = nameLbl->font();
                f.setPointSize(14);
                f.setWeight(QFont::DemiBold);
                nameLbl->setFont(f);
            }
            nameLbl->setStyleSheet(QString("color:%1; background:transparent;")
                                   .arg(Theme::textBright()));
            const int textW = 240 - 14 * 2 - 40 - 12;
            nameLbl->setText(QFontMetrics(nameLbl->font()).elidedText(
                                 name, Qt::ElideRight, textW));
            txt->addWidget(nameLbl);

            QString sub;
            if (!e.brand.isEmpty() && !e.ecuType.isEmpty())
                sub = e.brand + QStringLiteral(" \u00B7 ") + e.ecuType;
            else if (!e.brand.isEmpty())
                sub = e.brand;
            else if (!e.ecuType.isEmpty())
                sub = e.ecuType;
            else
                sub = tail;
            auto *subLbl = new QLabel();
            subLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
            {
                QFont f = subLbl->font();
                f.setPointSize(11);
                subLbl->setFont(f);
            }
            subLbl->setStyleSheet(QString("color:%1; background:transparent;")
                                  .arg(Theme::textMuted()));
            subLbl->setText(QFontMetrics(subLbl->font()).elidedText(
                                sub, Qt::ElideRight, textW));
            txt->addWidget(subLbl);

            topRow->addLayout(txt, 1);

            cl->addLayout(topRow);
            cl->addSpacing(8);
            cl->addStretch(1);

            // Footer: relative time
            auto *timeLbl = new QLabel(relativeTime(e.changedAt));
            timeLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
            {
                QFont f = timeLbl->font();
                f.setPointSize(10);
                f.setItalic(true);
                timeLbl->setFont(f);
            }
            timeLbl->setStyleSheet(QString("color:%1; background:transparent;")
                                   .arg(Theme::textDim()));
            cl->addWidget(timeLbl);

            const QString path = e.path;
            card->installEventFilter(
                new WelcomeClickRelay(card, [this, path]() {
                    if (path.isEmpty()) return;
                    auto *project = Project::open(path, this);
                    if (!project) {
                        QMessageBox::critical(this, tr("Error"),
                            tr("Failed to open project:\n%1").arg(path));
                        return;
                    }
                    openProject(project);
                }));
            return card;
        };

        // Adaptive grid host: rebuilt on resize via WelcomeResizeFilter.
        auto *gridHost = new QWidget();
        gridHost->setAttribute(Qt::WA_TranslucentBackground);
        auto *gridLay = new QGridLayout(gridHost);
        gridLay->setContentsMargins(0, 0, 0, 0);
        gridLay->setHorizontalSpacing(20);
        gridLay->setVerticalSpacing(20);

        QVector<QFrame *> cards;
        cards.reserve(sorted.size());
        for (const auto &e : sorted)
            cards.push_back(makeProjectCard(e));

        // Re-pack on resize. 260 = 240 card + 20 gutter.
        auto reflow = [gridLay, cards](int viewportW) {
            // Card is 260px wide + 20px gutter = 280px reserved per column.
            // qBound(1, ..., 10) keeps grid usable on narrow + ultrawide displays.
            const int cols = qBound(1, viewportW / 280, 10);
            for (QFrame *c : cards) gridLay->removeWidget(c);
            for (int i = 0; i < cards.size(); ++i)
                gridLay->addWidget(cards[i], i / cols, i % cols);
        };
        reflow(m_welcomePage->width());

        auto *gridOuter = new QHBoxLayout();
        gridOuter->setContentsMargins(0, 0, 0, 0);
        gridOuter->addStretch(1);
        gridOuter->addWidget(gridHost, 0);
        gridOuter->addStretch(1);
        hostLay->addLayout(gridOuter);

        m_welcomePage->installEventFilter(
            new WelcomeResizeFilter(m_welcomePage, std::move(reflow)));
    } else {
        // Empty state: single muted italic line
        auto *empty = new QLabel(
            tr("No projects yet \u2014 drop a ROM above to begin."));
        empty->setAlignment(Qt::AlignCenter);
        empty->setAttribute(Qt::WA_TranslucentBackground);
        {
            QFont f = empty->font();
            f.setPointSize(12);
            f.setItalic(true);
            empty->setFont(f);
        }
        empty->setStyleSheet(QString("color:%1; background:transparent;")
                             .arg(Theme::textMuted()));
        hostLay->addWidget(empty);
    }

    // ── 4. Recent Maps chip strip (only when ≥1 entry) ─────────────────────
    if (!m_recentMaps.isEmpty()) {
        addSectionDivider(24, 16);

        auto *mapsHdrWrap = new QWidget();
        mapsHdrWrap->setAttribute(Qt::WA_TranslucentBackground);
        mapsHdrWrap->setMaximumWidth(640);
        auto *mhLay = new QHBoxLayout(mapsHdrWrap);
        mhLay->setContentsMargins(0, 0, 0, 0);
        mhLay->setSpacing(0);

        auto *mapsTitle = new QLabel(tr("Recent maps"));
        mapsTitle->setAttribute(Qt::WA_TranslucentBackground);
        {
            QFont f = mapsTitle->font();
            f.setPointSize(14);
            f.setWeight(QFont::DemiBold);
            mapsTitle->setFont(f);
        }
        mapsTitle->setStyleSheet(QString("color:%1; background:transparent;")
                                 .arg(Theme::textBright()));
        mhLay->addWidget(mapsTitle);
        mhLay->addStretch(1);

        auto *mapsHdrOuter = new QHBoxLayout();
        mapsHdrOuter->setContentsMargins(0, 0, 0, 0);
        mapsHdrOuter->addStretch(1);
        mapsHdrOuter->addWidget(mapsHdrWrap);
        mapsHdrOuter->addStretch(1);
        hostLay->addLayout(mapsHdrOuter);

        hostLay->addSpacing(12);

        auto *chipsHost = new QWidget();
        chipsHost->setAttribute(Qt::WA_TranslucentBackground);
        chipsHost->setMaximumWidth(640);
        auto *chipsLay = new QHBoxLayout(chipsHost);
        chipsLay->setContentsMargins(0, 0, 0, 0);
        chipsLay->setSpacing(8);
        chipsLay->setAlignment(Qt::AlignHCenter);

        const int kMaxChips = 5;
        int added = 0;
        for (const auto &entry : m_recentMaps) {
            if (added >= kMaxChips) break;
            Project *proj = entry.first;
            const QString name = entry.second;
            if (!proj || !m_projects.contains(proj)) continue;

            QString shown = name;
            if (shown.size() > 14)
                shown = shown.left(13) + QStringLiteral("\u2026");

            auto *chip = new QFrame();
            chip->setCursor(Qt::PointingHandCursor);
            chip->setAttribute(Qt::WA_Hover, true);
            chip->setFixedHeight(32);
            chip->setMinimumWidth(120);
            chip->setStyleSheet(QString(
                "QFrame {"
                "  background:%1; border:1px solid %2; border-radius:16px;"
                "}"
                "QFrame:hover {"
                "  background:#1c2128; border:1px solid %3;"
                "}").arg(Theme::bgCard(), Theme::borderSubtle(), Theme::accent()));
            {
                auto *fx = new QGraphicsDropShadowEffect(chip);
                fx->setBlurRadius(10);
                fx->setColor(QColor(0, 0, 0, 70));
                fx->setOffset(0, 2);
                chip->setGraphicsEffect(fx);
            }
            chip->setToolTip(name + QStringLiteral("  \u2014  ")
                             + proj->displayName());

            auto *cl = new QHBoxLayout(chip);
            cl->setContentsMargins(12, 0, 14, 0);
            cl->setSpacing(6);
            cl->setAlignment(Qt::AlignVCenter);

            auto *gl = new QLabel(QStringLiteral("\u25A6"));
            gl->setAttribute(Qt::WA_TransparentForMouseEvents);
            {
                QFont f = gl->font();
                f.setPointSize(10);
                gl->setFont(f);
            }
            gl->setStyleSheet(QString("color:%1; background:transparent;")
                              .arg(Theme::accent()));
            cl->addWidget(gl);

            auto *tl = new QLabel(shown);
            tl->setAttribute(Qt::WA_TransparentForMouseEvents);
            {
                QFont f = tl->font();
                f.setPointSize(11);
                f.setWeight(QFont::DemiBold);
                tl->setFont(f);
            }
            tl->setStyleSheet(QString("color:%1; background:transparent;")
                              .arg(Theme::textBright()));
            cl->addWidget(tl);

            chip->installEventFilter(new WelcomeClickRelay(chip,
                [this, proj, name]() {
                    if (!m_projects.contains(proj)) return;
                    for (const auto &m : proj->maps) {
                        if (m.name == name) {
                            onMapActivated(m, proj);
                            return;
                        }
                    }
                    for (const auto &m : proj->autoDetectedMaps) {
                        if (m.name == name) {
                            onMapActivated(m, proj);
                            return;
                        }
                    }
                }));

            chipsLay->addWidget(chip);
            ++added;
        }

        if (added > 0) {
            auto *chipsOuter = new QHBoxLayout();
            chipsOuter->setContentsMargins(0, 0, 0, 0);
            chipsOuter->addStretch(1);
            chipsOuter->addWidget(chipsHost);
            chipsOuter->addStretch(1);
            hostLay->addLayout(chipsOuter);
        } else {
            // All recent-map entries pointed at unloaded projects; hide host
            chipsHost->deleteLater();
        }
    }

    // ── 5. Footer (single muted line, always shown) ────────────────────────
    hostLay->addSpacing(28);
    hostLay->addStretch(1);
    hostLay->addSpacing(12);
    auto *footer = new QLabel(
        tr("CT14 Garage \u00B7 Bangkok, Thailand \u00B7 \u00A9 2026"));
    footer->setAlignment(Qt::AlignCenter);
    footer->setAttribute(Qt::WA_TranslucentBackground);
    {
        QFont f = footer->font();
        f.setPointSize(8);
        footer->setFont(f);
    }
    footer->setStyleSheet(QString("color:%1; background:transparent;")
                          .arg(Theme::textDim()));
    hostLay->addWidget(footer);
    hostLay->addSpacing(12);

    scroll->setWidget(scrollHost);
    root->addWidget(scroll, 1);
}

void MainWindow::refreshProjectTree()
{
    // Debounced entry point: collapse bursts of calls into one rebuild.
    if (m_treeRefreshTimer) m_treeRefreshTimer->start();
    else refreshProjectTreeNow();
    updateCentralPage();
}

void MainWindow::updateCentralPage()
{
    if (!m_centralStack) return;
    const bool hasProjects = !m_projects.isEmpty();
    m_centralStack->setCurrentIndex(hasProjects ? 1 : 0);
    if (m_formatToolbar) m_formatToolbar->setVisible(hasProjects);
}

void MainWindow::refreshProjectTreeNow()
{
    QElapsedTimer __rpt; __rpt.start();
    qCInfo(catFind) << "refreshProjectTreeNow start; projects="
                    << m_projects.size();
    // ── Save expand-state for EVERY level of the tree, not just two
    // levels.  Old code captured only top-level groups + their direct
    // children, so deeper A2L sub-groups (Engine → Fuel → Injection → …)
    // collapsed on every dataChanged() emit.  We now key by the full path
    // ("Project / My maps / Engine / Fuel" etc.) so every previously open
    // node is restored exactly where it was.
    //
    // Backwards-compatibility: still emit the legacy sentinel-keys so the
    // existing `expandedGroups.contains("__linkedroms__")` and
    // `contains(g.name)` checks below keep working.
    QSet<QString> expandedGroups;
    QSet<QString> expandedPaths;
    QString       currentPath;          // re-select this item after rebuild

    auto pathOf = [](QTreeWidgetItem *it) {
        QStringList segs;
        for (auto *p = it; p; p = p->parent())
            segs.prepend(p->text(0));
        return segs.join(QStringLiteral(" / "));
    };
    std::function<void(QTreeWidgetItem*)> collectExpanded;
    collectExpanded = [&](QTreeWidgetItem *it) {
        if (it->isExpanded())
            expandedPaths.insert(pathOf(it));
        for (int k = 0; k < it->childCount(); ++k)
            collectExpanded(it->child(k));
    };
    for (int i = 0; i < m_projectTree->topLevelItemCount(); ++i) {
        auto *proj = m_projectTree->topLevelItem(i);
        collectExpanded(proj);
        // Legacy keys
        for (int j = 0; j < proj->childCount(); ++j) {
            auto *child = proj->child(j);
            if (!child->isExpanded()) continue;
            const QString txt = child->text(0);
            if (txt.startsWith(tr("Linked ROMs")))
                expandedGroups.insert("__linkedroms__");
            else if (txt.startsWith(tr("Versions")))
                expandedGroups.insert("__versions__");
            for (int k = 0; k < child->childCount(); ++k) {
                auto *grp = child->child(k);
                if (grp->isExpanded())
                    expandedGroups.insert(grp->text(0).section("  ", 0, 0));
            }
        }
    }
    if (auto *cur = m_projectTree->currentItem())
        currentPath = pathOf(cur);

    m_projectTree->setUpdatesEnabled(false);
    m_projectTree->clear();

    const int headerSize = m_treeFontSize;
    const int leafSize   = m_treeFontSize;

    m_projectTree->setFont(QFont("Segoe UI", leafSize));
    QFont leafFont("Segoe UI", leafSize);
    QFont boldFont("Segoe UI", headerSize);
    boldFont.setBold(true);
    QFont monoFont("Consolas", qMax(7, leafSize - 1));
    monoFont.setStyleHint(QFont::Monospace);

    static const QIcon iconMap   = makeIcon("\u25A6", QColor("" + AppConfig::instance().colors.uiAccent.lighter(120).name() + ""), 9);
    static const QIcon iconCurve = makeIcon("\u223F", QColor("#bc8cff"), 10);
    static const QIcon iconValue = makeIcon("\u25CF", QColor("#3fb950"), 9);
    static const QIcon iconBlk   = makeIcon("\u25AA", QColor("#6e7681"), 9);
    const QIcon iconFolderClosed = QApplication::style()->standardIcon(QStyle::SP_DirClosedIcon);
    const QIcon iconFolderOpen   = QApplication::style()->standardIcon(QStyle::SP_DirOpenIcon);

    // Render "My maps (N)" tree for any Project under the given tree item.
    // Shared by top-level projects AND sub-projects (multi-Version .ols
    // imports), so the 6000+ maps that live on each child are visible in
    // the tree without having to open every MDI window manually.
    auto renderMapsFor = [&](Project *p, QTreeWidgetItem *under) {
        if (!p || !under) return;
        const bool isLargeProject = p->maps.size() > 5000;
        const bool forceExpandAll = m_expandAllOnNextBuild.contains(p)
                                    && !isLargeProject;

        auto addLeaf = [&](QTreeWidgetItem *parentItem, const MapInfo &m) {
            // mapHasChanges is a single memcmp on map bytes — keep it
            // active even on huge projects (6000+ maps × ~256 bytes ≈ 1.5 MB
            // total memcmp, ~few ms).  Without it, the "modified" highlight
            // and the filter-modified button silently fail on big OLS-imported
            // ROMs (the original ~5000-map gate hid this).
            const bool changed = mapHasChanges(p, m);
            const bool starred = isProjectMapStarred(*p, m);
            auto *mi = new QTreeWidgetItem(parentItem);
            if      (m.type == "MAP")     mi->setIcon(0, iconMap);
            else if (m.type == "CURVE")   mi->setIcon(0, iconCurve);
            else if (m.type == "VAL_BLK") mi->setIcon(0, iconBlk);
            else                          mi->setIcon(0, iconValue);
            const TranslationResult *tx = m_translations.contains(m.name)
                                          ? &m_translations[m.name] : nullptr;
            // Prefer the normalized technical ID when available. Until the parser
            // enhancement is merged, imported KP maps expose the named ID as kpIdName.
            // Keep the legacy identifier and map name as compatibility fallbacks.
            QString mapIdentifier = m.getSideProp(QStringLiteral("kpTechnicalId")).toString();
            if (mapIdentifier.isEmpty())
                mapIdentifier = m.getSideProp(QStringLiteral("kpIdName")).toString();
            if (mapIdentifier.isEmpty())
                mapIdentifier = m.getSideProp(QStringLiteral("kpMapIdentifier")).toString();
            if (mapIdentifier.isEmpty())
                mapIdentifier = m.name;
            QString displayName = m.name;
            if (changed)                displayName.prepend("\u25cf ");
            if (!m.userNotes.isEmpty()) displayName.prepend("\u270e ");
            if (starred)                displayName.prepend("\u2605 ");
            mi->setText(0, displayName);
            mi->setText(1, QStringLiteral("0x%1")
                .arg(m.address, 8, 16, QChar('0')).toUpper());
            mi->setText(2, mapIdentifier);
            mi->setText(3, m.type);
            mi->setText(4, QStringLiteral("%1x%2")
                .arg(m.dimensions.x).arg(m.dimensions.y));
            mi->setFont(0, leafFont);
            mi->setFont(1, monoFont);
            mi->setTextAlignment(4, Qt::AlignRight | Qt::AlignVCenter);
            QColor mapColor;
            if (starred)
                mapColor = changed ? QColor("#ff7b72") : QColor("#d4a017");
            else if (changed)
                mapColor = QColor("#ff7b72");
            else if (!m.userNotes.isEmpty())
                mapColor = QColor("#d29a22");
            else if (!tx)
                mapColor = QColor(AppConfig::instance().colors.uiTextDim);
            if (mapColor.isValid()) {
                for (int column = 0; column < mi->columnCount(); ++column)
                    mi->setForeground(column, mapColor);
            }
            if (!isLargeProject) {
                QString tip = m.name;
                if (tx)  tip += "\n" + tx->translation;
                if (!m.description.isEmpty() && m.description != m.name)
                    tip += "\n\n" + m.description;
                if (!m.userNotes.isEmpty()) tip += "\n\n\u270e " + m.userNotes;
                if (changed) tip += "\n\n\u26a0 " + tr("This map has unsaved edits");
                mi->setToolTip(0, tip);
            }
            mi->setData(0, Qt::UserRole + 3, changed);
            mi->setData(0, Qt::UserRole + 1,
                        QVariant::fromValue(static_cast<void *>(p)));
            mi->setData(0, Qt::UserRole + 2, QVariant::fromValue(m));
        };

        const bool hasFolderPaths = std::any_of(
            p->maps.cbegin(), p->maps.cend(),
            [](const MapInfo &m) { return !m.folderPath.isEmpty(); });
        const int unfiledMapCount = std::count_if(
            p->maps.cbegin(), p->maps.cend(),
            [](const MapInfo &m) { return m.folderPath.isEmpty(); });

        // My maps is always present at the project root. Imported folder
        // paths are its siblings; only maps without a folder live inside it.
        auto *myMaps = new QTreeWidgetItem(under);
        const int mapCount = hasFolderPaths ? unfiledMapCount : p->maps.size();
        myMaps->setText(0, tr("My maps  (%1)").arg(mapCount));
        myMaps->setData(0, kTreeFolderRole, true);
        myMaps->setFont(0, boldFont);
        myMaps->setForeground(0, QColor("" + AppConfig::instance().colors.uiText.name() + ""));
        myMaps->setExpanded(!isLargeProject);
        myMaps->setIcon(0, myMaps->isExpanded() ? iconFolderOpen : iconFolderClosed);
        myMaps->setFlags(myMaps->flags() & ~Qt::ItemIsSelectable);

        // ── Auto-detected maps (overlay layer while no A2L is imported) ──
        // Rendered as a SEPARATE sibling group so the user can tell real
        // maps from scan-suggested ones. Same addLeaf() wiring so clicks
        // open the overlay and the left-panel counts stay accurate.
        if (p->maps.isEmpty() && !p->autoDetectedMaps.isEmpty()
            && !p->hideAutoDetectedMaps) {
            auto *autoGroup = new QTreeWidgetItem(under);
            autoGroup->setText(0, tr("Auto-detected  (%1)")
                                      .arg(p->autoDetectedMaps.size()));
            autoGroup->setData(0, kTreeFolderRole, true);
            autoGroup->setFont(0, boldFont);
            autoGroup->setForeground(0, QColor("#d29922"));   // amber
            autoGroup->setExpanded(!isLargeProject);
            autoGroup->setIcon(0, autoGroup->isExpanded() ? iconFolderOpen : iconFolderClosed);
            autoGroup->setFlags(autoGroup->flags() & ~Qt::ItemIsSelectable);
            for (const auto &m : p->autoDetectedMaps) addLeaf(autoGroup, m);
        }

        if (hasFolderPaths) {
            // KP schema-750 stores the actual folder path on every
            // map.  It takes precedence over Project::groups, which may be
            // legacy A2L data or the former name-prefix heuristic.  Render it
            // directly so nesting and identically named maps are preserved.
            QHash<QString, QTreeWidgetItem *> folderNodes;
            std::function<QTreeWidgetItem *(const QString &)> folderFor =
                [&](const QString &path) -> QTreeWidgetItem * {
                if (path.isEmpty()) return myMaps;
                if (auto it = folderNodes.constFind(path); it != folderNodes.cend())
                    return it.value();
                const int slash = path.lastIndexOf(QLatin1Char('/'));
                const QString parentPath = slash < 0 ? QString() : path.left(slash);
                const QString name = slash < 0 ? path : path.mid(slash + 1);
                auto *node = new QTreeWidgetItem(
                    parentPath.isEmpty() ? under : folderFor(parentPath));
                node->setText(0, name);
                node->setData(0, kTreeFolderRole, true);
                node->setToolTip(0, tr("Folder — use the arrow to expand or collapse"));
                node->setFont(0, boldFont);
                node->setExpanded(forceExpandAll || !isLargeProject);
                node->setIcon(0, node->isExpanded() ? iconFolderOpen : iconFolderClosed);
                node->setFlags(node->flags() & ~Qt::ItemIsSelectable);
                folderNodes.insert(path, node);
                return node;
            };

            QVector<int> order(p->maps.size());
            std::iota(order.begin(), order.end(), 0);
            std::sort(order.begin(), order.end(), [p](int a, int b) {
                const MapInfo &ma = p->maps[a], &mb = p->maps[b];
                return ma.folderPath == mb.folderPath
                    ? ma.address < mb.address : ma.folderPath < mb.folderPath;
            });
            for (int i : order)
                addLeaf(folderFor(p->maps[i].folderPath), p->maps[i]);

            // A folder count represents every map below it, not merely its
            // direct children, making nested folders useful at a glance.
            std::function<int(QTreeWidgetItem *)> countMaps =
                [&](QTreeWidgetItem *node) -> int {
                int count = 0;
                for (int child = 0; child < node->childCount(); ++child) {
                    QTreeWidgetItem *item = node->child(child);
                    if (item->data(0, Qt::UserRole + 2).isValid()) ++count;
                    else count += countMaps(item);
                }
                return count;
            };
            for (auto it = folderNodes.cbegin(); it != folderNodes.cend(); ++it) {
                QTreeWidgetItem *node = it.value();
                node->setText(0, QStringLiteral("%1  (%2)")
                    .arg(node->text(0)).arg(countMaps(node)));
            }
        } else if (p->groups.isEmpty()) {
            for (const auto &m : p->maps) addLeaf(myMaps, m);
        } else {
            QHash<QString, int> lookup;
            lookup.reserve(p->maps.size());
            for (int mi = 0; mi < p->maps.size(); mi++)
                lookup[p->maps[mi].name] = mi;
            for (const auto &g : p->groups) {
                auto *gi = new QTreeWidgetItem(myMaps);
                gi->setText(0, g.name + QString("  (%1)").arg(g.characteristics.size()));
                gi->setData(0, kTreeFolderRole, true);
                gi->setFont(0, boldFont);
                gi->setExpanded(forceExpandAll || expandedGroups.contains(g.name));
                gi->setIcon(0, gi->isExpanded() ? iconFolderOpen : iconFolderClosed);
                gi->setFlags(gi->flags() & ~Qt::ItemIsSelectable);
                for (const auto &cn : g.characteristics) {
                    auto it = lookup.find(cn);
                    if (it != lookup.end()) addLeaf(gi, p->maps[it.value()]);
                }
            }
        }
    };

    for (auto *project : m_projects) {
        // ── Project root ──────────────────────────────────────────────
        auto *projItem = new QTreeWidgetItem(m_projectTree);
        {
            // ── Status badge (color-blind safe: glyph + tooltip) ─────
            // Priority order: scanning > linked-reference > linked-child
            // > modified > saved.
            QString badge;
            QString badgeTip;
            if (m_mapScanWatchers.contains(project)) {
                badge    = QStringLiteral("\u25D0 ");   // half-filled circle
                badgeTip = tr("Scanning\u2026");
            } else if (project->isLinkedRom && project->isLinkedReference) {
                badge    = QStringLiteral("\u25C9 ");   // fisheye / target
                badgeTip = tr("Linked-ROM reference (ORI)");
            } else if (project->isLinkedRom) {
                badge    = QStringLiteral("\u25CE ");   // bullseye / ring
                badgeTip = tr("Linked-ROM child");
            } else if (project->modified) {
                badge    = QStringLiteral("\u25CF ");   // dot (amber via tooltip)
                badgeTip = tr("Modified");
            } else {
                badge    = QStringLiteral("\u25CF ");   // dot (green via tooltip)
                badgeTip = tr("Saved");
            }

            QString rootLabel = badge + project->listLabel()
                              + "  " + fmtSize(project->currentData.size());
            if (project->isLinkedRom && project->isLinkedReference)
                rootLabel = badge + QStringLiteral("[") + tr("ORI")
                          + QStringLiteral("]  ") + project->listLabel()
                          + "  " + fmtSize(project->currentData.size());
            projItem->setText(0, rootLabel);

            // Full state line in tooltip — uses autosave state when relevant.
            QString fullTip = badgeTip;
            if (m_mapScanWatchers.contains(project)) {
                // already says "Scanning…"
            } else if (project->modified) {
                if (m_autoSaveTimer && m_autoSaveTimer->isActive()) {
                    const int ms = m_autoSaveTimer->remainingTime();
                    if (ms > 0)
                        fullTip = tr("Modified \u2014 autosave in %1s")
                                      .arg((ms + 999) / 1000);
                }
            } else if (m_lastAutoSaveTime.isValid()) {
                const qint64 secs = m_lastAutoSaveTime.secsTo(
                                        QDateTime::currentDateTime());
                if (secs < 5)
                    fullTip = tr("Saved");
                else if (secs < 60)
                    fullTip = tr("Saved \u00B7 %1s ago").arg(secs);
                else if (secs < 3600)
                    fullTip = tr("Saved \u00B7 %1m ago").arg(secs / 60);
                else
                    fullTip = tr("Saved \u00B7 %1h ago").arg(secs / 3600);
            }
            projItem->setToolTip(0, fullTip);
        }
        projItem->setData(0, Qt::UserRole,
                          QVariant::fromValue(static_cast<void *>(project)));
        projItem->setFont(0, boldFont);
        projItem->setForeground(0, project->isLinkedReference
                                    ? QColor("#3fb950") : QColor("" + AppConfig::instance().colors.uiAccent.lighter(140).name() + ""));
        projItem->setExpanded(true);

        // ECU info row
        if (!project->ecuType.isEmpty() || project->year > 0) {
            auto *ecuItem = new QTreeWidgetItem(projItem);
            QString ecuText = project->ecuType;
            if (project->year > 0) ecuText += "  " + QString::number(project->year);
            ecuItem->setText(0, ecuText);
            QFont ef("Segoe UI", qMax(7, headerSize - 2)); ef.setItalic(true);
            ecuItem->setFont(0, ef);
            ecuItem->setForeground(0, QColor("" + AppConfig::instance().colors.uiTextDim.name() + ""));
            ecuItem->setFlags(ecuItem->flags() & ~Qt::ItemIsSelectable);
        }

        // Hexdump entry
        {
            auto *hexItem = new QTreeWidgetItem(projItem);
            hexItem->setText(0, tr("Hexdump  ") + fmtSize(project->currentData.size()));
            hexItem->setFont(0, QFont("Segoe UI", qMax(7, headerSize - 2)));
            hexItem->setForeground(0, QColor("" + AppConfig::instance().colors.uiTextDim.name() + ""));
            hexItem->setIcon(0, makeIcon("Hex", QColor("#6e7681"), 5));
            hexItem->setData(0, Qt::UserRole,
                             QVariant::fromValue(static_cast<void *>(project)));
        }


        // ── Linked ROMs ───────────────────────────────────────────────
        if (!project->linkedRoms.isEmpty()) {
            auto *linksItem = new QTreeWidgetItem(projItem);
            linksItem->setText(0, tr("Linked ROMs  (%1)").arg(project->linkedRoms.size()));
            linksItem->setFont(0, boldFont);
            linksItem->setForeground(0, QColor("" + AppConfig::instance().colors.uiText.name() + ""));
            linksItem->setExpanded(expandedGroups.contains("__linkedroms__"));
            linksItem->setFlags(linksItem->flags() & ~Qt::ItemIsSelectable);
            for (const auto &lr : project->linkedRoms) {
                auto *lrItem = new QTreeWidgetItem(linksItem);
                QString lbl = lr.isReference
                    ? QString("[ORI]  ") + lr.label
                    : lr.label;
                lrItem->setText(0, lbl);
                lrItem->setFont(0, leafFont);
                lrItem->setForeground(0, lr.isReference
                    ? QColor("#3fb950")   // green for ORI
                    : QColor("" + AppConfig::instance().colors.uiText.name() + ""));
                lrItem->setIcon(0, lr.isReference
                    ? makeIcon("◉", QColor("#3fb950"), 9)
                    : makeIcon("◎", QColor("#6e7681"), 9));
                lrItem->setFlags(lrItem->flags() & ~Qt::ItemIsSelectable);
            }
        }

        // ── Versions ──────────────────────────────────────────────────
        if (!project->versions.isEmpty()) {
            auto *versItem = new QTreeWidgetItem(projItem);
            versItem->setText(0, tr("Versions  (%1)").arg(project->versions.size()));
            versItem->setFont(0, boldFont);
            versItem->setForeground(0, QColor("" + AppConfig::instance().colors.uiText.name() + ""));
            versItem->setExpanded(expandedGroups.contains("__versions__"));
            versItem->setFlags(versItem->flags() & ~Qt::ItemIsSelectable);
            for (int vi = 0; vi < project->versions.size(); ++vi) {
                const auto &v = project->versions[vi];
                auto *vItem = new QTreeWidgetItem(versItem);
                vItem->setText(0, v.name + "  "
                    + v.created.toString("dd/MM/yy HH:mm"));
                vItem->setFont(0, leafFont);
                vItem->setForeground(0, QColor("" + AppConfig::instance().colors.uiTextDim.name() + ""));
                vItem->setIcon(0, makeIcon("\u25F7", QColor("#6e7681"), 9));
                // Clickable: Qt::UserRole carries the Project*, UserRole+4
                // flags this as a ProjectVersion snapshot with its index
                // so onTreeItemClicked can call restoreVersion().
                vItem->setData(0, Qt::UserRole,
                               QVariant::fromValue(static_cast<void *>(project)));
                vItem->setData(0, Qt::UserRole + 4, vi);
            }
        }

        // ── My maps ───────────────────────────────────────────────────
        renderMapsFor(project, projItem);

        // ── Potential maps ────────────────────────────────────────────
        auto *potMaps = new QTreeWidgetItem(projItem);
        potMaps->setText(0, tr("Potential maps  (0)"));
        potMaps->setFont(0, boldFont);
        potMaps->setForeground(0, QColor("#7d8590"));
        potMaps->setExpanded(false);
        potMaps->setDisabled(true);
    }

    // ── Restore expand state for every previously-open node + the
    // previous selection (using the full-path key collected above).
    {
        std::function<void(QTreeWidgetItem*)> restore;
        restore = [&](QTreeWidgetItem *it) {
            const QString p = pathOf(it);
            if (expandedPaths.contains(p))
                it->setExpanded(true);
            for (int k = 0; k < it->childCount(); ++k)
                restore(it->child(k));
        };
        QTreeWidgetItem *toSelect = nullptr;
        std::function<QTreeWidgetItem*(QTreeWidgetItem*)> findSelected;
        findSelected = [&](QTreeWidgetItem *it) -> QTreeWidgetItem* {
            if (!currentPath.isEmpty() && pathOf(it) == currentPath) return it;
            for (int k = 0; k < it->childCount(); ++k) {
                if (auto *r = findSelected(it->child(k))) return r;
            }
            return nullptr;
        };
        for (int i = 0; i < m_projectTree->topLevelItemCount(); ++i) {
            auto *top = m_projectTree->topLevelItem(i);
            restore(top);
            if (!toSelect) toSelect = findSelected(top);
        }
        if (toSelect) {
            m_projectTree->setCurrentItem(toSelect);
            m_projectTree->scrollToItem(toSelect, QAbstractItemView::PositionAtCenter);
        }
    }

    m_projectTree->setUpdatesEnabled(true);
    m_projectTree->doItemsLayout();                // recalc scroll range after bulk add
    applyTreeFilter();

    // Consume the auto-expand intent — only expand on the FIRST rebuild after
    // openProject(); subsequent rebuilds preserve whatever the user has done.
    m_expandAllOnNextBuild.clear();
    qCInfo(catFind) << "refreshProjectTreeNow done in" << __rpt.elapsed() << "ms";
}

void MainWindow::applyTreeFilter()
{
    const QString txt      = m_filterEdit ? m_filterEdit->text() : QString();
    const bool changedOnly = m_filterChangedBtn && m_filterChangedBtn->isChecked();
    const bool noFilter    = txt.isEmpty() && !changedOnly && m_panelFilter == PanelFilter::All;

    // Fast path: no filter active → show everything, skip recursion
    if (noFilter) {
        for (int i = 0; i < m_projectTree->topLevelItemCount(); ++i) {
            std::function<void(QTreeWidgetItem*)> showAll = [&](QTreeWidgetItem *it) {
                it->setHidden(false);
                for (int j = 0; j < it->childCount(); ++j)
                    showAll(it->child(j));
            };
            showAll(m_projectTree->topLevelItem(i));
        }
        return;
    }

    std::function<bool(QTreeWidgetItem *)> apply = [&](QTreeWidgetItem *it) -> bool {
        bool anyChildVisible = false;
        for (int i = 0; i < it->childCount(); ++i)
            anyChildVisible |= apply(it->child(i));

        // Leaf map items carry UserRole+2 (MapInfo) — non-map items (headers, etc.) always show
        bool isMapLeaf = it->data(0, Qt::UserRole + 2).isValid();
        if (!isMapLeaf) {
            it->setHidden(!anyChildVisible && it->childCount() > 0 && !noFilter);
            if (!noFilter && anyChildVisible) it->setExpanded(true);
            return anyChildVisible || noFilter;
        }

        // Fuzzy text matching: substring, separator-stripped, or subsequence
        bool textMatch = true;
        if (!txt.isEmpty()) {
            QString itemText;
            for (int column = 0; column < m_projectTree->columnCount(); ++column)
                itemText += QLatin1Char(' ') + it->text(column);
            itemText = itemText.toLower();
            QString queryLow = txt.toLower();
            if (itemText.contains(queryLow)) {
                textMatch = true;
            } else {
                // Strip separators and try again
                auto strip = [](const QString &s) {
                    QString n; n.reserve(s.size());
                    for (auto c : s)
                        if (c != '_' && c != '.' && c != '-' && c != ' ')
                            n.append(c);
                    return n;
                };
                QString itemNorm = strip(itemText);
                QString queryNorm = strip(queryLow);
                if (itemNorm.contains(queryNorm)) {
                    textMatch = true;
                } else {
                    textMatch = false;
                }
            }
        }
        bool changedMatch = !changedOnly || it->data(0, Qt::UserRole + 3).toBool();

        // Panel filter mode
        auto mapVar = it->data(0, Qt::UserRole + 2);
        auto m = mapVar.value<MapInfo>();
        auto *proj = static_cast<Project*>(it->data(0, Qt::UserRole + 1).value<void*>());
        const bool starred  = proj && isProjectMapStarred(*proj, m);
        const bool isRecent = m_recentMaps.contains({proj, m.name});
        bool modeMatch = true;
        switch (m_panelFilter) {
        case PanelFilter::Modified:  modeMatch = it->data(0, Qt::UserRole + 3).toBool(); break;
        case PanelFilter::Starred:   modeMatch = starred; break;
        case PanelFilter::Recent:    modeMatch = isRecent; break;
        case PanelFilter::TypeValue: modeMatch = m.type == "VALUE" || m.type == "VAL_BLK"; break;
        case PanelFilter::TypeCurve: modeMatch = m.type == "CURVE"; break;
        case PanelFilter::TypeMap:   modeMatch = m.type == "MAP"; break;
        default: modeMatch = true; break;
        }

        bool show = textMatch && changedMatch && modeMatch;

        it->setHidden(!show);
        return show;
    };

    for (int i = 0; i < m_projectTree->topLevelItemCount(); ++i)
        apply(m_projectTree->topLevelItem(i));
}

// ── File slots ─────────────────────────────────────────────────────────────────

void MainWindow::actProjectManager()
{
    ProjectManagerDialog dlg(this);
    connect(&dlg, &ProjectManagerDialog::openProjectRequested,
            this, [this](const QString &path){
        auto *project = Project::open(path, this);
        if (!project) {
            QMessageBox::critical(this, tr("Error"), tr("Failed to open project:\n") + path);
            return;
        }
        openProject(project);
    });
    connect(&dlg, &ProjectManagerDialog::newProjectRequested,
            this, &MainWindow::actNewProject);
    connect(&dlg, &ProjectManagerDialog::buildSimilarityIndexRequested,
            this, &MainWindow::actBuildSimilarityIndex);
    connect(&dlg, &ProjectManagerDialog::openOlsRequested,
            this, [this](const QString &path) {
        // Replicate actImportOlsProject minus the QFileDialog.
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            QMessageBox::critical(this, tr("Error"),
                tr("Cannot open file: %1").arg(path));
            return;
        }
        QByteArray fileData = f.readAll();
        f.close();
        ols::OlsImportResult result =
            ols::OlsImporter::importFromBytes(fileData);
        if (!result.error.isEmpty()) {
            QMessageBox::critical(this, tr("Import Error"), result.error);
            return;
        }
        QVector<Project*> projects =
            ols::buildProjectsFromOlsImport(result, path, this);
        if (projects.isEmpty()) {
            QMessageBox::critical(this, tr("Import Error"),
                tr("No Versions found in WinOLS file."));
            return;
        }
        for (Project *p : projects) {
            m_projects.append(p);
            openProject(p);
        }
        broadcastAvailableProjects();
        refreshProjectTree();
    });
    dlg.exec();
}

void MainWindow::actNewProject()
{
    // 1. Pick ROM file
    QString romPath = QFileDialog::getOpenFileName(this, tr("Select ROM file"), {},
        tr("ROM files (*.bin *.hex *.rom *.ori *.bbf *.mot *.s19 *.mpc);;All files (*)"));
    if (romPath.isEmpty()) return;

    // 2. Create project and load ROM data
    auto *project = new Project(this);
    project->createdAt = QDateTime::currentDateTime();
    project->createdBy = qEnvironmentVariable("USERNAME",
                             qEnvironmentVariable("USER", "Unknown"));
    loadROMIntoProject(project, romPath);

    // 3. Show WinOLS-style project properties dialog (auto-fills ECU fields from ROM)
    ProjectPropertiesDialog dlg(project, this);
    dlg.setWindowTitle(tr("New Project — Import"));
    if (dlg.exec() != QDialog::Accepted) {
        // User cancelled — kill the in-flight map auto-scan so we don't
        // burn CPU on a project that's about to be deleted.
        cancelMapScan(project);
        delete project;
        return;
    }

    // 4. Auto-save to AppData projects folder; show Save As if name is still blank
    QString name = project->displayName();
    if (name.isEmpty()) name = QFileInfo(romPath).baseName();

    QString dir = ProjectRegistry::defaultProjectDir();
    QDir().mkpath(dir);
    QString path = dir + "/" + name + ".rx14proj";

    // Avoid overwriting an existing file without asking
    if (QFileInfo::exists(path)) {
        path = QFileDialog::getSaveFileName(this, tr("Save Project As"), path,
            tr("RX14 Projects (*.rx14proj);;All Files (*)"));
        if (path.isEmpty()) { cancelMapScan(project); delete project; return; }
    }

    if (project->saveAs(path)) {
        ProjectRegistry::instance().registerProject(path, project);
        statusBar()->showMessage(tr("Project saved: ") + path);
    }

    openProject(project);
}

void MainWindow::actOpenProject()
{
    QString path = QFileDialog::getOpenFileName(this, tr("Open RX14 Project"), {},
        tr("RX14 Projects (*.rx14proj);;All Files (*)"));
    if (path.isEmpty()) return;

    auto *project = Project::open(path, this);
    if (!project) {
        QMessageBox::critical(this, tr("Error"), tr("Failed to open project:\n%1").arg(path));
        return;
    }
    ProjectRegistry::instance().registerProject(path, project);
    openProject(project);
}

void MainWindow::actSaveProject()
{
    auto *proj = activeProject();
    if (!proj) return;
    if (proj->filePath.isEmpty()) { actSaveProjectAs(); return; }
    if (proj->save()) {
        statusBar()->showMessage(tr("Project saved: ") + proj->filePath);
        ProjectRegistry::instance().registerProject(proj->filePath, proj);
    } else {
        QMessageBox::critical(this, tr("Error"), tr("Failed to save project."));
    }
}

void MainWindow::actSaveProjectAs()
{
    auto *proj = activeProject();
    if (!proj) return;
    // Suggest AppData projects folder as default directory
    QString defaultDir = proj->filePath.isEmpty()
        ? ProjectRegistry::defaultProjectDir()
        : QFileInfo(proj->filePath).absolutePath();
    QDir().mkpath(ProjectRegistry::defaultProjectDir());

    const QString prevPath = proj->filePath;

    QString path = QFileDialog::getSaveFileName(this, tr("Save Project As"),
        defaultDir + "/" + suggestedProjectBasename(proj) + ".rx14proj",
        tr("RX14 Projects (*.rx14proj);;All Files (*)"));
    if (path.isEmpty()) return;
    if (proj->saveAs(path)) {
        statusBar()->showMessage(tr("Project saved: ") + path);
        ProjectRegistry::instance().registerProject(path, proj);

        // Drop any orphaned autosave from the previous filePath (or from the
        // unsaved-fallback location). Stale autosaves would otherwise show up
        // in crash recovery and confuse the user.
        if (!prevPath.isEmpty() && prevPath != path)
            QFile::remove(prevPath + ".autosave");
        const QString fallback = ProjectRegistry::defaultProjectDir()
            + "/autosave/" + suggestedProjectBasename(proj) + ".rx14proj.autosave";
        QFile::remove(fallback);

        // Linked-ROM children point at the OLD parent path via
        // linkedToProjectPath — refresh so future re-opens find the parent.
        for (Project *p : m_projects) {
            if (p->isLinkedRom && p->parentProject == proj)
                p->linkedToProjectPath = path;
        }
    } else {
        QMessageBox::critical(this, tr("Error"), tr("Failed to save project."));
    }
}

void MainWindow::actGoHome()
{
    // Close every open project (prompting to save modified ones), then
    // return to the welcome page. If the user cancels any save prompt,
    // abort the whole operation.
    QVector<Project *> projects = m_projects;   // iterate a copy — we modify m_projects
    for (Project *proj : projects) {
        if (proj->modified) {
            QMessageBox mb(this);
            mb.setWindowTitle(tr("Close Project"));
            mb.setIcon(QMessageBox::Warning);
            mb.setText(QString("<b>%1</b>").arg(proj->fullTitle()));
            mb.setInformativeText(tr("This project has unsaved changes."));
            auto *btnSave    = mb.addButton(tr("Save && Close"),         QMessageBox::AcceptRole);
            auto *btnDiscard = mb.addButton(tr("Close without saving"),  QMessageBox::DestructiveRole);
            mb.addButton(QMessageBox::Cancel);
            mb.setDefaultButton(btnSave);
            mb.exec();
            const auto *clicked = mb.clickedButton();
            if (clicked != btnSave && clicked != btnDiscard) return;   // Cancel — abort
            if (clicked == btnSave) {
                if (proj->filePath.isEmpty()) {
                    // Never been saved — ask for path
                    QString path = QFileDialog::getSaveFileName(
                        this, tr("Save Project As"),
                        ProjectRegistry::defaultProjectDir() + "/" + suggestedProjectBasename(proj) + ".rx14proj",
                        tr("RX14 Projects (*.rx14proj);;All Files (*)"));
                    if (path.isEmpty()) return;   // user cancelled save-as → abort
                    proj->saveAs(path);
                    ProjectRegistry::instance().registerProject(path, proj);
                } else {
                    proj->save();
                    ProjectRegistry::instance().registerProject(proj->filePath, proj);
                }
            }
        }
    }

    // All prompts answered — close everything.
    // 1. Close + remove all overlays
    for (auto ov : m_overlays)
        if (ov) ov->close();
    m_overlays.clear();

    // 2. Close all MDI subwindows
    for (auto *sw : m_mdi->subWindowList()) {
        sw->removeEventFilter(this);   // prevent our eventFilter re-prompting
        sw->setAttribute(Qt::WA_DeleteOnClose, true);
        sw->close();
    }

    // 3. Clean up project objects
    for (Project *proj : projects)
        proj->deleteLater();
    m_projects.clear();
    m_recentMaps.clear();
    m_translations.clear();
    if (m_savepoints)
        m_savepoints->attachTo(nullptr);

    refreshProjectTree();       // triggers updateCentralPage → welcome shows
    refreshRecentMapsStrip();
    broadcastAvailableProjects();
}

void MainWindow::actCloseProject()
{
    auto *sw = m_mdi->activeSubWindow();
    if (!sw) return;
    auto *pv = qobject_cast<ProjectView *>(sw->widget());
    if (!pv) return;
    auto *proj = pv->project();

    if (proj && proj->modified) {
        QMessageBox mb(this);
        mb.setWindowTitle(tr("Close Project"));
        mb.setIcon(QMessageBox::Warning);
        mb.setText(QString("<b>%1</b>").arg(proj->fullTitle()));
        mb.setInformativeText(tr("This project has unsaved changes."));
        auto *btnSave    = mb.addButton(tr("Save && Close"),         QMessageBox::AcceptRole);
        auto *btnDiscard = mb.addButton(tr("Close without saving"),  QMessageBox::DestructiveRole);
        mb.addButton(QMessageBox::Cancel);
        mb.setDefaultButton(btnSave);
        mb.exec();

        if (mb.clickedButton() != btnSave && mb.clickedButton() != btnDiscard) return; // Cancel
        if (mb.clickedButton() == btnSave) actSaveProject();
    }

    m_projects.removeAll(proj);
    // Drop recent-map entries tied to this project (m_recentMaps is keyed by
    // Project*, so stale pointers would dangle after deleteLater()).
    if (proj) {
        m_recentMaps.removeIf([proj](const QPair<Project*, QString> &p){
            return p.first == proj;
        });

        // Map overlays are parented by MainWindow and can target this project;
        // close all of them before the project is destroyed.
        for (auto ov : m_overlays)
            if (ov && ov->targetProject() == proj) ov->close();
        m_overlays.removeAll(QPointer<MapOverlay>());
    }
    if (proj) {
        if (m_savepoints && m_savepoints->project() == proj)
            m_savepoints->attachTo(nullptr);
        proj->deleteLater();
    }
    sw->close();
    m_translations.clear();
    refreshProjectTree();
    refreshRecentMapsStrip();
    broadcastAvailableProjects();
}

void MainWindow::actImportA2L(const QString &droppedPath)
{
    auto *proj = activeProject();
    if (!proj) {
        QMessageBox::information(this, tr("No project"),
            tr("Open or create a project first."));
        return;
    }
    QString path = droppedPath;
    if (path.isEmpty())
        path = QFileDialog::getOpenFileName(this, tr("Import A2L / DAMOS File"), {},
            tr("A2L / DAMOS (*.a2l *.dam *.damos);;A2L Files (*.a2l);;All Files (*)"));
    if (!path.isEmpty())
        loadA2LIntoProject(proj, path);
}

void MainWindow::actAddVersion()
{
    auto *view = activeView();
    if (view) view->onAddVersion();
}

void MainWindow::actExportROM()
{
    auto *proj = activeProject();
    if (!proj || proj->currentData.isEmpty()) return;

    // --- Let the user choose which ROM version to export -----------------------
    //  Index -1 = current working ROM; 0..N = project->versions[idx]
    int selectedIdx = -1;

    if (!proj->versions.isEmpty()) {
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Export ROM"));

        auto *layout = new QVBoxLayout(&dlg);
        layout->addWidget(new QLabel(tr("Select ROM version to export:")));

        auto *combo = new QComboBox;
        combo->addItem(tr("Current ROM (working)"));
        for (const auto &ver : proj->versions)
            combo->addItem(ver.name);
        layout->addWidget(combo);

        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        layout->addWidget(bb);

        if (dlg.exec() != QDialog::Accepted)
            return;

        // combo index 0 = current ROM, 1..N = versions[0..N-1]
        selectedIdx = combo->currentIndex() - 1;
    }

    // --- Resolve the data to export -------------------------------------------
    QByteArray data;
    if (selectedIdx >= 0) {
        data = proj->versions[selectedIdx].data;
    } else {
        auto *view = activeView();
        data = (view && view->findChild<HexWidget *>())
            ? view->findChild<HexWidget *>()->exportBinary()
            : proj->currentData;
    }

    // --- Checksum guard: verify (and optionally correct) before writing -------
    // A ROM with a stale checksum can be rejected by the ECU on flash, so we
    // never write one silently. This may correct `data` in place.
    if (!confirmChecksumBeforeExport(proj, data))
        return;

    // --- Build the default file name ------------------------------------------
    QString baseName = proj->romPath.isEmpty()
        ? proj->displayName()
        : QFileInfo(proj->romPath).completeBaseName();

    if (selectedIdx >= 0)
        baseName += QStringLiteral(" [%1]").arg(proj->versions[selectedIdx].name);

    QString defaultName = baseName + QStringLiteral(".bin");

    // --- File-save dialog -----------------------------------------------------
    QString path = QFileDialog::getSaveFileName(this, tr("Export ROM"), defaultName,
        tr("ROM Files (*.bin *.rom);;All Files (*)"));
    if (path.isEmpty()) return;

    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(data);
        statusBar()->showMessage(tr("Exported ROM to: ") + path);
    } else {
        QMessageBox::critical(this, tr("Error"), tr("Could not write file."));
    }
}

// ── WinOLS export ─────────────────────────────────────────────────────────────

void MainWindow::actExportOlsProject()
{
    auto *proj = activeProject();
    if (!proj || proj->currentData.isEmpty()) {
        QMessageBox::information(this, tr("Export WinOLS"),
            tr("Open a project with ROM data first."));
        return;
    }

    // ── HEX/BIN-import refusal ─────────────────────────────────────────
    // OLS export only produces a WinOLS-loadable file when the project
    // was originally imported from an .ols file — that import path
    // captures the per-segment WinOLS BinInfo metadata (the 26-byte
    // pre-descriptor blocks WinOLS validates on load) into
    // project.olsSegments / project.versions[i].olsSegments.
    //
    // For HEX/BIN/SREC imports there is no captured BinInfo; only ONE
    // of the 6 internal pointers per preamble has a known formula
    // (u32[3] = (flash_end+1) | 0x80000000), the other 5 reference
    // WinOLS's internal map/code-region pointer table which we cannot
    // synthesise from raw ROM bytes. Producing the file would silently
    // yield bytes WinOLS rejects with the cryptic "OLS-ser~589" error.
    //
    // Until we reverse-engineer enough of the BinInfo format to fake
    // those pointers (separate, multi-week task), refuse cleanly with
    // a clear explanation + suggested alternatives.
    bool hasOlsMetadata = !proj->olsSegments.isEmpty();
    for (const auto &v : proj->versions)
        if (!v.olsSegments.isEmpty()) { hasOlsMetadata = true; break; }
    if (!hasOlsMetadata) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Cannot Export to WinOLS"));
        box.setText(tr("This project cannot be exported to a WinOLS .ols "
                       "file."));
        box.setInformativeText(tr(
            "WinOLS export requires the per-segment metadata that is only "
            "captured when importing an existing .ols file. This project "
            "was created from a raw ROM (HEX / BIN / SREC), so that "
            "metadata isn't available — WinOLS would reject the resulting "
            "file as corrupt.\n\n"
            "Alternatives:\n"
            "  • Save Project (.rx14proj) — preserves all your edits, "
            "maps and A2L data\n"
            "  • Export ROM (.bin) — writes the raw ROM bytes you can "
            "flash directly\n"
            "  • Re-import an existing .ols file as your starting point, "
            "then OLS export will work end-to-end"));
        box.setStandardButtons(QMessageBox::Ok);
        box.exec();
        return;
    }

    // Build default filename
    QString baseName = proj->name.isEmpty()
        ? QStringLiteral("export")
        : proj->name;
    QString defaultPath = baseName + QStringLiteral(".ols");

    QString path = QFileDialog::getSaveFileName(this,
        tr("Export WinOLS Project"), defaultPath,
        tr("WinOLS files (*.ols);;All files (*)"));
    if (path.isEmpty()) return;

    // Run the exporter
    ols::OlsExportResult result = ols::OlsExporter::exportProject(*proj);

    if (!result.error.isEmpty()) {
        QMessageBox::critical(this, tr("Export Error"), result.error);
        return;
    }

    // Write to disk
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, tr("Error"),
            tr("Could not write file: %1").arg(path));
        return;
    }
    f.write(result.fileData);
    f.close();

    QString msg = tr("Exported WinOLS project to: %1 (%2 bytes, %3 maps)")
        .arg(QFileInfo(path).fileName())
        .arg(result.fileData.size())
        .arg(proj->maps.size());

    if (!result.warnings.isEmpty()) {
        msg += tr(" — %1 warning(s)").arg(result.warnings.size());
    }
    statusBar()->showMessage(msg);
}

// ── Window slots ───────────────────────────────────────────────────────────────

void MainWindow::actTileWindows()    { m_mdi->tileSubWindows(); }
void MainWindow::actCascadeWindows() { m_mdi->cascadeSubWindows(); }

// Retile all visible project sub-windows to fill the MDI viewport perfectly.
// Called after AI assistant is shown/hidden so windows always use all available space.
void MainWindow::retileWindows()
{
    QList<QMdiSubWindow *> visible;
    for (auto *sub : m_mdi->subWindowList())
        if (sub->isVisible()) visible.append(sub);
    if (visible.isEmpty()) return;

    const QSize vp = m_mdi->viewport()->size();
    const int w    = vp.width();
    const int h    = vp.height();
    const int n    = visible.size();

    // Stack vertically: each window gets full width, equal slice of height
    for (int i = 0; i < n; ++i) {
        int y0 = (h * i)     / n;
        int y1 = (h * (i+1)) / n;
        visible[i]->showNormal();
        visible[i]->setGeometry(0, y0, w, y1 - y0);
    }
}

void MainWindow::actCompareProjects()
{
    auto windows = m_mdi->subWindowList();
    if (windows.size() < 2) {
        QMessageBox::information(this, tr("Compare"),
            tr("Open at least two projects to compare."));
        return;
    }
    auto *w1 = windows[windows.size() - 2];
    auto *w2 = windows[windows.size() - 1];
    QRect area = m_mdi->contentsRect();
    int half = area.width() / 2;
    w1->setGeometry(area.x(),          area.y(), half - 1, area.height());
    w2->setGeometry(area.x() + half + 1, area.y(), area.width() - half - 1, area.height());
    w1->show(); w2->show();

    // Compare implies "I want them moving together" — auto-enable Sync
    // Cursors so dragging a scrollbar in one window slides the other.
    // The user can still toggle it off afterwards if they want
    // independent navigation.
    if (m_actSyncCursors && !m_actSyncCursors->isChecked())
        m_actSyncCursors->setChecked(true);
}

// ── Navigation ─────────────────────────────────────────────────────────────────

void MainWindow::actPrevMap()
{
    auto *proj = activeProject();
    if (!proj || proj->maps.isEmpty()) return;
    if (m_currentMapIdx <= 0) m_currentMapIdx = proj->maps.size();
    --m_currentMapIdx;
    const auto &m = proj->maps[m_currentMapIdx];
    if (auto *v = activeView()) v->showMap(m);
    onMapActivated(m, proj);
}

void MainWindow::actNextMap()
{
    auto *proj = activeProject();
    if (!proj || proj->maps.isEmpty()) return;
    ++m_currentMapIdx;
    if (m_currentMapIdx >= proj->maps.size()) m_currentMapIdx = 0;
    const auto &m = proj->maps[m_currentMapIdx];
    if (auto *v = activeView()) v->showMap(m);
    onMapActivated(m, proj);
}

// ── 2D view scroll sync ────────────────────────────────────────────────────────

// ── Alignment-aware sync helpers ────────────────────────────────────────────
//
// When a (A, B [, C]) triple has an alignment map (set via DiffPanel), the
// Sync Cursors mechanism must scroll each view to its own offset-adjusted
// position rather than to the raw byte address from the source.  Without
// this, nudging the offset only changes the diff list — the user wants
// to physically see B's waveform slide left/right on screen.
//
// Identify the "source project" from the sender widget; ask DiffPanel to
// translate the source-side address into the target-side address; fall
// back to identity if either project is outside the alignment.

static Project *projectOfWaveform(WaveformWidget *w, QMdiArea *mdi) {
    if (!w || !mdi) return nullptr;
    for (auto *sub : mdi->subWindowList()) {
        auto *pv = qobject_cast<ProjectView *>(sub->widget());
        if (pv && pv->waveformWidget() == w) return pv->project();
    }
    return nullptr;
}

static Project *projectOfHex(HexWidget *h, QMdiArea *mdi) {
    if (!h || !mdi) return nullptr;
    for (auto *sub : mdi->subWindowList()) {
        auto *pv = qobject_cast<ProjectView *>(sub->widget());
        if (pv && pv->hexWidget() == h) return pv->project();
    }
    return nullptr;
}

void MainWindow::onWaveSyncScroll(int scrollOffset)
{
    auto *source = qobject_cast<WaveformWidget *>(sender());
    Project *srcProj = projectOfWaveform(source, m_mdi);

    for (auto *sub : m_mdi->subWindowList()) {
        auto *pv = qobject_cast<ProjectView *>(sub->widget());
        if (!pv) continue;
        auto *ww = pv->waveformWidget();
        if (!ww || ww == source) continue;

        qint64 target = scrollOffset;       // identity fallback
        if (m_diffPanel && srcProj && pv->project()) {
            qint64 t = m_diffPanel->translate(srcProj, scrollOffset, pv->project());
            if (t >= 0) target = t;
        }
        ww->syncScrollTo(static_cast<int>(target));
    }
}

void MainWindow::onHexSyncScroll(int byteOffset)
{
    auto *source = qobject_cast<HexWidget *>(sender());
    Project *srcProj = projectOfHex(source, m_mdi);

    for (auto *sub : m_mdi->subWindowList()) {
        auto *pv = qobject_cast<ProjectView *>(sub->widget());
        if (!pv) continue;
        auto *hw = pv->hexWidget();
        if (!hw || hw == source) continue;

        qint64 target = byteOffset;
        if (m_diffPanel && srcProj && pv->project()) {
            qint64 t = m_diffPanel->translate(srcProj, byteOffset, pv->project());
            if (t >= 0) target = t;
        }
        hw->syncScrollTo(static_cast<int>(target));
    }
}

void MainWindow::onWaveSyncZoom(int sliderValue)
{
    // The user dragged one waveform's zoom slider — apply the same zoom
    // to every other waveform so side-by-side compare keeps both at the
    // same scale (otherwise the shapes drift apart visually as the user
    // zooms only one window).
    auto *source = qobject_cast<WaveformWidget *>(sender());
    for (auto *sub : m_mdi->subWindowList()) {
        auto *pv = qobject_cast<ProjectView *>(sub->widget());
        if (!pv) continue;
        auto *ww = pv->waveformWidget();
        if (ww && ww != source) ww->syncZoomTo(sliderValue);
    }
}

void MainWindow::onSyncViewSwitch(int index)
{
    // Fan out the view change to every other open ProjectView (skip the sender)
    auto *source = qobject_cast<ProjectView *>(sender());
    for (auto *sub : m_mdi->subWindowList()) {
        auto *pv = qobject_cast<ProjectView *>(sub->widget());
        if (!pv || pv == source) continue;
        pv->switchView(index);
    }
}

void MainWindow::onProjectDataChangedForDiff()
{
    if (m_diffPanel && m_diffDock && m_diffDock->isVisible())
        m_diffPanel->setProjects(m_projects);
}

// Build a copy of @p src such that result[i] == src[i + delta], padding with
// zeros where the source is out of range.  Used so a pair of compared ROMs
// each see the *other* one's bytes lined up in their own coordinate space —
// this is what brings back the colour overlay on the waveform (the "I can
// see what changed" feedback) when offsets are non-zero.
static QByteArray shiftedCopy(const QByteArray &src, qint64 delta, qint64 targetSize)
{
    QByteArray r(targetSize, '\0');
    if (src.isEmpty() || targetSize <= 0) return r;
    qint64 iStart = qMax<qint64>(0,             -delta);
    qint64 iEnd   = qMin<qint64>(targetSize,    src.size() - delta);
    if (iEnd > iStart)
        std::memcpy(r.data() + iStart,
                    src.constData() + iStart + delta,
                    iEnd - iStart);
    return r;
}

void MainWindow::onDiffComparisonChanged()
{
    // Push project B's bytes (offset-shifted) into A's hex+waveform as
    // comparisonData, and vice-versa.  This is what lights up the colour
    // overlays the user expects from a "compare two projects" workflow:
    // without comparisonData, WaveformWidget falls back to the plain
    // single-curve render and loses the diff strip / change markers.
    if (!m_diffPanel || !m_diffDock || !m_diffDock->isVisible()) return;
    Project *pA = m_diffPanel->projectA();
    Project *pB = m_diffPanel->projectB();
    if (!pA || !pB || pA == pB) return;

    const qint64 deltaB = m_diffPanel->globalDeltaToA(pB);
    if (deltaB == -1) return;   // no global region yet

    // For A's view, comparison = B shifted by deltaB so that B[i+deltaB]
    // sits at A's index i.  For B's view, comparison = A shifted by
    // -deltaB.  C is left untouched — its compare-source is the user's
    // dropdown choice on its own ProjectView.
    QByteArray cmpForA = shiftedCopy(pB->currentData,  deltaB, pA->currentData.size());
    QByteArray cmpForB = shiftedCopy(pA->currentData, -deltaB, pB->currentData.size());

    for (auto *sub : m_mdi->subWindowList()) {
        auto *pv = qobject_cast<ProjectView *>(sub->widget());
        if (!pv) continue;
        Project *p = pv->project();
        if (p == pA) {
            if (auto *ww = pv->waveformWidget()) ww->setComparisonData(cmpForA);
            if (auto *hw = pv->hexWidget())      hw->setComparisonData(cmpForA);
        } else if (p == pB) {
            if (auto *ww = pv->waveformWidget()) ww->setComparisonData(cmpForB);
            if (auto *hw = pv->hexWidget())      hw->setComparisonData(cmpForB);
        }
    }
}

void MainWindow::onDiffAlignmentChanged()
{
    // Alignment also affects which bytes the comparison overlay should show
    // — recompute the shifted copies first, *then* slide the views.
    onDiffComparisonChanged();
    // Alignment offsets just changed (◀ ▶ nudge, spinbox edit, or Reset).
    // Re-run sync from A's current waveform/hex position so B and C slide
    // to their new aligned positions without the user having to scroll
    // anything.  This is what makes the panel's offset controls *feel*
    // like sliding the waveform on screen.
    if (!m_diffPanel) return;
    Project *pA = m_diffPanel->projectA();
    if (!pA) return;
    // Locate A's ProjectView
    ProjectView *vA = nullptr;
    for (auto *sub : m_mdi->subWindowList()) {
        auto *pv = qobject_cast<ProjectView *>(sub->widget());
        if (pv && pv->project() == pA) { vA = pv; break; }
    }
    if (!vA) return;

    // For each other view, translate A's current position via the
    // alignment map and apply.  Both waveform and hex are kept in sync
    // independently — whichever the user is looking at, the move will
    // be visible.
    auto *waveA = vA->waveformWidget();
    auto *hexA  = vA->hexWidget();
    const int waveAOff = waveA ? waveA->scrollOffset() : 0;
    const int hexAOff  = hexA
        ? hexA->verticalScrollBar()->value() * hexA->bytesPerRow()
        : 0;

    for (auto *sub : m_mdi->subWindowList()) {
        auto *pv = qobject_cast<ProjectView *>(sub->widget());
        if (!pv || pv == vA) continue;
        Project *pT = pv->project();
        if (!pT) continue;
        if (auto *ww = pv->waveformWidget()) {
            qint64 t = m_diffPanel->translate(pA, waveAOff, pT);
            if (t >= 0) ww->syncScrollTo(static_cast<int>(t));
        }
        if (auto *hw = pv->hexWidget()) {
            qint64 t = m_diffPanel->translate(pA, hexAOff, pT);
            if (t >= 0) hw->syncScrollTo(static_cast<int>(t));
        }
    }
}

void MainWindow::onDiffRowActivated(quint32 addressA)
{
    // The diff table's address is in A's coordinate space.  Each sibling
    // project (B and the optional target C) lives at its own delta
    // determined by the alignment map, so we translate per-project before
    // navigating.  Without this step, picking a row would jump A correctly
    // but land B/C on stale (mis-aligned) data.
    if (!m_diffPanel) return;
    Project *pA = m_diffPanel->projectA();
    Project *pB = m_diffPanel->projectB();
    Project *pC = m_diffPanel->projectC();
    const qint64 addrA = static_cast<qint64>(addressA);
    const qint64 addrB = m_diffPanel->mapAtoB(addrA);
    const qint64 addrC = m_diffPanel->mapAtoC(addrA);

    for (auto *sub : m_mdi->subWindowList()) {
        auto *pv = qobject_cast<ProjectView *>(sub->widget());
        if (!pv) continue;
        Project *p = pv->project();
        qint64 target = -1;
        if      (p == pA) target = addrA;
        else if (p == pB) target = addrB;
        else if (p == pC) target = addrC;
        else continue;
        if (target < 0) continue;
        if (auto *hw = pv->hexWidget())      hw->goToAddress(static_cast<quint32>(target));
        if (auto *ww = pv->waveformWidget()) ww->goToAddress(static_cast<quint32>(target));
    }
}

// ── Display format ─────────────────────────────────────────────────────────────

void MainWindow::onDataSizeChanged()
{
    if      (m_act8bit->isChecked())  { m_dataSize = 1; m_dataFloat = false; }
    else if (m_act16bit->isChecked()) { m_dataSize = 2; m_dataFloat = false; }
    else if (m_act32bit->isChecked()) { m_dataSize = 4; m_dataFloat = false; }
    else if (m_actFloat->isChecked()) { m_dataSize = 4; m_dataFloat = true;  }
    applyDisplayFormat();
}

void MainWindow::onDisplayFmtChanged()
{
    if      (m_actDec->isChecked()) m_displayFmt = 0;
    else if (m_actHex->isChecked()) m_displayFmt = 1;
    else if (m_actBin->isChecked()) m_displayFmt = 2;
    else if (m_actPct->isChecked()) m_displayFmt = 3;
    applyDisplayFormat();
}

void MainWindow::onByteOrderChanged()
{
    m_byteOrder = m_actLo->isChecked()
                  ? ByteOrder::LittleEndian : ByteOrder::BigEndian;
    applyDisplayFormat();
}

void MainWindow::onSignChanged()
{
    m_signed = m_actSigned->isChecked();
    applyDisplayFormat();
}

void MainWindow::applyDisplayFormat()
{
    // Hex editor always shows hex — only cell grouping and byte order apply to it.
    // Dec/Hex/Bin/Signed format buttons are for the map overlay only.
    for (auto *sub : m_mdi->subWindowList()) {
        if (auto *pv = qobject_cast<ProjectView *>(sub->widget()))
            pv->setDisplayParams(m_dataSize, m_byteOrder, 0 /* hex */, false);
    }

    // Push full format params to open overlays
    for (auto &ov : std::as_const(m_overlays))
        if (ov && ov->isVisible())
            ov->setDisplayParams(m_dataSize, m_byteOrder, m_heightColors);

    if (m_diffPanel)
        m_diffPanel->setDisplayParams(m_dataSize, m_byteOrder,
                                      m_displayFmt, m_signed);
}

// ── Event handlers ─────────────────────────────────────────────────────────────

void MainWindow::onSubWindowActivated(QMdiSubWindow *sw)
{
    // Auto-save mode: onFocusChange — fire when the active sub-window changes.
    if (rx14::appSettings().value("autoSaveMode", "afterDelay")
            .toString() == "onFocusChange") {
        autoSaveAll();
    }
    if (!sw) {
        // Last subwindow closed — reset to bare app title with version
#ifdef RX14_PRO_BUILD
        setWindowTitle(QStringLiteral("romHEX 14  \u2014  v") + qApp->applicationVersion());
#else
        setWindowTitle(QStringLiteral("romHEX 14 Community  \u2014  v") + qApp->applicationVersion());
#endif
        if (m_savepoints)
            m_savepoints->attachTo(nullptr);
        return;
    }
    auto *pv = qobject_cast<ProjectView *>(sw->widget());
    if (!pv) return;

    // Refresh the control-module (BCM) dock for the newly-active project so it
    // auto-reveals when the active project holds a recognized module dump.
    refreshModulePanel();

    // Sync byte-order toolbar to this project
    if (pv->project()) {
        m_byteOrder = pv->project()->byteOrder;
        m_actLo->blockSignals(true); m_actHi->blockSignals(true);
        m_actLo->setChecked(m_byteOrder == ByteOrder::LittleEndian);
        m_actHi->setChecked(m_byteOrder == ByteOrder::BigEndian);
        m_actLo->blockSignals(false); m_actHi->blockSignals(false);
    }

    setWindowTitle(pv->project()
        ? pv->project()->fullTitle() + QStringLiteral("  —  romHEX 14 v") + qApp->applicationVersion()
        : QStringLiteral("romHEX 14  —  v") + qApp->applicationVersion());

    // Update AI assistant project context
    if (m_aiAssistant && pv->project()) {
        m_aiAssistant->setProject(pv->project());
        m_aiAssistant->setAllProjects(m_projects);
    }

    // Re-bind the savepoint manager to the now-active project so the
    // Tuning Branches panel always reflects the project the user is
    // editing.  Sidecar JSON for the previous project is auto-saved
    // by SavepointManager::attachTo().
    if (m_savepoints && pv->project())
        m_savepoints->attachTo(pv->project());

    // Apply current toolbar display params and font size to the newly activated view
    applyDisplayFormat();
    if (auto *v = activeView()) v->setFontSize(m_fontSize);
}

void MainWindow::showMapOverlay(const QByteArray &romData, const MapInfo &map,
                                ByteOrder byteOrder, Project *project)
{
    // Unresolvable 1×1 VALUE: neighbours disagreed, address is a pure guess.
    // Showing the overlay would display data from a likely-wrong address — be honest instead.
    const bool isValue      = (map.type == "VALUE" ||
                               (map.dimensions.x <= 1 && map.dimensions.y <= 1));
    const bool unresolvable = (map.linkConfidence == 40);
    if (isValue && unresolvable) {
        QMessageBox info(this);
        info.setWindowTitle(tr("Value not located — %1").arg(map.name));
        info.setIcon(QMessageBox::Warning);

        // Read original value from the reference ROM if available (project->originalData)
        QString origVal;
        if (project && !project->originalData.isEmpty() &&
            (int)map.address + map.dataSize <= project->originalData.size())
        {
            const uchar *p = reinterpret_cast<const uchar *>(
                                 project->originalData.constData()) + map.address;
            uint32_t raw = 0;
            if (byteOrder == ByteOrder::BigEndian) {
                for (int b = 0; b < map.dataSize; ++b) raw = (raw << 8) | p[b];
            } else {
                for (int b = map.dataSize - 1; b >= 0; --b) raw = (raw << 8) | p[b];
            }
            origVal = tr("Reference ROM value: <b>%1</b> (0x%2)")
                          .arg(raw).arg(raw, 0, 16);
        }

        info.setText(tr("<b>%1</b> could not be located in the linked ROM.")
                         .arg(map.name));
        info.setInformativeText(
            tr("The surrounding maps disagreed on the address shift, so RomHEX 14 "
               "cannot safely determine where this value lives in the target ROM.\n\n"
               "%1\n\n"
               "Description: %2")
                .arg(origVal.isEmpty() ? tr("(Reference ROM not available)") : origVal)
                .arg(map.description.isEmpty() ? map.name : map.description));
        info.setStandardButtons(QMessageBox::Ok);
        info.exec();
        return;
    }

    // Each activation gets its own tool window. This deliberately does not
    // reuse a map-name keyed dialog: tuners often need several maps visible
    // at once, including two views of the same map.
    auto *ov = new MapOverlay(this);
    m_overlays.append(ov);
    connect(ov, &QObject::destroyed, this, [this]() {
        m_overlays.removeAll(QPointer<MapOverlay>());
    });

    // Wire ROM patch write-back.
    if (project) {
        QPointer<Project> projPtr(project);

        // romPatchReady fires once PER CELL — patch silently, no tree rebuild
        connect(ov, &MapOverlay::romPatchReady,
                this, [projPtr](uint32_t offset, QByteArray bytes) {
            if (!projPtr) return;
            if ((int)(offset + bytes.size()) > projPtr->currentData.size()) return;
            auto *dst = reinterpret_cast<uint8_t*>(projPtr->currentData.data());
            std::memcpy(dst + offset, bytes.constData(), bytes.size());
            projPtr->modified = true;
            // NOTE: do NOT emit dataChanged here — editBatchDone does it once
        });

        // editBatchDone fires ONCE per user action (applyDelta / undo / redo)
        connect(ov, &MapOverlay::editBatchDone,
                this, [projPtr, this]() {
            if (!projPtr) return;
            emit projPtr->dataChanged();   // triggers exactly one refreshProjectTree
        });

        // addressCorrected — user manually confirmed the correct address
        connect(ov, &MapOverlay::addressCorrected,
                this, [projPtr, this](const QString &mapName, uint32_t newAddress) {
            if (!projPtr) return;
            // Update the map in the project and persist the confidence override
            for (auto &m : projPtr->maps) {
                if (m.name == mapName) {
                    m.address         = newAddress;
                    m.linkConfidence  = 95;
                    projPtr->modified = true;
                    break;
                }
            }
            refreshProjectTree();
        });

        // Share this project's view editor so overlay edits land on the
        // same undo stack as the hex / waveform / 3D views, and route the
        // overlay's embedded 3D "Edit map" menu through the shared path.
        WaveformEditor *sharedEd = nullptr;
        for (auto *sub : m_mdi->subWindowList()) {
            auto *pv = qobject_cast<ProjectView *>(sub->widget());
            if (pv && pv->project() == project && pv->waveformWidget()) {
                sharedEd = pv->waveformWidget()->editor();
                break;
            }
        }
        ov->setSharedEditor(sharedEd, project);
        connect(ov, &MapOverlay::editOpRequested,
                this, &MainWindow::onEditOpRequestedFromView);
    }
    // Apply AI translation to map description if available
    MapInfo displayMap = map;
    if (m_translations.contains(map.name)) {
        const auto &tx = m_translations[map.name];
        if (!tx.translation.isEmpty())
            displayMap.description = tx.translation;
        if (!tx.description.isEmpty() && displayMap.description != tx.description)
            displayMap.description += "  —  " + tx.description;
    }
    ov->showMap(romData, displayMap, byteOrder,
                project ? project->originalData : QByteArray{});

    if (project) {
        const QString &desc = displayMap.description;
        QString title = desc.isEmpty() || desc == displayMap.name
                          ? displayMap.name
                          : displayMap.name + QStringLiteral("  \u2014  ") + desc;
        ov->setWindowTitle(title);
    }
}

void MainWindow::onMapActivated(const MapInfo &map, Project *project)
{
    // Track recent maps
    if (project) {
        m_recentMaps.removeIf([&](const QPair<Project*,QString> &p){
            return p.first == project && p.second == map.name;
        });
        m_recentMaps.prepend({project, map.name});
        if (m_recentMaps.size() > 20) m_recentMaps.resize(20);
        refreshRecentMapsStrip();
    }

    // Update current map index
    if (project) {
        m_currentMapIdx = project->maps.indexOf(map);
        // Update data-size toolbar AND m_dataSize to match this map's cell size
        const int ds = map.dataSize;
        m_dataSize = ds;  // sync global state so applyDisplayFormat() uses the right size
        m_dataFloat = false;
        m_act8bit->blockSignals(true);  m_act8bit->setChecked(ds == 1);  m_act8bit->blockSignals(false);
        m_act16bit->blockSignals(true); m_act16bit->setChecked(ds == 2); m_act16bit->blockSignals(false);
        m_act32bit->blockSignals(true); m_act32bit->setChecked(ds >= 4); m_act32bit->blockSignals(false);
        m_actFloat->blockSignals(true); m_actFloat->setChecked(false);   m_actFloat->blockSignals(false);
    }
    // Sync byte order from project
    if (project) {
        m_byteOrder = project->byteOrder;
        if (m_actLo) {
            m_actLo->blockSignals(true);
            m_actLo->setChecked(m_byteOrder == ByteOrder::LittleEndian);
            m_actLo->blockSignals(false);
        }
        if (m_actHi) {
            m_actHi->blockSignals(true);
            m_actHi->setChecked(m_byteOrder == ByteOrder::BigEndian);
            m_actHi->blockSignals(false);
        }
    }

    // Show WinOLS-compatible address: file offset of data start (after inline axes)
    statusBar()->showMessage(
        tr("%1  |  %2  |  Addr: %3  |  %4×%5")
            .arg(map.name, map.type)
            .arg(map.address + map.mapDataOffset, 0, 16).toUpper()
            .arg(map.dimensions.x).arg(map.dimensions.y));

    // Feed selected map context to AI assistant
    if (m_aiAssistant) m_aiAssistant->setSelectedMap(map);
}

void MainWindow::openMapViewer(Project *project, const MapInfo &map)
{
    if (!project) return;
    const int len = map.length > 0 ? map.length
              : map.dimensions.x * map.dimensions.y * map.dataSize;
    const int dataLen = qMax(map.dataSize, len);
    const int endOffset = static_cast<int>(map.address + map.mapDataOffset) + dataLen;
    if (map.address > static_cast<uint32_t>(INT_MAX)
        || endOffset > project->currentData.size()) {
        statusBar()->showMessage(
            tr("Map \"%1\" address 0x%2 is outside ROM bounds — skipped")
                .arg(map.name).arg(map.address, 0, 16).toUpper(), 5000);
        return;
    }
    showMapOverlay(project->currentData, map, project->byteOrder, project);
}

// Rebuild the "Recent Maps" chip strip above the project tree from
// m_recentMaps (newest first). Up to 5 chips are shown; clicking a chip
// re-opens the matching map via onMapActivated(). When the list is empty
// a small italic "no recent maps yet" placeholder is shown instead.
void MainWindow::refreshRecentMapsStrip()
{
    if (!m_recentMapsRow || !m_recentMapsStrip) return;

    // Tear down everything except the persistent empty-state label
    while (m_recentMapsRow->count() > 0) {
        QLayoutItem *item = m_recentMapsRow->takeAt(0);
        if (!item) continue;
        if (QWidget *w = item->widget()) {
            if (w == m_recentMapsEmpty) {
                w->hide();
            } else {
                w->setParent(nullptr);
                w->deleteLater();
            }
        }
        delete item;
    }

    if (m_recentMaps.isEmpty()) {
        if (m_recentMapsEmpty) {
            m_recentMapsRow->addWidget(m_recentMapsEmpty);
            m_recentMapsEmpty->show();
        }
        m_recentMapsRow->addStretch();
        return;
    }

    const QString chipSS = QString(
        "QPushButton { background:#1c2330; color:" + AppConfig::instance().colors.uiText.name() + ";"
        " border:1px solid " + AppConfig::instance().colors.uiBorder.name() + "; border-radius:11px;"
        " font-size:8pt; padding:0 8px; }"
        "QPushButton:hover { border-color:" + AppConfig::instance().colors.uiAccent.lighter(140).name() + "; color:#ffffff;"
        " background:#1f2a3d; }"
        "QPushButton:pressed { background:" + AppConfig::instance().colors.uiAccent.name() + "; color:#ffffff; }");

    const int kMax = 5;
    int count = 0;
    for (const auto &entry : m_recentMaps) {
        if (count >= kMax) break;
        Project *proj = entry.first;
        const QString &name = entry.second;
        if (!proj) continue;
        // Validate that the project is still tracked
        if (!m_projects.contains(proj)) continue;

        QString shown = name;
        if (shown.size() > 14)
            shown = shown.left(13) + QStringLiteral("\u2026");

        auto *chip = new QPushButton(shown);
        chip->setCursor(Qt::PointingHandCursor);
        chip->setFixedHeight(24);
        chip->setStyleSheet(chipSS);
        chip->setToolTip(name + QStringLiteral("  \u2014  ") + proj->displayName());

        connect(chip, &QPushButton::clicked, this, [this, proj, name]() {
            if (!m_projects.contains(proj)) return;
            for (const auto &m : proj->maps) {
                if (m.name == name) {
                    onMapActivated(m, proj);
                    return;
                }
            }
            // Fall back to auto-detected list if not found in maps
            for (const auto &m : proj->autoDetectedMaps) {
                if (m.name == name) {
                    onMapActivated(m, proj);
                    return;
                }
            }
        });
        m_recentMapsRow->addWidget(chip);
        ++count;
    }
    m_recentMapsRow->addStretch();
}

void MainWindow::onStatusMessage(const QString &msg)
{
    statusBar()->showMessage(msg);
}

void MainWindow::actBuildSimilarityIndex()
{
    auto *cfg = new winols::Config();
    QStringList roots = cfg->scanFallback();

    // First-run convenience: auto-import paths from ols.cfg if user
    // hasn't configured any roots yet.  Same logic ProjectManagerDlg
    // uses on its WinOLS tab.
    if (roots.isEmpty()) {
        const auto found = winols::OlsCfgParser::extractScanRoots();
        if (!found.isEmpty()) {
            cfg->setScanFallback(found);
            roots = found;
        }
    }
    // dbRoots may also contain Cache_*.db's parent dir which often
    // sits next to the actual .ols folders — include them as scan
    // candidates too.
    for (const QString &r : cfg->dbRoots()) {
        if (!roots.contains(r)) roots << r;
    }

    if (roots.isEmpty()) {
        QMessageBox::warning(this, tr("Similarity index"),
            tr("No scan roots configured.\n\nOpen Project Manager "
               "→ WinOLS Catalog → Settings to import paths from "
               "WinOLS' ols.cfg first."));
        delete cfg;
        return;
    }

    // Confirmation: list the roots, total file count estimate, ETA.
    QString rootsList;
    for (const QString &r : roots) rootsList += "  " + r + "\n";
    auto reply = QMessageBox::question(this, tr("Build similarity index"),
        tr("Scan the following %1 folder(s) recursively for .ols / "
           ".kp / .bin / .rom files and build the similarity index?\n\n"
           "%2\n"
           "This may take a while (potentially hours for terabyte "
           "collections).  You can pause or cancel at any point — "
           "already-processed files are kept.")
            .arg(roots.size())
            .arg(rootsList),
        QMessageBox::Ok | QMessageBox::Cancel,
        QMessageBox::Ok);
    if (reply != QMessageBox::Ok) {
        delete cfg;
        return;
    }

    auto *idx = new winols::SimilarityIndex(this);
    QString err;
    if (!idx->open(&err)) {
        QMessageBox::critical(this, tr("Similarity index"),
            tr("Could not open index database:\n%1").arg(err));
        delete idx;
        delete cfg;
        return;
    }
    winols::BuildIndexProgressDlg dlg(idx, roots, this);
    dlg.exec();
    delete idx;
    delete cfg;
    statusBar()->showMessage(tr("Similarity index ready"), 4000);
}

void MainWindow::actFindSimilarFiles()
{
    Project *p = activeProject();
    if (!p || p->currentData.isEmpty()) {
        statusBar()->showMessage(
            tr("Open a project first to find similar files."), 4000);
        return;
    }
    // Compare against originalData if it differs from currentData
    // (a virgin / un-edited ROM gives the cleanest fingerprint), but
    // fall back to currentData if the project has no separate baseline.
    const QByteArray &needleBytes = !p->originalData.isEmpty()
        ? p->originalData : p->currentData;
    auto *dlg = new winols::SimilarFilesDlg(
        p->filePath.isEmpty() ? p->name : p->filePath, needleBytes, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &winols::SimilarFilesDlg::openSimilarRequested,
            this, [this](const QString &path) {
        qCInfo(catFind) << "openSimilarRequested path=" << path;
        QElapsedTimer total; total.start();
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            qCWarning(catFind) << "  cannot open file:" << f.errorString();
            QMessageBox::warning(this, tr("Open similar"),
                tr("Could not read file: %1").arg(f.errorString()));
            return;
        }
        QElapsedTimer step; step.start();
        QByteArray fileData = f.readAll();
        f.close();
        qCInfo(catFind) << "  read" << fileData.size() << "B in"
                        << step.elapsed() << "ms";

        step.restart();
        ols::OlsImportResult result;
        try {
            result = ols::OlsImporter::importFromBytes(fileData);
        } catch (const std::exception &e) {
            qCCritical(catFind) << "  importFromBytes threw:" << e.what();
            QMessageBox::critical(this, tr("Import error"),
                tr("OLS import threw: %1").arg(QString::fromUtf8(e.what())));
            return;
        } catch (...) {
            qCCritical(catFind) << "  importFromBytes threw unknown";
            QMessageBox::critical(this, tr("Import error"),
                tr("OLS import threw an unknown exception"));
            return;
        }
        qCInfo(catFind) << "  importFromBytes done in" << step.elapsed()
                        << "ms; error=" << result.error
                        << " warnings=" << result.warnings.size()
                        << " versions=" << result.versions.size();
        if (!result.error.isEmpty()) {
            QMessageBox::critical(this, tr("Import error"), result.error);
            return;
        }
        step.restart();
        QVector<Project*> projects =
            ols::buildProjectsFromOlsImport(result, path, this);
        qCInfo(catFind) << "  buildProjectsFromOlsImport in"
                        << step.elapsed() << "ms →"
                        << projects.size() << "projects";
        for (Project *p : projects) {
            m_projects.append(p);
            QElapsedTimer t; t.start();
            openProject(p);
            qCInfo(catFind) << "  openProject in" << t.elapsed()
                            << "ms (project mapCount="
                            << p->maps.size() << ")";
        }
        // openProject() already runs broadcastAvailableProjects() + refreshProjectTree()
        // internally; calling them again here is redundant and just slows the open.
        qCInfo(catFind) << "openSimilarRequested done; total="
                        << total.elapsed() << "ms";
    });

    // "Open as comparison" — same import flow as Open, then pop the
    // existing Compare-Hex dialog so the user lands directly in diff view.
    connect(dlg, &winols::SimilarFilesDlg::compareWithRequested,
            this, [this](const QString &path) {
        qCInfo(catFind) << "compareWithRequested path=" << path;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, tr("Open as comparison"),
                tr("Could not read file: %1").arg(f.errorString()));
            return;
        }
        const QByteArray fileData = f.readAll();
        f.close();
        if (fileData.isEmpty()) {
            QMessageBox::warning(this, tr("Open as comparison"),
                tr("File is empty: %1").arg(path));
            return;
        }
        ols::OlsImportResult result;
        try { result = ols::OlsImporter::importFromBytes(fileData); }
        catch (const std::exception &e) {
            QMessageBox::critical(this, tr("Import error"),
                tr("OLS import threw: %1").arg(QString::fromUtf8(e.what())));
            return;
        } catch (...) {
            QMessageBox::critical(this, tr("Import error"),
                tr("OLS import threw an unknown exception"));
            return;
        }
        if (!result.error.isEmpty()) {
            QMessageBox::critical(this, tr("Import error"), result.error);
            return;
        }
        const auto projects =
            ols::buildProjectsFromOlsImport(result, path, this);
        Project *justOpened = nullptr;
        for (Project *p : projects) {
            m_projects.append(p);
            openProject(p);
            if (!justOpened) justOpened = p;
        }
        // Auto-pop HexCompareDlg with the active project on the left and
        // the newly-opened file on the right.
        if (justOpened) {
            // Defer the HexCompareDlg creation until after the MDI cascade
            // from openProject() above settles.  Showing a top-level dialog
            // mid-cascade has crashed in QMdiArea::resizeEvent on some
            // builds (likely a QPointer-into-deleted-subwindow race).
            QPointer<MainWindow> self = this;
            QPointer<Project>    rightP = justOpened;
            QTimer::singleShot(0, this, [self, rightP]() {
                if (!self || !rightP) return;
                Project *leftP = nullptr;
                for (Project *p : self->m_projects)
                    if (p && p != rightP) { leftP = p; break; }
                auto *cmp = new HexCompareDlg(
                    QList<Project *>(self->m_projects.begin(),
                                     self->m_projects.end()),
                    leftP, self.data(), rightP.data());
                cmp->setAttribute(Qt::WA_DeleteOnClose);
                cmp->show();
                cmp->raise();
                cmp->activateWindow();
            });
        }
        statusBar()->showMessage(
            tr("Opened %1 as comparison.").arg(QFileInfo(path).fileName()),
            4000);
    });

    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void MainWindow::onCloneVersionRequested(Project *parent, int versionIndex)
{
    if (!parent) return;

    // Snapshot bytes for the chosen version.  versionIndex == -1 means
    // "current working copy" (Project::currentData).  Anything >= 0
    // indexes into Project::versions[].
    QByteArray bytes;
    QString    label;
    if (versionIndex < 0) {
        bytes = parent->currentData;
        label = tr("Current");
    } else if (versionIndex < parent->versions.size()) {
        const auto &v = parent->versions[versionIndex];
        bytes = v.data;
        label = v.name.isEmpty()
                  ? tr("Version %1").arg(versionIndex + 1) : v.name;
    } else {
        QMessageBox::warning(this, tr("Open version"),
            tr("Version index %1 out of range").arg(versionIndex));
        return;
    }
    if (bytes.isEmpty()) {
        QMessageBox::warning(this, tr("Open version"),
            tr("That version has no ROM data."));
        return;
    }

    // Build an ephemeral sibling Project mirroring the parent's
    // metadata but with the chosen version's bytes as currentData.
    // No filePath -> autosave will not overwrite the parent's file;
    // user can Save As to persist.
    auto *clone = new Project(this);
    clone->name           = parent->name + QStringLiteral(" — ") + label;
    clone->brand          = parent->brand;
    clone->model          = parent->model;
    clone->vehicleType    = parent->vehicleType;
    clone->ecuType        = parent->ecuType;
    clone->ecuProducer    = parent->ecuProducer;
    clone->ecuSwNumber    = parent->ecuSwNumber;
    clone->ecuNrEcu       = parent->ecuNrEcu;
    clone->ecuNrProd      = parent->ecuNrProd;
    clone->year           = parent->year;
    clone->byteOrder      = parent->byteOrder;
    clone->baseAddress    = parent->baseAddress;
    clone->createdAt      = QDateTime::currentDateTime();
    clone->createdBy      = parent->createdBy;
    clone->maps           = parent->maps;
    clone->groups         = parent->groups;
    clone->a2lContent     = parent->a2lContent;
    clone->starredMaps    = parent->starredMaps;
    clone->currentData    = bytes;
    clone->originalData   = bytes;     // each clone is its own baseline
    clone->modified       = false;

    m_projects.append(clone);
    openProject(clone);
    broadcastAvailableProjects();
    refreshProjectTree();
    statusBar()->showMessage(
        tr("Opened %1 alongside %2").arg(label, parent->name), 5000);
}

void MainWindow::onTreeItemClicked(QTreeWidgetItem *item, int)
{
    // Version-snapshot click → ask to restore that version in the owning
    // project's MDI window. UserRole+4 holds the snapshot index.
    auto versionVar = item->data(0, Qt::UserRole + 4);
    auto projRootVar = item->data(0, Qt::UserRole);
    auto mapVar      = item->data(0, Qt::UserRole + 2);
    if (versionVar.isValid() && projRootVar.isValid() && !mapVar.isValid()) {
        auto *proj = static_cast<Project *>(projRootVar.value<void *>());
        const int vIdx = versionVar.toInt();
        if (proj && vIdx >= 0 && vIdx < proj->versions.size()) {
            // Raise the project's MDI window first so any confirmation
            // dialog is centered on the right window.
            openProject(proj);
            const QString vname = proj->versions[vIdx].name;
            auto res = QMessageBox::question(this, tr("Switch Version"),
                tr("Switch to version <b>%1</b>?<br>"
                   "Unsaved changes to the current ROM will be lost.")
                    .arg(vname),
                QMessageBox::Yes | QMessageBox::Cancel);
            if (res == QMessageBox::Yes) {
                proj->restoreVersion(vIdx);
                statusBar()->showMessage(
                    tr("Switched to version %1").arg(vname), 3000);
            }
        }
        return;
    }

    // Project root click → show or reopen its MDI window
    if (projRootVar.isValid() && !mapVar.isValid()) {
        auto *proj = static_cast<Project *>(projRootVar.value<void *>());
        if (proj) openProject(proj);   // openProject already handles re-show
        return;
    }

    // Only act on map leaves (have UserRole+2 data)
    if (!mapVar.isValid()) return;

    auto map   = mapVar.value<MapInfo>();
    auto *proj = static_cast<Project *>(
                     item->data(0, Qt::UserRole + 1).value<void *>());
    if (!proj) return;

    // Selecting a sidebar map only navigates and highlights the corresponding
    // hex range. Opening the editable map window is a double-click action.
    openProject(proj);
    if (auto *pv = activeView()) pv->showMap(map);
    m_currentMapIdx = proj->maps.indexOf(map);
    onMapActivated(map, proj);
}

// ── Misc ───────────────────────────────────────────────────────────────────────

void MainWindow::editProjectInfo()
{
    auto *proj = activeProject();
    if (!proj) return;
    ProjectPropertiesDialog dlg(proj, this);
    if (dlg.exec() == QDialog::Accepted)
        refreshProjectTree();
}

// ── Auto-detect Maps (WinOLS-style scanner) ──────────────────────────────────
// Wires ols::MapAutoDetect into the Misc menu. If a project is open we
// scan its currentData; otherwise we fall back to a file-picker. Results are
// shown in a sortable table dialog with "Add selected to project" wired to
// create new MapInfo entries on the active project.
void MainWindow::actAutoDetectMaps()
{
    Project *proj = activeProject();
    QByteArray romBytes;
    quint32    baseAddr = 0;
    QString    sourceLabel;

    if (proj && !proj->currentData.isEmpty()) {
        romBytes    = proj->currentData;
        baseAddr    = proj->baseAddress;
        sourceLabel = proj->displayName();
    } else {
        // Fallback: file picker (no project loaded)
        QString path = QFileDialog::getOpenFileName(
            this, tr("Select ROM to scan"),
            QString(), tr("ROM files (*.bin *.hex *.rom *.mpc);;All files (*)"));
        if (path.isEmpty()) return;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, tr("Auto-detect Maps"),
                                 tr("Could not open file:\n%1").arg(path));
            return;
        }
        romBytes    = f.readAll();
        sourceLabel = QFileInfo(path).fileName();
    }
    if (romBytes.isEmpty()) {
        QMessageBox::information(this, tr("Auto-detect Maps"),
                                 tr("ROM is empty — nothing to scan."));
        return;
    }

    // ── Run the scan with a busy progress dialog (it may take 5-30s) ────
    QProgressDialog progress(
        tr("Scanning ROM for map candidates…\nSource: %1\nSize: %2 KB")
            .arg(sourceLabel).arg(romBytes.size() / 1024),
        QString(), 0, 0, this);
    progress.setWindowTitle(tr("Auto-detect Maps"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);
    progress.setValue(0);
    progress.show();
    QApplication::processEvents();

    QFutureWatcher<QVector<ols::MapCandidate>> watcher;
    QEventLoop loop;
    connect(&watcher,
            &QFutureWatcher<QVector<ols::MapCandidate>>::finished,
            &loop, &QEventLoop::quit);
    QFuture<QVector<ols::MapCandidate>> fut = QtConcurrent::run(
        [romBytes, baseAddr]() {
            ols::MapAutoDetectOptions opts;
            opts.topN = 500;   // strict mode: top candidates only (#25)
            return ols::MapAutoDetect::scan(romBytes, baseAddr, opts);
        });
    watcher.setFuture(fut);
    loop.exec();
    progress.close();

    QVector<ols::MapCandidate> candidates = fut.result();
    if (candidates.isEmpty()) {
        QMessageBox::information(this, tr("Auto-detect Maps"),
            tr("No map candidates found in the ROM."));
        return;
    }

    // ── Result dialog: sortable table + Add/Cancel ─────────────────────
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Auto-detected Map Candidates — %1 (%2)")
                           .arg(sourceLabel).arg(candidates.size()));
    dlg.resize(900, 600);

    auto *layout = new QVBoxLayout(&dlg);
    auto *info = new QLabel(tr("Found %1 candidate map(s). Select rows and click "
                               "“Add selected” to create them in the active project.")
                                .arg(candidates.size()), &dlg);
    info->setWordWrap(true);
    layout->addWidget(info);

    auto *table = new QTableWidget(candidates.size(), 6, &dlg);
    table->setHorizontalHeaderLabels(
        QStringList() << tr("Name") << tr("Address") << tr("W × H")
                      << tr("Bits") << tr("Score") << tr("Reason"));
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSortingEnabled(false);    // populate first, then enable
    table->verticalHeader()->setVisible(false);

    for (int i = 0; i < candidates.size(); ++i) {
        const auto &c = candidates[i];
        auto *nameItem = new QTableWidgetItem(c.name);
        nameItem->setData(Qt::UserRole, i);     // back-ref into vector
        table->setItem(i, 0, nameItem);

        auto *addrItem = new QTableWidgetItem(
            QStringLiteral("0x%1").arg(c.romAddress, 8, 16, QLatin1Char('0')).toUpper());
        addrItem->setData(Qt::UserRole, c.romAddress);   // numeric sort key
        table->setItem(i, 1, addrItem);

        auto *dimItem = new QTableWidgetItem(QStringLiteral("%1 × %2")
                                                 .arg(c.width).arg(c.height));
        dimItem->setData(Qt::UserRole, int(c.width * c.height));
        table->setItem(i, 2, dimItem);

        auto *bitsItem = new QTableWidgetItem(
            QString::number(int(c.cellBytes) * 8) +
            (c.cellSigned ? QStringLiteral(" s") : QStringLiteral(" u")));
        bitsItem->setData(Qt::UserRole, int(c.cellBytes));
        table->setItem(i, 3, bitsItem);

        auto *scoreItem = new QTableWidgetItem(
            QString::number(c.score, 'f', 3));
        scoreItem->setData(Qt::UserRole, c.score);
        table->setItem(i, 4, scoreItem);

        table->setItem(i, 5, new QTableWidgetItem(c.reason));
    }
    table->setSortingEnabled(true);
    table->sortByColumn(4, Qt::DescendingOrder);   // best score first
    table->resizeColumnsToContents();
    layout->addWidget(table, 1);

    auto *btnRow = new QHBoxLayout();
    auto *addBtn = new QPushButton(tr("Add selected to project"), &dlg);
    auto *cancelBtn = new QPushButton(tr("Close"), &dlg);
    addBtn->setDefault(true);
    addBtn->setEnabled(proj != nullptr);
    if (!proj) {
        addBtn->setToolTip(tr("Open or create a project to import these maps."));
    }
    btnRow->addStretch(1);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(cancelBtn);
    layout->addLayout(btnRow);

    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(addBtn,    &QPushButton::clicked, &dlg, [&]() {
        if (!proj) { dlg.accept(); return; }
        QSet<int> rows;
        for (auto *it : table->selectedItems()) rows.insert(it->row());
        if (rows.isEmpty()) {
            QMessageBox::information(&dlg, tr("Auto-detect Maps"),
                tr("No rows selected."));
            return;
        }
        int added = 0;
        for (int row : rows) {
            QTableWidgetItem *first = table->item(row, 0);
            if (!first) continue;
            int idx = first->data(Qt::UserRole).toInt();
            if (idx < 0 || idx >= candidates.size()) continue;
            const auto &c = candidates[idx];
            MapInfo m;
            m.name           = c.name;
            m.description    = tr("Auto-detected: %1").arg(c.reason);
            m.type           = (c.height <= 1) ? QStringLiteral("CURVE")
                                               : QStringLiteral("MAP");
            m.rawAddress     = c.romAddress;
            m.address        = (c.romAddress >= proj->baseAddress)
                                 ? c.romAddress - proj->baseAddress : c.romAddress;
            m.dimensions.x   = int(c.width);
            m.dimensions.y   = int(c.height);
            m.dataSize       = c.cellBytes;
            m.dataSigned     = c.cellSigned;
            m.length         = m.dimensions.x * m.dimensions.y * m.dataSize;
            m.cellDataType   = (c.cellBytes == 1) ? 1 : (c.bigEndian ? 2 : 3);
            m.cellBigEndian  = c.bigEndian;
            m.linkConfidence = 100;
            // Skip if a map with same name+address already exists.
            bool dup = false;
            for (const auto &ex : proj->maps) {
                if (ex.name == m.name && ex.address == m.address) { dup = true; break; }
            }
            if (!dup) { proj->maps.append(m); ++added; }
        }
        if (added > 0) {
            proj->modified = true;
            emit proj->dataChanged();
            refreshProjectTree();
        }
        QMessageBox::information(&dlg, tr("Auto-detect Maps"),
            tr("Added %1 new map(s) to the project.").arg(added));
        dlg.accept();
    });

    dlg.exec();
}

// ── Helpers ───────────────────────────────────────────────────────────────────

// Cleanup helper invoked AFTER Qt's close-event flow has unwound.
// Doing model mutation (m_projects.removeAll, refreshProjectTree, ...) inside
// the event filter left MDI in an inconsistent state during its post-close
// resize/relayout pass — signals fired with widgets we'd already cut from
// m_projects, producing access violations.  Deferring via QTimer::singleShot(0)
// puts this work on the next event-loop turn, after MDI is done.
void MainWindow::finalizeClosedProject(Project *p)
{
    if (!p) return;
    qCInfo(catFind) << "finalizeClosedProject:" << p->name;
    cancelMapScan(p);
    luaClearLastCreatedMap(p);
    if (m_savepoints && m_savepoints->project() == p)
        m_savepoints->attachTo(nullptr);
    m_projects.removeAll(p);
    m_recentMaps.removeIf([p](const QPair<Project *, QString> &e) {
        return e.first == p;
    });
    // Close every independent map window backed by this project.
    for (auto ov : m_overlays)
        if (ov && ov->targetProject() == p) ov->close();
    m_overlays.removeAll(QPointer<MapOverlay>());
    // If this is a parent, also close any still-open linked-ROM children
    if (!p->isLinkedRom) {
        QVector<Project *> children;
        for (Project *c : m_projects) {
            if (c->isLinkedRom && c->parentProject == p)
                children.append(c);
        }
        for (Project *c : children) {
            cancelMapScan(c);
            if (m_savepoints && m_savepoints->project() == c)
                m_savepoints->attachTo(nullptr);
            m_projects.removeAll(c);
            for (auto *csw : m_mdi->subWindowList()) {
                auto *cpv = qobject_cast<ProjectView *>(csw->widget());
                if (cpv && cpv->project() == c) {
                    csw->removeEventFilter(this);
                    csw->setAttribute(Qt::WA_DeleteOnClose, true);
                    csw->close();
                }
            }
            c->deleteLater();
        }
    }
    p->deleteLater();
    refreshProjectTree();
    refreshRecentMapsStrip();
    broadcastAvailableProjects();
}

bool MainWindow::eventFilter(QObject *obj, QEvent *ev)
{
    if (ev->type() != QEvent::Close)
        return QMainWindow::eventFilter(obj, ev);
    auto *sw = qobject_cast<QMdiSubWindow *>(obj);
    if (!sw)
        return QMainWindow::eventFilter(obj, ev);
    auto *pv = qobject_cast<ProjectView *>(sw->widget());
    Project *proj = pv ? pv->project() : nullptr;
    qCInfo(catFind) << "subwindow close: project="
                    << (proj ? proj->name : QStringLiteral("(none)"))
                    << " modified="
                    << (proj ? proj->modified : false)
                    << " openCount=" << m_projects.size();
    if (!proj)
        return QMainWindow::eventFilter(obj, ev);

    // Modified project → ask the user before letting Qt close the window.
    // The save / discard / cancel dialog is synchronous; everything below
    // deferred to a singleShot.
    //
    // Iter 8.2 v2: under RX14_LUA_TEST=1 (the headless RPC test harness)
    // we MUST NOT show modals — they block the Qt event loop forever
    // because nobody is on the keyboard.  Treat modified as "discard" so
    // close proceeds non-interactively.
    if (proj->modified && qgetenv("RX14_LUA_TEST") != "1") {
        QMessageBox mb(this);
        mb.setWindowTitle(tr("Close Project"));
        mb.setText(QString("<b>%1</b>").arg(proj->fullTitle()));
        mb.setInformativeText(tr("This project has unsaved changes."));
        auto *btnSave    = mb.addButton(tr("Save & Close"),         QMessageBox::AcceptRole);
        auto *btnDiscard = mb.addButton(tr("Close without saving"), QMessageBox::DestructiveRole);
        auto *btnCancel  = mb.addButton(QMessageBox::Cancel);
        mb.exec();

        if (mb.clickedButton() == btnCancel) {
            ev->ignore();   // user cancelled — keep the window open
            return true;
        }
        if (mb.clickedButton() == btnSave) {
            if (proj->filePath.isEmpty()) {
                QString path = QFileDialog::getSaveFileName(
                    this, tr("Save Project As"),
                    ProjectRegistry::defaultProjectDir() + "/" +
                        suggestedProjectBasename(proj) + ".rx14proj",
                    tr("RX14 Projects (*.rx14proj);;All Files (*)"));
                if (path.isEmpty()) { ev->ignore(); return true; }
                proj->saveAs(path);
                ProjectRegistry::instance().registerProject(path, proj);
            } else {
                proj->save();
                ProjectRegistry::instance().registerProject(proj->filePath, proj);
            }
        }
        // btnDiscard or post-save: fall through to deferred close
    }

    // Detach from this subwindow so we don't re-enter once Qt resumes
    // delivering the close event.  WA_DeleteOnClose makes Qt destroy the
    // subwindow when its close completes.
    sw->removeEventFilter(this);
    sw->setAttribute(Qt::WA_DeleteOnClose, true);

    // Defer model cleanup to the next event-loop turn — see comment on
    // finalizeClosedProject().  QPointer guards against the project being
    // taken down by some other path before our timer fires.
    QPointer<Project> projPtr(proj);
    QTimer::singleShot(0, this, [this, projPtr]() {
        if (projPtr)
            finalizeClosedProject(projPtr.data());
    });

    return false;  // pass-through: let Qt actually close the subwindow
}

ProjectView *MainWindow::activeView() const
{
    auto *sw = m_mdi->activeSubWindow();
    if (!sw) {
        // The MDI area transiently reports no active subwindow — e.g. during a
        // drag-and-drop from another app, or when focus is on a dock/menu. Fall
        // back to the most-recently-active project window so actions like
        // "import KP/A2L/XDF" still target the project the user has open.
        const auto history = m_mdi->subWindowList(QMdiArea::ActivationHistoryOrder);
        if (!history.isEmpty()) sw = history.last();
    }
    return sw ? qobject_cast<ProjectView *>(sw->widget()) : nullptr;
}

Project *MainWindow::activeProject() const
{
    auto *v = activeView();
    return v ? v->project() : nullptr;
}

// ── Drag & drop ───────────────────────────────────────────────────────────────

void MainWindow::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *e)
{
    for (const auto &url : e->mimeData()->urls()) {
        QString path = url.toLocalFile();
        // Route by file extension: overlays (KP/A2L/XDF/CSV) apply to the
        // active project; a project file opens; everything else is a ROM.
        const QString ext = QFileInfo(path).suffix().toLower();
        if (ext == QLatin1String("rx14proj")) {
            auto *proj = Project::open(path, this);
            if (proj) openProject(proj);
        } else if (ext == QLatin1String("a2l") || ext == QLatin1String("dam")
                   || ext == QLatin1String("damos")) {
            actImportA2L(path);
        } else if (ext == QLatin1String("kp")) {
            actImportKP(path);
        } else if (ext == QLatin1String("xdf")) {
            actImportXdf(path);
        } else if (ext == QLatin1String("csv")) {
            if (auto *proj = activeProject())
                MapPackDlg::importPack(proj, this, /*preferCsv=*/true, path);
            else
                QMessageBox::information(this, tr("Import Map List"),
                    tr("Open a project with ROM data first to import a map list."));
        } else {
            // Everything else (.bin, .mpc, .ori, .hex, .srec, …) → ROM:
            // create project, load ROM, show properties dialog
            auto *project = new Project(this);
            project->createdAt = QDateTime::currentDateTime();
            project->createdBy = qEnvironmentVariable("USERNAME",
                                     qEnvironmentVariable("USER", "Unknown"));
            loadROMIntoProject(project, path);

            ProjectPropertiesDialog dlg(project, this);
            dlg.setWindowTitle(tr("New Project — Import"));
            if (dlg.exec() != QDialog::Accepted) {
                cancelMapScan(project);
                delete project;
                continue;  // use continue instead of return since we're in a for loop
            }

            QString name = project->displayName();
            if (name.isEmpty()) name = QFileInfo(path).baseName();
            QString dir = ProjectRegistry::defaultProjectDir();
            QDir().mkpath(dir);
            QString savePath = dir + "/" + name + ".rx14proj";
            if (QFileInfo::exists(savePath)) {
                savePath = QFileDialog::getSaveFileName(this, tr("Save Project As"), savePath,
                    tr("RX14 Projects (*.rx14proj);;All Files (*)"));
                if (savePath.isEmpty()) { cancelMapScan(project); delete project; continue; }
            }
            if (project->saveAs(savePath))
                ProjectRegistry::instance().registerProject(savePath, project);
            openProject(project);
        }
    }
}

void MainWindow::applyUiTheme()
{
    const AppColors &c = AppConfig::instance().colors;

    // Load the QSS template and substitute color placeholders
    QFile qssFile(":/style.qss");
    QString qss;
    if (qssFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qss = QString::fromUtf8(qssFile.readAll());
        qss.replace("${uiBg}",          c.uiBg.name());
        qss.replace("${uiPanel}",       c.uiPanel.name());
        qss.replace("${buttonBg}",      c.buttonBg.name());
        qss.replace("${uiBorder}",      c.uiBorder.name());
        qss.replace("${uiAccent}",      c.uiAccent.name());
        qss.replace("${uiAccentHover}", c.uiAccent.lighter(120).name());
        qss.replace("${uiAccentLight}", c.uiAccent.lighter(140).name());
        qss.replace("${uiTextDim}",     c.uiTextDim.name());
        qss.replace("${uiText}",        c.uiText.name());
    }
    qApp->setStyleSheet(qss);

    // MDI area background
    m_mdi->setBackground(QBrush(c.uiBg));

    // Left panel & tree
    auto hex = [](const QColor &col) { return col.name(); };
    auto rgba = [](const QColor &col) {
        return QString("rgba(%1,%2,%3,%4)")
            .arg(col.red()).arg(col.green()).arg(col.blue())
            .arg(QString::number(col.alphaF(), 'f', 2));
    };
    m_projectTree->setStyleSheet(QString(
        "QTreeWidget { font-size:7pt; background:%1; color:%2;"
        "  border:none; alternate-background-color:%3; }"
        "QTreeWidget::item { padding-top:0px; padding-bottom:0px; min-height:14px; }"
        "QTreeWidget::item:selected { background:%4; color:#ffffff; }"
        "QTreeWidget::branch { background:%1; }"
        "QHeaderView::section { background:%5; color:%6; font-size:7pt;"
        "  border:none; border-bottom:1px solid %7; padding:2px 4px; }")
        .arg(hex(c.treeBg))
        .arg(hex(c.uiText))
        .arg(hex(c.uiPanel))
        .arg(rgba(c.treeSelected))
        .arg(hex(c.uiPanel))
        .arg(hex(c.uiTextDim))
        .arg(hex(c.uiBorder)));

    // Update bar
    if (m_updateBar) {
        m_updateBar->setStyleSheet(QString(
            "QFrame{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
            "stop:0 rgba(%1,0.15), stop:0.5 rgba(%1,0.08), stop:1 rgba(%1,0.15));"
            "border-bottom:1px solid rgba(%1,0.4);padding:0}")
            .arg(QString("%1,%2,%3").arg(c.uiAccent.red()).arg(c.uiAccent.green()).arg(c.uiAccent.blue())));
        if (m_updateLabel)
            m_updateLabel->setStyleSheet("color:" + c.uiText.name() + ";font-size:9pt;font-weight:500");
    }

    // Force full repaint on all open views — schedule a deferred
    // repaint so the stylesheet changes have fully propagated.
    QTimer::singleShot(0, this, [this]() {
        for (auto *sub : m_mdi->subWindowList()) {
            if (!sub->widget()) continue;
            sub->widget()->setStyleSheet(sub->widget()->styleSheet());
            sub->widget()->update();
            // Recurse into child widgets (waveform, hex, overlay)
            for (auto *child : sub->widget()->findChildren<QWidget*>())
                child->update();
        }
        // Also repaint the welcome page if visible
        if (m_welcomePage) {
            m_welcomePage->setStyleSheet(m_welcomePage->styleSheet());
            m_welcomePage->update();
            for (auto *child : m_welcomePage->findChildren<QWidget*>())
                child->update();
        }
    });
}

// ── Auto-detect ECU (73-detector chain port) ─────────────────────────────────
// Wires ols::EcuAutoDetect into the Misc menu. Uses the active project's
// ROM if one is loaded, otherwise opens a file picker for raw .bin/.hex/.s19.
// Result is shown in a modal dialog with HW/SW/family/data-area summary.
void MainWindow::actAutoDetectEcu()
{
    Project *proj = activeProject();
    QByteArray romBytes;
    QString    sourceLabel;
    QString    sourcePath;     // for filename-based HEX/SREC sniffing

    if (proj && !proj->currentData.isEmpty()) {
        romBytes    = proj->currentData;
        sourceLabel = proj->displayName();
    } else {
        // Fallback: file picker (no project loaded)
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Select ROM to identify"),
            QString(),
            tr("ECU dumps (*.bin *.hex *.rom *.s19 *.srec *.s28 *.s37 *.mpc);;"
               "All files (*)"));
        if (path.isEmpty()) return;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, tr("Auto-detect ECU"),
                                 tr("Could not open file:\n%1").arg(path));
            return;
        }
        romBytes    = f.readAll();
        sourcePath  = path;
        sourceLabel = QFileInfo(path).fileName();
    }
    if (romBytes.isEmpty()) {
        QMessageBox::information(this, tr("Auto-detect ECU"),
                                 tr("ROM is empty — nothing to detect."));
        return;
    }

    // If the file looks like Intel-HEX or SREC (ASCII envelope), decode it
    // to a flat binary first. This is done unconditionally because some
    // .bin files are actually misnamed HEX, and decode_rom() is a no-op
    // when the bytes are already binary.
    QByteArray flat = ols::EcuAutoDetect::decodeRom(romBytes, sourcePath);
    const bool decoded = flat.size() != romBytes.size();

    // Long flashes (8-19 MB after HEX decode) may take a noticeable beat;
    // a brief busy dialog is friendlier than a frozen UI.
    QProgressDialog progress(
        tr("Identifying ECU…\nSource: %1\nSize: %2 KB%3")
            .arg(sourceLabel).arg(flat.size() / 1024)
            .arg(decoded ? tr(" (decoded from %1 to %2 KB)")
                              .arg(romBytes.size() / 1024)
                              .arg(flat.size() / 1024)
                         : QString()),
        QString(), 0, 0, this);
    progress.setWindowTitle(tr("Auto-detect ECU"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);
    progress.setValue(0);
    progress.show();
    QApplication::processEvents();

    QFutureWatcher<ols::EcuDetectionResult> watcher;
    QEventLoop loop;
    connect(&watcher,
            &QFutureWatcher<ols::EcuDetectionResult>::finished,
            &loop, &QEventLoop::quit);
    QFuture<ols::EcuDetectionResult> fut = QtConcurrent::run(
        [flat]() { return ols::EcuAutoDetect::detect(flat); });
    watcher.setFuture(fut);
    loop.exec();
    progress.close();

    const ols::EcuDetectionResult res = fut.result();

    if (!res.ok) {
        QMessageBox::information(
            this, tr("Auto-detect ECU"),
            tr("No detector matched. The flash does not contain any of the "
               "73 known ECU family anchors. (See "
               "RE/winOLS/winols_analysis/ECU_PATTERN_DB.json for the catalog.)"));
        return;
    }

    // Build a human-readable summary: family / detector / id-block / fields.
    QStringList lines;
    auto add = [&](const QString &k, const QString &v) {
        if (!v.isEmpty())
            lines << QStringLiteral("<b>%1:</b> %2").arg(k.toHtmlEscaped(),
                                                         v.toHtmlEscaped());
    };
    add(tr("Family"),         res.family);
    add(tr("Detector"),       res.detector);
    add(tr("HW number"),      res.hwNumber);
    add(tr("SW number"),      res.swNumber);
    add(tr("SW version"),     res.swVersion);
    add(tr("Production no."), res.productionNo);
    add(tr("HW alt number"),  res.hwAltNumber);
    add(tr("Engine code"),    res.engineCode);
    if (res.idBlockOffset >= 0) {
        add(tr("ID block offset"),
            QStringLiteral("0x%1").arg(res.idBlockOffset, 0, 16));
    }
    if (!res.dataAreas.isEmpty()) {
        lines << QStringLiteral("<br><b>%1:</b><br><tt>%2</tt>")
                     .arg(tr("Data areas").toHtmlEscaped())
                     .arg(res.dataAreas.join("<br>").toHtmlEscaped());
    }
    if (!res.rawIdBlock.isEmpty()) {
        const QString snippet = QString::fromLatin1(res.rawIdBlock.left(160));
        lines << QStringLiteral("<br><b>%1:</b><br><tt>%2</tt>")
                     .arg(tr("Raw ID block").toHtmlEscaped())
                     .arg(snippet.toHtmlEscaped());
    }

    QMessageBox box(this);
    box.setWindowTitle(tr("Auto-detect ECU — %1").arg(sourceLabel));
    box.setIcon(QMessageBox::Information);
    box.setTextFormat(Qt::RichText);
    box.setText(tr("<h3>%1</h3>").arg(res.family.toHtmlEscaped()));
    box.setInformativeText(lines.join("<br>"));
    box.setStandardButtons(QMessageBox::Ok);
    box.exec();
}

// ── ROM linking / compare slots ───────────────────────────────────────────────

void MainWindow::actLinkRom()
{
    auto *proj = activeProject();
    if (!proj) {
        QMessageBox::information(this, tr("No project"),
            tr("Please open a project with maps before linking a ROM."));
        return;
    }
    if (proj->maps.isEmpty()) {
        QMessageBox::warning(this, tr("No maps in active project"),
            tr("The currently active project has no map definitions to "
               "match against.\n\n"
               "Link ROM to Project works by taking the maps from the "
               "ACTIVE project (the reference) and locating them in the "
               "ROM you select.  So the active project must be the one "
               "WITH metadata — typically the .ols / .kp file, or a "
               "project that already has an A2L imported.\n\n"
               "Workflow:\n"
               "  1. Open the reference .ols / .kp / A2L project first "
               "(it provides the map names, addresses, scaling).\n"
               "  2. Make sure that project is the active one.\n"
               "  3. Then Project → Link ROM to Project… and pick the "
               "raw .bin you want to map onto it."));
        return;
    }

    RomLinkDialog dlg(proj, this);
    if (dlg.exec() != QDialog::Accepted) return;

    LinkedRom              lr      = dlg.result();
    const RomLinkSession  &session = dlg.session();

    // ── Store link result on the reference project ─────────────────────
    lr.sourceProjectPath = proj->filePath;  // Track where this linked ROM comes from
    proj->linkedRoms.append(lr);
    const int linkedIndex = proj->linkedRoms.size() - 1;
    proj->modified = true;
    emit proj->linkedRomsChanged();

    // ── Create a new project with the linked ROM data + remapped maps ───
    auto *newProj = new Project(this);
    newProj->name         = lr.label;
    newProj->brand        = proj->brand;
    newProj->model        = proj->model;
    newProj->ecuType      = proj->ecuType;
    newProj->displacement = proj->displacement;
    newProj->year         = proj->year;
    newProj->notes        = proj->notes;
    newProj->byteOrder    = proj->byteOrder;
    newProj->baseAddress  = proj->baseAddress;
    newProj->currentData  = lr.data;
    newProj->originalData = lr.data;
    newProj->a2lContent   = proj->a2lContent;
    newProj->groups       = proj->groups;
    newProj->isLinkedRom  = true;
    newProj->isLinkedReference = lr.isReference;
    newProj->linkedToProjectPath = proj->filePath;  // Store the factory project path
    newProj->linkedFromData = proj->currentData;  // Store factory project's data for comparison
    newProj->parentProject     = proj;            // live-sync back to parent.linkedRoms[idx].data
    newProj->parentLinkedIndex = linkedIndex;

    // Copy every map, adjusting addresses to where the linker found them.
    // Axis data is remapped using its own independently-found offset (Phase 5),
    // falling back to the map delta only when the axis wasn't located directly.
    for (const MapInfo &m : proj->maps) {
        MapInfo nm = m;
        // Set confidence from linker results (0 if map was not processed by linker)
        nm.linkConfidence = lr.mapConfidence.value(m.name, 0);

        if (lr.mapOffsets.contains(m.name)) {
            uint32_t linked = lr.mapOffsets[m.name];
            int64_t  delta  = (int64_t)linked - (int64_t)m.address;
            nm.address    = linked;
            nm.rawAddress = (uint32_t)((int64_t)m.rawAddress + delta);

            // Per-axis remapping — each axis address is looked up from the
            // independent Phase 5 search; fall back to map delta if not found.
            auto remapAxis = [&](AxisInfo &ax) {
                if (!ax.hasPtsAddress) return;
                if (session.axisOffsets.contains(ax.ptsAddress))
                    ax.ptsAddress = session.axisOffsets[ax.ptsAddress];
                else
                    ax.ptsAddress = (uint32_t)((int64_t)ax.ptsAddress + delta);
            };
            remapAxis(nm.xAxis);
            remapAxis(nm.yAxis);
        }
        newProj->maps.append(nm);
    }

    newProj->modified = true;
    m_projects.append(newProj);
    openProject(newProj);
    // openProject() wires the parent-sync connection via wireLinkedRomSync()
    // — see MainWindow::openProject().

    // Tile all visible windows to fill the MDI area (respects AI assistant panel)
    QTimer::singleShot(0, this, [this, proj]() {
        retileWindows();

        // Auto-enable sync cursors for linked ROMs
        if (m_actSyncCursors && !m_actSyncCursors->isChecked())
            m_actSyncCursors->setChecked(true);

        // Step 1: Find first MAP in the original project
        const MapInfo *firstMap = nullptr;
        for (const auto &m : proj->maps) {
            if (m.type == "MAP") { firstMap = &m; break; }
        }
        if (!firstMap && !proj->maps.isEmpty())
            firstMap = &proj->maps.first();

        uint32_t syncAddress = firstMap ? firstMap->address : 0;

        // Step 2: Set up ALL views (load ROM, set maps, switch to 2D) WITHOUT navigating
        QVector<WaveformWidget*> allWaves;
        for (auto *sub : m_mdi->subWindowList()) {
            auto *pv = qobject_cast<ProjectView *>(sub->widget());
            if (!pv || !pv->project()) continue;
            auto *ww = pv->waveformWidget();
            if (!pv->project()->currentData.isEmpty())
                ww->showROM(pv->project()->currentData, pv->hexWidget()->getOriginalData());
            ww->setMaps(pv->project()->maps);
            if (firstMap) {
                for (const auto &m : pv->project()->maps) {
                    if (m.name == firstMap->name) {
                        ww->setCurrentMap(m);
                        break;
                    }
                }
            }
            pv->switchView(1);  // 2D view (tab bar updated)
            allWaves.append(ww);
        }

        // Step 3: Navigate ALL views to the SAME offset AFTER all are set up.
        // Use syncScrollTo (no re-emit) to avoid feedback loops.
        for (auto *ww : allWaves)
            ww->syncScrollTo((int)syncAddress);
    });

    // Show a floating notification near the sync button
    QTimer::singleShot(800, this, [this]() {
        if (!m_actSyncCursors) return;

        // Find the toolbar button widget
        QWidget *btn = nullptr;
        for (auto *tb : findChildren<QToolBar *>()) {
            btn = tb->widgetForAction(m_actSyncCursors);
            if (btn) break;
        }
        if (!btn) return;

        // Create a floating label below the button
        auto *tip = new QLabel(tr("  \u21d4  Cursors are now synchronized.\n"
                                  "       Click this button to unlink them."), this);
        tip->setStyleSheet(
            "QLabel { background:#1f3a6e; color:" + AppConfig::instance().colors.uiText.name() + "; border:1px solid " + AppConfig::instance().colors.uiAccent.lighter(140).name() + ";"
            "  border-radius:6px; padding:8px 14px; font-size:9pt; }");
        tip->setWindowFlags(Qt::ToolTip);
        tip->setAttribute(Qt::WA_DeleteOnClose);
        tip->adjustSize();

        QPoint pos = btn->mapToGlobal(QPoint(btn->width() / 2 - tip->width() / 2, btn->height() + 4));
        tip->move(pos);
        tip->show();

        // Auto-hide after 6 seconds
        QTimer::singleShot(6000, tip, &QLabel::close);

        // Also close on click
        tip->installEventFilter(tip);
    });

    statusBar()->showMessage(tr("Linked ROM '%1' opened — %2/%3 maps located.")
        .arg(lr.label)
        .arg(lr.mapOffsets.size())
        .arg(proj->maps.size()));
}

void MainWindow::actImportLinkedVersion()
{
    auto *proj = activeProject();
    if (!proj) {
        QMessageBox::information(this, tr("No project"), tr("No active project."));
        return;
    }

    // Pick a ROM file and give it a version label
    QString path = QFileDialog::getOpenFileName(this, tr("Import ROM as Version"),
        QString(), tr("ROM files (*.bin *.hex *.rom *.ori *.mpc);;All files (*)"));
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("Error"), tr("Cannot open file:\n%1").arg(path));
        return;
    }
    QByteArray data = f.readAll();
    f.close();

    bool ok;
    QString name = QInputDialog::getText(this, tr("Version Label"),
        tr("Enter a name for this ROM version:"),
        QLineEdit::Normal,
        QFileInfo(path).baseName(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    ProjectVersion v;
    v.name    = name.trimmed();
    v.created = QDateTime::currentDateTime();
    v.data    = data;
    proj->versions.append(v);
    const int newIdx = proj->versions.size() - 1;
    proj->modified = true;
    emit proj->versionsChanged();

    // Also open the imported version as its own sibling project window, so it
    // is a first-class, comparable entity — visible in the tree and usable in
    // the Differences panel (which compares open projects, not in-project
    // version snapshots).  This mirrors how a multi-version .ols opens each
    // version in its own window.  The snapshot stays in the parent's Version
    // dropdown for quick restore.
    onCloneVersionRequested(proj, newIdx);

    statusBar()->showMessage(tr("Version '%1' imported from %2  (%3 bytes).")
        .arg(v.name).arg(QFileInfo(path).fileName()).arg(data.size()));
}

void MainWindow::actCompareRoms()
{
    auto *proj = activeProject();
    if (!proj) {
        QMessageBox::information(this, tr("No project"), tr("No active project."));
        return;
    }
    if (proj->currentData.isEmpty()) {
        QMessageBox::warning(this, tr("No ROM"), tr("The active project has no ROM data loaded."));
        return;
    }

    // Build a menu of candidates: linked ROMs + saved versions
    QStringList labels;
    QVector<QByteArray> roms;
    QVector<QMap<QString, uint32_t>> offsetMaps;

    for (const auto &lr : proj->linkedRoms) {
        labels << tr("[Linked] %1").arg(lr.label);
        roms   << lr.data;
        offsetMaps << lr.mapOffsets;
    }
    for (const auto &v : proj->versions) {
        labels << tr("[Version] %1  (%2)").arg(v.name)
                    .arg(v.created.toString("yyyy-MM-dd HH:mm"));
        roms   << v.data;
        offsetMaps << QMap<QString, uint32_t>{};  // same offsets as current
    }

    if (labels.isEmpty()) {
        QMessageBox::information(this, tr("Nothing to compare"),
            tr("No linked ROMs or saved versions found.\n"
               "Use 'Link ROM to Project…' or 'Import ROM as Version…' first."));
        return;
    }

    bool ok;
    QString chosen = QInputDialog::getItem(this, tr("Compare ROM"),
        tr("Select a ROM to compare against the current data:"),
        labels, 0, false, &ok);
    if (!ok || chosen.isEmpty()) return;

    int idx = labels.indexOf(chosen);
    if (idx < 0) return;

    auto *dlg = new RomCompareDlg(proj, roms[idx], chosen, offsetMaps[idx], this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::actCompareHex()
{
    if (m_projects.isEmpty()) {
        QMessageBox::information(this, tr("No project"), tr("No active project."));
        return;
    }

    auto *dlg = new HexCompareDlg(QList<Project *>(m_projects.begin(), m_projects.end()),
                                  activeProject(), this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::scheduleAutoSave()
{
    if (!m_autoSaveTimer) return;
    // Update the status indicator immediately so the user sees "Modified"
    // the moment they make a change, before the debounced save fires.
    updateAutoSaveStatus();
    const QString mode = rx14::appSettings()
                            .value("autoSaveMode", "afterDelay").toString();
    if (mode == "afterDelay")
        m_autoSaveTimer->start();   // debounce each call
    // off / onFocusChange / onWindowDeactivate handled elsewhere
}

// VSCode-style auto-save: write to the REAL .rx14proj atomically (no
// sidecar), clear the modified flag on success, update the status-bar
// indicator. Project::save() already does atomic-rename + sets
// modified=false on success, so we just call it directly.
//
// For brand-new projects (no filePath yet), we DO NOT silently fall back
// to an autosave directory — that just creates orphan files the user
// never sees. Instead, first auto-save attempt prompts Save As exactly
// once, then subsequent autosaves go to the chosen path. Set
// m_promptedFirstSave to suppress repeated prompts within one session
// for projects that the user explicitly cancelled.
void MainWindow::autoSaveAll()
{
    int saved = 0;
    int needSavePrompt = 0;
    Project *firstNeedsPrompt = nullptr;
    for (Project *proj : m_projects) {
        if (!proj->modified) continue;
        if (proj->filePath.isEmpty()) {
            ++needSavePrompt;
            if (!firstNeedsPrompt && !m_promptedFirstSave.contains(proj))
                firstNeedsPrompt = proj;
            continue;
        }
        if (proj->save()) {   // atomic save, clears modified on success
            ++saved;
            ProjectRegistry::instance().registerProject(proj->filePath, proj);
        } else {
            qWarning() << "Auto-save failed for" << proj->filePath;
        }
    }
    if (saved > 0) {
        m_lastAutoSaveTime = QDateTime::currentDateTime();
        updateAutoSaveStatus();
        // Refresh every subwindow title so the dirty-dot indicator is removed.
        for (auto *sw : m_mdi->subWindowList()) {
            auto *pv = qobject_cast<ProjectView *>(sw->widget());
            if (pv && pv->project() && !pv->project()->modified)
                sw->setWindowTitle(pv->project()->fullTitle());
        }
    }
    // First-time-only Save As prompt for unsaved projects. Once the user
    // either saves or cancels, this won't be re-triggered for that
    // project until they manually do something.
    if (firstNeedsPrompt) {
        m_promptedFirstSave.insert(firstNeedsPrompt);
        const QString defaultDir = ProjectRegistry::defaultProjectDir();
        QDir().mkpath(defaultDir);
        const QString suggested = defaultDir + "/"
            + suggestedProjectBasename(firstNeedsPrompt) + ".rx14proj";
        const QString path = QFileDialog::getSaveFileName(
            this, tr("Auto-save: choose a location for this project"),
            suggested,
            tr("RX14 Projects (*.rx14proj);;All Files (*)"));
        if (!path.isEmpty() && firstNeedsPrompt->saveAs(path)) {
            ProjectRegistry::instance().registerProject(path, firstNeedsPrompt);
            m_lastAutoSaveTime = QDateTime::currentDateTime();
            updateAutoSaveStatus();
        }
    }
}

// Refresh the status-bar "Saved · Ns ago" / "Modified" indicator.
// Called on every save and on a 1Hz tick so the elapsed-time string
// stays current.
void MainWindow::updateAutoSaveStatus()
{
    if (!m_saveStatusLabel) return;
    bool anyModified = false;
    for (Project *p : m_projects) if (p->modified) { anyModified = true; break; }
    if (anyModified) {
        m_saveStatusLabel->setText(tr("\u25CF  Modified"));
        m_saveStatusLabel->setStyleSheet("color:#d29922;font-weight:bold");
        return;
    }
    if (!m_lastAutoSaveTime.isValid()) {
        m_saveStatusLabel->clear();
        return;
    }
    const qint64 secs = m_lastAutoSaveTime.secsTo(QDateTime::currentDateTime());
    QString text;
    if (secs < 5)         text = tr("\u2713  Saved");
    else if (secs < 60)   text = tr("\u2713  Saved %1s ago").arg(secs);
    else if (secs < 3600) text = tr("\u2713  Saved %1m ago").arg(secs / 60);
    else                  text = tr("\u2713  Saved %1h ago").arg(secs / 3600);
    m_saveStatusLabel->setText(text);
    m_saveStatusLabel->setStyleSheet("color:#3fb950;");
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    // Collect all modified, non-linked projects
    QVector<Project *> unsaved;
    for (auto *proj : m_projects) {
        qWarning() << "closeEvent project:" << proj->displayName()
                   << "modified:" << proj->modified
                   << "isLinkedRom:" << proj->isLinkedRom
                   << "filePath:" << proj->filePath;
        if (proj->modified && !proj->isLinkedRom)
            unsaved.append(proj);
    }
    qWarning() << "Total projects:" << m_projects.size() << "unsaved:" << unsaved.size();

    if (!unsaved.isEmpty()) {
        QString names;
        for (auto *proj : unsaved)
            names += QString("<li><b>%1</b></li>").arg(proj->listLabel());

        QMessageBox box(this);
        box.setWindowTitle(tr("Unsaved Changes"));
        box.setIcon(QMessageBox::Warning);
        box.setText(tr("The following projects have unsaved changes:"));
        box.setInformativeText(QString("<ul>%1</ul>").arg(names));
        auto *saveAll = box.addButton(tr("Save All && Exit"), QMessageBox::AcceptRole);
        auto *discard = box.addButton(tr("Exit Without Saving"), QMessageBox::DestructiveRole);
        auto *cancel  = box.addButton(tr("Cancel"),            QMessageBox::RejectRole);
        box.setDefaultButton(saveAll);
        box.exec();

        if (box.clickedButton() == cancel) { e->ignore(); return; }
        if (box.clickedButton() == saveAll) {
            for (auto *proj : unsaved) {
                if (proj->filePath.isEmpty()) {
                    QString dir = ProjectRegistry::defaultProjectDir();
                    QDir().mkpath(dir);
                    QString path = QFileDialog::getSaveFileName(this, tr("Save Project As"),
                        dir + "/" + suggestedProjectBasename(proj) + ".rx14proj",
                        tr("RX14 Projects (*.rx14proj);;All Files (*)"));
                    if (path.isEmpty()) { e->ignore(); return; }
                    proj->saveAs(path);
                    ProjectRegistry::instance().registerProject(path, proj);
                } else {
                    proj->save();
                    ProjectRegistry::instance().registerProject(proj->filePath, proj);
                }
            }
        }
        // discard — fall through and close
    }

    // Clean up autosave files on normal exit
    for (Project *proj : m_projects) {
        if (!proj->filePath.isEmpty())
            QFile::remove(proj->filePath + ".autosave");
    }
    QString autoSaveDir = ProjectRegistry::defaultProjectDir() + "/autosave";
    QDir(autoSaveDir).removeRecursively();

    e->accept();
}

// ── Map pack / patch script ───────────────────────────────────────────────────

void MainWindow::actImportMapPack()
{
    Project *proj = activeProject();
    if (!proj) {
        QMessageBox::information(this, tr("No project"),
            tr("Open a project before importing a map pack."));
        return;
    }
    MapPackDlg::importPack(proj, this);
}

void MainWindow::actOpenPatchEditor()
{
    // Works with or without an open project.
    // When no project is open, only "Apply to ROM file…" is available.
    Project *proj = activeProject();
    auto *ed = new PatchEditorDlg(proj, this);
    ed->setAttribute(Qt::WA_DeleteOnClose);
    ed->show();
}

// ── shared helper: show dialog and get DLL selection ─────────────────────────
static ChecksumDllInfo checksumPickDll(Project* proj, QWidget* parent, const QString& title)
{
    ChecksumSelectDlg dlg(proj->currentData, proj->ecuType, parent);
    dlg.setWindowTitle(title);
    if (dlg.exec() != QDialog::Accepted) return {};
    return dlg.selectedDll();
}

void MainWindow::actVerifyChecksum()
{
    auto *proj = activeProject();
    if (!proj) { QMessageBox::information(this, tr("Verify Checksum"), tr("Open a project first.")); return; }
    if (proj->currentData.isEmpty()) { QMessageBox::information(this, tr("Verify Checksum"), tr("No ROM data loaded.")); return; }

    const ChecksumDllInfo dll = checksumPickDll(proj, this, tr("Verify Checksum"));
    if (dll.devNum == 0) return; // user cancelled

    QString errorMsg;
    const ChecksumResult result = ChecksumManager::instance()->verify(proj->currentData, dll, errorMsg);

    switch (result) {
    case ChecksumResult::OK:
        // Non-destructive success → status-bar message instead of modal popup.
        statusBar()->showMessage(
            tr("Checksum OK — %1 (%2)").arg(proj->ecuType, dll.description), 5000);
        break;
    case ChecksumResult::Mismatch:
        QMessageBox::warning(this, tr("Verify Checksum"),
            tr("✗ Checksum mismatch\n\nECU: %1\nAlgorithm: %2\n\nUse \"Correct Checksum\" to fix it before flashing.").arg(proj->ecuType, dll.description));
        break;
    case ChecksumResult::Unsupported:
        QMessageBox::information(this, tr("Verify Checksum"),
#ifdef Q_OS_WIN
            tr("Checksum verification is not supported for this ECU.\n\nECU: %1").arg(proj->ecuType)
#else
            tr("Native checksum verification unavailable for this ECU on macOS/Linux.\n\nECU: %1").arg(proj->ecuType)
#endif
        );
        break;
    case ChecksumResult::Error:
        QMessageBox::critical(this, tr("Verify Checksum"), tr("Checksum error: %1").arg(errorMsg));
        break;
    }
}

void MainWindow::actCorrectChecksum()
{
    auto *proj = activeProject();
    if (!proj) { QMessageBox::information(this, tr("Correct Checksum"), tr("Open a project first.")); return; }
    if (proj->currentData.isEmpty()) { QMessageBox::information(this, tr("Correct Checksum"), tr("No ROM data loaded.")); return; }

    const ChecksumDllInfo dll = checksumPickDll(proj, this, tr("Correct Checksum"));
    if (dll.devNum == 0) return;

    if (QMessageBox::question(this, tr("Correct Checksum"),
            tr("Recalculate and write checksum for:\n\nECU: %1\nAlgorithm: %2\n\nThis modifies ROM data in memory (not saved until export).").arg(proj->ecuType, dll.description),
            QMessageBox::Yes | QMessageBox::Cancel) != QMessageBox::Yes) return;

    QByteArray romCopy = proj->currentData;
    QString errorMsg;
    const ChecksumResult result = ChecksumManager::instance()->correct(romCopy, dll, errorMsg);

    switch (result) {
    case ChecksumResult::OK:
        proj->currentData = romCopy;
        proj->modified = true;
        emit proj->dataChanged();
        // Status-bar feedback only — the "Correct Checksum" prompt already
        // confirmed user intent; no need for a follow-up "OK done" modal.
        statusBar()->showMessage(
            tr("Checksum corrected — %1 (%2)").arg(proj->ecuType, dll.description), 5000);
        break;
    case ChecksumResult::Unsupported:
        QMessageBox::information(this, tr("Correct Checksum"),
#ifdef Q_OS_WIN
            tr("Checksum correction is not supported for this ECU.\n\nECU: %1").arg(proj->ecuType)
#else
            tr("Native checksum correction unavailable for this ECU on macOS/Linux.\n\nECU: %1").arg(proj->ecuType)
#endif
        );
        break;
    case ChecksumResult::Error:
        QMessageBox::critical(this, tr("Correct Checksum"), tr("Checksum correction failed: %1").arg(errorMsg));
        break;
    default: break;
    }
}

bool MainWindow::confirmChecksumBeforeExport(Project *proj, QByteArray &data)
{
    if (!proj || data.isEmpty())
        return true;

    // Detect the algorithm from the bytes actually being exported.
    ChecksumManager *cm = ChecksumManager::instance();
    const ChecksumDllInfo dll = cm->autoDetect(data, proj->ecuType);
    if (dll.devNum == 0)
        return true;   // no algorithm known for this ROM — nothing to check

    QString err;
    const ChecksumResult vr = cm->verify(data, dll, err);

    // Only a confirmed mismatch is worth interrupting the export for.
    // OK → checksum already valid; Unsupported → cannot verify on this
    // platform (e.g. DLL-only ECU on macOS/Linux); Error → verification
    // failed. In all three cases we let the export proceed rather than nag.
    if (vr != ChecksumResult::Mismatch) {
        if (vr == ChecksumResult::Error)
            qWarning() << "Export checksum verify error" << dll.description << err;
        return true;
    }

    const QString ecuName = proj->ecuType.isEmpty() ? tr("Unknown") : proj->ecuType;

    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Checksum"));
    box.setText(tr("This ROM has an invalid checksum."));
    box.setInformativeText(
        tr("ECU: %1\nAlgorithm: %2\n\n"
           "Correct the checksum before exporting? Flashing a ROM with an "
           "invalid checksum can be rejected by the ECU.").arg(ecuName, dll.description));
    QPushButton *correctBtn = box.addButton(tr("Correct && Export"), QMessageBox::AcceptRole);
    QPushButton *anywayBtn  = box.addButton(tr("Export Anyway"),     QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(correctBtn);
    box.exec();

    QAbstractButton *clicked = box.clickedButton();
    if (clicked == anywayBtn)
        return true;                 // user accepts the risk
    if (clicked != correctBtn)
        return false;                // Cancel / dialog closed → abort export

    // Correct & Export — fix the bytes we're about to write.
    QString cerr;
    const ChecksumResult cr = cm->correct(data, dll, cerr);
    if (cr == ChecksumResult::OK) {
        statusBar()->showMessage(
            tr("Checksum corrected — %1 (%2)").arg(ecuName, dll.description), 5000);
        return true;
    }

    QMessageBox::critical(this, tr("Checksum"),
        tr("Checksum correction failed: %1\n\nExport cancelled.")
            .arg(cerr.isEmpty() ? tr("unknown error") : cerr));
    return false;
}

// ── Command palette (Cmd/Ctrl+K) ──────────────────────────────────────────────
//
// Builds a fresh PaletteEntry list every invocation so the palette never
// shows stale projects/maps after the user closes a project. Order matters
// for tie-breaking on identical fuzzy scores: open maps first, then open
// projects, then registry projects, then settings, then menu actions.
void MainWindow::actShowCommandPalette()
{
    QVector<PaletteEntry> entries;

    // ── 1. Maps (across every open project) ───────────────────────────
    QSet<QString> openProjectPaths;
    for (Project *proj : m_projects) {
        if (!proj) continue;
        openProjectPaths.insert(proj->filePath);
        const QString projDisp = proj->displayName().isEmpty()
                                     ? QFileInfo(proj->filePath).baseName()
                                     : proj->displayName();
        for (const MapInfo &m : proj->maps) {
            PaletteEntry e;
            e.kind     = PaletteEntry::Kind::Map;
            e.name     = m.name;
            QStringList sub;
            if (!projDisp.isEmpty()) sub << projDisp;
            if (!m.type.isEmpty())   sub << m.type;
            if (m.dimensions.x > 1 || m.dimensions.y > 1)
                sub << QString("%1×%2").arg(m.dimensions.x).arg(m.dimensions.y);
            e.subtitle = sub.join(QStringLiteral(" · "));
            e.project  = proj;
            e.mapName  = m.name;
            entries.push_back(std::move(e));
        }
    }

    // ── 2. Projects (every entry in the registry) ─────────────────────
    for (const ProjectEntry &pe : ProjectRegistry::instance().entries()) {
        PaletteEntry e;
        e.kind = PaletteEntry::Kind::Project;
        const QString fname = QFileInfo(pe.path).fileName();
        e.name = fname.isEmpty() ? pe.name : fname;
        QStringList sub;
        if (!pe.brand.isEmpty())   sub << pe.brand;
        if (!pe.model.isEmpty())   sub << pe.model;
        if (!pe.ecuType.isEmpty()) sub << pe.ecuType;
        if (openProjectPaths.contains(pe.path))
            sub.prepend(tr("open"));
        e.subtitle    = sub.join(QStringLiteral(" · "));
        e.projectPath = pe.path;
        entries.push_back(std::move(e));
    }

    // ── 3. Settings (hardcoded common preferences) ────────────────────
    auto addSetting = [&](const QString &id, const QString &label,
                          const QString &subtitle) {
        PaletteEntry e;
        e.kind      = PaletteEntry::Kind::Setting;
        e.name      = label;
        e.subtitle  = subtitle;
        e.settingId = id;
        entries.push_back(std::move(e));
    };
    addSetting(QStringLiteral("language"),  tr("Language"),
               tr("Change UI language"));
    addSetting(QStringLiteral("autosave"),  tr("Auto Save Mode"),
               tr("Off / After Delay / On Focus Change / On Window Deactivate"));
    addSetting(QStringLiteral("byteOrder"), tr("Byte Order"),
               tr("Little Endian / Big Endian"));
    addSetting(QStringLiteral("theme"),     tr("Theme & Colors"),
               tr("Open Preferences → Colors"));

    // ── 4. Menu actions ───────────────────────────────────────────────
    // Iterate every QAction owned by this window. Skip the palette's own
    // action (would be a no-op) and anything without a label.
    QSet<QAction *> seen;
    const QList<QAction *> all = findChildren<QAction *>();
    for (QAction *a : all) {
        if (!a || a == m_actCmdPalette) continue;
        if (seen.contains(a)) continue;
        if (!a->isVisible() || !a->isEnabled()) continue;
        if (a->isSeparator()) continue;
        const QString text = a->text().remove(QChar('&'));
        if (text.trimmed().isEmpty()) continue;
        seen.insert(a);

        PaletteEntry e;
        e.kind     = PaletteEntry::Kind::Action;
        e.name     = text;
        e.subtitle = a->toolTip() != text ? a->toolTip() : QString();
        const QKeySequence ks = a->shortcut();
        if (!ks.isEmpty())
            e.shortcut = ks.toString(QKeySequence::NativeText);
        e.action = a;
        entries.push_back(std::move(e));
    }

    // ── Show the palette ──────────────────────────────────────────────
    if (!m_cmdPalette) {
        m_cmdPalette = new CommandPalette(this);
        connect(m_cmdPalette, &CommandPalette::activated,
                this, [this](const PaletteEntry &chosen) {
            switch (chosen.kind) {
                case PaletteEntry::Kind::Project: {
                    // Re-show if already open; otherwise load from disk.
                    for (Project *p : m_projects) {
                        if (p && p->filePath == chosen.projectPath) {
                            openProject(p);
                            return;
                        }
                    }
                    if (chosen.projectPath.isEmpty()) return;
                    auto *project = Project::open(chosen.projectPath, this);
                    if (!project) {
                        QMessageBox::critical(this, tr("Error"),
                            tr("Failed to open project:\n") + chosen.projectPath);
                        return;
                    }
                    openProject(project);
                    break;
                }
                case PaletteEntry::Kind::Map: {
                    Project *proj = chosen.project;
                    if (!proj) return;
                    openProject(proj);   // ensure window exists & is active
                    for (const MapInfo &m : proj->maps) {
                        if (m.name == chosen.mapName) {
                            showMapOverlay(proj->currentData, m,
                                           proj->byteOrder, proj);
                            break;
                        }
                    }
                    break;
                }
                case PaletteEntry::Kind::Action: {
                    if (chosen.action) chosen.action->trigger();
                    break;
                }
                case PaletteEntry::Kind::Setting: {
                    const QString &id = chosen.settingId;
                    if (id == QStringLiteral("language")) {
                        if (m_menuLang)
                            m_menuLang->exec(QCursor::pos());
                    } else if (id == QStringLiteral("autosave")) {
                        // Cycle: Off → AfterDelay → OnFocusChange → OnWindowDeactivate
                        const QString cur = rx14::appSettings()
                            .value("autoSaveMode", "afterDelay").toString();
                        QAction *next = m_actAutoSaveDelay;
                        if      (cur == "off")                next = m_actAutoSaveDelay;
                        else if (cur == "afterDelay")         next = m_actAutoSaveFocus;
                        else if (cur == "onFocusChange")      next = m_actAutoSaveDeact;
                        else if (cur == "onWindowDeactivate") next = m_actAutoSaveOff;
                        if (next) next->setChecked(true);
                    } else if (id == QStringLiteral("byteOrder")) {
                        // Toggle between LE and BE
                        if (m_actLo && m_actHi) {
                            if (m_actLo->isChecked()) m_actHi->setChecked(true);
                            else                       m_actLo->setChecked(true);
                            onByteOrderChanged();
                        }
                    } else if (id == QStringLiteral("theme")) {
                        ConfigDialog dlg(this);
                        dlg.exec();
                        for (auto *sub : m_mdi->subWindowList())
                            sub->widget()->update();
                    }
                    break;
                }
            }
        });
    }

    m_cmdPalette->setEntries(entries);
    m_cmdPalette->show();
    m_cmdPalette->raise();
    m_cmdPalette->activateWindow();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Selection / map editing dispatcher (Sprint A)
//
//  All edit operations funnel through here.  The dispatcher's job:
//    1. Resolve the active view's selection (waveform or hex)
//    2. Find that view's WaveformEditor (single per ProjectView, owns
//       the undo stack the user expects Ctrl+Z to drive)
//    3. Translate (op, params) into the right WaveformEditor method call
//    4. Cache (op, params) for F4 "Again"
//
//  3D view: no per-cell selection, so the operation runs over the entire
//  current map's byte range.  Hex view: HexWidget has its own selection
//  mechanism (selStart()/selEnd()) — we read those bytes and feed them
//  to the same WaveformEditor.
// ─────────────────────────────────────────────────────────────────────────────

#include "edit/SliderEditDlg.h"
#include "waveformeditor.h"

namespace {

// Resolve (start, end) byte range from the active ProjectView.  Priority:
// waveform selection > hex selection > whole-map range when 3D view is
// active and a map is loaded.  Returns false if nothing actionable.
bool resolveActiveRange(MainWindow * /*mw*/, ProjectView *pv,
                        int *outStart, int *outEnd, QString *outWhy)
{
    if (!pv) { *outWhy = QObject::tr("No active project view."); return false; }

    if (auto *ww = pv->waveformWidget(); ww && ww->hasSelection()) {
        *outStart = static_cast<int>(ww->selectionStart());
        *outEnd   = static_cast<int>(ww->selectionEnd()) + 1; // inclusive → exclusive
        return *outEnd > *outStart;
    }
    if (auto *hw = pv->hexWidget(); hw && hw->hasSelection()) {
        *outStart = hw->selStart();
        *outEnd   = hw->selEnd() + 1;
        return *outEnd > *outStart;
    }
    // Fall back to "whole currently-shown map".  3D view has no per-cell
    // selection, so right-clicking it should operate on the entire map.
    // We also accept this path from waveform/hex when no selection has
    // been drawn — it's "do this to the whole map" semantics.
    const MapInfo &m = pv->selectedMap();
    if (m.length > 0) {
        *outStart = static_cast<int>(m.address);
        *outEnd   = static_cast<int>(m.address + m.length);
        return *outEnd > *outStart;
    }
    *outWhy = QObject::tr("Select a map (or drag a range in the waveform / "
                          "hex view) first.");
    return false;
}

} // namespace

void MainWindow::onEditOpRequestedFromView(int code)
{
    onEditOpFromMenu(static_cast<EditOp>(code));
}

void MainWindow::onToggleDiffOriginal(bool on)
{
    m_showOriginalDiff = on;
    rx14::appSettings().setValue("view/showOriginalDiff", on);
    // Propagate to every open ProjectView so hex / waveform / 3D each
    // pick up the flag and repaint.
    for (auto *sub : m_mdi->subWindowList()) {
        auto *pv = qobject_cast<ProjectView *>(sub->widget());
        if (!pv) continue;
        pv->setShowOriginalDiffOverlay(on);
    }
}

// ── Sprint C: annotation slots ─────────────────────────────────────────────

MainWindow::AnnoCtx MainWindow::resolveAnnoCtx()
{
    AnnoCtx c;
    auto *v = activeView();
    if (m_mdi) {
        const auto subs = m_mdi->subWindowList();
        // If the active view (or first if no active) has no annotations
        // but another subwindow does, prefer that one — it is what the
        // user just edited.  Also covers the unfocused-RPC case where
        // activeView() is null.
        auto hasAnnos = [](ProjectView *pv) {
            return pv && pv->project()
                   && !pv->project()->annotations()->all().isEmpty();
        };
        if (!hasAnnos(v)) {
            for (auto *sw : subs) {
                auto *pv = qobject_cast<ProjectView *>(sw->widget());
                if (hasAnnos(pv)) { v = pv; break; }
            }
        }
        // Final fallback: first ProjectView so F5 still works on a
        // project with no annotations yet.
        if (!v) {
            for (auto *sw : subs) {
                if (auto *pv = qobject_cast<ProjectView *>(sw->widget())) {
                    v = pv; break;
                }
            }
        }
    }
    if (!v || !v->project()) return c;
    c.view    = v;
    c.project = v->project();
    c.store   = c.project->annotations();
    if (!c.store) return c;

    // Priority: hex selection start → hex caret → waveform selection start → 0
    if (v->hexWidget()->hasSelection())
        c.offset = v->hexWidget()->selStart();
    else if (v->hexWidget()->currentOffset() >= 0)
        c.offset = v->hexWidget()->currentOffset();
    else if (v->waveformWidget()->hasSelection())
        c.offset = static_cast<qint64>(v->waveformWidget()->selectionStart());
    else
        c.offset = 0;
    if (c.offset < 0) c.offset = 0;
    c.ok = true;
    return c;
}

void MainWindow::onInsertComment()
{
    auto ctx = resolveAnnoCtx();
    if (!ctx.ok) {
        statusBar()->showMessage(tr("Open a project first."), 4000);
        return;
    }
    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(
        this, tr("Insert comment"),
        tr("Comment for offset 0x%1:")
            .arg(ctx.offset, 8, 16, QChar('0')).toUpper(),
        {}, &ok);
    if (!ok || text.isEmpty()) return;
    qint64 length = 1;
    if (ctx.view && ctx.view->hexWidget()->hasSelection())
        length = qMax(1, ctx.view->hexWidget()->selEnd()
                            - ctx.view->hexWidget()->selStart() + 1);
    ctx.store->add(ctx.offset, text, length);
    statusBar()->showMessage(
        tr("Comment added at 0x%1").arg(ctx.offset, 8, 16, QChar('0')).toUpper(),
        4000);
}

void MainWindow::onInsertMarker()
{
    auto ctx = resolveAnnoCtx();
    if (!ctx.ok) {
        statusBar()->showMessage(tr("Open a project first."), 4000);
        return;
    }
    ctx.store->add(ctx.offset, QString(), 1);
    statusBar()->showMessage(
        tr("Marker added at 0x%1").arg(ctx.offset, 8, 16, QChar('0')).toUpper(),
        4000);
}

void MainWindow::onDeleteComment()
{
    auto ctx = resolveAnnoCtx();
    if (!ctx.ok) return;
    if (!ctx.store->removeAt(ctx.offset)) {
        for (const Annotation &a : ctx.store->all()) {
            if (ctx.offset >= a.addr && ctx.offset < a.addr + a.length) {
                ctx.store->removeAt(a.addr);
                break;
            }
        }
    }
}

void MainWindow::exportMapListCsv()
{
    auto *p = activeProject();
    if (!p || p->maps.isEmpty()) {
        QMessageBox::information(this, tr("Export map list"),
            tr("Open a project with at least one map first."));
        return;
    }
    const QString suggested = QDir::homePath() + "/" + p->displayName() + ".csv";
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export map list as CSV"), suggested,
        tr("CSV (*.csv)"));
    if (path.isEmpty()) return;
    QString err;
    if (!MapListExporter::toCsv(*p, path, &err)) {
        QMessageBox::warning(this, tr("Export failed"), err);
        return;
    }
    statusBar()->showMessage(tr("Wrote %1 maps to %2")
                                 .arg(p->maps.size()).arg(path), 6000);
}

void MainWindow::exportMapListJson()
{
    auto *p = activeProject();
    if (!p || p->maps.isEmpty()) {
        QMessageBox::information(this, tr("Export map list"),
            tr("Open a project with at least one map first."));
        return;
    }
    const QString suggested = QDir::homePath() + "/" + p->displayName() + ".json";
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export map list as JSON"), suggested,
        tr("JSON (*.json)"));
    if (path.isEmpty()) return;
    QString err;
    if (!MapListExporter::toJson(*p, path, &err)) {
        QMessageBox::warning(this, tr("Export failed"), err);
        return;
    }
    statusBar()->showMessage(tr("Wrote %1 maps to %2")
                                 .arg(p->maps.size()).arg(path), 6000);
}

void MainWindow::exportTuningReport()
{
    auto *p = activeProject();
    if (!p || p->maps.isEmpty()) {
        QMessageBox::information(this, tr("Tuning Report"),
            tr("Open a project with at least one map first. The report "
               "compares the current ROM against its original snapshot."));
        return;
    }

    const QByteArray tuned = p->currentData;
    // Prefer the project's original snapshot; if it's missing or a different
    // size, ask the user to point at the baseline ROM to diff against.
    QByteArray stock = p->originalData;
    if (stock.isEmpty() || stock.size() != tuned.size()) {
        const QString basePath = QFileDialog::getOpenFileName(this,
            tr("Select the original (baseline) ROM to compare against"),
            QDir::homePath(), tr("ROM files (*.bin *.rom *.ori *.hex);;All files (*)"));
        if (basePath.isEmpty()) return;
        QFile bf(basePath);
        if (!bf.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, tr("Tuning Report"),
                tr("Could not open %1").arg(basePath));
            return;
        }
        stock = bf.readAll();
        bf.close();
        if (stock.size() != tuned.size()) {
            QMessageBox::warning(this, tr("Tuning Report"),
                tr("The baseline ROM (%1 bytes) is a different size from the "
                   "current ROM (%2 bytes); they must match to compare maps.")
                   .arg(stock.size()).arg(tuned.size()));
            return;
        }
    }

    tunereport::Options opt;
    opt.projectName = p->displayName();
    opt.vehicle = QStringList{ p->brand, p->model, p->vehicleModel }
                      .join(QLatin1Char(' ')).simplified();
    opt.ecuName = QStringList{ p->ecuProducer, p->ecuType }
                      .join(QLatin1Char(' ')).simplified();
    opt.softwareVersion = p->ecuSwVersion.isEmpty() ? p->ecuNrProd : p->ecuSwVersion;
    opt.author = p->clientName;
    opt.generatedAt = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));

    const auto changes = tunereport::analyze(p->maps, stock, tuned);
    int changedMaps = 0;
    for (const auto &c : changes) if (c.changed) changedMaps++;
    if (changedMaps == 0) {
        QMessageBox::information(this, tr("Tuning Report"),
            tr("No differences found between the current ROM and the baseline — "
               "nothing to report."));
        return;
    }

    const QString html = tunereport::renderHtml(changes, opt);
    const QString suggested = QDir::homePath() + "/" + p->displayName()
                              + tr("-tuning-report") + ".html";
    const QString path = QFileDialog::getSaveFileName(this,
        tr("Save Tuning Report"), suggested, tr("HTML report (*.html)"));
    if (path.isEmpty()) return;

    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Tuning Report"),
            tr("Could not write %1").arg(path));
        return;
    }
    out.write(html.toUtf8());
    out.close();

    statusBar()->showMessage(
        tr("Tuning report: %1 changed maps written to %2").arg(changedMaps).arg(path), 8000);
    if (QMessageBox::question(this, tr("Tuning Report"),
            tr("Report saved with %1 changed maps.\n\nOpen it now?").arg(changedMaps))
            == QMessageBox::Yes) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}

void MainWindow::actFindValue()
{
    auto *proj = activeProject();
    if (!proj || proj->currentData.isEmpty()) {
        QMessageBox::information(this, tr("Find Value"),
            tr("Open a project with ROM data first."));
        return;
    }
    auto *dlg = new ValueSearchDlg(proj->currentData, proj->byteOrder, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &ValueSearchDlg::goToAddressRequested, this, [this](uint32_t a) {
        if (auto *v = activeView()) v->goToAddress(a);
    });
    connect(dlg, &ValueSearchDlg::createMapRequested, this, [this](uint32_t addr, int cellSize) {
        auto *pv = activeView();
        if (!pv || !pv->project()) return;
        CreateMapDlg d(addr, cellSize, cellSize, this);
        if (d.exec() != QDialog::Accepted) return;
        MapInfo m = d.resultMap();
        pv->project()->maps.append(m);
        pv->project()->modified = true;
        emit pv->project()->dataChanged();
        pv->goToMap(m);
    });
    dlg->show();
}

void MainWindow::actImportFrf()
{
    const QString path = QFileDialog::getOpenFileName(this,
        tr("Import VAG FRF / ODX"), {},
        tr("VAG flash containers (*.frf *.sgo *.odx *.zip);;All files (*)"));
    if (path.isEmpty()) return;

    auto *dlg = new FrfImportDlg(path, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &FrfImportDlg::openRomRequested,
            this, &MainWindow::openExtractedRom);
    dlg->show();
}

void MainWindow::openExtractedRom(const QByteArray &romBytes, const QString &suggestedName)
{
    if (romBytes.isEmpty()) return;

    // Stage the extracted block to a temp .bin so the normal ROM-load path
    // (format parse, ECU detect, auto-scan) runs unchanged.
    QString safe = suggestedName;
    safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
    if (safe.isEmpty()) safe = QStringLiteral("frf_block");
    const QString tmp = QDir(QDir::tempPath()).filePath(safe + ".bin");
    QFile f(tmp);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, tr("Import FRF / ODX"),
            tr("Could not stage the extracted ROM to a temporary file."));
        return;
    }
    f.write(romBytes);
    f.close();

    auto *project = new Project(this);
    project->createdAt = QDateTime::currentDateTime();
    project->createdBy = qEnvironmentVariable("USERNAME",
                             qEnvironmentVariable("USER", "Unknown"));
    loadROMIntoProject(project, tmp);
    if (project->currentData.isEmpty()) { delete project; return; }

    project->name = suggestedName;
    project->romPath.clear();   // temp file — force an explicit Save As later
    openProject(project);
    broadcastAvailableProjects();
    refreshProjectTree();
    statusBar()->showMessage(
        tr("Opened extracted ROM: %1 (%2 bytes)").arg(suggestedName).arg(romBytes.size()), 5000);
}

void MainWindow::actImportXdf(const QString &droppedPath)
{
    auto *proj = activeProject();
    if (!proj || proj->currentData.isEmpty()) {
        QMessageBox::information(this, tr("Import XDF"),
            tr("Open a project with ROM data first. XDF definitions are added "
               "on top of an existing ROM (the same way A2L files are)."));
        return;
    }
    QString path = droppedPath;
    if (path.isEmpty())
        path = QFileDialog::getOpenFileName(this,
            tr("Import XDF definition"), {}, tr("TunerPro XDF (*.xdf);;All files (*)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("Error"), tr("Cannot open file: %1").arg(path));
        return;
    }
    const QByteArray data = f.readAll();
    f.close();

    auto res = xdf::importFromXml(data);
    if (!res.error.isEmpty()) {
        QMessageBox::critical(this, tr("Import XDF"), res.error);
        return;
    }
    if (res.maps.isEmpty()) {
        QMessageBox::information(this, tr("Import XDF"),
            tr("No maps with addresses were found in this XDF."));
        return;
    }

    // De-dupe against existing maps by (name + address), then apply. XDF carries
    // its own folder (CATEGORY) per map, so the tree groups itself.
    QSet<QPair<QString, uint32_t>> existing;
    for (const auto &m : proj->maps)
        existing.insert({m.name, m.address});
    const int romSize = proj->currentData.size();
    int added = 0, skipped = 0, outOfBounds = 0;
    for (auto &m : res.maps) {
        if (m.address >= (uint32_t)romSize
            || m.address + (uint32_t)m.length > (uint32_t)romSize) { ++outOfBounds; continue; }
        if (existing.contains({m.name, m.address})) { ++skipped; continue; }
        proj->maps.append(m);
        existing.insert({m.name, m.address});
        ++added;
    }

    if (added == 0) {
        QMessageBox::information(this, tr("Import XDF"),
            tr("No new maps were added (%1 already present, %2 outside the ROM).")
                .arg(skipped).arg(outOfBounds));
        return;
    }
    proj->modified = true;
    emit proj->dataChanged();   // tree refresh + autosave
    QString msg = tr("Imported %1 maps from XDF").arg(added);
    if (skipped)     msg += tr(", %1 already present").arg(skipped);
    if (outOfBounds) msg += tr(", %1 outside ROM").arg(outOfBounds);
    statusBar()->showMessage(msg, 8000);
}

void MainWindow::actExportXdf()
{
    auto *p = activeProject();
    if (!p || p->maps.isEmpty()) {
        QMessageBox::information(this, tr("Export XDF"),
            tr("Open a project with at least one map first."));
        return;
    }
    const QString suggested = QDir::homePath() + "/" + p->displayName() + ".xdf";
    const QString path = QFileDialog::getSaveFileName(this,
        tr("Export XDF (TunerPro)"), suggested, tr("TunerPro XDF (*.xdf)"));
    if (path.isEmpty()) return;

    xdf::ExportOptions opt;
    opt.romSize = static_cast<uint32_t>(qMax(0, p->currentData.size()));
    opt.description = p->displayName();
    const QByteArray xml = xdf::exportToXml(p->maps, opt);

    QFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("Export XDF"), tr("Could not write %1").arg(path));
        return;
    }
    out.write(xml);
    out.close();
    statusBar()->showMessage(tr("Exported %1 maps to %2").arg(p->maps.size()).arg(path), 6000);
}

void MainWindow::actExportKp()
{
    auto *p = activeProject();
    if (!p || p->maps.isEmpty()) {
        QMessageBox::information(this, tr("Export KP"),
            tr("Open a project with at least one map first."));
        return;
    }
    const QString suggested = QDir::homePath() + "/" + p->displayName() + ".kp";
    const QString path = QFileDialog::getSaveFileName(this,
        tr("Export KP map pack"), suggested, tr("KP map pack (*.kp)"));
    if (path.isEmpty()) return;

    const uint32_t romSize = static_cast<uint32_t>(qMax(0, p->currentData.size()));
    auto res = ols::KpExporter::exportToBytes(p->maps, romSize);
    if (!res.error.isEmpty()) {
        QMessageBox::warning(this, tr("Export KP"), res.error);
        return;
    }
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("Export KP"), tr("Could not write %1").arg(path));
        return;
    }
    out.write(res.data);
    out.close();
    statusBar()->showMessage(tr("Exported %1 maps to %2").arg(res.mapCount).arg(path), 6000);
}

void MainWindow::onJumpMarker(bool forward)
{
    auto ctx = resolveAnnoCtx();
    if (!ctx.ok) return;
    if (ctx.store->all().isEmpty()) {
        statusBar()->showMessage(tr("No annotations in this project."), 4000);
        return;
    }
    const qint64 next = forward ? ctx.store->nextAfter(ctx.offset)
                                : ctx.store->prevBefore(ctx.offset);
    if (next < 0) return;
    if (ctx.view) ctx.view->goToAddress(static_cast<uint32_t>(next));
    statusBar()->showMessage(
        tr("→ 0x%1").arg(next, 8, 16, QChar('0')).toUpper(), 4000);
}

void MainWindow::onEditOpFromMenu(EditOp op)
{
    EditParams p;
    bool ok = false;

    switch (op) {
    case EditOp::ValuePlus1:
    case EditOp::ValueMinus1:
    case EditOp::Original:
    case EditOp::Interpolate:
    case EditOp::Smooth:
    case EditOp::Flatten:
        // No params needed; dispatcher uses the op alone.
        applyEditOp(op, p);
        return;

    case EditOp::Absolute: {
        double v = QInputDialog::getDouble(this, tr("Change absolute"),
            tr("Set every selected cell to:"), 0.0, -1e9, 1e9, 4, &ok);
        if (!ok) return;
        p.value = v;
        applyEditOp(op, p);
        return;
    }
    case EditOp::Relative: {
        // Two-mode: enter "+5" → addDelta(5); "*1.05" → scale(1.05);
        // "5%" → scale(1.05).
        QString s = QInputDialog::getText(this, tr("Change relative"),
            tr("Enter delta (e.g. +5, -3, *1.10, +5%):"),
            QLineEdit::Normal, QStringLiteral("+1"), &ok);
        if (!ok || s.trimmed().isEmpty()) return;
        s = s.trimmed();
        if (s.endsWith('%')) {
            double pct = s.chopped(1).toDouble(&ok);
            if (!ok) return;
            p.isScale = true;
            p.scaleVal = 1.0 + pct / 100.0;
        } else if (s.startsWith('*')) {
            double f = s.mid(1).toDouble(&ok);
            if (!ok) return;
            p.isScale = true;
            p.scaleVal = f;
        } else {
            double d = s.toDouble(&ok);
            if (!ok) return;
            p.delta = d;
        }
        applyEditOp(op, p);
        return;
    }
    case EditOp::Slider: {
        // The dialog itself drives applyEditOp via setAbsolute previews —
        // we don't go through dispatcher for it.
        ProjectView *pv = activeView();
        int start, end; QString why;
        if (!resolveActiveRange(this, pv, &start, &end, &why)) {
            QMessageBox::information(this, tr("Selection"), why);
            return;
        }
        WaveformEditor *ed = pv->waveformWidget()
                                 ? pv->waveformWidget()->editor()
                                 : nullptr;
        if (!ed) return;
        // Reasonable default range: 0 to 2^(8*cellSize)-1 for unsigned,
        // ±half for signed.  We don't know per-view cellSize cheaply, so
        // use a generous range; users can type in spinbox.
        SliderEditDlg dlg(ed, start, end, ed->readValue(start),
                          0, 65535, this);
        if (dlg.exec() == QDialog::Accepted) {
            p.value  = dlg.committedValue();
            m_lastEditOp     = EditOp::Slider;
            m_lastEditParams = p;
            m_haveLastEdit   = true;
        }
        return;
    }
    case EditOp::RoundLimit: {
        // Three-input mini-dialog using QInputDialog series — keeps it
        // light without a full custom widget.
        int mult = QInputDialog::getInt(this, tr("Round / limit"),
            tr("Round to nearest multiple of:"), 1, 0, 1000000, 1, &ok);
        if (!ok) return;
        double mn = QInputDialog::getDouble(this, tr("Round / limit"),
            tr("Minimum allowed value:"), 0.0, -1e9, 1e9, 4, &ok);
        if (!ok) return;
        double mx = QInputDialog::getDouble(this, tr("Round / limit"),
            tr("Maximum allowed value:"), 65535.0, -1e9, 1e9, 4, &ok);
        if (!ok) return;
        p.multiple = mult;
        p.minVal   = mn;
        p.maxVal   = mx;
        applyEditOp(op, p);
        return;
    }
    }
}

void MainWindow::applyEditOp(EditOp op, const EditParams &p)
{
    ProjectView *pv = activeView();
    if (!pv) {
        QMessageBox::information(this, tr("Selection"),
            tr("No active project view."));
        return;
    }
    WaveformWidget *ww = pv->waveformWidget();
    WaveformEditor *ed = ww ? ww->editor() : nullptr;
    if (!ed) {
        QMessageBox::information(this, tr("Selection"),
            tr("Editor not ready for this view."));
        return;
    }

    int start = 0, end = 0;
    QString why;
    if (!resolveActiveRange(this, pv, &start, &end, &why)) {
        QMessageBox::information(this, tr("Selection"), why);
        return;
    }

    switch (op) {
    case EditOp::ValuePlus1:   ed->increment(start, end, +1); break;
    case EditOp::ValueMinus1:  ed->increment(start, end, -1); break;
    case EditOp::Absolute:     ed->setAbsolute(start, end, p.value); break;
    case EditOp::Relative:
        if (p.isScale) ed->scale(start, end, p.scaleVal);
        else           ed->addDelta(start, end, p.delta);
        break;
    case EditOp::Slider:       ed->setAbsolute(start, end, p.value); break;
    case EditOp::RoundLimit:
        ed->roundLimit(start, end, p.multiple, p.minVal, p.maxVal);
        break;
    case EditOp::Original:     ed->restoreOriginal(start, end); break;
    case EditOp::Interpolate:  ed->interpolate(start, end); break;
    case EditOp::Smooth:       ed->smooth(start, end, 3); break;
    case EditOp::Flatten:      ed->flatten(start, end); break;
    }

    m_lastEditOp     = op;
    m_lastEditParams = p;
    m_haveLastEdit   = true;
}

// ── Sprint F: find similar maps dispatcher ─────────────────────────────────

void MainWindow::runFindSimilar(const MapInfo &reference)
{
    auto *p = activeProject();
    if (!p || p->maps.size() < 2) {
        QMessageBox::information(this, tr("Find similar"),
            tr("Need at least 2 maps in the project to compare."));
        return;
    }
    auto *dlg = new FindSimilarMapsDlg(p, reference, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &FindSimilarMapsDlg::goToRequested,
            this, [this](const MapInfo &m) {
                if (auto *v = activeView()) v->goToMap(m);
            });
    dlg->show();
}

// ── Sprint E: bulk edit dispatcher ──────────────────────────────────────────

void MainWindow::runBulkEdit(const QVector<MapInfo> &maps)
{
    if (maps.size() < 2) return;
    ProjectView *pv = activeView();
    if (!pv) return;
    WaveformEditor *ed = pv->waveformWidget()
                             ? pv->waveformWidget()->editor() : nullptr;
    if (!ed) return;

    // Operation picker (subset of EditOp that makes sense for whole-map ranges)
    QStringList opNames = {
        tr("Value +1"), tr("Value −1"),
        tr("Change absolute"), tr("Change relative (delta)"),
        tr("Change relative (scale ×)"),
        tr("Round / limit"),
        tr("Restore original"),
    };
    bool ok = false;
    QString chosen = QInputDialog::getItem(
        this, tr("Bulk edit %1 maps").arg(maps.size()),
        tr("Operation:"), opNames, 0, false, &ok);
    if (!ok) return;
    int opIdx = opNames.indexOf(chosen);
    if (opIdx < 0) return;

    EditParams p;
    EditOp op = EditOp::Original;
    switch (opIdx) {
    case 0: op = EditOp::ValuePlus1;  break;
    case 1: op = EditOp::ValueMinus1; break;
    case 2: {
        op = EditOp::Absolute;
        p.value = QInputDialog::getDouble(
            this, tr("Bulk edit"), tr("Absolute value:"),
            0.0, -1e9, 1e9, 3, &ok);
        if (!ok) return;
        break;
    }
    case 3: {
        op = EditOp::Relative;
        p.delta = QInputDialog::getDouble(
            this, tr("Bulk edit"), tr("Add delta (raw):"),
            0.0, -1e9, 1e9, 3, &ok);
        if (!ok) return;
        p.isScale = false;
        break;
    }
    case 4: {
        op = EditOp::Relative;
        p.scaleVal = QInputDialog::getDouble(
            this, tr("Bulk edit"), tr("Scale factor (1.05 = +5%):"),
            1.0, -1e3, 1e3, 4, &ok);
        if (!ok) return;
        p.isScale = true;
        break;
    }
    case 5: {
        op = EditOp::RoundLimit;
        p.multiple = QInputDialog::getInt(
            this, tr("Bulk edit"), tr("Round to multiple of:"),
            1, 1, 1000000, 1, &ok);
        if (!ok) return;
        p.minVal = QInputDialog::getDouble(
            this, tr("Bulk edit"), tr("Min:"), 0.0, -1e9, 1e9, 3, &ok);
        if (!ok) return;
        p.maxVal = QInputDialog::getDouble(
            this, tr("Bulk edit"), tr("Max:"), 65535.0, -1e9, 1e9, 3, &ok);
        if (!ok) return;
        break;
    }
    case 6: op = EditOp::Original; break;
    }

    qint64 totalCells = 0;
    for (const auto &m : maps) totalCells += m.length;
    auto reply = QMessageBox::question(
        this, tr("Bulk edit"),
        tr("This will modify %1 byte ranges across %2 maps. Proceed?")
            .arg(totalCells).arg(maps.size()),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    for (const MapInfo &m : maps) {
        const int start = static_cast<int>(m.address);
        const int end   = static_cast<int>(m.address + m.length);
        if (start < 0 || end <= start) continue;
        switch (op) {
        case EditOp::ValuePlus1:   ed->increment(start, end, +1); break;
        case EditOp::ValueMinus1:  ed->increment(start, end, -1); break;
        case EditOp::Absolute:     ed->setAbsolute(start, end, p.value); break;
        case EditOp::Relative:
            if (p.isScale) ed->scale(start, end, p.scaleVal);
            else           ed->addDelta(start, end, p.delta);
            break;
        case EditOp::RoundLimit:
            ed->roundLimit(start, end, p.multiple, p.minVal, p.maxVal);
            break;
        case EditOp::Original:     ed->restoreOriginal(start, end); break;
        default: break;
        }
    }
    statusBar()->showMessage(
        tr("Bulk edit applied to %1 maps").arg(maps.size()), 6000);
}

// ── Sprint L Lua bridge ─────────────────────────────────────────────────────
Project *MainWindow::luaActiveProject() const
{
    return activeProject();   // delegate to existing private impl
}

void MainWindow::luaNewProject()
{
    // Headless equivalent of "File → New Project": create a Project,
    // add it to MainWindow's project list, and open a sub-window for it.
    // Lua's projectImport(file) then operates on this freshly created project.
    auto *p = new Project(this);
    p->name = QStringLiteral("LuaNewProject");
    p->createdAt = QDateTime::currentDateTime();
    p->changedAt = p->createdAt;
    if (m_mdi) openProject(p);
}

int MainWindow::luaSaveAllProjects()
{
    int saved = 0;
    for (Project *p : m_projects) {
        if (!p) continue;
        if (p->filePath.isEmpty()) continue;   // skip in-memory projects
        if (p->save()) ++saved;
    }
    return saved;
}

int MainWindow::luaCloseAllProjects()
{
    if (!m_mdi) return 0;
    int count = m_mdi->subWindowList().size();
    m_mdi->closeAllSubWindows();
    return count;
}

bool MainWindow::luaCloseActiveProject(bool deleteFile)
{
    // Thin wrapper — does NOT touch m_projects directly.  Setting
    // modified=false skips the "save?" modal in eventFilter(); sw->close()
    // then triggers eventFilter which schedules finalizeClosedProject() via
    // QTimer::singleShot(0).  All m_projects/recentMaps/overlays mutation
    // happens deferred, after MDI's post-close relayout pass — see comment
    // on finalizeClosedProject() for the race history.
    if (!m_mdi) return false;
    auto *sw = m_mdi->activeSubWindow();
    if (!sw) return false;
    auto *pv = qobject_cast<ProjectView *>(sw->widget());
    if (!pv) return false;
    auto *proj = pv->project();
    if (!proj) return false;

    const QString path = proj->filePath;
    proj->modified = false;
    sw->close();

    if (deleteFile && !path.isEmpty()) {
        QTimer::singleShot(0, this, [path]{
            if (QFile::exists(path)) QFile::remove(path);
        });
    }
    return true;
}

// ── Lua bridge helpers ──────────────────────────────────────────────────────

void MainWindow::luaRememberLastCreatedMap(Project *project, int mapIndex)
{
    if (!project || mapIndex < 0 || mapIndex >= project->maps.size()) {
        luaClearLastCreatedMap();
        return;
    }
    m_luaLastCreatedMapProject = project;
    m_luaLastCreatedMapIndex = mapIndex;
}

MapInfo *MainWindow::luaLastCreatedMap(Project *project) const
{
    if (!project || m_luaLastCreatedMapProject.isNull())
        return nullptr;
    if (m_luaLastCreatedMapProject.data() != project)
        return nullptr;
    if (m_luaLastCreatedMapIndex < 0 || m_luaLastCreatedMapIndex >= project->maps.size())
        return nullptr;
    return &project->maps[m_luaLastCreatedMapIndex];
}

void MainWindow::luaClearLastCreatedMap(Project *project)
{
    if (project && !m_luaLastCreatedMapProject.isNull()
        && m_luaLastCreatedMapProject.data() != project) {
        return;
    }
    m_luaLastCreatedMapProject.clear();
    m_luaLastCreatedMapIndex = -1;
}

// ── Datalog menu wiring ─────────────────────────────────────────────────────

#include "datalog/LogViewerWindow.h"
#include "datalog/CompareLogsDialog.h"
#include "lua/LuaEngine.h"

QStringList MainWindow::datalogRecent() const
{
    return rx14::appSettings().value(QStringLiteral("datalog/recent")).toStringList();
}

void MainWindow::datalogPushRecent(const QString &path)
{
    QStringList list = datalogRecent();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > 10) list.removeLast();
    rx14::appSettings().setValue(QStringLiteral("datalog/recent"), list);
    rebuildDatalogMenu();
}

void MainWindow::openDatalog()
{
    QString p = QFileDialog::getOpenFileName(
        this, tr("Open datalog"), QString(),
        tr("Vehical logs (*.csv);;All files (*)"));
    if (p.isEmpty()) return;

    auto *w = new datalog::LogViewerWindow(this);
    w->setAttribute(Qt::WA_DeleteOnClose);
    QString err;
    if (!w->openFile(p, &err)) {
        QMessageBox::warning(this, tr("Open datalog"),
                             tr("Failed to open %1:\n%2").arg(p, err));
        w->deleteLater();
        return;
    }
    datalogPushRecent(p);
    w->show();
}

void MainWindow::compareDatalogs()
{
    auto *dlg = new datalog::CompareLogsDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::rebuildDatalogMenu()
{
    if (!m_menuDatalog) return;
    m_menuDatalog->clear();

    auto *openAct = m_menuDatalog->addAction(tr("&Open Log…"));
    openAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+L")));
    connect(openAct, &QAction::triggered, this, &MainWindow::openDatalog);

    auto *cmpAct = m_menuDatalog->addAction(tr("&Compare Logs…"));
    connect(cmpAct, &QAction::triggered, this, &MainWindow::compareDatalogs);

    // Sprint L — run an external Lua script through the embedded engine.
    m_menuDatalog->addSeparator();
    auto *luaAct = m_menuDatalog->addAction(tr("Run &Lua Script…"));
    luaAct->setEnabled(!lua::LuaEngine::instance().isScriptRunning());
    connect(&lua::LuaEngine::instance(),
            &lua::LuaEngine::scriptRunningChanged,
            luaAct,
            [luaAct](bool running) { luaAct->setEnabled(!running); });
    connect(luaAct, &QAction::triggered, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this,
            tr("Run Lua Script"), QString(),
            tr("Lua scripts (*.lua);;All files (*)"));
        if (path.isEmpty()) return;

        // P0-3 consent gate.  Even with the sandbox in place (P0-1) and
        // the HTTP allowlist (P0-2), a Lua script can still touch project
        // bytes, query the catalog, and make file-write side effects
        // inside the project tree.  Treat a fresh script the same way a
        // browser treats a fresh executable: require explicit consent.
        //
        // Repeat runs of the same exact content are silent — we key
        // trust on SHA-256 so a malicious edit re-prompts even if the
        // filename is unchanged.
        QFile probe(path);
        if (!probe.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, tr("Run Lua Script"),
                tr("Could not open %1").arg(path));
            return;
        }
        QCryptographicHash hasher(QCryptographicHash::Sha256);
        hasher.addData(&probe);
        probe.close();
        const QString hash = QString::fromLatin1(hasher.result().toHex());

        QSettings settings;
        const QString key = QStringLiteral("luaScriptTrust/") + hash;
        if (!settings.value(key, false).toBool()) {
            QMessageBox dlg(this);
            dlg.setIcon(QMessageBox::Warning);
            dlg.setWindowTitle(tr("Run Lua Script — consent required"));
            dlg.setText(tr("<b>About to execute:</b><br><code>%1</code>")
                            .arg(path.toHtmlEscaped()));
            dlg.setInformativeText(tr(
                "This Lua script will run with privileges to:"
                "<ul>"
                "<li>read and modify the active project's ROM bytes;</li>"
                "<li>read, write, and delete files inside the project tree "
                "    and the system temp directory;</li>"
                "<li>make HTTP requests to allowlisted hosts;</li>"
                "<li>query the WOLS similarity catalog.</li>"
                "</ul>"
                "Code-execution paths (<code>os.execute</code>, <code>io.popen</code>, "
                "loading native libraries, <code>dofile</code>) are <b>blocked</b> "
                "by the sandbox.<br><br>"
                "<b>Only run scripts from sources you trust.</b><br>"
                "SHA-256: <code>%1…</code>")
                .arg(hash.left(16)));
            auto *runOnce = dlg.addButton(tr("Run once"),
                                          QMessageBox::AcceptRole);
            auto *runAlways = dlg.addButton(tr("Trust this script"),
                                            QMessageBox::AcceptRole);
            dlg.addButton(QMessageBox::Cancel);
            dlg.setDefaultButton(QMessageBox::Cancel);
            dlg.exec();
            auto *clicked = dlg.clickedButton();
            if (clicked == runAlways) {
                settings.setValue(key, true);
            } else if (clicked != runOnce) {
                return;     // Cancelled.
            }
        }

        auto &engine = lua::LuaEngine::instance();
        auto *conn = new QMetaObject::Connection;
        *conn = connect(&engine, &lua::LuaEngine::scriptFinished, this,
                        [this, conn](const QJsonObject &r) {
            QObject::disconnect(*conn);
            delete conn;
            if (!r.value(QStringLiteral("ok")).toBool()) {
                QMessageBox::warning(this, tr("Lua error"),
                                     r.value(QStringLiteral("error")).toString());
            } else {
                QString out = r.value(QStringLiteral("output")).toString();
                if (!out.isEmpty()) {
                    QMessageBox::information(this, tr("Lua output"), out);
                }
            }
        });
        engine.runFileAsync(path);
    });

    QStringList list = datalogRecent();
    if (!list.isEmpty()) {
        m_menuDatalog->addSeparator();
        auto *recentMenu = m_menuDatalog->addMenu(tr("&Recent"));
        for (const QString &p : list) {
            auto *a = recentMenu->addAction(QFileInfo(p).fileName());
            a->setToolTip(p);
            const QString path = p;
            connect(a, &QAction::triggered, this, [this, path]() {
                auto *w = new datalog::LogViewerWindow(this);
                w->setAttribute(Qt::WA_DeleteOnClose);
                QString err;
                if (!w->openFile(path, &err)) {
                    QMessageBox::warning(this, tr("Open datalog"),
                                         tr("Failed to open %1:\n%2").arg(path, err));
                    w->deleteLater();
                    return;
                }
                datalogPushRecent(path);
                w->show();
            });
        }
        recentMenu->addSeparator();
        auto *clearAct = recentMenu->addAction(tr("&Clear list"));
        connect(clearAct, &QAction::triggered, this, [this]() {
            rx14::appSettings().remove(QStringLiteral("datalog/recent"));
            rebuildDatalogMenu();
        });
    }
}

#ifdef RX14_DEBUG_RPC
// ─────────────────────────────────────────────────────────────────────────────
//  Debug RPC entry points
//
//  Called by DebugRpc on the Qt main thread.  Each method is a thin facade
//  over private state so the RPC layer doesn't have to grow as new private
//  fields appear in MainWindow.
// ─────────────────────────────────────────────────────────────────────────────

QJsonObject MainWindow::debugStateSnapshot() const
{
    QJsonObject root;

    QJsonArray projects;
    for (Project *p : m_projects) {
        if (!p) continue;
        QJsonObject pj;
        pj.insert("path",        p->filePath);
        pj.insert("title",       p->fullTitle());
        pj.insert("modified",    p->modified);
        pj.insert("isLinkedRom", p->isLinkedRom);
        pj.insert("mapCount",    int(p->maps.size()));
        pj.insert("dataSize",    int(p->currentData.size()));
        projects.append(pj);
    }
    root.insert("projects", projects);

    QJsonArray subs;
    if (m_mdi) {
        const auto list = m_mdi->subWindowList();
        for (int i = 0; i < list.size(); ++i) {
            QMdiSubWindow *sw = list[i];
            if (!sw) continue;
            auto *pv = qobject_cast<ProjectView *>(sw->widget());
            QJsonObject sj;
            sj.insert("index",   i);
            sj.insert("title",   sw->windowTitle());
            sj.insert("active",  sw == m_mdi->activeSubWindow());
            QRect g = sw->geometry();
            QJsonObject geom;
            geom.insert("x", g.x()); geom.insert("y", g.y());
            geom.insert("w", g.width()); geom.insert("h", g.height());
            sj.insert("geometry", geom);
            if (pv) {
                Project *p = pv->project();
                sj.insert("projectPath", p ? p->filePath : QString());
                if (auto *hw = pv->hexWidget()) {
                    QJsonObject hex;
                    int row = hw->verticalScrollBar()->value();
                    int rowMax = hw->verticalScrollBar()->maximum();
                    hex.insert("row",         row);
                    hex.insert("rowMax",      rowMax);
                    hex.insert("bytesPerRow", hw->bytesPerRow());
                    hex.insert("byteOffset",  row * hw->bytesPerRow());
                    hex.insert("selectedOffset", hw->currentOffset());
                    sj.insert("hex", hex);
                }
                if (auto *ww = pv->waveformWidget()) {
                    QJsonObject wave;
                    wave.insert("scrollOffset", ww->scrollOffset());
                    sj.insert("wave", wave);
                }
            }
            subs.append(sj);
        }
    }
    root.insert("subwindows", subs);

    QJsonObject toggles;
    toggles.insert("syncCursors",
                   m_actSyncCursors ? m_actSyncCursors->isChecked() : false);
    toggles.insert("showDiff",         m_showDiff);
    toggles.insert("showOriginalDiff", m_showOriginalDiff);
    toggles.insert("heightColors",     m_heightColors);
    toggles.insert("dataSize",     m_dataSize);
    toggles.insert("displayFmt",   m_displayFmt);
    toggles.insert("byteOrder",
        m_byteOrder == ByteOrder::LittleEndian ? "LE" : "BE");
    root.insert("toggles", toggles);

    return root;
}

bool MainWindow::debugTakeScreenshot(const QString &target,
                                     QString *outPath, QString *outErr)
{
    QWidget *w = nullptr;
    if (target.isEmpty() || target == "main") {
        w = this;
    } else if (target == "active") {
        w = m_mdi ? m_mdi->activeSubWindow() : nullptr;
    } else if (target.startsWith("subwindow:")) {
        bool ok = false;
        int idx = target.mid(QStringLiteral("subwindow:").size()).toInt(&ok);
        if (ok && m_mdi) {
            const auto list = m_mdi->subWindowList();
            if (idx >= 0 && idx < list.size()) w = list[idx];
        }
    }
    if (!w) {
        if (outErr) *outErr = QStringLiteral("widget not found for target '%1'")
                                  .arg(target);
        return false;
    }

    const QString dir = QStringLiteral("D:/rx14-debug/screenshots");
    QDir().mkpath(dir);
    const QString tag = target.isEmpty() ? QStringLiteral("main")
                                         : QString(target).replace(':','_');
    const QString fname = QString("%1_%2.png")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz"))
        .arg(tag);
    const QString full = dir + "/" + fname;

    QPixmap pm = w->grab();
    if (pm.isNull() || !pm.save(full, "PNG")) {
        if (outErr) *outErr = QStringLiteral("save failed: %1").arg(full);
        return false;
    }
    if (outPath) *outPath = full;
    qCDebug(catRpc) << "screenshot saved" << full;
    return true;
}

bool MainWindow::debugTriggerAction(const QString &name,
                                    bool *outChecked, QString *outErr)
{
    // Map of names → action pointers.  Only sync/window/navigation toggles
    // are exposed for now — extend as needed.
    struct Entry { const char *name; QAction *MainWindow::*ptr; };
    static const Entry table[] = {
        { "m_actSyncCursors",   &MainWindow::m_actSyncCursors  },
        { "m_actCompare",       &MainWindow::m_actCompare      },
        { "m_actTile",          &MainWindow::m_actTile         },
        { "m_actCascade",       &MainWindow::m_actCascade      },
        { "m_actPrevMap",       &MainWindow::m_actPrevMap      },
        { "m_actNextMap",       &MainWindow::m_actNextMap      },
        { "m_actHome",          &MainWindow::m_actHome         },
        { "m_actCompareRoms",   &MainWindow::m_actCompareRoms  },
        { "m_actCompareHex",    &MainWindow::m_actCompareHex   },
        { "m_actHeightColors",  &MainWindow::m_actHeightColors },
        { "m_actDifference",    &MainWindow::m_actDifference   },
        { "m_actToggleDiff",    &MainWindow::m_actToggleDiff   },
        { "m_actToggleAI",      &MainWindow::m_actToggleAI     },
        { "m_actDiffOriginal",  &MainWindow::m_actDiffOriginal },
        { "m_actToggleSavepoints", &MainWindow::m_actToggleSavepoints },
        { "m_actValPlus1",      &MainWindow::m_actValPlus1     },
        { "m_actValMinus1",     &MainWindow::m_actValMinus1    },
        { "m_actChangeAbs",     &MainWindow::m_actChangeAbs    },
        { "m_actChangeRel",     &MainWindow::m_actChangeRel    },
        { "m_actOriginalVal",   &MainWindow::m_actOriginalVal  },
        { "m_actInterpolate",   &MainWindow::m_actInterpolate  },
        { "m_actAgain",         &MainWindow::m_actAgain        },
        { "m_actInsertComment", &MainWindow::m_actInsertComment },
        { "m_actInsertMarker",  &MainWindow::m_actInsertMarker  },
        { "m_actDeleteComment", &MainWindow::m_actDeleteComment },
        { "m_actNextMarker",    &MainWindow::m_actNextMarker    },
        { "m_actPrevMarker",    &MainWindow::m_actPrevMarker    },
    };
    for (const auto &e : table) {
        if (name == QLatin1String(e.name)) {
            QAction *a = this->*(e.ptr);
            if (!a) {
                if (outErr) *outErr = QStringLiteral("action is null: %1").arg(name);
                return false;
            }
            // Toggle if checkable, else trigger.
            if (a->isCheckable()) a->toggle();
            else                  a->trigger();
            if (outChecked) *outChecked = a->isChecked();
            qCDebug(catRpc) << "triggered" << name
                            << "checked=" << a->isChecked();
            return true;
        }
    }
    if (outErr) *outErr = QStringLiteral("unknown action: %1").arg(name);
    return false;
}

bool MainWindow::debugSetScroll(const QString &target, int subIdx, int value,
                                QString *outErr)
{
    if (!m_mdi) {
        if (outErr) *outErr = "no MDI area";
        return false;
    }
    const auto list = m_mdi->subWindowList();
    if (subIdx < 0 || subIdx >= list.size()) {
        if (outErr) *outErr = QStringLiteral("sub index out of range: %1")
                                  .arg(subIdx);
        return false;
    }
    auto *pv = qobject_cast<ProjectView *>(list[subIdx]->widget());
    if (!pv) {
        if (outErr) *outErr = "subwindow has no ProjectView";
        return false;
    }
    if (target == "hex") {
        auto *hw = pv->hexWidget();
        if (!hw) { if (outErr) *outErr = "no hex widget"; return false; }
        // value is interpreted as a row number — same units as
        // verticalScrollBar()->value()
        hw->verticalScrollBar()->setValue(value);
        qCDebug(catRpc) << "scroll hex sub" << subIdx << "row=" << value;
        return true;
    }
    if (target == "wave") {
        auto *ww = pv->waveformWidget();
        if (!ww) { if (outErr) *outErr = "no waveform widget"; return false; }
        // value is a byte offset — use syncScrollTo which interprets it.
        ww->syncScrollTo(value);
        qCDebug(catRpc) << "scroll wave sub" << subIdx << "offset=" << value;
        return true;
    }
    if (outErr) *outErr = QStringLiteral("unknown scroll target: %1").arg(target);
    return false;
}

bool MainWindow::debugLoadRom(const QString &romPath, QString *outErr)
{
    QFileInfo fi(romPath);
    if (!fi.exists() || !fi.isReadable()) {
        if (outErr) *outErr = QStringLiteral("file not readable: %1").arg(romPath);
        return false;
    }

    auto *project = new Project(this);
    project->createdAt = QDateTime::currentDateTime();
    project->createdBy = qEnvironmentVariable("USERNAME",
                             qEnvironmentVariable("USER", "Unknown"));
    loadROMIntoProject(project, romPath);

    if (project->currentData.isEmpty()) {
        if (outErr) *outErr = QStringLiteral("ROM parse produced no data");
        delete project;
        return false;
    }

    // Skip ProjectPropertiesDialog (modal — would block RPC).  Auto-save
    // to the standard project dir, register, open MDI subwindow.
    QString name = project->displayName();
    if (name.isEmpty()) name = fi.baseName();
    QString dir = ProjectRegistry::defaultProjectDir();
    QDir().mkpath(dir);
    QString savePath = dir + "/" + name + ".rx14proj";
    if (QFileInfo::exists(savePath)) {
        // Append timestamp to avoid clobbering existing project under
        // the same name — important for repeat tests.
        savePath = dir + "/" + name + "_" +
                   QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") +
                   ".rx14proj";
    }
    if (project->saveAs(savePath))
        ProjectRegistry::instance().registerProject(savePath, project);
    openProject(project);
    return true;
}

bool MainWindow::debugApplyEdit(int subIdx, int op, int start, int end,
                                double value, QString *outErr)
{
    if (!m_mdi) { if (outErr) *outErr = "no MDI"; return false; }
    const auto subs = m_mdi->subWindowList();
    if (subIdx < 0 || subIdx >= subs.size()) {
        if (outErr) *outErr = QStringLiteral("sub index %1 out of range").arg(subIdx);
        return false;
    }
    auto *pv = qobject_cast<ProjectView *>(subs[subIdx]->widget());
    if (!pv) { if (outErr) *outErr = "no ProjectView"; return false; }
    auto *ww = pv->waveformWidget();
    auto *ed = ww ? ww->editor() : nullptr;
    if (!ed) { if (outErr) *outErr = "no editor"; return false; }

    switch (static_cast<EditOp>(op)) {
    case EditOp::ValuePlus1:   ed->increment(start, end, +1); break;
    case EditOp::ValueMinus1:  ed->increment(start, end, -1); break;
    case EditOp::Absolute:     ed->setAbsolute(start, end, value); break;
    case EditOp::Relative:     ed->addDelta(start, end, value); break;
    case EditOp::Original:     ed->restoreOriginal(start, end); break;
    case EditOp::Interpolate:  ed->interpolate(start, end); break;
    case EditOp::Smooth:       ed->smooth(start, end, 3); break;
    case EditOp::Flatten:      ed->flatten(start, end); break;
    case EditOp::RoundLimit:
        ed->roundLimit(start, end, 1, 0, value);
        break;
    default:
        if (outErr) *outErr = QStringLiteral("op %1 not supported via RPC").arg(op);
        return false;
    }
    return true;
}

bool MainWindow::debugAddAnnotation(int subIdx, qint64 addr,
                                    const QString &text, qint64 length,
                                    QString *outErr)
{
    if (!m_mdi) { if (outErr) *outErr = "no MDI"; return false; }
    const auto subs = m_mdi->subWindowList();
    if (subIdx < 0 || subIdx >= subs.size()) {
        if (outErr) *outErr = QStringLiteral("sub index out of range: %1").arg(subIdx);
        return false;
    }
    auto *pv = qobject_cast<ProjectView *>(subs[subIdx]->widget());
    if (!pv || !pv->project()) {
        if (outErr) *outErr = "no project"; return false;
    }
    auto *store = pv->project()->annotations();
    if (!store) { if (outErr) *outErr = "no store"; return false; }
    store->add(addr, text, qMax<qint64>(1, length));
    return true;
}

QJsonArray MainWindow::debugAnnotationList(int subIdx) const
{
    QJsonArray arr;
    if (!m_mdi) return arr;
    const auto subs = m_mdi->subWindowList();
    if (subIdx < 0 || subIdx >= subs.size()) return arr;
    auto *pv = qobject_cast<ProjectView *>(subs[subIdx]->widget());
    if (!pv || !pv->project()) return arr;
    auto *store = pv->project()->annotations();
    if (!store) return arr;
    for (const Annotation &a : store->all()) {
        QJsonObject o;
        o.insert("addr",   static_cast<double>(a.addr));
        o.insert("length", static_cast<double>(a.length));
        o.insert("text",   a.text);
        o.insert("isMarker", a.isMarker());
        arr.append(o);
    }
    return arr;
}

bool MainWindow::debugExportMapList(int subIdx, bool csv,
                                    const QString &path, QString *outErr)
{
    if (!m_mdi) { if (outErr) *outErr = "no MDI"; return false; }
    const auto subs = m_mdi->subWindowList();
    if (subIdx < 0 || subIdx >= subs.size()) {
        if (outErr) *outErr = "sub idx out of range";
        return false;
    }
    auto *pv = qobject_cast<ProjectView *>(subs[subIdx]->widget());
    if (!pv || !pv->project()) {
        if (outErr) *outErr = "no project"; return false;
    }
    return csv ? MapListExporter::toCsv(*pv->project(), path, outErr)
               : MapListExporter::toJson(*pv->project(), path, outErr);
}

QJsonArray MainWindow::debugFindSimilar(int subIdx, const QString &refName,
                                        double threshold) const
{
    QJsonArray arr;
    if (!m_mdi) return arr;
    const auto subs = m_mdi->subWindowList();
    if (subIdx < 0 || subIdx >= subs.size()) return arr;
    auto *pv = qobject_cast<ProjectView *>(subs[subIdx]->widget());
    if (!pv || !pv->project()) return arr;
    const Project *p = pv->project();
    const MapInfo *ref = nullptr;
    for (const MapInfo &m : p->maps) if (m.name == refName) { ref = &m; break; }
    if (!ref) return arr;
    const auto refFp = MapFingerprintEngine::computeFor(p->currentData,
                                                        *ref, p->byteOrder);
    if (!refFp.isValid()) return arr;
    for (const MapInfo &m : p->maps) {
        if (m.name == refName) continue;
        const auto fp = MapFingerprintEngine::computeFor(p->currentData,
                                                         m, p->byteOrder);
        if (!fp.isValid()) continue;
        const double s = MapFingerprintEngine::similarity(refFp, fp);
        if (s >= threshold) {
            QJsonObject o;
            o.insert("name", m.name);
            o.insert("address", QString("0x%1")
                                    .arg(m.address, 8, 16, QChar('0')).toUpper());
            o.insert("dims", QString("%1x%2").arg(m.dimensions.x).arg(m.dimensions.y));
            o.insert("score", s);
            arr.append(o);
        }
    }
    return arr;
}

QString MainWindow::debugReadBytes(int subIdx, qint64 addr, int len) const
{
    if (!m_mdi) return {};
    const auto subs = m_mdi->subWindowList();
    if (subIdx < 0 || subIdx >= subs.size()) return {};
    auto *pv = qobject_cast<ProjectView *>(subs[subIdx]->widget());
    if (!pv || !pv->project()) return {};
    const auto &d = pv->project()->currentData;
    len = qBound(1, len, 256);
    if (addr < 0 || addr + len > d.size()) return {};
    return QString::fromLatin1(d.mid(addr, len).toHex());
}

bool MainWindow::debugBulkEdit(int subIdx, const QStringList &mapNames,
                               int op, double v, QString *outErr)
{
    if (!m_mdi) { if (outErr) *outErr = "no MDI"; return false; }
    const auto subs = m_mdi->subWindowList();
    if (subIdx < 0 || subIdx >= subs.size()) {
        if (outErr) *outErr = "sub idx out of range"; return false;
    }
    auto *pv = qobject_cast<ProjectView *>(subs[subIdx]->widget());
    if (!pv || !pv->project()) { if (outErr) *outErr = "no project"; return false; }
    auto *ed = pv->waveformWidget() ? pv->waveformWidget()->editor() : nullptr;
    if (!ed) { if (outErr) *outErr = "no editor"; return false; }

    QHash<QString, const MapInfo *> byName;
    for (const MapInfo &m : pv->project()->maps) byName.insert(m.name, &m);

    int touched = 0;
    for (const QString &name : mapNames) {
        const MapInfo *m = byName.value(name, nullptr);
        if (!m) continue;
        const int s = static_cast<int>(m->address);
        const int e = static_cast<int>(m->address + m->length);
        if (s < 0 || e <= s) continue;
        switch (static_cast<EditOp>(op)) {
        case EditOp::ValuePlus1:   ed->increment(s, e, +1); break;
        case EditOp::ValueMinus1:  ed->increment(s, e, -1); break;
        case EditOp::Absolute:     ed->setAbsolute(s, e, v); break;
        case EditOp::Relative:     ed->addDelta(s, e, v); break;
        case EditOp::Original:     ed->restoreOriginal(s, e); break;
        case EditOp::Interpolate:  ed->interpolate(s, e); break;
        case EditOp::Smooth:       ed->smooth(s, e, 3); break;
        case EditOp::Flatten:      ed->flatten(s, e); break;
        default:
            if (outErr) *outErr = "op not supported in bulk RPC";
            return false;
        }
        ++touched;
    }
    if (outErr && touched == 0) *outErr = "no maps matched";
    return touched > 0;
}

bool MainWindow::debugUndo(int subIdx, QString *outErr)
{
    if (!m_mdi) { if (outErr) *outErr = "no MDI"; return false; }
    const auto subs = m_mdi->subWindowList();
    if (subIdx < 0 || subIdx >= subs.size()) {
        if (outErr) *outErr = "sub idx out of range"; return false;
    }
    auto *pv = qobject_cast<ProjectView *>(subs[subIdx]->widget());
    if (!pv) { if (outErr) *outErr = "no view"; return false; }
    auto *ed = pv->waveformWidget() ? pv->waveformWidget()->editor() : nullptr;
    if (!ed) { if (outErr) *outErr = "no editor"; return false; }
    ed->undo();
    return true;
}

bool MainWindow::debugRemoveAnnotation(int subIdx, qint64 addr, QString *outErr)
{
    if (!m_mdi) { if (outErr) *outErr = "no MDI"; return false; }
    const auto subs = m_mdi->subWindowList();
    if (subIdx < 0 || subIdx >= subs.size()) {
        if (outErr) *outErr = "sub idx out of range"; return false;
    }
    auto *pv = qobject_cast<ProjectView *>(subs[subIdx]->widget());
    if (!pv || !pv->project()) { if (outErr) *outErr = "no project"; return false; }
    auto *st = pv->project()->annotations();
    if (!st) { if (outErr) *outErr = "no store"; return false; }
    return st->removeAt(addr);
}

bool MainWindow::debugOpenProject(const QString &path, QString *outErr)
{
    Project *p = Project::open(path, this);
    if (!p) {
        if (outErr) *outErr = QStringLiteral("Project::open failed for %1").arg(path);
        return false;
    }
    m_projects.append(p);
    openProject(p);
    return true;
}

QJsonArray MainWindow::debugMapList(int subIdx, int limit) const
{
    QJsonArray arr;
    if (!m_mdi) return arr;
    const auto subs = m_mdi->subWindowList();
    if (subIdx < 0 || subIdx >= subs.size()) return arr;
    auto *pv = qobject_cast<ProjectView *>(subs[subIdx]->widget());
    if (!pv || !pv->project()) return arr;
    const auto &maps = pv->project()->maps;
    const int n = (limit > 0) ? qMin(limit, int(maps.size())) : maps.size();
    for (int i = 0; i < n; ++i) {
        const MapInfo &m = maps[i];
        QJsonObject o;
        o.insert("name", m.name);
        o.insert("address", QString("0x%1")
                                .arg(m.address, 8, 16, QChar('0')).toUpper());
        o.insert("addressDec", static_cast<double>(m.address));
        o.insert("length", m.length);
        o.insert("dims", QString("%1x%2").arg(m.dimensions.x).arg(m.dimensions.y));
        o.insert("dataSize", m.dataSize);
        o.insert("type", m.type);
        arr.append(o);
    }
    return arr;
}

bool MainWindow::debugSwitchView(int subIdx, int viewIdx, QString *outErr)
{
    if (!m_mdi) { if (outErr) *outErr = "no MDI area"; return false; }
    const auto list = m_mdi->subWindowList();
    if (subIdx < 0 || subIdx >= list.size()) {
        if (outErr) *outErr = QStringLiteral("sub index out of range: %1").arg(subIdx);
        return false;
    }
    auto *pv = qobject_cast<ProjectView *>(list[subIdx]->widget());
    if (!pv) { if (outErr) *outErr = "subwindow has no ProjectView"; return false; }
    pv->switchView(viewIdx);
    qCDebug(catRpc) << "switch_view sub" << subIdx << "->" << viewIdx;
    return true;
}

#endif // RX14_DEBUG_RPC

// ── Control-module panel (BCM) ──────────────────────────────────────────────
void MainWindow::refreshModulePanel()
{
    if (!m_modulePanel) return;
    auto *proj = activeProject();
    module::ModuleInfo info;
    if (proj && !proj->currentData.isEmpty())
        info = module::ModuleRegistry::instance().parse(proj->currentData);
    m_modulePanel->setModuleInfo(info);
    // Auto-reveal the pinned dock the first time a module is recognized.
    if (info.detected && m_moduleDock && m_moduleDock->isHidden())
        m_moduleDock->show();
}
