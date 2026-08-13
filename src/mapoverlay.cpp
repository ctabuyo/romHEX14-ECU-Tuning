/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "mapoverlay.h"
#include "project.h"
#include "appconfig.h"
#include "appconstants.h"
#include <QSignalBlocker>
#include "mappropertiesdlg.h"
#include "featuregate.h"
#ifdef RX14_PRO_BUILD
#include "apiclient.h"
#endif
#include "aiprovider.h"
#include "aiassistant.h"
#include "logger.h"
#include <QSettings>
#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QMenu>
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QTextStream>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QHeaderView>
#include <QPainter>
#include <QScrollBar>
#include <QScreen>
#include <QApplication>
#include <QStyledItemDelegate>
#include <QKeyEvent>
#include <QShortcut>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <cmath>
#include <limits>

// ═══════════════════════════════════════════════════════════════════════════════
// Icons
// ═══════════════════════════════════════════════════════════════════════════════
static QIcon makeIcon(int type, QColor col)
{
    // 0=undo  1=redo  2=ORI  3=3D  4=+%  5=+val  6==val
    const int S = 18;
    const qreal dpr = qApp->devicePixelRatio();
    const int pxS = qRound(S * dpr);
    QPixmap pm(pxS, pxS);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    QPen thick(col, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    QPen thin (col, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);

    switch (type) {
    case 0: // undo
        p.setPen(thick); p.setBrush(Qt::NoBrush);
        p.drawArc(3, 5, 11, 9, 90*16, 180*16);
        p.setBrush(col); p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygonF({{3,9.5},{7,6},{7,13}}));
        break;
    case 1: // redo
        p.setPen(thick); p.setBrush(Qt::NoBrush);
        p.drawArc(4, 5, 11, 9, 90*16, -180*16);
        p.setBrush(col); p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygonF({{15,9.5},{11,6},{11,13}}));
        break;
    case 2: // ORI (circular arrow)
        p.setPen(thick); p.setBrush(Qt::NoBrush);
        p.drawArc(3, 3, 12, 11, 150*16, -300*16);
        p.setBrush(col); p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygonF({{3,9},{6.5,6.5},{6.5,12}}));
        break;
    case 3: // 3D cube
        p.setPen(thin);
        p.drawRect(3, 6, 8, 8);
        p.drawLine(3,6, 7,2); p.drawLine(11,6, 15,2);
        p.drawLine(7,2, 15,2); p.drawLine(11,14, 15,10);
        p.drawLine(15,2, 15,10);
        break;
    case 4: { // +%  — draw "%" symbol and small "+"
        QFont fPct("Segoe UI", 0, QFont::Bold);
        fPct.setPixelSize(14);
        p.setFont(fPct);
        p.setPen(col);
        p.drawText(QRectF(-1, 0, S, S), Qt::AlignCenter, "%");
        // Small "+" in top-right corner
        p.setPen(QPen(col, 1.5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(14, 1, 14, 6);
        p.drawLine(11.5, 3.5, 16.5, 3.5);
        break;
    }
    case 5: { // +val — draw "+" symbol
        QFont fPlus("Segoe UI", 0, QFont::Bold);
        fPlus.setPixelSize(16);
        p.setFont(fPlus);
        p.setPen(col);
        p.drawText(QRectF(0, 0, S, S), Qt::AlignCenter, "+");
        break;
    }
    case 6: { // =val — draw "=" symbol
        QFont fEq("Segoe UI", 0, QFont::Bold);
        fEq.setPixelSize(16);
        p.setFont(fEq);
        p.setPen(col);
        p.drawText(QRectF(0, 0, S, S), Qt::AlignCenter, "=");
        break;
    }
    }
    return QIcon(pm);
}

static QToolButton *mkBtn(int iconType, QColor iconColor,
                           const QString &tip, bool checkable = false)
{
    auto *b = new QToolButton();
    b->setIcon(makeIcon(iconType, iconColor));
    b->setIconSize(QSize(18, 18));
    b->setFixedSize(26, 26);
    b->setCheckable(checkable);
    b->setToolTip(tip);
    b->setToolButtonStyle(Qt::ToolButtonIconOnly);
    b->setStyleSheet(
        "QToolButton { background:transparent; border:1px solid transparent; border-radius:4px; }"
        "QToolButton:hover   { background:#2d333b; border-color:#444c56; }"
        "QToolButton:pressed { background:#161b22; }"
        "QToolButton:checked { background:#272115; border-color:#ffc000; }"
        "QToolButton:disabled{ opacity:0.3; }");
    return b;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Cell delegate
// ═══════════════════════════════════════════════════════════════════════════════
class MapCellDelegate : public QStyledItemDelegate {
public:
    bool heatEnabled = true;
    QColor cellBg       = QColor(15, 22, 36);
    QColor cellText     = QColor(220, 230, 240);
    QColor cellModified = QColor(255, 200, 60);
    QColor gridLine     = QColor(30, 40, 60);

    explicit MapCellDelegate(QObject *p) : QStyledItemDelegate(p) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &opt,
               const QModelIndex &idx) const override
    {
        const QColor heatBg   = idx.data(Qt::BackgroundRole).value<QBrush>().color();
        const bool   modified = idx.data(Qt::UserRole + 1).toBool();
        const bool   pending  = idx.data(Qt::UserRole + 2).toBool();
        const bool   selected = (opt.state & QStyle::State_Selected) != 0;
        const QRect  r        = opt.rect;

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        // ── 1. Background with subtle inner gradient for depth ──
        QColor bg = heatEnabled ? heatBg : cellBg;
        if (heatEnabled) {
            QLinearGradient grad(r.topLeft(), r.bottomLeft());
            grad.setColorAt(0.0, bg.lighter(115));
            grad.setColorAt(0.5, bg);
            grad.setColorAt(1.0, bg.darker(112));
            painter->fillRect(r, grad);
        } else {
            painter->fillRect(r, bg);
        }

        // ── 2. Cell borders — subtle depth lines ──
        // Bottom edge: slightly darker (shadow)
        painter->setPen(QPen(QColor(0, 0, 0, heatEnabled ? 40 : 25), 0.5));
        painter->drawLine(r.left(), r.bottom(), r.right(), r.bottom());
        // Right edge: subtle separator
        painter->setPen(QPen(QColor(255, 255, 255, heatEnabled ? 12 : 8), 0.5));
        painter->drawLine(r.right(), r.top() + 2, r.right(), r.bottom() - 2);
        // Top edge: highlight (creates inset look)
        painter->setPen(QPen(QColor(255, 255, 255, heatEnabled ? 15 : 6), 0.5));
        painter->drawLine(r.left(), r.top(), r.right(), r.top());

        // ── 3. Value text — white with dark outline, always readable ──
        QFont f = opt.font;
        f.setStyleStrategy(QFont::PreferAntialias);
        f.setWeight(modified ? QFont::Bold : QFont::DemiBold);
        painter->setFont(f);

        const QString text = idx.data(Qt::DisplayRole).toString();
        const QRect textRect = r.adjusted(2, 1, -3, -1);

        if (heatEnabled) {
            // Dark outline for legibility on any heat color
            painter->setPen(QColor(0, 0, 0, 180));
            for (int dx = -1; dx <= 1; dx++)
                for (int dy = -1; dy <= 1; dy++)
                    if (dx || dy)
                        painter->drawText(textRect.adjusted(dx, dy, dx, dy),
                                          Qt::AlignRight | Qt::AlignVCenter, text);
            painter->setPen(Qt::white);
        } else {
            painter->setPen(modified ? cellModified : cellText);
        }
        painter->drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, text);

        // ── 4. Modified indicator — glowing left edge ──
        if (modified) {
            QLinearGradient glow(r.topLeft(), QPoint(r.left() + 4, r.top()));
            glow.setColorAt(0, QColor(255, 180, 0, 180));
            glow.setColorAt(1, QColor(255, 180, 0, 0));
            painter->fillRect(QRect(r.left(), r.top(), 4, r.height()), glow);
        }

        // ── 5. Pending overlay — soft amber pulse ──
        if (pending) {
            painter->fillRect(r, QColor(255, 160, 40, 30));
            painter->setPen(QPen(QColor(255, 180, 60, 200), 1.5));
            painter->drawRect(r.adjusted(1, 1, -1, -1));
        }
        // ── 6. Selection — bold bright highlight ──
        else if (selected) {
            painter->fillRect(r, QColor(255, 255, 255, 50));
            painter->setPen(QPen(QColor(255, 255, 255), 2.5));
            painter->drawRect(r.adjusted(1, 1, -1, -1));
        }

        painter->restore();
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// Row bar delegate — OLS-style horizontal bar at right edge
// ═══════════════════════════════════════════════════════════════════════════════
class RowBarDelegate : public QStyledItemDelegate {
public:
    explicit RowBarDelegate(QObject *p) : QStyledItemDelegate(p) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &opt,
               const QModelIndex &idx) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        const QRect r = opt.rect;
        painter->fillRect(r, QColor(8, 11, 16));

        double pct = qBound(0.0, idx.data(Qt::UserRole).toDouble(), 1.0);
        QRectF barRect = QRectF(r).adjusted(3, 3, -3, -3);
        double barW = qMax(2.0, pct * barRect.width());

        // Track background — subtle rounded rect
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(20, 28, 40));
        painter->drawRoundedRect(barRect, 3, 3);

        // Bar fill — uses the heat color palette for consistency
        QColor barCol = heatColor(pct);
        QRectF fill = barRect;
        fill.setWidth(barW);
        QLinearGradient grad(fill.topLeft(), fill.bottomLeft());
        grad.setColorAt(0, barCol.lighter(120));
        grad.setColorAt(1, barCol.darker(110));
        painter->setBrush(grad);
        painter->drawRoundedRect(fill, 3, 3);

        painter->restore();
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// Constructor
// ═══════════════════════════════════════════════════════════════════════════════
MapOverlay::MapOverlay(QWidget *parent)
    : QDialog(parent,
              Qt::Tool | Qt::WindowTitleHint |
              Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint)
{
    setWindowTitle(tr("Map"));
    setAttribute(Qt::WA_DeleteOnClose, true);
    resize(680, 460);
    setMinimumSize(480, 320);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 4);
    root->setSpacing(4);

    // ── Toolbar ──────────────────────────────────────────────────────────────
    auto *toolbar = new QHBoxLayout();
    toolbar->setSpacing(3);

    m_statsLabel = new QLabel();
    m_statsLabel->setStyleSheet("color:#6888a8; font-size:8pt; font-family:'Segoe UI',sans-serif;");
    toolbar->addWidget(m_statsLabel, 1);

    auto mkSep = [&]() {
        auto *f = new QFrame();
        f->setFrameShape(QFrame::VLine);
        f->setFixedWidth(1);
        f->setStyleSheet("background:#30363d; border:none;");
        toolbar->addWidget(f);
    };

    mkSep();

    m_cellLabel = new QLabel(tr("Cell:"));
    m_cellLabel->setStyleSheet("color:#8b949e; font-size:8pt;");
    toolbar->addWidget(m_cellLabel);
    m_cellSizeCombo = new QComboBox();
    m_cellSizeCombo->addItem(tr("8-bit"),  1);
    m_cellSizeCombo->addItem(tr("16-bit"), 2);
    m_cellSizeCombo->addItem(tr("32-bit"), 4);
    m_cellSizeCombo->setCurrentIndex(1);
    m_cellSizeCombo->setFixedWidth(66);
    m_cellSizeCombo->setStyleSheet(
        "QComboBox { background:#161b22; color:#c9d1d9; border:1px solid #30363d;"
        " border-radius:3px; font-size:8pt; padding:1px 4px; }"
        "QComboBox::drop-down { border:none; width:14px; }"
        "QComboBox QAbstractItemView { background:#161b22; color:#c9d1d9;"
        " selection-background-color:#1f6feb; border:1px solid #30363d; }");
    toolbar->addWidget(m_cellSizeCombo);

    m_orderLabel = new QLabel(tr("Order:"));
    m_orderLabel->setStyleSheet("color:#8b949e; font-size:8pt;");
    toolbar->addWidget(m_orderLabel);
    m_byteOrderCombo = new QComboBox();
    m_byteOrderCombo->addItem(tr("Big Endian"),    int(ByteOrder::BigEndian));
    m_byteOrderCombo->addItem(tr("Little Endian"), int(ByteOrder::LittleEndian));
    m_byteOrderCombo->setFixedWidth(100);
    m_byteOrderCombo->setStyleSheet(m_cellSizeCombo->styleSheet());
    toolbar->addWidget(m_byteOrderCombo);

    mkSep();

    m_btnUndo = mkBtn(0, QColor("#8b949e"), tr("Undo  Ctrl+Z"));
    m_btnRedo = mkBtn(1, QColor("#8b949e"), tr("Redo  Ctrl+Y"));
    m_btnUndo->setEnabled(false);
    m_btnRedo->setEnabled(false);
    toolbar->addWidget(m_btnUndo);
    toolbar->addWidget(m_btnRedo);

    mkSep();

    m_btnOri  = mkBtn(2, QColor("#ffc000"), tr("Show original values (read-only)"), /*checkable*/true);
    m_btnHeat = new QToolButton();
    m_btnHeat->setText(tr("Heat"));
    m_btnHeat->setCheckable(true);
    m_btnHeat->setChecked(true);
    m_btnHeat->setToolTip(tr("Toggle heat map coloring on/off"));
    m_btnHeat->setStyleSheet(
        "QToolButton{background:rgba(58,145,208,0.15);color:#58a6ff;border:1px solid rgba(58,145,208,0.3);"
        "border-radius:4px;padding:2px 8px;font-size:8pt;font-weight:bold}"
        "QToolButton:checked{background:rgba(58,145,208,0.3);border-color:#58a6ff}"
        "QToolButton:!checked{background:transparent;color:#484f58;border-color:#30363d}");
    m_btn3D  = mkBtn(3, QColor("#8b949e"), tr("Toggle 3D view"), /*checkable*/true);
    // 3D Sim button
    m_btn3DSim = new QToolButton();
    m_btn3DSim->setText("3D Sim");
    m_btn3DSim->setCheckable(true);
    m_btn3DSim->setToolTip(tr("3D Simulation View"));
    m_btn3DSim->setStyleSheet(
        "QToolButton{background:transparent;color:#8b949e;border:1px solid #30363d;"
        "border-radius:4px;padding:3px 8px;font-size:8pt;font-weight:bold}"
        "QToolButton:hover{background:#1c2128;color:#e7eefc;border-color:#484f58}"
        "QToolButton:checked{background:rgba(124,58,237,0.3);color:#c084fc;border-color:#7c3aed}");

    // Difference view selector — plain values vs numeric delta / % vs original
    m_diffCombo = new QComboBox();
    m_diffCombo->addItem(tr("Values"));
    m_diffCombo->addItem(tr("Δ Original"));
    m_diffCombo->addItem(tr("% Original"));
    m_diffCombo->setFixedWidth(104);
    m_diffCombo->setToolTip(tr("Show plain values, or the numeric difference from the original ROM"));
    m_diffCombo->setStyleSheet(m_cellSizeCombo->styleSheet());

    toolbar->addWidget(m_btnOri);
    toolbar->addWidget(m_diffCombo);
    toolbar->addWidget(m_btnHeat);
    toolbar->addWidget(m_btn3D);
    toolbar->addWidget(m_btn3DSim);

    mkSep();

    // AI translate button — only visible when logged in with translation feature
    m_btnTranslate = new QPushButton(tr("Translate"));
    m_btnTranslate->setToolTip(tr("AI-translate map name and description (Pro)"));
    m_btnTranslate->setFixedHeight(24);
    m_btnTranslate->setStyleSheet(
        "QPushButton { background:#1f3a5f; color:#58a6ff; border:1px solid #1f6feb;"
        " border-radius:3px; font-size:8pt; padding:0 8px; }"
        "QPushButton:hover { background:#1f6feb; color:white; }"
        "QPushButton:disabled { background:#1c2230; color:#444; border-color:#333; }");
    toolbar->addWidget(m_btnTranslate);

    auto updateTranslateBtn = [this]() {
        const bool can = FeatureGate::isAvailable(QStringLiteral("translation"));
        m_btnTranslate->setVisible(FeatureGate::isLoggedIn());
        m_btnTranslate->setEnabled(can);
        if (!can && FeatureGate::isLoggedIn())
            m_btnTranslate->setToolTip(tr("AI map translation requires a Pro account"));
    };
    updateTranslateBtn();
#ifdef RX14_PRO_BUILD
    connect(&ApiClient::instance(), &ApiClient::loginStateChanged,
            this, updateTranslateBtn);
#endif

#ifdef RX14_PRO_BUILD
    connect(m_btnTranslate, &QPushButton::clicked, this, [this]() {
        m_btnTranslate->setEnabled(false);
        m_btnTranslate->setText(tr("\u2026"));

        const QString lang = QLocale::system().name().left(2); // "en", "es", etc.
        ApiClient::instance().translateMap(
            m_map.name, m_map.description, lang,
            [this](bool ok, const QString &name, const QString &desc) {
                m_btnTranslate->setEnabled(true);
                m_btnTranslate->setText(tr("Translate"));
                if (ok && !name.isEmpty()) {
                    // Update window title with translation
                    QString title = m_map.name + "  \u2014  " + name;
                    if (!desc.isEmpty())
                        title += "\n" + desc;
                    setWindowTitle(title);
                    // Show translated description in axis bar
                    if (!desc.isEmpty()) {
                        m_axisBar->setText(desc);
                        m_axisBar->setVisible(true);
                    }
                }
            });
    });
#endif // RX14_PRO_BUILD

    // AI Explain button — streams Claude response with typewriter effect
    m_btnAIExplain = new QPushButton("AI");
    m_btnAIExplain->setToolTip(tr("AI map explanation — what does this map do?"));
    m_btnAIExplain->setFixedHeight(24);
    m_btnAIExplain->setStyleSheet(
        "QPushButton { background:#1a1e2e; color:#a78bfa; border:1px solid #7c3aed;"
        " border-radius:3px; font-size:8pt; font-weight:bold; padding:0 8px; }"
        "QPushButton:hover { background:#7c3aed; color:white; }"
        "QPushButton:disabled { background:#1c2230; color:#444; border-color:#333; }");
    toolbar->addWidget(m_btnAIExplain);

    connect(m_btnAIExplain, &QPushButton::clicked, this, [this]() {
        // ── Check cache first ───────────────────────────────────────────
        if (m_aiCache.contains(m_map.name)) {
            auto *dlg = new QDialog(this);
            dlg->setWindowTitle(QString::fromUtf8("\xe2\x9c\xa8 AI \xe2\x80\x94 ") + m_map.name);
            dlg->setMinimumSize(420, 120);
            dlg->setFixedWidth(520);
            dlg->setStyleSheet("QDialog{background:#0d1117;}QLabel{background:transparent;border:none;}");
            auto *lay = new QVBoxLayout(dlg);
            lay->setContentsMargins(16, 14, 16, 14);
            lay->setSpacing(8);
            QString fullTitle = windowTitle();
            auto *hdr = new QLabel(QString("<span style='color:#a78bfa;font-size:10pt;font-weight:bold;'>")
                + QString::fromUtf8("\xe2\x9c\xa8 ") + fullTitle.toHtmlEscaped() + "</span>", dlg);
            hdr->setWordWrap(true);
            lay->addWidget(hdr);
            auto *sep = new QFrame(dlg); sep->setFrameShape(QFrame::HLine);
            sep->setStyleSheet("background:#21262d;max-height:1px;border:none;");
            lay->addWidget(sep);
            auto *lbl = new QLabel(m_aiCache[m_map.name], dlg);
            lbl->setWordWrap(true);
            lbl->setTextFormat(Qt::PlainText);
            lbl->setStyleSheet("color:#c9d1d9;font-size:9pt;padding:4px 0;");
            lay->addWidget(lbl, 1);
            dlg->adjustSize();
            dlg->show();
            return;
        }

        auto *prov = AIAssistant::createOneShotProvider(this);
        if (!prov) {
            QMessageBox::information(this, "AI",
                tr("Configure an AI provider in the AI assistant settings first."));
            return;
        }
        auto *aiProv = prov;

        m_btnAIExplain->setEnabled(false);
        m_btnAIExplain->setText(QString::fromUtf8("\xe2\x9c\xa6")); // ✦

        // Determine language
        QString uiLang = rx14::appSettings().value("language", "en").toString();
        QMap<QString,QString> langMap = {
            {"en","English"}, {"zh","Simplified Chinese"}, {"zh_CN","Simplified Chinese"},
            {"es","Spanish"}, {"th","Thai"}, {"de","German"},
            {"ja","Japanese"}, {"ko","Korean"}, {"fr","French"}, {"ru","Russian"}};
        QString langName = langMap.value(uiLang, "English");

        QString info = QString("%1x%2").arg(m_map.dimensions.x).arg(m_map.dimensions.y);
        if (m_map.hasScaling) info += QString(" factor=%1").arg(m_map.scaling.linA);
        if (!m_map.scaling.unit.isEmpty()) info += " unit=" + m_map.scaling.unit;
        if (!m_map.xAxis.scaling.unit.isEmpty()) info += " X=" + m_map.xAxis.scaling.unit;
        if (!m_map.yAxis.scaling.unit.isEmpty()) info += " Y=" + m_map.yAxis.scaling.unit;

        QString prompt = QString(
            "Map: %1. %2. Dimensions: %3. "
            "Briefly in %4: what does this map control, how does calibration affect the engine, "
            "and what are complementary maps? Under 80 words, practical.")
            .arg(m_map.name,
                 m_map.description.isEmpty() ? "" : "Description: " + m_map.description,
                 info, langName);

        // ── Dialog ──────────────────────────────────────────────────────
        auto *dlg = new QDialog(this);
        dlg->setWindowTitle(QString::fromUtf8("\xe2\x9c\xa8 AI \xe2\x80\x94 ") + m_map.name);
        dlg->setMinimumSize(420, 120);
        dlg->setFixedWidth(520);
        dlg->setStyleSheet(
            "QDialog { background:#0d1117; }"
            "QLabel  { background:transparent; border:none; }");

        auto *lay = new QVBoxLayout(dlg);
        lay->setContentsMargins(16, 14, 16, 14);
        lay->setSpacing(8);

        // Full map title: name + description + type + dims + axes + unit
        QString fullTitle = windowTitle(); // e.g. "KFMIOP — Kennfeld optimales... · MAP 16×14 ..."
        auto *hdr = new QLabel(
            QString("<span style='color:#a78bfa; font-size:10pt; font-weight:bold;'>")
            + QString::fromUtf8("\xe2\x9c\xa8 ")
            + fullTitle.toHtmlEscaped() + "</span>", dlg);
        hdr->setWordWrap(true);
        lay->addWidget(hdr);

        auto *sep = new QFrame(dlg);
        sep->setFrameShape(QFrame::HLine);
        sep->setStyleSheet("background:#21262d; max-height:1px; border:none;");
        lay->addWidget(sep);

        auto *statusLabel = new QLabel(dlg);
        statusLabel->setStyleSheet("color:#8b949e; font-size:8pt;");
        lay->addWidget(statusLabel);

        auto *textLabel = new QLabel(dlg);
        textLabel->setWordWrap(true);
        textLabel->setTextFormat(Qt::PlainText);
        textLabel->setStyleSheet("color:#c9d1d9; font-size:9pt; padding:4px 0;");
        textLabel->setMinimumHeight(60);
        lay->addWidget(textLabel, 1);

        QStringList phrases = {
            tr("Analyzing map structure"),
            tr("Reading ECU parameters"),
            tr("Consulting calibration database"),
            tr("Cross-referencing complementary maps"),
            tr("Generating explanation"),
            tr("Almost there"),
        };

        auto *animTimer = new QTimer(dlg);
        auto phase = std::make_shared<int>(0);
        auto pidx = std::make_shared<int>(0);
        animTimer->start(350);
        connect(animTimer, &QTimer::timeout, dlg, [statusLabel, phrases, phase, pidx]() {
            *phase = (*phase + 1) % 4;
            if (*phase == 0) *pidx = (*pidx + 1) % phrases.size();
            QString dots = QString(".").repeated(*phase + 1);
            QString orb = (*phase % 2 == 0)
                ? QString::fromUtf8("\xe2\x9c\xa6 ") : QString::fromUtf8("\xe2\x97\x87 ");
            statusLabel->setText(orb + phrases[*pidx] + dots);
        });
        statusLabel->setText(QString::fromUtf8("\xe2\x9c\xa6 ") + phrases[0] + "...");
        dlg->show();

        // ── Use streaming provider (handles auth correctly) ─────────────
        if (aiProv->providerName().contains("Claude")) {
            aiProv->setModel("claude-haiku-4-5-20251001");
        }

        auto state = std::make_shared<bool>(false); // done flag

        QVector<AIMessage> msgs;
        AIMessage msg;
        msg.role = AIMessage::User;
        msg.content = prompt;
        msgs.append(msg);

        auto accum = std::make_shared<QString>();

        aiProv->send(QString(), msgs, {},
            // onChunk
            [accum, textLabel, animTimer, statusLabel, dlg, this](const QString &text) {
                if (accum->isEmpty()) {
                    animTimer->stop();
                    statusLabel->setText(QString::fromUtf8("\xe2\x9c\xa6 ") + tr("Streaming..."));
                }
                *accum += text;
                textLabel->setText(*accum + QString::fromUtf8(" \xe2\x96\x8c"));
                dlg->adjustSize();
            },
            // onReasoning
            [](const QString &) {},
            // onToolCall
            [](const QString &, const QString &, const QJsonObject &) {},
            // onDone
            [this, state, accum, textLabel, statusLabel, animTimer, aiProv, dlg]() {
                if (*state) return;
                *state = true;
                animTimer->stop();
                QString result = accum->trimmed();
                textLabel->setText(result);
                statusLabel->setText(QString::fromUtf8("\xe2\x9c\x85 ") + tr("Done"));
                dlg->adjustSize();
                if (!result.isEmpty()) m_aiCache[m_map.name] = result;
                m_btnAIExplain->setEnabled(true);
                m_btnAIExplain->setText("AI");
                aiProv->deleteLater();
            },
            // onError
            [this, state, accum, textLabel, statusLabel, animTimer, aiProv, dlg](const QString &err) {
                if (*state) return;
                *state = true;
                animTimer->stop();
                if (accum->isEmpty())
                    textLabel->setText(tr("AI error: %1\n\nPlease try again.").arg(err));
                else
                    textLabel->setText(accum->trimmed());
                statusLabel->setText(QString::fromUtf8("\xe2\x9a\xa0 ") + tr("Error"));
                dlg->adjustSize();
                m_btnAIExplain->setEnabled(true);
                m_btnAIExplain->setText("AI");
                aiProv->deleteLater();
            });

        connect(dlg, &QDialog::finished, this, [this, state, animTimer, aiProv]() {
            if (!*state) {
                *state = true;
                animTimer->stop();
                aiProv->abort();
                aiProv->deleteLater();
            }
            m_btnAIExplain->setEnabled(true);
            m_btnAIExplain->setText("AI");
        });
    });

    mkSep();

    m_fontSpin = new QSpinBox();
    m_fontSpin->setRange(7, 18);
    m_fontSpin->setValue(m_fontSize);
    m_fontSpin->setSuffix("pt");
    m_fontSpin->setFixedSize(52, 24);
    m_fontSpin->setToolTip(tr("Font size"));
    m_fontSpin->setStyleSheet(
        "QSpinBox { background:#161b22; color:#c9d1d9; border:1px solid #30363d;"
        " border-radius:3px; font-size:8pt; padding-left:3px; }"
        "QSpinBox:hover { border-color:#58a6ff; }"
        "QSpinBox::up-button, QSpinBox::down-button { width:14px; border:none; background:#1c2230; }"
        "QSpinBox::up-button:hover, QSpinBox::down-button:hover { background:#1f6feb; }"
        "QSpinBox::up-arrow   { image:none; border-left:4px solid transparent;"
        " border-right:4px solid transparent; border-bottom:5px solid #c9d1d9; }"
        "QSpinBox::down-arrow { image:none; border-left:4px solid transparent;"
        " border-right:4px solid transparent; border-top:5px solid #c9d1d9; }");
    toolbar->addWidget(m_fontSpin);

    root->addLayout(toolbar);

    // ── Operation bar ─────────────────────────────────────────────────────────
    m_opBarWidget = new QWidget();
    auto *opRow = new QHBoxLayout(m_opBarWidget);
    opRow->setContentsMargins(0, 0, 0, 0);
    opRow->setSpacing(3);

    auto *deltaLbl = new QLabel(tr("Δ  "));
    deltaLbl->setStyleSheet("color:#8b949e; font-size:8pt;");
    opRow->addWidget(deltaLbl);

    m_deltaInput = new QLineEdit();
    m_deltaInput->setFixedWidth(80);
    m_deltaInput->setPlaceholderText(tr("value"));
    m_deltaInput->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_deltaInput->setStyleSheet(
        "QLineEdit { background:#0d1117; color:#e6edf3;"
        " border:1px solid #30363d; border-radius:3px;"
        " padding:1px 6px; font-family:Consolas; font-size:9pt; }"
        "QLineEdit:focus { border-color:#58a6ff; }");
    opRow->addWidget(m_deltaInput);
    opRow->addSpacing(2);

    m_btnAddPct = mkBtn(4, QColor("#3fb950"),
        tr("Add %  —  new = old × (1 + Δ÷100)\n"
           "Applies to all selected cells"));
    m_btnAddVal = mkBtn(5, QColor("#58a6ff"),
        tr("Add value  —  new = old + Δ\n"
           "Applies to all selected cells"));
    m_btnSetVal = mkBtn(6, QColor("#d2a8ff"),
        tr("Set value  —  new = Δ\n"
           "Applies to all selected cells\n"
           "Tip: select cells and press Enter to type directly"));

    // Give the op buttons a text label for discoverability
    for (auto *b : {m_btnAddPct, m_btnAddVal, m_btnSetVal}) {
        b->setFixedSize(32, 26);
    }

    // Interpolate — bilinear fill of the selected block from its corners
    m_btnInterp = new QToolButton();
    m_btnInterp->setText(tr("Interp"));
    m_btnInterp->setFixedHeight(26);
    m_btnInterp->setToolTip(
        tr("Interpolate the selected cells\n"
           "Fills a block bilinearly from its four corners; a single row or\n"
           "column is filled linearly between its ends. Select at least 3 cells."));
    m_btnInterp->setStyleSheet(
        "QToolButton{background:transparent;color:" + AppConfig::instance().colors.uiTextDim.name() + ";"
        "border:1px solid " + AppConfig::instance().colors.uiBorder.name() + ";border-radius:4px;"
        "padding:2px 8px;font-size:8pt;font-weight:bold}"
        "QToolButton:hover{background:#1c2128;color:" + AppConfig::instance().colors.uiText.name() + ";border-color:#484f58}"
        "QToolButton:disabled{color:#3a3f47;border-color:#22262c}");

    opRow->addWidget(m_btnAddPct);
    opRow->addWidget(m_btnAddVal);
    opRow->addWidget(m_btnSetVal);
    opRow->addSpacing(6);
    opRow->addWidget(m_btnInterp);
    opRow->addStretch(1);

    m_opHint = new QLabel(
        tr("Select cells  ·  Enter to edit directly  ·  Shift+click / Ctrl+click for multi-select"));
    m_opHint->setStyleSheet("color:#484f58; font-size:7.5pt; font-style:italic;");
    opRow->addWidget(m_opHint);

    root->addWidget(m_opBarWidget);

    // ── Axis info bar ─────────────────────────────────────────────────────────
    m_axisBar = new QLabel();
    m_axisBar->setAlignment(Qt::AlignCenter);
    m_axisBar->setStyleSheet(
        "color:#70a8d8; font-size:9pt; font-weight:600; letter-spacing:1px;"
        "background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "  stop:0 #060a10, stop:0.3 #0c1422, stop:0.5 #101c30,"
        "  stop:0.7 #0c1422, stop:1 #060a10);"
        "border:none; border-top:1px solid rgba(88,166,255,0.12);"
        "border-bottom:1px solid rgba(88,166,255,0.12); padding:6px 16px;"
        "min-height:20px;");
    root->addWidget(m_axisBar);

    // ── Table / 3D stack ──────────────────────────────────────────────────────
    m_stack = new QStackedWidget();
    m_stack->setMinimumHeight(120);

    m_table = new QTableWidget();
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(false);
    m_table->setShowGrid(false);
    m_table->setTabKeyNavigation(false);  // we handle Tab ourselves
    // Apply axis header colors from config
    {
        m_table->setStyleSheet(
            "QTableWidget { background:#060a10; border:none; outline:none; gridline-color:transparent; }"

            // X-axis headers — deep blue glass with bright text
            "QHeaderView::section:horizontal {"
            "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "    stop:0 #0f1a2e, stop:0.4 #0d1525, stop:1 #080e18);"
            "  color:#60b0f0; border:none;"
            "  border-right:1px solid rgba(96,176,240,0.12);"
            "  border-bottom:2px solid rgba(96,176,240,0.2);"
            "  padding:6px 4px; font-size:8pt; font-weight:700;"
            "  font-family:'Consolas','Courier New',monospace; }"
            "QHeaderView::section:horizontal:hover {"
            "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "    stop:0 #152540, stop:0.4 #112030, stop:1 #0c1520);"
            "  color:#90d0ff; }"

            // Y-axis headers — subtle warm tint
            "QHeaderView::section:vertical {"
            "  background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
            "    stop:0 #0f1a2e, stop:0.4 #0d1525, stop:1 #080e18);"
            "  color:#c0d8f0; border:none;"
            "  border-right:2px solid rgba(96,176,240,0.2);"
            "  border-bottom:1px solid rgba(96,176,240,0.08);"
            "  padding:4px 10px; font-size:8.5pt; font-weight:600;"
            "  font-family:'Consolas','Courier New',monospace; }"
            "QHeaderView::section:vertical:hover {"
            "  background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
            "    stop:0 #152540, stop:0.4 #112030, stop:1 #0c1520);"
            "  color:#ffffff; }"

            "QTableWidget::item { border:none; padding:0; }"
            "QTableCornerButton::section {"
            "  background:#080e18; border:none;"
            "  border-right:2px solid rgba(96,176,240,0.2);"
            "  border-bottom:2px solid rgba(96,176,240,0.2); }");
    }
    m_table->horizontalHeader()->setDefaultSectionSize(72);
    m_table->horizontalHeader()->setMinimumSectionSize(52);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->verticalHeader()->setDefaultSectionSize(32);
    m_table->verticalHeader()->setMinimumSectionSize(24);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_table->setFont(QFont("Consolas", m_fontSize));
    auto *cellDel = new MapCellDelegate(m_table);
    const auto &ac = AppConfig::instance().colors;
    cellDel->cellBg       = ac.mapCellBg;
    cellDel->cellText     = ac.mapCellText;
    cellDel->cellModified = ac.mapCellModified;
    cellDel->gridLine     = ac.mapGridLine;
    m_table->setItemDelegate(cellDel);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu menu(this);
        menu.setStyleSheet(
            "QMenu { background:#1c2128; color:#e6edf3; border:1px solid #30363d; }"
            "QMenu::item:selected { background:#2d333b; }"
            "QMenu::separator { height:1px; background:#30363d; margin:4px 8px; }");

        auto *actProp   = menu.addAction(tr("Properties…"));
        menu.addSeparator();
        auto *actCopy   = menu.addAction(tr("Copy selection"));
        auto *actPaste  = menu.addAction(tr("Paste values"));
        menu.addSeparator();
        auto *actCsv    = menu.addAction(tr("Export to CSV…"));

        QAction *chosen = menu.exec(m_table->viewport()->mapToGlobal(pos));
        if (!chosen) return;

        if (chosen == actProp) {
            MapInfo savedMap = m_map;  // snapshot for cancel
            ByteOrder savedBO = m_byteOrder;
            MapPropertiesDialog dlg(m_map, m_byteOrder, this);
            connect(&dlg, &MapPropertiesDialog::previewChanged,
                    this, [this](const MapInfo &preview) {
                previewMapUpdate(preview);
                emit mapInfoPreview(preview);
            });
            if (dlg.exec() == QDialog::Accepted) {
                m_map       = dlg.result();
                m_byteOrder = dlg.byteOrder();
                emit mapInfoChanged(m_map, m_byteOrder);
                previewMapUpdate(m_map);
            } else {
                m_map       = savedMap;
                m_byteOrder = savedBO;
                emit mapInfoPreview(savedMap);
                previewMapUpdate(savedMap);
            }
        } else if (chosen == actCopy) {
            copySelectionToClipboard();
        } else if (chosen == actPaste) {
            pasteFromClipboard();
        } else if (chosen == actCsv) {
            QString path = QFileDialog::getSaveFileName(this, tr("Export CSV"),
                m_map.name + ".csv", "CSV files (*.csv)");
            if (path.isEmpty()) return;
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
            QTextStream ts(&f);
            int dataCols = m_map.dimensions.x;
            int dataRows = qMax(1, m_map.dimensions.y);
            // Header row: empty corner + X axis labels
            QStringList hdr;
            hdr << m_map.name;
            for (int c = 0; c < dataCols; ++c) {
                auto *hi = m_table->horizontalHeaderItem(c);
                hdr << (hi ? hi->text() : QString::number(c));
            }
            ts << hdr.join(',') << '\n';
            // Data rows: Y axis label + values
            for (int r = 0; r < dataRows; ++r) {
                QStringList row;
                auto *vi = m_table->verticalHeaderItem(r);
                row << (vi ? vi->text() : QString::number(r));
                for (int c = 0; c < dataCols; ++c) {
                    auto *item = m_table->item(r, c);
                    row << (item ? item->text() : "");
                }
                ts << row.join(',') << '\n';
            }
        }
    });

    m_map3d = new Map3DWidget();
    // The embedded 3D "Edit map" submenu was previously inert here — re-emit
    // its request so the host routes it through the shared edit dispatcher
    // (same path/undo stack as the main window's 3D view).
    connect(m_map3d, &Map3DWidget::editOpRequested,
            this, &MapOverlay::editOpRequested);

    m_map3dSim = new Map3DSimWidget();

    m_stack->addWidget(m_table);    // 0 = table
    m_stack->addWidget(m_map3d);    // 1 = 3D view
    m_stack->addWidget(m_map3dSim); // 2 = 3D simulation
    root->addWidget(m_stack, 1);

    // ── Low-confidence warning bar ────────────────────────────────────────────
    m_warningBar = new QFrame(this);
    m_warningBar->setStyleSheet(
        "QFrame { background:#3d1f00; border-top:2px solid #f0883e; border-bottom:1px solid #7d3b00; }"
        "QLabel { color:#ffa657; font-size:8.5pt; font-weight:bold; background:transparent; border:none; }"
        "QPushButton { color:#f0883e; background:#1a0a00; border:1px solid #7d3b00;"
        "  border-radius:3px; padding:2px 10px; font-size:8pt; }"
        "QPushButton:hover { background:#2a1200; border-color:#f0883e; }");
    auto *warnRow = new QHBoxLayout(m_warningBar);
    warnRow->setContentsMargins(8, 4, 8, 4);
    auto *warnIcon = new QLabel("\u26a0\ufe0f");
    warnIcon->setStyleSheet("font-size:12pt; background:transparent; border:none;");
    m_warningLabel = new QLabel();
    m_warningLabel->setWordWrap(false);
    auto *btnFix = new QPushButton(tr("Fix address…"));
    warnRow->addWidget(warnIcon);
    warnRow->addWidget(m_warningLabel, 1);
    warnRow->addWidget(btnFix);
    m_warningBar->hide();
    root->addWidget(m_warningBar);

    connect(btnFix, &QPushButton::clicked, this, [this]() {
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Set Correct Address — %1").arg(m_map.name));
        dlg.setMinimumWidth(360);
        auto *lay = new QVBoxLayout(&dlg);

        auto *info = new QLabel(
            tr("<b>%1</b><br>"
               "RomHEX 14 could not verify this map's location in the linked ROM.<br>"
               "Enter the correct hex address from a trusted source (e.g. DAMOS file, EEPROM editor).")
                .arg(m_map.name));
        info->setWordWrap(true);
        lay->addWidget(info);

        auto *addrEdit = new QLineEdit(
            QString("0x%1").arg(m_map.address, 8, 16, QChar('0')).toUpper());
        addrEdit->setPlaceholderText("0x00000000");
        addrEdit->setFont(QFont("monospace"));
        lay->addWidget(addrEdit);

        auto *btns = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        lay->addWidget(btns);
        connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

        if (dlg.exec() != QDialog::Accepted) return;

        bool ok = false;
        QString txt = addrEdit->text().trimmed();
        uint32_t addr = txt.toUInt(&ok, 0);
        if (!ok) {
            QMessageBox::warning(this, tr("Invalid address"), tr("Could not parse address."));
            return;
        }
        m_map.address = addr;
        m_map.linkConfidence = 95;   // user-confirmed
        m_warningBar->hide();
        buildTable();
        emit addressCorrected(m_map.name, addr);
    });

    // ── Status bar ────────────────────────────────────────────────────────────
    m_statusBar = new QLabel(tr("No map loaded"));
    m_statusBar->setStyleSheet(
        "color:#506882; font-size:7.5pt; padding:3px 8px;"
        "background:#060a10; border-top:1px solid rgba(88,166,255,0.1);");
    root->addWidget(m_statusBar);

    // ── Inline editor (created once, hidden until needed) ─────────────────────
    m_inlineEdit = new QLineEdit(m_table->viewport());
    m_inlineEdit->setAlignment(Qt::AlignCenter);
    m_inlineEdit->setStyleSheet(
        "QLineEdit {"
        "  background:#1a2540;"
        "  color:#f0f6fc;"
        "  border:2px solid #1f6feb;"
        "  border-radius:2px;"
        "  font-family:Consolas; font-size:10pt; font-weight:bold;"
        "  selection-background-color:#1f6feb;"
        "  padding:0;"
        "}");
    m_inlineEdit->hide();
    m_inlineEdit->installEventFilter(this);

    // ── Connections ───────────────────────────────────────────────────────────
    connect(m_fontSpin, &QSpinBox::valueChanged, this, [this](int pt) {
        m_fontSize = pt;
        m_table->setFont(QFont("Consolas", pt));
        m_table->verticalHeader()->setDefaultSectionSize(pt + 16);
        m_table->horizontalHeader()->setDefaultSectionSize(qMax(44, pt * 6));
        if (m_editOpen) repositionInlineEditor();
    });

    connect(m_cellSizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        m_cellSize = m_cellSizeCombo->itemData(idx).toInt();
        m_map.dataSize = m_cellSize;
        cancelInlineEditor();
        buildTable();
        if (m_btn3D->isChecked()) m_map3d->showMap(m_data, m_map);
    });

    connect(m_byteOrderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        m_byteOrder = ByteOrder(m_byteOrderCombo->itemData(idx).toInt());
        cancelInlineEditor();
        buildTable();
    });

    connect(m_btn3D, &QToolButton::toggled, this, [this](bool on) {
        cancelInlineEditor();
        if (on) {
            m_btn3DSim->blockSignals(true);
            m_btn3DSim->setChecked(false);
            m_btn3DSim->blockSignals(false);
            m_stack->setCurrentIndex(1);
            m_map3d->showMap(m_data, m_map);
        } else {
            m_stack->setCurrentIndex(0);
        }
    });

    connect(m_btn3DSim, &QToolButton::toggled, this, [this](bool on) {
        cancelInlineEditor();
        if (on) {
            m_btn3D->blockSignals(true);
            m_btn3D->setChecked(false);
            m_btn3D->blockSignals(false);
            m_stack->setCurrentIndex(2);
            m_map3dSim->showMap(m_data, m_map, m_byteOrder);
        } else {
            m_stack->setCurrentIndex(0);
        }
    });

    connect(m_btnOri, &QToolButton::toggled, this, [this](bool on) {
        m_showingOriginal = on;
        // "Show original" and the difference views are mutually exclusive.
        if (on && m_displayMode != DisplayMode::Values) {
            m_displayMode = DisplayMode::Values;
            m_diffCombo->blockSignals(true);
            m_diffCombo->setCurrentIndex(0);
            m_diffCombo->blockSignals(false);
        }
        cancelInlineEditor();
        buildTable();
    });

    connect(m_diffCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        m_displayMode = static_cast<DisplayMode>(qBound(0, idx, 2));
        // Leaving plain-values mode turns off the read-only "original" view.
        if (m_displayMode != DisplayMode::Values && m_showingOriginal) {
            m_showingOriginal = false;
            m_btnOri->blockSignals(true);
            m_btnOri->setChecked(false);
            m_btnOri->blockSignals(false);
        }
        cancelInlineEditor();
        buildTable();
    });

    connect(m_btnHeat, &QToolButton::toggled, this, [this](bool on) {
        m_heatEnabled = on;
        if (auto *d = static_cast<MapCellDelegate*>(m_table->itemDelegate()))
            d->heatEnabled = on;
        buildTable();
    });

    connect(m_btnUndo, &QToolButton::clicked, this, &MapOverlay::undoEdit);
    connect(m_btnRedo, &QToolButton::clicked, this, &MapOverlay::redoEdit);

    connect(m_btnAddPct, &QToolButton::clicked, this, [this]{ applyBatchOp(0); });
    connect(m_btnAddVal, &QToolButton::clicked, this, [this]{ applyBatchOp(1); });
    connect(m_btnSetVal, &QToolButton::clicked, this, [this]{ applyBatchOp(2); });
    connect(m_btnInterp, &QToolButton::clicked, this, &MapOverlay::interpolateSelection);

    // Close inline editor if user scrolls (simple and reliable)
    connect(m_table->horizontalScrollBar(), &QScrollBar::valueChanged,
            this, [this]{ cancelInlineEditor(); });
    connect(m_table->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this]{ cancelInlineEditor(); });

    // Update status bar on selection change
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this]{ updateStatusBar(); });

    // Install event filter on table and its viewport for key interception
    m_table->installEventFilter(this);
    m_table->viewport()->installEventFilter(this);

    // Keyboard shortcuts
    auto *undoSc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z), this);
    connect(undoSc, &QShortcut::activated, this, &MapOverlay::undoEdit);
    auto *redoSc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Y), this);
    connect(redoSc, &QShortcut::activated, this, &MapOverlay::redoEdit);
}

// ═══════════════════════════════════════════════════════════════════════════════
// showMap
// ═══════════════════════════════════════════════════════════════════════════════
void MapOverlay::showMap(const QByteArray &romData, const MapInfo &map,
                         ByteOrder byteOrder, const QByteArray &originalData)
{
    const bool mapChanged = (m_map.name != map.name || m_map.address != map.address);

    cancelInlineEditor();
    m_data = romData;
    m_map  = map;

    if (!originalData.isEmpty())
        m_originalData = originalData;
    else if (m_originalData.isEmpty())
        m_originalData = romData;

    if (mapChanged) {
        m_undoStack.clear();
        m_redoStack.clear();
        updateUndoRedoButtons();
        m_showingOriginal = false;
        m_btnOri->blockSignals(true);
        m_btnOri->setChecked(false);
        m_btnOri->blockSignals(false);
        m_displayMode = DisplayMode::Values;
        m_diffCombo->blockSignals(true);
        m_diffCombo->setCurrentIndex(0);
        m_diffCombo->blockSignals(false);
    }

    m_cellSize  = (map.dataSize > 0) ? map.dataSize : 2;
    m_byteOrder = byteOrder;

    // Window title
    QString display = map.description.isEmpty() ? map.name
                    : QString("%1  —  %2").arg(map.name, map.description);
    auto unitStr = [](const AxisInfo &ax) -> QString {
        if (ax.inputName.isEmpty() || ax.inputName == "NO_INPUT_QUANTITY") return {};
        QString s = ax.inputName;
        if (ax.hasScaling && !ax.scaling.unit.isEmpty()) s += " [" + ax.scaling.unit + "]";
        return s;
    };
    QString xs = unitStr(map.xAxis), ys = unitStr(map.yAxis);
    QString axes;
    if (!xs.isEmpty() && !ys.isEmpty()) axes = QString("  (%1,  %2)").arg(xs, ys);
    else if (!xs.isEmpty()) axes = "  (" + xs + ")";
    else if (!ys.isEmpty()) axes = "  (" + ys + ")";

    QString zUnit;
    if (map.hasScaling && map.scaling.type != CompuMethod::Type::Identical
            && !map.scaling.unit.isEmpty())
        zUnit = "  [" + map.scaling.unit + "]";

    setWindowTitle(QString("%1  ·  %2  %3×%4%5%6")
        .arg(display, map.type)
        .arg(map.dimensions.x).arg(map.dimensions.y)
        .arg(axes, zUnit));

    m_cellSizeCombo->blockSignals(true);
    m_byteOrderCombo->blockSignals(true);
    m_cellSizeCombo->setCurrentIndex(m_cellSize == 1 ? 0 : m_cellSize == 4 ? 2 : 1);
    m_byteOrderCombo->setCurrentIndex(byteOrder == ByteOrder::LittleEndian ? 1 : 0);
    m_cellSizeCombo->blockSignals(false);
    m_byteOrderCombo->blockSignals(false);

    // Warning bar — show for any linked map whose position wasn't directly verified
    const int conf = map.linkConfidence;
    if (conf > 0 && conf < 60) {
        m_warningLabel->setText(
            tr("⚠  RomHEX 14 cannot guarantee this data is correct "
               "(link confidence: %1%).  Verify before editing.").arg(conf));
        m_warningBar->show();
    } else {
        m_warningBar->hide();
    }

    // Hide non-essential UI for small maps to prevent cramming
    const bool isSmall = (map.dimensions.x <= 4 && map.dimensions.y <= 2);
    m_opHint->setVisible(!isSmall);
    m_orderLabel->setVisible(!isSmall);
    m_byteOrderCombo->setVisible(!isSmall);
    m_btn3D->setVisible(!isSmall);
    m_btnHeat->setVisible(!isSmall);

    buildTable();
    autoResize();
    if (!isVisible()) show();
    raise();
    activateWindow();
    m_table->setFocus();
}

bool MapOverlay::displaysMap(const MapInfo &map) const
{
    return m_map.name == map.name && m_map.address == map.address;
}

// ═══════════════════════════════════════════════════════════════════════════════
// setDisplayParams
// ═══════════════════════════════════════════════════════════════════════════════
void MapOverlay::setDisplayParams(int cellSize, ByteOrder byteOrder, bool /*heightColors*/)
{
    bool changed = false;
    if (cellSize != m_cellSize) {
        m_cellSize = cellSize; m_map.dataSize = cellSize;
        m_cellSizeCombo->blockSignals(true);
        m_cellSizeCombo->setCurrentIndex(cellSize==1?0:cellSize==4?2:1);
        m_cellSizeCombo->blockSignals(false);
        changed = true;
    }
    if (byteOrder != m_byteOrder) {
        m_byteOrder = byteOrder;
        m_byteOrderCombo->blockSignals(true);
        m_byteOrderCombo->setCurrentIndex(byteOrder==ByteOrder::LittleEndian?1:0);
        m_byteOrderCombo->blockSignals(false);
        changed = true;
    }
    if (changed && isVisible()) { cancelInlineEditor(); buildTable(); }
}

// ═══════════════════════════════════════════════════════════════════════════════
// retranslateUi
// ═══════════════════════════════════════════════════════════════════════════════
void MapOverlay::retranslateUi()
{
    const int ci = m_cellSizeCombo->currentIndex();
    const int oi = m_byteOrderCombo->currentIndex();
    m_cellSizeCombo->blockSignals(true);
    m_cellSizeCombo->clear();
    m_cellSizeCombo->addItem(tr("8-bit"),  1);
    m_cellSizeCombo->addItem(tr("16-bit"), 2);
    m_cellSizeCombo->addItem(tr("32-bit"), 4);
    m_cellSizeCombo->setCurrentIndex(ci);
    m_cellSizeCombo->blockSignals(false);

    m_byteOrderCombo->blockSignals(true);
    m_byteOrderCombo->clear();
    m_byteOrderCombo->addItem(tr("Big Endian"),    int(ByteOrder::BigEndian));
    m_byteOrderCombo->addItem(tr("Little Endian"), int(ByteOrder::LittleEndian));
    m_byteOrderCombo->setCurrentIndex(oi);
    m_byteOrderCombo->blockSignals(false);

    if (m_diffCombo) {
        const int di = m_diffCombo->currentIndex();
        m_diffCombo->blockSignals(true);
        m_diffCombo->clear();
        m_diffCombo->addItem(tr("Values"));
        m_diffCombo->addItem(tr("Δ Original"));
        m_diffCombo->addItem(tr("% Original"));
        m_diffCombo->setCurrentIndex(di);
        m_diffCombo->setToolTip(tr("Show plain values, or the numeric difference from the original ROM"));
        m_diffCombo->blockSignals(false);
    }
    if (m_btnInterp) {
        m_btnInterp->setText(tr("Interp"));
        m_btnInterp->setToolTip(
            tr("Interpolate the selected cells\n"
               "Fills a block bilinearly from its four corners; a single row or\n"
               "column is filled linearly between its ends. Select at least 3 cells."));
    }

    if (isVisible()) buildTable();
}

// ═══════════════════════════════════════════════════════════════════════════════
// autoResize
// ═══════════════════════════════════════════════════════════════════════════════
void MapOverlay::autoResize()
{
    // Calculate ideal table size
    int tw = m_table->verticalHeader()->width();
    for (int c = 0; c < m_table->columnCount(); ++c) tw += m_table->columnWidth(c);
    tw += m_table->verticalScrollBar()->sizeHint().width() + 4;

    int th = m_table->horizontalHeader()->height();
    for (int r = 0; r < m_table->rowCount(); ++r) th += m_table->rowHeight(r);
    th += m_table->horizontalScrollBar()->sizeHint().height() + 4;

    // Chrome overhead: toolbar (~30) + opbar (~30) + axis bar (~24) + status (~24)
    // + warning bar (~30 if visible) + margins (~30)
    const int chrome = 170;

    QScreen *scr = screen() ? screen() : QApplication::primaryScreen();
    QRect av = scr ? scr->availableGeometry() : QRect(0,0,1920,1080);
    int idealW = qMax(tw + 30, 480);  // ensure toolbar widgets fit
    int idealH = qMax(th + chrome, 320);
    resize(qMin(idealW, int(av.width()  * 0.92)),
           qMin(idealH, int(av.height() * 0.88)));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Preview updates
// ═══════════════════════════════════════════════════════════════════════════════
void MapOverlay::previewScalingFormat(const QString &format)
{
    m_map.scaling.format = format;
    buildTable();
}

void MapOverlay::previewMapUpdate(const MapInfo &preview)
{
    m_map = preview;
    setWindowTitle(tr("%1 — %2").arg(m_map.name).arg(m_map.description.isEmpty() ? m_map.type : m_map.description));
    buildTable();
}

// ═══════════════════════════════════════════════════════════════════════════════
// buildTable
// ═══════════════════════════════════════════════════════════════════════════════
void MapOverlay::buildTable()
{
    if (m_cellSize < 1 || m_cellSize > 4) {
        LOG_WARN(QString("MapOverlay::buildTable — invalid cellSize %1, resetting to 2").arg(m_cellSize));
        m_cellSize = 2;
    }

    const QByteArray &src = (m_showingOriginal && !m_originalData.isEmpty())
                            ? m_originalData : m_data;
    const auto *raw = reinterpret_cast<const uint8_t*>(src.constData());
    const int dlen  = src.size();
    const int cols  = qBound(1, m_map.dimensions.x, 256);
    const int rows  = qBound(1, m_map.dimensions.y, 256);
    // If the originating dimensions are obviously broken, or the cells fall
    // outside the ROM buffer, show an empty table rather than crashing.
    if (m_map.dimensions.x > 256 || m_map.dimensions.y > 256
        || dlen <= 0
        || static_cast<qint64>(m_map.address) + qint64(rows) * cols * m_cellSize > dlen) {
        m_table->setRowCount(0);
        m_table->setColumnCount(0);
        setWindowTitle(tr("%1 — not yet supported").arg(m_map.name));
        return;
    }
    const bool scaled = m_map.hasScaling
                        && m_map.scaling.type != CompuMethod::Type::Identical;
    const CompuMethod &cm = m_map.scaling;

    QVector<QVector<uint32_t>> rawV(rows, QVector<uint32_t>(cols, 0));
    QVector<QVector<double>>   physV(rows, QVector<double>(cols, 0.0));
    double minP = 1e300, maxP = -1e300;

    const bool colMaj = m_map.columnMajor;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            uint32_t off = m_map.address + m_map.mapDataOffset
                           + uint32_t(colMaj ? c * rows + r : r * cols + c) * m_cellSize;
            const ByteOrder cellBO = cellByteOrder(m_map, m_byteOrder);
            uint32_t v = readRomValue(raw, dlen, off, m_cellSize, cellBO);
            rawV[r][c] = v;
            double sv = m_map.dataSigned ? readRomValueAsDouble(raw, dlen, off, m_cellSize, cellBO, true) : double(v);
            double p = scaled ? cm.toPhysical(sv) : sv;
            physV[r][c] = p;
            if (p < minP) minP = p;
            if (p > maxP) maxP = p;
        }
    }

    const double range = (maxP > minP) ? (maxP - minP) : 1.0;
    const QString unit = (scaled && !cm.unit.isEmpty()) ? cm.unit : QString();

    // ── Difference view: per-cell delta (or %) vs the original ROM ──────────
    const bool diffMode = (m_displayMode != DisplayMode::Values)
                          && !m_originalData.isEmpty();
    QVector<QVector<double>> deltaV;
    double maxAbsDelta = 0.0;
    if (diffMode) {
        deltaV = QVector<QVector<double>>(rows, QVector<double>(cols, 0.0));
        const auto *oraw = reinterpret_cast<const uint8_t*>(m_originalData.constData());
        const int   olen = m_originalData.size();
        const ByteOrder cellBO = cellByteOrder(m_map, m_byteOrder);
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                uint32_t off = m_map.address + m_map.mapDataOffset
                               + uint32_t(colMaj ? c * rows + r : r * cols + c) * m_cellSize;
                uint32_t orv = readRomValue(oraw, olen, off, m_cellSize, cellBO);
                double osv = m_map.dataSigned
                             ? readRomValueAsDouble(oraw, olen, off, m_cellSize, cellBO, true)
                             : double(orv);
                double op  = scaled ? cm.toPhysical(osv) : osv;
                double d;
                if (m_displayMode == DisplayMode::DeltaPct)
                    d = (op != 0.0) ? (physV[r][c] - op) / std::fabs(op) * 100.0
                                    : (physV[r][c] == op ? 0.0
                                       : (physV[r][c] > op ? 100.0 : -100.0));
                else
                    d = physV[r][c] - op;
                deltaV[r][c] = d;
                maxAbsDelta = qMax(maxAbsDelta, std::fabs(d));
            }
        }
        if (maxAbsDelta <= 0.0) maxAbsDelta = 1.0;
    }

    // Stats label
    auto fmtN = [&](double v) { return scaled ? cm.formatValue(v) : QString::number(uint32_t(v)); };
    m_statsLabel->setText(
        tr("Rows: %1  Cols: %2  Min: %3  Max: %4  Range: %5%6")
            .arg(rows).arg(cols)
            .arg(fmtN(minP)).arg(fmtN(maxP)).arg(fmtN(maxP - minP))
            .arg(unit.isEmpty() ? QString() : "  " + unit));

    // Axis bar
    auto fmtAx = [](const AxisInfo &ax) -> QString {
        if (ax.inputName.isEmpty() || ax.inputName == "NO_INPUT_QUANTITY") return {};
        QString s = ax.inputName;
        if (ax.hasScaling && !ax.scaling.unit.isEmpty()) s += "  [" + ax.scaling.unit + "]";
        return s;
    };
    QString xU = fmtAx(m_map.xAxis), yU = fmtAx(m_map.yAxis);
    QString axisText;
    if (!xU.isEmpty()) axisText += "X:  " + xU;
    if (!xU.isEmpty() && !yU.isEmpty()) axisText += "        ";
    if (!yU.isEmpty()) axisText += "Y:  " + yU;
    if (!unit.isEmpty()) { if (!axisText.isEmpty()) axisText += "        "; axisText += "Z:  [" + unit + "]"; }
    if (m_showingOriginal) axisText += (axisText.isEmpty() ? "" : "     ·     ") + tr("ORIGINAL  (read-only)");
    m_axisBar->setText(axisText);
    m_axisBar->setVisible(!axisText.isEmpty());

    // Axis tick helper
    auto axLabel = [&](const AxisInfo &ax, int idx) -> QString {
        double rv = idx;
        bool have = false;
        if (!ax.fixedValues.isEmpty() && idx < ax.fixedValues.size()) {
            rv = ax.fixedValues[idx]; have = true;
        } else if (ax.hasPtsAddress) {
            uint32_t off = ax.ptsAddress + uint32_t(idx) * ax.ptsDataSize;
            if (off + ax.ptsDataSize <= uint32_t(dlen)) {
                rv = readRomValueAsDouble(raw, dlen, off, ax.ptsDataSize, axisByteOrder(ax, m_byteOrder), ax.ptsSigned);
                have = true;
            }
        }
        if (!have) return QString::number(idx);
        double phys = (ax.hasScaling && ax.scaling.type != CompuMethod::Type::Identical)
                      ? ax.scaling.toPhysical(rv) : rv;
        return ax.hasScaling ? ax.scaling.formatValue(phys) : QString::number(int(phys));
    };

    const auto *origRaw = reinterpret_cast<const uint8_t*>(m_originalData.constData());

    m_table->setUpdatesEnabled(false);
    m_table->clearContents();
    m_table->setRowCount(rows);
    m_table->setColumnCount(cols + 1);  // +1 right heat bar column

    for (int c = 0; c < cols; ++c)
        m_table->setHorizontalHeaderItem(c, new QTableWidgetItem(axLabel(m_map.xAxis, c)));
    m_table->setHorizontalHeaderItem(cols, new QTableWidgetItem(""));

    for (int r = 0; r < rows; ++r)
        m_table->setVerticalHeaderItem(r, new QTableWidgetItem(axLabel(m_map.yAxis, r)));

    for (int r = 0; r < rows; ++r) {
        double rowPct = 0;
        for (int c = 0; c < cols; ++c) {
            const uint32_t rv  = rawV[r][c];
            const double   pv  = physV[r][c];
            const double   pct = qBound(0.0, (pv - minP) / range, 1.0);
            rowPct += pct;

            bool modified = false;
            if (!m_showingOriginal && !m_originalData.isEmpty()) {
                uint32_t origOff = m_map.address + m_map.mapDataOffset
                                   + uint32_t(colMaj ? c * rows + r : r * cols + c) * m_cellSize;
                uint32_t origV = readRomValue(origRaw, m_originalData.size(),
                                              origOff, m_cellSize, cellByteOrder(m_map, m_byteOrder));
                modified = (origV != rv);
            }

            QColor bg, fg;
            QString display, fullVal;

            if (diffMode) {
                // Cell shows the signed difference from the original ROM;
                // colored green (increase) / red (decrease), intensity by
                // magnitude relative to the largest change in the map.
                const double d = deltaV[r][c];
                if (m_displayMode == DisplayMode::DeltaPct)
                    display = QString("%1%2%").arg(d > 0 ? "+" : "").arg(d, 0, 'f', 1);
                else if (scaled)
                    display = (d > 0 ? "+" : d < 0 ? "-" : "") + cm.formatValue(std::fabs(d));
                else
                    display = (d > 0 ? "+" : "") + QString::number((qlonglong)std::llround(d));
                fullVal = display;

                const double inten = qBound(0.18, std::fabs(d) / maxAbsDelta, 1.0);
                if (std::fabs(d) < 1e-9)
                    bg = QColor(0x16, 0x1a, 0x20);           // unchanged — neutral
                else if (d > 0)
                    bg = QColor(int(28 + 34 * inten), int(96 + 120 * inten), int(46 + 44 * inten));
                else
                    bg = QColor(int(130 + 110 * inten), int(44 + 28 * inten), int(46 + 26 * inten));
                fg = QColor(0xf5, 0xf5, 0xf5);
            } else {
                bg = heatColor(pct);
                const int luma = bg.red()*299 + bg.green()*587 + bg.blue()*114;
                fg = (luma > 100000) ? QColor(0x0d,0x0d,0x0d) : QColor(0xf5,0xf5,0xf5);
                display = scaled ? cm.formatValue(pv) : QString::number(rv);
                fullVal = scaled ? (cm.formatValue(pv) + (unit.isEmpty()?" ":" "+unit))
                                 : QString::number(rv);
            }

            auto *item = new QTableWidgetItem(display);
            item->setBackground(bg);
            item->setForeground(fg);
            item->setTextAlignment(Qt::AlignCenter);
            item->setData(Qt::UserRole,     pct);
            item->setData(Qt::UserRole + 1, modified);
            item->setData(Qt::UserRole + 2, false);   // pending flag — set by inline editor
            item->setToolTip(
                tr("[row %1, col %2]  =  %3  (raw 0x%4)")
                    .arg(r).arg(c).arg(fullVal)
                    .arg(rv, 0, 16).toUpper());
            m_table->setItem(r, c, item);
        }

        // Right-side row bar visualization (OLS style)
        double rowAvgPct = rowPct / qMax(1, cols);
        auto *bar = new QTableWidgetItem();
        bar->setBackground(QColor(0x0d, 0x11, 0x17)); // dark background
        bar->setData(Qt::UserRole, rowAvgPct);
        bar->setFlags(bar->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsEditable));
        m_table->setItem(r, cols, bar);
    }

    // Column widths
    m_table->resizeColumnsToContents();
    const bool hasX = m_map.xAxis.hasValues();
    const int minW = hasX ? 52 : 40, maxW = hasX ? 88 : 76;
    for (int c = 0; c < cols; ++c)
        m_table->setColumnWidth(c, qBound(minW, m_table->columnWidth(c), maxW));
    // Bar column: proportional width (~40-60px)
    m_table->setColumnWidth(cols, 48);
    m_table->horizontalHeader()->setSectionResizeMode(cols, QHeaderView::Fixed);
    m_table->setItemDelegateForColumn(cols, new RowBarDelegate(m_table));
    m_table->verticalHeader()->setMinimumWidth(m_map.yAxis.hasValues() ? 64 : 36);

    // In a difference view the per-cell sign tints live in BackgroundRole,
    // which the delegate only paints when heat coloring is on — so force it
    // on and lock the Heat toggle while the diff view is active.
    if (auto *d = static_cast<MapCellDelegate*>(m_table->itemDelegate()))
        d->heatEnabled = m_heatEnabled || diffMode;
    if (m_btnHeat) m_btnHeat->setEnabled(!diffMode);

    m_table->setUpdatesEnabled(true);

    if (m_btn3D->isChecked()) m_map3d->showMap(m_data, m_map);
    updateStatusBar();
}

// ═══════════════════════════════════════════════════════════════════════════════
// writeCell — write one physical value, return CellEdit for undo
// ═══════════════════════════════════════════════════════════════════════════════
CellEdit MapOverlay::writeCell(int row, int col, double newPhys)
{
    const bool scaled = m_map.hasScaling
                        && m_map.scaling.type != CompuMethod::Type::Identical;
    const CompuMethod &cm = m_map.scaling;

    double rawD = scaled ? cm.toRaw(newPhys) : newPhys;
    uint32_t newRaw;
    if (m_map.dataSigned) {
        // Signed: clamp to signed range, then cast to unsigned for storage
        double minS, maxS;
        if (m_cellSize == 1)      { minS = -128.0;       maxS = 127.0; }
        else if (m_cellSize == 4) { minS = -2147483648.0; maxS = 2147483647.0; }
        else                      { minS = -32768.0;     maxS = 32767.0; }
        int32_t sv = (int32_t)qBound(minS, std::round(rawD), maxS);
        newRaw = (uint32_t)sv; // two's complement
    } else {
        const double maxRaw = (m_cellSize == 1) ? 255.0
                            : (m_cellSize == 4) ? 4294967295.0 : 65535.0;
        newRaw = uint32_t(qBound(0.0, std::round(rawD), maxRaw));
    }

    return writeRawCell(row, col, newRaw);
}

CellEdit MapOverlay::writeRawCell(int row, int col, uint32_t newRaw)
{
    const int cols = qMax(1, m_map.dimensions.x);
    const int rows = qMax(1, m_map.dimensions.y);

    const uint32_t offset = m_map.address + m_map.mapDataOffset
                            + uint32_t(m_map.columnMajor ? col * rows + row : row * cols + col) * m_cellSize;
    const ByteOrder cellBO = cellByteOrder(m_map, m_byteOrder);
    const auto *src = reinterpret_cast<const uint8_t*>(m_data.constData());
    const uint32_t oldRaw = readRomValue(src, m_data.size(), offset, m_cellSize, cellBO);

    auto *dst = reinterpret_cast<uint8_t*>(m_data.data());
    writeRomValue(dst, m_data.size(), offset, m_cellSize, cellBO, newRaw);

    // A project-backed overlay submits the complete batch through Project.
    // Standalone overlays retain their local patch signal for legacy callers.
    if (!m_syncProject) {
        QByteArray patch(m_cellSize, '\0');
        writeRomValue(reinterpret_cast<uint8_t*>(patch.data()), m_cellSize,
                      0, m_cellSize, cellBO, newRaw);
        emit romPatchReady(offset, patch);
    }

    return {offset, oldRaw, newRaw};
}

// ═══════════════════════════════════════════════════════════════════════════════
// Shared-undo wiring
// ═══════════════════════════════════════════════════════════════════════════════
void MapOverlay::setProject(Project *project)
{
    m_syncProject = project;
    if (m_syncProject) {
        connect(m_syncProject, &Project::romPatched,
                this, [this](int, int) { reloadFromProject(); });
        connect(m_syncProject, &Project::romUndoStateChanged,
                this, &MapOverlay::updateUndoRedoButtons);
        connect(m_syncProject, &QObject::destroyed, this, [this]{
            m_syncProject = nullptr;
            updateUndoRedoButtons();
        });
    }
    updateUndoRedoButtons();
}

void MapOverlay::commitBatch(const EditBatch &batch)
{
    if (batch.isEmpty()) return;

    if (m_syncProject) {
        QVector<QPair<int, QByteArray>> patches;
        for (const auto &e : batch) {
            if (int(e.offset + uint32_t(m_cellSize)) <= m_data.size())
                patches.append({int(e.offset), m_data.mid(int(e.offset), m_cellSize)});
        }
        m_syncProject->applyRomPatches(patches, tr("Map edit"));
    } else {
        pushUndo(batch);
    }
}

void MapOverlay::reloadFromProject()
{
    if (!m_syncProject) return;
    if (m_syncProject->currentData.isEmpty()) return;
    m_data = m_syncProject->currentData;
    cancelInlineEditor();
    rebuildTablePreservingGridState();
    updateUndoRedoButtons();
}

void MapOverlay::rebuildTablePreservingGridState()
{
    struct Cell { int row; int column; };
    QVector<Cell> selected;
    selected.reserve(m_table->selectedItems().size());
    for (const auto *item : m_table->selectedItems())
        selected.append({item->row(), item->column()});

    const QModelIndex current = m_table->currentIndex();
    const int verticalScroll = m_table->verticalScrollBar()->value();
    const int horizontalScroll = m_table->horizontalScrollBar()->value();

    buildTable();

    QItemSelectionModel *selectionModel = m_table->selectionModel();
    if (selectionModel) {
        QSignalBlocker block(selectionModel);
        selectionModel->clearSelection();
        for (const Cell &cell : selected) {
            const QModelIndex index = m_table->model()->index(cell.row, cell.column);
            if (index.isValid())
                selectionModel->select(index, QItemSelectionModel::Select);
        }
        if (current.isValid()) {
            const QModelIndex index = m_table->model()->index(current.row(), current.column());
            if (index.isValid())
                selectionModel->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
        }
    }

    m_table->verticalScrollBar()->setValue(verticalScroll);
    m_table->horizontalScrollBar()->setValue(horizontalScroll);
    updateStatusBar();
}

// ═══════════════════════════════════════════════════════════════════════════════
// applyBatchOp — bulk operation via the Δ input
// ═══════════════════════════════════════════════════════════════════════════════
void MapOverlay::applyBatchOp(int mode)
{
    if (isReadOnlyView()) return;
    cancelInlineEditor();

    bool ok;
    double delta = m_deltaInput->text().trimmed().toDouble(&ok);
    if (!ok) {
        if (mode == 2 && m_deltaInput->text().trimmed().isEmpty()) delta = 0.0;
        else return;
    }

    const int cols = qMax(1, m_map.dimensions.x);
    const int rows = qMax(1, m_map.dimensions.y);
    const bool scaled = m_map.hasScaling
                        && m_map.scaling.type != CompuMethod::Type::Identical;
    const CompuMethod &cm = m_map.scaling;

    // Snapshot selection before writes
    struct Sel { int row, col; };
    QVector<Sel> sel;
    for (auto *it : m_table->selectedItems())
        if (it->column() < cols) sel.append({it->row(), it->column()});
    if (sel.isEmpty()) return;

    EditBatch batch;
    for (const auto &s : sel) {
        uint32_t off = m_map.address + m_map.mapDataOffset
                       + uint32_t(m_map.columnMajor ? s.col * rows + s.row : s.row * cols + s.col) * m_cellSize;
        const auto *raw = reinterpret_cast<const uint8_t*>(m_data.constData());
        const ByteOrder cellBO = cellByteOrder(m_map, m_byteOrder);
        uint32_t rv = readRomValue(raw, m_data.size(), off, m_cellSize, cellBO);
        double sv = m_map.dataSigned ? readRomValueAsDouble(raw, m_data.size(), off, m_cellSize, cellBO, true) : double(rv);
        double phys = scaled ? cm.toPhysical(sv) : sv;

        double newPhys;
        switch (mode) {
        case 0:  newPhys = phys * (1.0 + delta / 100.0); break;
        case 1:  newPhys = phys + delta;                  break;
        default: newPhys = delta;                          break;
        }
        batch.append(writeCell(s.row, s.col, newPhys));
    }

    commitBatch(batch);
    if (!m_syncProject) rebuildTablePreservingGridState();
    emit editBatchDone();
}

bool MapOverlay::isReadOnlyView() const
{
    return m_showingOriginal || m_displayMode != DisplayMode::Values;
}

double MapOverlay::cellPhys(int row, int col) const
{
    const int cols = qMax(1, m_map.dimensions.x);
    const int rows = qMax(1, m_map.dimensions.y);
    const uint32_t off = m_map.address + m_map.mapDataOffset
                         + uint32_t(m_map.columnMajor ? col * rows + row
                                                      : row * cols + col) * m_cellSize;
    const auto *raw = reinterpret_cast<const uint8_t*>(m_data.constData());
    const ByteOrder cellBO = cellByteOrder(m_map, m_byteOrder);
    uint32_t rv = readRomValue(raw, m_data.size(), off, m_cellSize, cellBO);
    double sv = m_map.dataSigned
                ? readRomValueAsDouble(raw, m_data.size(), off, m_cellSize, cellBO, true)
                : double(rv);
    const bool scaled = m_map.hasScaling
                        && m_map.scaling.type != CompuMethod::Type::Identical;
    return scaled ? m_map.scaling.toPhysical(sv) : sv;
}

// ═══════════════════════════════════════════════════════════════════════════════
// interpolateSelection — bilinear fill of the selected rectangle from its corners
//   • block (≥3 along one axis): bilinear from the four corners
//   • single row / column:       linear between the two ends
// The corner (anchor) cells are preserved; everything between is recomputed.
// ═══════════════════════════════════════════════════════════════════════════════
void MapOverlay::interpolateSelection()
{
    if (isReadOnlyView()) return;
    cancelInlineEditor();

    const int cols = qMax(1, m_map.dimensions.x);

    // Bounding rectangle of the current selection (data cells only)
    int r0 = -1, r1 = -1, c0 = -1, c1 = -1;
    for (auto *it : m_table->selectedItems()) {
        if (it->column() >= cols) continue;   // skip the heat-bar column
        const int r = it->row(), c = it->column();
        if (r0 < 0) { r0 = r1 = r; c0 = c1 = c; }
        else { r0 = qMin(r0, r); r1 = qMax(r1, r); c0 = qMin(c0, c); c1 = qMax(c1, c); }
    }
    if (r0 < 0) return;   // nothing selected

    const int nr = r1 - r0 + 1;
    const int nc = c1 - c0 + 1;
    if (nr < 3 && nc < 3) {
        // A 2×2 (or smaller) block is all corners — nothing to fill.
        m_statusBar->setText(
            tr("Interpolate needs at least 3 cells across a row, column, or block."));
        return;
    }

    const double v00 = cellPhys(r0, c0);   // top-left
    const double v01 = cellPhys(r0, c1);   // top-right
    const double v10 = cellPhys(r1, c0);   // bottom-left
    const double v11 = cellPhys(r1, c1);   // bottom-right

    EditBatch batch;
    for (int r = r0; r <= r1; ++r) {
        const double t = (nr > 1) ? double(r - r0) / (nr - 1) : 0.0;
        for (int c = c0; c <= c1; ++c) {
            // Keep the four corner anchors exactly as they are.
            if ((r == r0 || r == r1) && (c == c0 || c == c1)) continue;
            const double u = (nc > 1) ? double(c - c0) / (nc - 1) : 0.0;
            const double top = v00 + (v01 - v00) * u;
            const double bot = v10 + (v11 - v10) * u;
            const double val = top + (bot - top) * t;
            batch.append(writeCell(r, c, val));
        }
    }
    if (batch.isEmpty()) return;

    commitBatch(batch);
    if (!m_syncProject) rebuildTablePreservingGridState();
    emit editBatchDone();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Inline editor
// ═══════════════════════════════════════════════════════════════════════════════
void MapOverlay::openInlineEditor(const QString &prefill)
{
    if (isReadOnlyView() || m_btn3D->isChecked()) return;

    QTableWidgetItem *cur = m_table->currentItem();
    if (!cur) return;

    const int cols = qMax(1, m_map.dimensions.x);
    if (cur->column() >= cols) return;   // heat bar column — skip

    m_editRow = cur->row();
    m_editCol = cur->column();

    // Mark all selected data cells as "pending"
    markPendingCells(true);

    // Pre-fill with current value (unless a typed character was provided)
    if (prefill.isEmpty()) {
        const bool scaled = m_map.hasScaling
                            && m_map.scaling.type != CompuMethod::Type::Identical;
        const int rows = qMax(1, m_map.dimensions.y);
        const int cols2 = qMax(1, m_map.dimensions.x);
        uint32_t off = m_map.address + m_map.mapDataOffset
                       + uint32_t(m_map.columnMajor ? m_editCol * rows + m_editRow : m_editRow * cols2 + m_editCol) * m_cellSize;
        const auto *raw = reinterpret_cast<const uint8_t*>(m_data.constData());
        const ByteOrder cellBO = cellByteOrder(m_map, m_byteOrder);
        uint32_t rv = readRomValue(raw, m_data.size(), off, m_cellSize, cellBO);
        double sv = m_map.dataSigned ? readRomValueAsDouble(raw, m_data.size(), off, m_cellSize, cellBO, true) : double(rv);
        double phys = scaled ? m_map.scaling.toPhysical(sv) : sv;
        m_inlineEdit->setText(scaled ? m_map.scaling.formatValue(phys) : QString::number(sv));
        m_inlineEdit->selectAll();
    } else {
        m_inlineEdit->setText(prefill);
        m_inlineEdit->setCursorPosition(prefill.length());
    }

    repositionInlineEditor();
    m_inlineEdit->show();
    m_inlineEdit->setFocus();
    m_editOpen = true;
    updateStatusBar();
}

void MapOverlay::repositionInlineEditor()
{
    // May be called before m_editOpen is set — always reposition if row/col are valid
    if (m_editRow < 0 || m_editCol < 0) return;
    QTableWidgetItem *cur = m_table->item(m_editRow, m_editCol);
    if (!cur) return;
    QRect r = m_table->visualItemRect(cur);
    if (r.isValid())
        m_inlineEdit->setGeometry(r.adjusted(-1, -1, 1, 1));
}

void MapOverlay::markPendingCells(bool mark)
{
    const int cols = qMax(1, m_map.dimensions.x);
    for (auto *it : m_table->selectedItems()) {
        if (it->column() < cols)
            it->setData(Qt::UserRole + 2, mark);
    }
    m_table->viewport()->update();
}

void MapOverlay::commitInlineEditor(int dCol, int dRow)
{
    if (!m_editOpen) return;

    bool ok;
    double newPhys = m_inlineEdit->text().trimmed().toDouble(&ok);
    if (!ok) {
        cancelInlineEditor();
        return;
    }

    // Hide editor before writes (prevents visual glitch)
    m_editOpen = false;
    m_inlineEdit->hide();

    const int cols = qMax(1, m_map.dimensions.x);
    const int rows = qMax(1, m_map.dimensions.y);

    // Collect selected data cells
    QList<QTableWidgetItem*> selItems = m_table->selectedItems();
    QVector<QTableWidgetItem*> targets;
    for (auto *it : selItems)
        if (it->column() < cols) targets.append(it);

    if (targets.isEmpty()) {
        // Fallback: just write current cell
        auto *cur = m_table->item(m_editRow, m_editCol);
        if (cur) targets.append(cur);
    }

    EditBatch batch;
    for (auto *it : targets)
        batch.append(writeCell(it->row(), it->column(), newPhys));

    commitBatch(batch);
    if (!m_syncProject) rebuildTablePreservingGridState();
    emit editBatchDone();

    // Navigate after commit
    if (dCol != 0 || dRow != 0) {
        int nr = qBound(0, m_editRow + dRow, rows - 1);
        int nc = qBound(0, m_editCol + dCol, cols - 1);
        m_table->setCurrentCell(nr, nc);
        m_table->scrollToItem(m_table->item(nr, nc));
        openInlineEditor();
    } else {
        m_table->setFocus();
        updateStatusBar();
    }
}

void MapOverlay::cancelInlineEditor()
{
    if (!m_editOpen) return;
    m_editOpen = false;
    m_inlineEdit->hide();
    markPendingCells(false);
    m_table->setFocus();
    updateStatusBar();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Table-level keyboard helpers
// ═══════════════════════════════════════════════════════════════════════════════
int MapOverlay::firstDataCol() const { return 0; }
int MapOverlay::lastDataCol()  const { return qMax(1, m_map.dimensions.x) - 1; }
int MapOverlay::firstDataRow() const { return 0; }
int MapOverlay::lastDataRow()  const { return qMax(1, m_map.dimensions.y) - 1; }

void MapOverlay::navigateCell(int dRow, int dCol, bool extend)
{
    auto *cur = m_table->currentItem();
    if (!cur) return;

    const int fCol = firstDataCol(), lCol = lastDataCol();
    const int fRow = firstDataRow(), lRow = lastDataRow();

    int r = cur->row() + dRow;
    int c = cur->column() + dCol;

    // Left/Right wrap across rows
    if (dCol != 0 && dRow == 0) {
        if (c > lCol) { c = fCol; r = qMin(r + 1, lRow); }
        else if (c < fCol) { c = lCol; r = qMax(r - 1, fRow); }
    }

    r = qBound(fRow, r, lRow);
    c = qBound(fCol, c, lCol);

    if (extend) {
        m_table->setCurrentCell(r, c,
            QItemSelectionModel::Select | QItemSelectionModel::Current);
    } else {
        m_table->setCurrentCell(r, c);
    }
    auto *item = m_table->item(r, c);
    if (item) m_table->scrollToItem(item);
}

void MapOverlay::incrementSelectedCells(int delta)
{
    if (isReadOnlyView()) return;

    const int cols = qMax(1, m_map.dimensions.x);
    const int rows = qMax(1, m_map.dimensions.y);
    const bool scaled = m_map.hasScaling
                        && m_map.scaling.type != CompuMethod::Type::Identical;
    const CompuMethod &cm = m_map.scaling;

    struct Sel { int row, col; };
    QVector<Sel> sel;
    for (auto *it : m_table->selectedItems())
        if (it->column() < cols) sel.append({it->row(), it->column()});
    if (sel.isEmpty()) return;

    // WinOLS treats +/- as an engineering-value step.  A formatted map with
    // two decimals steps by 0.01, for example, rather than by one physical
    // unit.  Keep the same automatic precision rule as formatValue().
    auto displayPrecision = [&cm, scaled]() {
        if (!scaled) return 0;
        if (!cm.format.isEmpty()) {
            const int dot = cm.format.indexOf('.');
            if (dot >= 0) {
                int end = dot + 1;
                while (end < cm.format.size() && cm.format[end].isDigit()) ++end;
                return qBound(0, cm.format.mid(dot + 1, end - dot - 1).toInt(), 100);
            }
        }
        if (cm.type == CompuMethod::Type::Linear && cm.linA != 0.0 && cm.linA != 1.0)
            return qBound(0, int(std::floor(-std::log10(std::abs(cm.linA)))), 3);
        return 0;
    };
    const double engineeringStep = std::pow(10.0, -displayPrecision());
    const double requestedDelta = double(delta) * engineeringStep;

    const int bits = m_cellSize * 8;
    const qint64 minStored = m_map.dataSigned ? -(qint64(1) << (bits - 1)) : 0;
    const qint64 maxStored = m_map.dataSigned ?  (qint64(1) << (bits - 1)) - 1
                                               :  (qint64(1) << bits) - 1;
    auto storageValue = [this](uint32_t bitsValue) -> qint64 {
        if (!m_map.dataSigned) return qint64(bitsValue);
        if (m_cellSize == 1) return qint8(bitsValue);
        if (m_cellSize == 4) return qint32(bitsValue);
        return qint16(bitsValue);
    };

    EditBatch batch;
    for (const auto &s : sel) {
        uint32_t off = m_map.address + m_map.mapDataOffset
                       + uint32_t(m_map.columnMajor ? s.col * rows + s.row : s.row * cols + s.col) * m_cellSize;
        const auto *raw = reinterpret_cast<const uint8_t*>(m_data.constData());
        const ByteOrder cellBO = cellByteOrder(m_map, m_byteOrder);
        uint32_t rv = readRomValue(raw, m_data.size(), off, m_cellSize, cellBO);
        double sv = m_map.dataSigned ? readRomValueAsDouble(raw, m_data.size(), off, m_cellSize, cellBO, true) : double(rv);
        double phys = scaled ? cm.toPhysical(sv) : sv;
        const double targetPhys = phys + requestedDelta;
        const double rawTarget = scaled ? cm.toRaw(targetPhys) : targetPhys;

        uint32_t candidateRaw;
        if (m_map.dataSigned) {
            candidateRaw = uint32_t(int32_t(qBound(double(minStored), std::round(rawTarget), double(maxStored))));
        } else {
            candidateRaw = uint32_t(qBound(0.0, std::round(rawTarget), double(maxStored)));
        }

        // A converted integer can be different in storage yet still move the
        // displayed value the wrong way for a negative/non-linear conversion.
        // Accept only a representable value that advances in the requested
        // engineering direction.
        const double epsilon = 1e-12 * qMax(1.0, std::abs(phys));
        auto advancesRequestedDirection = [&](uint32_t rawBits) {
            const double candidateStored = double(storageValue(rawBits));
            const double candidatePhys = scaled ? cm.toPhysical(candidateStored) : candidateStored;
            if (!std::isfinite(candidatePhys)) return false;
            return requestedDelta > 0.0 ? candidatePhys > phys + epsilon
                                        : candidatePhys < phys - epsilon;
        };

        if (candidateRaw != rv && !advancesRequestedDirection(candidateRaw))
            candidateRaw = rv;

        // WinOLS does not let scaling quantisation turn a non-zero +/- command
        // into a silent no-op.  Probe the adjacent representable raw values and
        // choose the one that moves physical value towards the requested step.
        if (candidateRaw == rv && requestedDelta != 0.0) {
            const qint64 currentStored = storageValue(rv);
            uint32_t bestRaw = rv;
            double bestDistance = std::numeric_limits<double>::infinity();
            for (const qint64 neighbour : {currentStored - 1, currentStored + 1}) {
                if (neighbour < minStored || neighbour > maxStored) continue;
                const uint32_t neighbourBits = uint32_t(neighbour);
                if (!advancesRequestedDirection(neighbourBits)) continue;
                const double neighbourPhys = scaled ? cm.toPhysical(double(neighbour)) : double(neighbour);
                const double distance = std::abs(targetPhys - neighbourPhys);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    bestRaw = neighbourBits;
                }
            }
            candidateRaw = bestRaw;
        }

        if (candidateRaw != rv)
            batch.append(writeRawCell(s.row, s.col, candidateRaw));
    }

    if (batch.isEmpty()) return;
    commitBatch(batch);
    if (!m_syncProject) rebuildTablePreservingGridState();
    emit editBatchDone();
}

bool MapOverlay::incrementGridSelectionFromMenu(int delta)
{
    // A 3D map command intentionally remains on MainWindow's whole-map path:
    // its view has no grid selection. Only the visible table can own a
    // selection-scoped menu increment.
    if (!isVisible() || m_stack->currentWidget() != m_table)
        return false;

    const int dataCols = qMax(1, m_map.dimensions.x);
    bool hasDataCell = false;
    for (const auto *item : m_table->selectedItems()) {
        if (item && item->column() < dataCols) {
            hasDataCell = true;
            break;
        }
    }
    if (!hasDataCell)
        return false;

    // Treat a selected read-only / saturated cell as handled too. Falling
    // through would make MainWindow apply the edit to the full map instead.
    incrementSelectedCells(delta);
    return true;
}

void MapOverlay::deleteSelectedCells()
{
    if (isReadOnlyView()) return;

    const int cols = qMax(1, m_map.dimensions.x);
    struct Sel { int row, col; };
    QVector<Sel> sel;
    for (auto *it : m_table->selectedItems())
        if (it->column() < cols) sel.append({it->row(), it->column()});
    if (sel.isEmpty()) return;

    EditBatch batch;
    for (const auto &s : sel)
        batch.append(writeCell(s.row, s.col, 0.0));

    commitBatch(batch);
    if (!m_syncProject) rebuildTablePreservingGridState();
    emit editBatchDone();
}

void MapOverlay::copySelectionToClipboard()
{
    QStringList rows;
    int prevRow = -1;
    QStringList rowVals;
    const auto sel = m_table->selectedItems();
    for (auto *it : sel) {
        if (it->column() >= qMax(1, m_map.dimensions.x)) continue; // skip heat bar
        if (it->row() != prevRow && prevRow != -1) {
            rows << rowVals.join('\t');
            rowVals.clear();
        }
        rowVals << it->text();
        prevRow = it->row();
    }
    if (!rowVals.isEmpty()) rows << rowVals.join('\t');
    QApplication::clipboard()->setText(rows.join('\n'));
}

void MapOverlay::pasteFromClipboard()
{
    if (isReadOnlyView()) return;

    const QString text = QApplication::clipboard()->text().trimmed();
    if (text.isEmpty()) return;

    const int cols = qMax(1, m_map.dimensions.x);

    // Determine top-left corner from current cell
    auto *cur = m_table->currentItem();
    if (!cur) return;
    int startRow = cur->row();
    int startCol = cur->column();
    if (startCol >= cols) return;

    // Parse clipboard as rows of tab-delimited values
    const QStringList lines = text.split('\n');
    EditBatch batch;
    for (int dr = 0; dr < lines.size(); ++dr) {
        int r = startRow + dr;
        if (r > lastDataRow()) break;
        const QStringList vals = lines[dr].split('\t');
        for (int dc = 0; dc < vals.size(); ++dc) {
            int c = startCol + dc;
            if (c > lastDataCol()) break;
            bool ok;
            double v = vals[dc].trimmed().toDouble(&ok);
            if (!ok) continue;
            batch.append(writeCell(r, c, v));
        }
    }

    if (!batch.isEmpty()) {
        commitBatch(batch);
        if (!m_syncProject) rebuildTablePreservingGridState();
        emit editBatchDone();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// eventFilter — keyboard handling for table and inline editor
// ═══════════════════════════════════════════════════════════════════════════════
bool MapOverlay::event(QEvent *event)
{
    if (event->type() == QEvent::WindowActivate)
        emit activated(this);
    return QDialog::event(event);
}

bool MapOverlay::eventFilter(QObject *obj, QEvent *ev)
{
    // QAction shortcuts are resolved before the corresponding KeyPress.  The
    // main window also owns +/- shortcuts, but when the map grid has focus
    // they must belong to this selection, not the waveform/hex dispatcher.
    if (ev->type() == QEvent::ShortcutOverride
        && (obj == m_table || obj == m_table->viewport()) && !m_editOpen) {
        auto *ke = static_cast<QKeyEvent*>(ev);
        if (ke->key() == Qt::Key_Plus || ke->key() == Qt::Key_Equal
            || ke->key() == Qt::Key_Minus) {
            ke->accept();
            return true;
        }
    }

    if (ev->type() != QEvent::KeyPress)
        return QDialog::eventFilter(obj, ev);

    auto *ke = static_cast<QKeyEvent*>(ev);

    // ── Inline editor key handling ────────────────────────────────────────────
    if (obj == m_inlineEdit) {
        switch (ke->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            commitInlineEditor(0, +1);   // confirm + move down (OLS convention)
            return true;
        case Qt::Key_Tab:
            commitInlineEditor(+1, 0);
            return true;
        case Qt::Key_Backtab:
            commitInlineEditor(-1, 0);
            return true;
        case Qt::Key_Up:
            commitInlineEditor(0, -1);
            return true;
        case Qt::Key_Down:
            commitInlineEditor(0, +1);
            return true;
        case Qt::Key_Escape:
            cancelInlineEditor();
            return true;
        default:
            break;
        }
        return false;
    }

    // ── Table key handling ────────────────────────────────────────────────────
    if (obj == m_table || obj == m_table->viewport()) {
        if (m_editOpen) return false;

        const bool shift = ke->modifiers() & Qt::ShiftModifier;
        const bool ctrl  = ke->modifiers() & Qt::ControlModifier;

        // ── Navigation keys ──────────────────────────────────────────────
        switch (ke->key()) {
        case Qt::Key_Left:
            navigateCell(0, -1, shift);
            return true;
        case Qt::Key_Right:
            navigateCell(0, +1, shift);
            return true;
        case Qt::Key_Up:
            navigateCell(-1, 0, shift);
            return true;
        case Qt::Key_Down:
            navigateCell(+1, 0, shift);
            return true;

        case Qt::Key_Home:
            if (ctrl)
                navigateCell(-(lastDataRow() + 1), -(lastDataCol() + 1), shift);
            else {
                auto *cur = m_table->currentItem();
                if (cur) {
                    int r = cur->row();
                    if (shift)
                        m_table->setCurrentCell(r, firstDataCol(),
                            QItemSelectionModel::Select | QItemSelectionModel::Current);
                    else
                        m_table->setCurrentCell(r, firstDataCol());
                    m_table->scrollToItem(m_table->item(r, firstDataCol()));
                }
            }
            return true;

        case Qt::Key_End:
            if (ctrl)
                navigateCell(lastDataRow() + 1, lastDataCol() + 1, shift);
            else {
                auto *cur = m_table->currentItem();
                if (cur) {
                    int r = cur->row();
                    if (shift)
                        m_table->setCurrentCell(r, lastDataCol(),
                            QItemSelectionModel::Select | QItemSelectionModel::Current);
                    else
                        m_table->setCurrentCell(r, lastDataCol());
                    m_table->scrollToItem(m_table->item(r, lastDataCol()));
                }
            }
            return true;

        case Qt::Key_PageUp: {
            int visRows = m_table->viewport()->height() / m_table->rowHeight(0);
            if (visRows < 1) visRows = 1;
            navigateCell(-visRows, 0, shift);
            return true;
        }
        case Qt::Key_PageDown: {
            int visRows = m_table->viewport()->height() / m_table->rowHeight(0);
            if (visRows < 1) visRows = 1;
            navigateCell(+visRows, 0, shift);
            return true;
        }

        // ── Tab / Shift+Tab ──────────────────────────────────────────────
        case Qt::Key_Tab:
            navigateCell(0, +1, false);
            return true;
        case Qt::Key_Backtab:
            navigateCell(0, -1, false);
            return true;

        // ── Editing shortcuts ────────────────────────────────────────────
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_F2:
            openInlineEditor();
            return true;

        case Qt::Key_Delete:
            deleteSelectedCells();
            return true;

        case Qt::Key_Backspace:
            openInlineEditor("0");
            return true;

        // ── +/- increment/decrement ──────────────────────────────────────
        case Qt::Key_Plus:
        case Qt::Key_Equal:
            incrementSelectedCells(shift ? 10 : 1);
            return true;
        case Qt::Key_Minus:
            incrementSelectedCells(shift ? -10 : -1);
            return true;

        default:
            break;
        }

        // ── Ctrl+C / Ctrl+V / Ctrl+A ─────────────────────────────────────
        if (ke->matches(QKeySequence::Copy)) {
            copySelectionToClipboard();
            return true;
        }
        if (ke->matches(QKeySequence::Paste)) {
            pasteFromClipboard();
            return true;
        }
        if (ke->matches(QKeySequence::SelectAll)) {
            // Select all data cells, excluding the heat bar column
            const int fR = firstDataRow(), lR = lastDataRow();
            const int fC = firstDataCol(), lC = lastDataCol();
            m_table->clearSelection();
            QItemSelection sel(
                m_table->model()->index(fR, fC),
                m_table->model()->index(lR, lC));
            m_table->selectionModel()->select(sel, QItemSelectionModel::Select);
            return true;
        }

        // ── Any printable character opens the editor pre-seeded ──────────
        if (!ke->text().isEmpty() && ke->text().at(0).isPrint()
                && !(ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
            openInlineEditor(ke->text());
            return true;
        }
    }

    return QDialog::eventFilter(obj, ev);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Undo / Redo
// ═══════════════════════════════════════════════════════════════════════════════
void MapOverlay::undoEdit()
{
    if (m_syncProject) {
        cancelInlineEditor();
        m_syncProject->undoRomEdit();
        return;
    }
    if (m_undoStack.isEmpty()) return;
    cancelInlineEditor();
    EditBatch batch = m_undoStack.takeLast();
    auto *dst = reinterpret_cast<uint8_t*>(m_data.data());
    const ByteOrder cellBO = cellByteOrder(m_map, m_byteOrder);
    for (const auto &e : batch) {
        writeRomValue(dst, m_data.size(), e.offset, m_cellSize, cellBO, e.oldRaw);
        QByteArray patch(m_cellSize, '\0');
        writeRomValue(reinterpret_cast<uint8_t*>(patch.data()), m_cellSize,
                      0, m_cellSize, cellBO, e.oldRaw);
        emit romPatchReady(e.offset, patch);
    }
    m_redoStack.append(batch);
    updateUndoRedoButtons();
    buildTable();
    emit editBatchDone();
}

void MapOverlay::redoEdit()
{
    if (m_syncProject) {
        cancelInlineEditor();
        m_syncProject->redoRomEdit();
        return;
    }
    if (m_redoStack.isEmpty()) return;
    cancelInlineEditor();
    EditBatch batch = m_redoStack.takeLast();
    auto *dst = reinterpret_cast<uint8_t*>(m_data.data());
    const ByteOrder cellBO = cellByteOrder(m_map, m_byteOrder);
    for (const auto &e : batch) {
        writeRomValue(dst, m_data.size(), e.offset, m_cellSize, cellBO, e.newRaw);
        QByteArray patch(m_cellSize, '\0');
        writeRomValue(reinterpret_cast<uint8_t*>(patch.data()), m_cellSize,
                      0, m_cellSize, cellBO, e.newRaw);
        emit romPatchReady(e.offset, patch);
    }
    m_undoStack.append(batch);
    updateUndoRedoButtons();
    buildTable();
    emit editBatchDone();
}

void MapOverlay::pushUndo(const EditBatch &batch)
{
    if (batch.isEmpty()) return;
    m_undoStack.append(batch);
    m_redoStack.clear();
    updateUndoRedoButtons();
}

void MapOverlay::updateUndoRedoButtons()
{
    if (m_syncProject) {
        m_btnUndo->setEnabled(m_syncProject->canUndoRomEdit());
        m_btnRedo->setEnabled(m_syncProject->canRedoRomEdit());
        m_btnUndo->setToolTip(m_syncProject->canUndoRomEdit()
            ? tr("Undo  Ctrl+Z") : tr("Nothing to undo"));
        m_btnRedo->setToolTip(m_syncProject->canRedoRomEdit()
            ? tr("Redo  Ctrl+Y") : tr("Nothing to redo"));
        return;
    }
    m_btnUndo->setEnabled(!m_undoStack.isEmpty());
    m_btnRedo->setEnabled(!m_redoStack.isEmpty());
    m_btnUndo->setToolTip(m_undoStack.isEmpty()
        ? tr("Nothing to undo")
        : tr("Undo (%1 steps)  Ctrl+Z").arg(m_undoStack.size()));
    m_btnRedo->setToolTip(m_redoStack.isEmpty()
        ? tr("Nothing to redo")
        : tr("Redo (%1 steps)  Ctrl+Y").arg(m_redoStack.size()));
}

// ═══════════════════════════════════════════════════════════════════════════════
// updateStatusBar
// ═══════════════════════════════════════════════════════════════════════════════
void MapOverlay::updateStatusBar()
{
    if (m_editOpen) {
        const int cols = qMax(1, m_map.dimensions.x);
        int n = 0;
        for (auto *it : m_table->selectedItems())
            if (it->column() < cols) ++n;
        m_statusBar->setText(
            n > 1
            ? tr("Editing  —  value will be written to %1 selected cells  ·  Enter confirm  ·  Tab next column  ·  ↑↓ next row  ·  Esc cancel").arg(n)
            : tr("Editing  —  Enter to confirm  ·  Tab next column  ·  ↑↓ next row  ·  Esc cancel"));
        return;
    }

    const int cols = qMax(1, m_map.dimensions.x);
    int n = 0;
    for (auto *it : m_table->selectedItems())
        if (it->column() < cols) ++n;

    if (n == 0) {
        m_statusBar->setText(tr("Click a cell to select  ·  Shift+click or Ctrl+click for multi-select  ·  Enter or type to edit"));
    } else if (n == 1) {
        auto *it = m_table->currentItem();
        if (it && it->column() < cols)
            m_statusBar->setText(tr("1 cell selected  [row %1, col %2]  ·  Enter or type to edit  ·  Ctrl+Z undo")
                .arg(it->row()).arg(it->column()));
    } else {
        m_statusBar->setText(
            tr("%1 cells selected  ·  Enter or type to set all to the same value  ·  Use Δ bar for +% or add  ·  Ctrl+Z undo").arg(n));
    }
}
