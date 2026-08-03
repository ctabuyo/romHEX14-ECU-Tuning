/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "aiassistant.h"
#include "project.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QSettings>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QCheckBox>
#include <QKeyEvent>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QSizePolicy>
#include <QSpacerItem>
#include <QFrame>
#include <QTimer>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QConicalGradient>
#include <QLinearGradient>
#include <QTextBrowser>
#include <QFile>
#include <QStandardPaths>
#include <QJsonArray>
#include <QMenu>
#include <QTabWidget>
#include <QListWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QSplitter>

// ─────────────────────────────────────────────────────────────────────────────
// XOR obfuscation for API keys stored in QSettings (same as apiclient.cpp)
// ─────────────────────────────────────────────────────────────────────────────
static const quint8 OBF_KEY[] = {0xA3,0x7F,0x1C,0xD2,0x56,0x8B,0x4E,0x93,
                                  0xC1,0x2A,0xF7,0x65,0x3D,0xB8,0x0E,0x49};
static constexpr int OBF_LEN = sizeof(OBF_KEY);

static QByteArray obfuscate(const QByteArray &data)
{
    QByteArray out = data;
    for (int i = 0; i < out.size(); ++i)
        out[i] = out[i] ^ OBF_KEY[i % OBF_LEN];
    return out.toBase64();
}

static QByteArray deobfuscate(const QByteArray &data)
{
    QByteArray raw = QByteArray::fromBase64(data);
    for (int i = 0; i < raw.size(); ++i)
        raw[i] = raw[i] ^ OBF_KEY[i % OBF_LEN];
    return raw;
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal UI helpers
// ─────────────────────────────────────────────────────────────────────────────

// App accent colours (blue theme)
static const QColor kAccent(0x1f, 0x6f, 0xeb);       // #1f6feb
static const QColor kAccentDark(0x0d, 0x41, 0x9d);   // #0d419d
static const QColor kBg("#0d1117");
static const QColor kSurface("#161b22");
static const QColor kBorder("#30363d");
static const QColor kText("#c9d1d9");
static const QColor kDim("#8b949e");

// ── Provider compatibility tier colours / helpers ───────────────────────────
// Mirrors the legend used in ConfigDialog so the same visual vocabulary shows
// up both in the AI-assistant settings combo and the global app settings.
static const QColor kTierColors[3] = {
    QColor("#3fb950"),  // 0 — green (best)
    QColor("#d29922"),  // 1 — amber (good)
    QColor("#f85149"),  // 2 — red   (limited)
};

static QIcon tierDotIcon(int tier, int diameter = 10)
{
    const int pad = 2;
    const int box = diameter + pad * 2;
    QPixmap pm(box, box);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(kTierColors[qBound(0, tier, 2)]);
    p.setPen(Qt::NoPen);
    p.drawEllipse(pad, pad, diameter, diameter);
    return QIcon(pm);
}

// ── Claude orb logo (animated when thinking) ─────────────────────────────────
class ClaudeOrb : public QWidget {
    Q_OBJECT
public:
    explicit ClaudeOrb(int size = 32, QWidget *p = nullptr)
        : QWidget(p), m_size(size)
    {
        setFixedSize(size, size);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        connect(&m_timer, &QTimer::timeout, this, [this]{
            m_angle = (m_angle + 6) % 360;
            m_pulse = m_pulse + m_pulseDir * 0.04f;
            if (m_pulse >= 1.0f) { m_pulse = 1.0f; m_pulseDir = -1; }
            if (m_pulse <= 0.0f) { m_pulse = 0.0f; m_pulseDir =  1; }
            update();
        });
    }

    void setThinking(bool on) {
        m_thinking = on;
        if (on)  m_timer.start(30);
        else   { m_timer.stop(); m_angle = 0; m_pulse = 0; update(); }
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QRectF r(1, 1, m_size - 2, m_size - 2);

        // Outer glow when thinking
        if (m_thinking) {
            float glow = 0.3f + m_pulse * 0.5f;
            QRadialGradient g(r.center(), m_size * 0.6);
            g.setColorAt(0.5, QColor(31, 111, 235, int(glow * 60)));
            g.setColorAt(1.0, Qt::transparent);
            p.setBrush(g);
            p.setPen(Qt::NoPen);
            p.drawEllipse(r.adjusted(-4,-4,4,4));
        }

        // Main orb gradient
        QRadialGradient grad(r.center().x() - r.width() * 0.15,
                             r.center().y() - r.height() * 0.15,
                             r.width() * 0.6);
        grad.setColorAt(0.0, QColor("#58a6ff"));
        grad.setColorAt(0.4, kAccent);
        grad.setColorAt(1.0, kAccentDark);
        p.setBrush(grad);
        p.setPen(Qt::NoPen);
        p.drawEllipse(r);

        // Thinking: rotating arc ring
        if (m_thinking) {
            p.setBrush(Qt::NoBrush);
            QPen arc(QColor(255, 255, 255, 200), 1.5f);
            arc.setCapStyle(Qt::RoundCap);
            p.setPen(arc);
            p.drawArc(r.adjusted(2,2,-2,-2), (-m_angle) * 16, 110 * 16);
            p.drawArc(r.adjusted(2,2,-2,-2), (-m_angle + 180) * 16, 60 * 16);
        }

        // Claude "C" glyph — simplified curved stroke
        p.setBrush(Qt::NoBrush);
        QPen glyphPen(QColor(255,255,255,230), m_size * 0.115f);
        glyphPen.setCapStyle(Qt::RoundCap);
        p.setPen(glyphPen);
        float cx = r.center().x(), cy = r.center().y();
        float rad = r.width() * 0.27f;
        // Draw arc from ~45° to ~315° (open on right) — classic "C" shape
        p.drawArc(QRectF(cx - rad, cy - rad, rad*2, rad*2), 45 * 16, 270 * 16);
    }

private:
    int    m_size;
    int    m_angle    = 0;
    float  m_pulse    = 0.f;
    int    m_pulseDir = 1;
    bool   m_thinking = false;
    QTimer m_timer;
};

// ── User avatar (coloured circle with initial) ───────────────────────────────
class UserAvatar : public QWidget {
public:
    explicit UserAvatar(int size = 28, QWidget *p = nullptr) : QWidget(p), m_size(size) {
        setFixedSize(size, size);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor("#1f6feb"));
        p.setPen(Qt::NoPen);
        p.drawEllipse(rect().adjusted(1,1,-1,-1));
        p.setPen(Qt::white);
        QFont f = p.font(); f.setBold(true); f.setPointSize(m_size * 0.35);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter, "U");
    }
private: int m_size;
};

// ── Painted header icons ──────────────────────────────────────────────────────
//
// Qt on macOS doesn't reliably render colour emoji inside small QPushButtons
// (the glyph drops to a blank tofu), which is why the header buttons showed as
// empty squares. We paint simple vector glyphs instead so they look identical
// across platforms and scale with HiDPI.

enum class HeaderGlyph {
    Book,      // 📋 tuning logbook
    Lines,     // ≡ verbose
    Backspace, // ⌫ clear chat
    Key,       // settings / api key
    Shield,    // permission: ask
    Bolt,      // permission: auto-accept
    Clipboard, // permission: plan
};

static QPixmap paintHeaderGlyph(HeaderGlyph kind, const QColor &color,
                                int size = 18, qreal dpr = 1.0)
{
    QPixmap pm(int(size * dpr), int(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 1.6);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    const QRectF r(1, 1, size - 2, size - 2);
    const qreal cx = r.center().x();
    const qreal cy = r.center().y();
    const qreal w  = r.width();
    const qreal h  = r.height();

    switch (kind) {
    case HeaderGlyph::Book: {
        // Rounded rectangle with 3 horizontal lines inside.
        QRectF box(r.left() + w*0.12, r.top() + h*0.08, w*0.76, h*0.84);
        p.drawRoundedRect(box, 2.0, 2.0);
        pen.setWidthF(1.2);
        p.setPen(pen);
        for (int i = 0; i < 3; ++i) {
            qreal y = box.top() + box.height() * (0.28 + i * 0.22);
            p.drawLine(QPointF(box.left() + box.width()*0.18, y),
                       QPointF(box.right() - box.width()*0.18, y));
        }
        break;
    }
    case HeaderGlyph::Lines: {
        // Three stacked horizontal bars (hamburger).
        for (int i = 0; i < 3; ++i) {
            qreal y = r.top() + h * (0.28 + i * 0.22);
            p.drawLine(QPointF(r.left() + w*0.18, y),
                       QPointF(r.right() - w*0.18, y));
        }
        break;
    }
    case HeaderGlyph::Backspace: {
        // Pentagon with an X.
        QPolygonF pent;
        pent << QPointF(r.left() + w*0.30, r.top() + h*0.18)
             << QPointF(r.right() - w*0.10, r.top() + h*0.18)
             << QPointF(r.right() - w*0.10, r.bottom() - h*0.18)
             << QPointF(r.left() + w*0.30, r.bottom() - h*0.18)
             << QPointF(r.left() + w*0.08, cy);
        p.drawPolygon(pent);
        pen.setWidthF(1.4);
        p.setPen(pen);
        qreal xL = r.left() + w*0.48,  xR = r.right() - w*0.22;
        qreal yT = r.top()  + h*0.32,  yB = r.bottom() - h*0.32;
        p.drawLine(QPointF(xL, yT), QPointF(xR, yB));
        p.drawLine(QPointF(xL, yB), QPointF(xR, yT));
        break;
    }
    case HeaderGlyph::Key: {
        // Circle (key head) + stem with two teeth.
        qreal headR = w * 0.22;
        QPointF head(r.left() + w*0.28, cy);
        p.drawEllipse(head, headR, headR);
        qreal stemY = cy;
        p.drawLine(QPointF(head.x() + headR, stemY),
                   QPointF(r.right() - w*0.08, stemY));
        p.drawLine(QPointF(r.right() - w*0.22, stemY),
                   QPointF(r.right() - w*0.22, stemY + h*0.18));
        p.drawLine(QPointF(r.right() - w*0.08, stemY),
                   QPointF(r.right() - w*0.08, stemY + h*0.12));
        break;
    }
    case HeaderGlyph::Shield: {
        QPainterPath path;
        path.moveTo(cx, r.top() + h*0.08);
        path.lineTo(r.right() - w*0.15, r.top() + h*0.22);
        path.lineTo(r.right() - w*0.18, r.top() + h*0.58);
        path.quadTo(r.right() - w*0.22, r.bottom() - h*0.10,
                    cx, r.bottom() - h*0.04);
        path.quadTo(r.left() + w*0.22, r.bottom() - h*0.10,
                    r.left() + w*0.18, r.top() + h*0.58);
        path.lineTo(r.left() + w*0.15, r.top() + h*0.22);
        path.closeSubpath();
        p.drawPath(path);
        // Inner tick
        pen.setWidthF(1.4);
        p.setPen(pen);
        p.drawLine(QPointF(cx - w*0.14, cy + h*0.02),
                   QPointF(cx - w*0.02, cy + h*0.14));
        p.drawLine(QPointF(cx - w*0.02, cy + h*0.14),
                   QPointF(cx + w*0.18, cy - h*0.12));
        break;
    }
    case HeaderGlyph::Bolt: {
        QPolygonF bolt;
        bolt << QPointF(cx + w*0.08, r.top()    + h*0.06)
             << QPointF(r.left()    + w*0.28,   cy - h*0.02)
             << QPointF(cx - w*0.02, cy - h*0.02)
             << QPointF(cx - w*0.14, r.bottom() - h*0.06)
             << QPointF(r.right()   - w*0.26,   cy + h*0.04)
             << QPointF(cx + w*0.02, cy + h*0.04);
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawPolygon(bolt);
        break;
    }
    case HeaderGlyph::Clipboard: {
        // Board outline with clip on top and 2 inner lines.
        QRectF board(r.left() + w*0.16, r.top() + h*0.18, w*0.68, h*0.74);
        p.drawRoundedRect(board, 2.0, 2.0);
        QRectF clip(cx - w*0.16, r.top() + h*0.08, w*0.32, h*0.18);
        p.drawRoundedRect(clip, 1.5, 1.5);
        pen.setWidthF(1.2);
        p.setPen(pen);
        for (int i = 0; i < 2; ++i) {
            qreal y = board.top() + board.height() * (0.45 + i * 0.22);
            p.drawLine(QPointF(board.left() + w*0.10, y),
                       QPointF(board.right() - w*0.10, y));
        }
        break;
    }
    }
    p.end();
    return pm;
}

// Header icon button: 28×28, painted glyph, consistent hover/checked styling.
class HeaderIconBtn : public QPushButton {
public:
    HeaderIconBtn(HeaderGlyph glyph, QWidget *parent = nullptr)
        : QPushButton(parent), m_glyph(glyph)
    {
        setFixedSize(26, 26);
        setCursor(Qt::PointingHandCursor);
        setIconSize(QSize(14, 14));
        setStyleSheet(
            "QPushButton { background:#21262d; border:1px solid #30363d; "
            "              border-radius:5px; padding:0; }"
            "QPushButton:hover { background:#30363d; border-color:#58a6ff; }"
            "QPushButton:pressed { background:#161b22; }"
            "QPushButton:checked { background:#1f3f6a; border-color:#1f6feb; }");
        refresh(QColor("#c9d1d9"));
    }
    void refresh(const QColor &iconColor) {
        const qreal dpr = devicePixelRatioF();
        setIcon(QIcon(paintHeaderGlyph(m_glyph, iconColor, 14, dpr)));
    }
    void setGlyph(HeaderGlyph g, const QColor &iconColor) {
        m_glyph = g;
        refresh(iconColor);
    }
private:
    HeaderGlyph m_glyph;
};

// ── Chat bubble widget ────────────────────────────────────────────────────────
//
// Layout principles (Claude-Code style):
//   • Bubbles never grow wider than kBubbleMaxWidth — wide chat panels keep a
//     comfortable reading column instead of stretching one-line replies edge to
//     edge. The avatar/orb stays at the gutter and the bubble docks beside it.
//   • Avatar gutter is fixed (kGutter) so Tool / ToolResult cards align under
//     the assistant orb instead of being indented by ad-hoc addSpacing() calls.
//   • Long content (code, JSON, identifiers) wraps at word boundaries and the
//     containing label gets a sensible minimum width so the bubble doesn't
//     collapse to single-character columns when the panel is narrow.

class BubbleWidget : public QWidget {
    Q_OBJECT
public:
    enum Role { User, Assistant, Tool, ToolResult };

    static constexpr int kBubbleMaxWidth = 720;
    static constexpr int kGutter         = 36;   // avatar/orb column width

    BubbleWidget(Role role, QWidget *parent = nullptr)
        : QWidget(parent), m_role(role)
    {
        auto *hl = new QHBoxLayout(this);
        hl->setContentsMargins(4, 2, 4, 2);
        hl->setSpacing(8);

        // The bubble itself lives inside a frame so we can cap its width.
        m_bubbleFrame = new QFrame();
        m_bubbleFrame->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        m_bubbleFrame->setMaximumWidth(kBubbleMaxWidth);
        auto *bubbleLay = new QVBoxLayout(m_bubbleFrame);
        bubbleLay->setContentsMargins(0, 0, 0, 0);
        bubbleLay->setSpacing(2);

        m_textLabel = new QLabel();
        m_textLabel->setWordWrap(true);
        m_textLabel->setMinimumWidth(120);
        m_textLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        m_textLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        m_textLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);

        if (role == User) {
            // Keep sent messages right-aligned, but let them use the same
            // readable column as assistant replies instead of content width.
            m_bubbleFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            m_textLabel->setTextFormat(Qt::PlainText);
            m_textLabel->setStyleSheet(
                "color:#c9d1d9; background:#1a3a5f; border-radius:12px;"
                "padding:9px 13px; border:1px solid #1f4a7a;");
            bubbleLay->addWidget(m_textLabel);

            // User bubbles dock to the right next to the avatar.
            hl->addStretch(0);
            hl->addWidget(m_bubbleFrame, 1);
            auto *av = new UserAvatar(28);
            hl->addWidget(av, 0, Qt::AlignTop);

        } else if (role == Assistant) {
            // Assistant replies should use the readable column available beside
            // the avatar.  A trailing stretch with a zero-stretch bubble makes
            // Qt keep the frame at its content width, which turns long prose
            // into a very narrow column.
            m_bubbleFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            m_textLabel->setTextFormat(Qt::RichText);
            m_textLabel->setStyleSheet(
                "color:#c9d1d9; background:#1c2128; border-radius:12px;"
                "padding:10px 14px; border:1px solid #30363d; font-size:10pt;");
            bubbleLay->addWidget(m_textLabel);

            m_orb = new ClaudeOrb(28);
            hl->addWidget(m_orb, 0, Qt::AlignTop);
            hl->addWidget(m_bubbleFrame, 1);
            hl->addStretch(0);

        } else if (role == Tool) {
            // Collapsible tool-call row, indented under the assistant gutter.
            m_textLabel->setTextFormat(Qt::RichText);
            m_textLabel->setStyleSheet(
                "color:#8b949e; font-size:8pt;"
                "background:#0d1117; border-radius:4px; padding:3px 8px;"
                "border-left:2px solid #30363d;");

            m_detail = new QLabel();
            m_detail->setWordWrap(true);
            m_detail->setTextFormat(Qt::RichText);
            m_detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
            m_detail->setMinimumWidth(120);
            m_detail->setStyleSheet(
                "color:#484f58; font-family:Consolas,Menlo,monospace; font-size:7pt;"
                "background:#0d1117; border-radius:4px; padding:2px 8px 4px 8px;"
                "border-left:2px solid #21262d;");
            m_detail->hide();

            m_collapseBtn = new QPushButton("▶");
            m_collapseBtn->setFixedSize(16, 16);
            m_collapseBtn->setFlat(true);
            m_collapseBtn->setCursor(Qt::PointingHandCursor);
            m_collapseBtn->setStyleSheet(
                "QPushButton{color:#484f58;font-size:7pt;border:none;background:transparent;padding:0;}"
                "QPushButton:hover{color:#8b949e;}");

            auto *headerRow = new QHBoxLayout();
            headerRow->setContentsMargins(0, 0, 0, 0);
            headerRow->setSpacing(4);
            headerRow->addWidget(m_collapseBtn, 0, Qt::AlignTop);
            headerRow->addWidget(m_textLabel, 1);
            bubbleLay->addLayout(headerRow);
            bubbleLay->addWidget(m_detail);

            hl->addSpacing(kGutter);
            hl->addWidget(m_bubbleFrame, 0);
            hl->addStretch(1);

            connect(m_collapseBtn, &QPushButton::clicked, this, [this]() {
                m_expanded = !m_expanded;
                m_detail->setVisible(m_expanded);
                m_collapseBtn->setText(m_expanded ? "▼" : "▶");
            });

        } else { // ToolResult
            m_textLabel->setTextFormat(Qt::RichText);
            m_textLabel->setStyleSheet(
                "color:#c9d1d9; font-size:8.5pt;"
                "background:#0d1117; border-radius:6px; padding:6px 10px;"
                "border-left:3px solid #238636;");
            bubbleLay->addWidget(m_textLabel);

            hl->addSpacing(kGutter);
            hl->addWidget(m_bubbleFrame, 0);
            hl->addStretch(1);
        }
    }

    // For non-assistant bubbles (plain text)
    void setPlainContent(const QString &text) {
        if (m_textLabel) m_textLabel->setText(text);
    }

    // Tool call: set summary label + optional detail (shown on expand)
    void setToolCall(const QString &summary, const QString &detail = {}) {
        if (m_textLabel) m_textLabel->setText(summary);
        if (m_detail && !detail.isEmpty()) {
            m_detail->setText(detail);
            m_collapseBtn->show();
        } else if (m_collapseBtn) {
            m_collapseBtn->hide();
        }
    }

    // Rich HTML result card (for ToolResult role)
    void setRichContent(const QString &html) {
        if (m_textLabel) {
            m_textLabel->setTextFormat(Qt::RichText);
            m_textLabel->setText(html);
        }
    }

    // For assistant bubbles (markdown → rich text via QLabel).
    // Order matters: triple-backtick fenced code must be extracted *before* we
    // touch single-backtick inline code or HTML-escape, otherwise the contents
    // of a fence get re-interpreted as markdown.
    void setMarkdownContent(const QString &md) {
        if (!m_textLabel) return;
        if (m_role != Assistant) { m_textLabel->setText(md); return; }

        // 1. Lift fenced code blocks out into placeholders.
        QStringList fences;
        QString src = md;
        QRegularExpression fenceRe(R"(```([a-zA-Z0-9_+-]*)\n([\s\S]*?)```)");
        QRegularExpressionMatchIterator it = fenceRe.globalMatch(src);
        QString work; int last = 0;
        while (it.hasNext()) {
            auto m = it.next();
            work += src.mid(last, m.capturedStart() - last);
            work += QString("\x01" "FENCE%1" "\x01").arg(fences.size());
            fences << m.captured(2);
            last = m.capturedEnd();
        }
        work += src.mid(last);

        // 2. Standard markdown→HTML pass on the non-fenced text.
        QString html = work.toHtmlEscaped();
        html.replace("\n", "<br>");
        html.replace(QRegularExpression(R"(\*\*(.+?)\*\*)"), "<b>\\1</b>");
        html.replace(QRegularExpression(R"(\*(.+?)\*)"),     "<i>\\1</i>");
        html.replace(QRegularExpression(R"(`([^`]+)`)"),
            "<code style='background:#161b22;padding:1px 4px;border-radius:3px;"
            "font-family:Consolas,Menlo,monospace;font-size:9pt;'>\\1</code>");
        html.replace(QRegularExpression(R"(^• )"), "&#8226; ");

        // 3. Re-insert fenced code blocks as styled <pre>-equivalents. QLabel
        //    has no real <pre>, but a div + pre-wrap white-space and wrapped
        //    monospace gets the same look without horizontal overflow.
        for (int i = 0; i < fences.size(); ++i) {
            QString block = fences[i].toHtmlEscaped()
                .replace(" ", "&nbsp;")
                .replace("\n", "<br>");
            QString rendered = QString(
                "<div style='background:#0a0e14;border:1px solid #21262d;"
                "border-radius:6px;padding:8px 10px;margin:4px 0;"
                "font-family:Consolas,Menlo,monospace;font-size:9pt;color:#c9d1d9;'>"
                "%1</div>").arg(block);
            html.replace(QString("\x01" "FENCE%1" "\x01").arg(i), rendered);
        }

        m_textLabel->setText(html);
    }

    QLabel *textLabel()         const { return m_textLabel; }
    ClaudeOrb *orb()            const { return m_orb; }
    Role role()                 const { return m_role; }

    void setThinking(bool on) { if (m_orb) m_orb->setThinking(on); }

private:
    Role          m_role;
    QFrame       *m_bubbleFrame = nullptr;   // width-capped container for the bubble body
    QLabel       *m_textLabel   = nullptr;
    QLabel       *m_detail      = nullptr;   // Tool: expandable detail
    QPushButton  *m_collapseBtn = nullptr;   // Tool: expand/collapse toggle
    ClaudeOrb    *m_orb         = nullptr;
    bool          m_expanded    = false;
};

// ── Welcome screen ────────────────────────────────────────────────────────────
class WelcomeWidget : public QWidget {
    Q_OBJECT
public:
    explicit WelcomeWidget(QWidget *parent = nullptr) : QWidget(parent) {
        auto *vl = new QVBoxLayout(this);
        vl->setAlignment(Qt::AlignCenter);
        vl->setSpacing(20);

        // Big orb
        auto *orb = new ClaudeOrb(64);
        vl->addWidget(orb, 0, Qt::AlignHCenter);

        // Title
        auto *title = new QLabel(tr("Claude for RX14"));
        title->setStyleSheet(
            "color:#e6edf3; font-size:18pt; font-weight:bold; background:transparent;");
        title->setAlignment(Qt::AlignCenter);
        vl->addWidget(title);

        // Subtitle
        auto *sub = new QLabel(tr("ECU calibration AI assistant"));
        sub->setStyleSheet("color:#8b949e; font-size:10pt; background:transparent;");
        sub->setAlignment(Qt::AlignCenter);
        vl->addWidget(sub);

        vl->addSpacing(12);

        // Chip groups — organised by category.
        //
        // Each chip is a (display, prompt) pair: `display` is shown to the user
        // through tr(), but the `prompt` sent to the model is always the
        // canonical English phrase. This keeps tuning keywords ("decat",
        // "dpf off") and recipe triggers stable across locales — otherwise a
        // Chinese user clicking "除三元" would send a Chinese string that the
        // intent classifier and recipe matcher don't recognise.
        //
        // QT_TR_NOOP keeps the English phrase visible to lupdate (so the .ts
        // file gets an entry) without performing the lookup at that line — we
        // tr() at runtime with the same source string.
        struct Chip { const char *en; };   // NUL-terminated literal
        struct ChipGroup {
            QString label;
            QColor  accentColor;
            QVector<Chip> chips;
        };
        const QVector<ChipGroup> groups = {
            { tr("Tune"),     QColor("#1f6feb"),
              { {QT_TR_NOOP("decat")}, {QT_TR_NOOP("dpf off")},
                {QT_TR_NOOP("egr off")}, {QT_TR_NOOP("pops and bangs")} } },
            { tr("Analyze"),  QColor("#2ea043"),
              { {QT_TR_NOOP("Detect anomalies in modified maps")},
                {QT_TR_NOOP("Describe the shape of the selected map")},
                {QT_TR_NOOP("What does this map do?")},
                {QT_TR_NOOP("Find maps related to the selected map")} } },
            { tr("Review"),   QColor("#8b5cf6"),
              { {QT_TR_NOOP("Show all modified maps")},
                {QT_TR_NOOP("Summarize all differences from original")},
                {QT_TR_NOOP("What changed from original?")} } },
            { tr("Log"),      QColor("#d29922"),
              { {QT_TR_NOOP("Log a dyno result")},
                {QT_TR_NOOP("Show tuning history")} } },
        };

        static const QString chipStyle =
            "QPushButton { background:#21262d; color:#8b949e; "
            "  border:1px solid #30363d; border-radius:14px; "
            "  padding:5px 14px; font-size:8.5pt; "
            "  min-width:0; max-width:16777215; }"
            "QPushButton:hover { background:#2d333b; color:#e6edf3; "
            "  border-color:#58a6ff; }";

        for (const ChipGroup &grp : groups) {
            // Group label
            auto *grpLbl = new QLabel(grp.label);
            grpLbl->setStyleSheet(QString("color:%1; font-size:7pt; font-weight:bold; "
                                          "background:transparent; letter-spacing:1px;")
                                  .arg(grp.accentColor.name()));
            grpLbl->setAlignment(Qt::AlignCenter);
            vl->addWidget(grpLbl);

            auto *row = new QHBoxLayout();
            row->setSpacing(6);
            row->setAlignment(Qt::AlignCenter);
            for (const Chip &c : grp.chips) {
                const QString english   = QString::fromUtf8(c.en);
                const QString displayed = tr(c.en);  // runtime lookup
                auto *chip = new QPushButton(displayed);
                chip->setStyleSheet(chipStyle);
                chip->setCursor(Qt::PointingHandCursor);
                chip->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
                // The signal always carries the canonical English phrase so
                // the intent classifier, recipe matcher, and AI model see the
                // same text regardless of the UI locale.
                connect(chip, &QPushButton::clicked, this, [this, english](){
                    emit chipClicked(english);
                });
                row->addWidget(chip);
            }
            vl->addLayout(row);
            vl->addSpacing(2);
        }
    }
signals:
    void chipClicked(const QString &text);
};

// ─────────────────────────────────────────────────────────────────────────────
// Include moc for Q_OBJECT classes defined in .cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "aiassistant.moc"

// ── Constants ─────────────────────────────────────────────────────────────────

static const char *kSettingsGroup = "AIAssistant";

// Static storage for user-loaded recipes
QVector<AIAssistant::TuningRecipe> AIAssistant::m_userRecipes;

// ── Rich tool result HTML renderer ────────────────────────────────────────────

static QString confidenceColor(int score)
{
    if (score >= 80) return "#3fb950";  // green
    if (score >= 50) return "#d29922";  // yellow
    return "#f85149";                   // red
}

static QString confidenceBar(int score)
{
    int filled = qBound(0, score / 10, 10);
    QString bar = "<span style='font-family:Consolas;font-size:7pt;'>";
    for (int i = 0; i < 10; ++i)
        bar += (i < filled) ? "<span style='color:%1;'>█</span>" : "<span style='color:#21262d;'>░</span>";
    bar = bar.arg(confidenceColor(score));
    bar += "</span>";
    return bar;
}

static QString confBadge(const QString &level)
{
    QString col = (level == "high") ? "#3fb950" : (level == "medium") ? "#d29922" : "#8b949e";
    return QString("<span style='color:%1;font-size:7pt;background:#161b22;"
                   "padding:1px 5px;border-radius:3px;'>%2</span>").arg(col, level.toUpper());
}

static QString richToolResultHtml(const QString &toolName, const QString &resultJson)
{
    QJsonDocument doc = QJsonDocument::fromJson(resultJson.toUtf8());
    if (doc.isNull()) return {};   // not JSON — caller will fall back to plain

    const QJsonObject obj = doc.object();
    const QString err = obj["error"].toString();

    // ── Error response ────────────────────────────────────────────────────────
    if (!err.isEmpty() && err != "cancelled") {
        return QString("<span style='color:#f85149;'>⚠ %1</span>").arg(err.toHtmlEscaped());
    }
    if (err == "cancelled") {
        return "<span style='color:#8b949e;'>✕ Cancelled</span>";
    }

    // ── describe_map_shape ───────────────────────────────────────────────────
    if (toolName == "describe_map_shape") {
        const QString mapName = obj["map"].toString();
        const QString shape   = obj["shape"].toString();
        const double vMin = obj["min"].toDouble(), vMax = obj["max"].toDouble();
        const double vMean = obj["mean"].toDouble(), avgGrad = obj["avg_gradient"].toDouble();
        const QString unit = obj["unit"].toString();
        const double flatPct = obj["flat_pct"].toDouble();
        QString html =
            QString("<b style='color:#58a6ff;'>📊 %1</b> — shape analysis<br>").arg(mapName.toHtmlEscaped()) +
            QString("<span style='color:#c9d1d9;'>%1</span><br>").arg(shape.toHtmlEscaped()) +
            QString("<table style='color:#8b949e;font-size:8pt;margin-top:4px;'>"
                    "<tr><td>Min:</td><td style='color:#c9d1d9;padding-right:12px;'>%1 %5</td>"
                    "<td>Max:</td><td style='color:#c9d1d9;'>%2 %5</td></tr>"
                    "<tr><td>Mean:</td><td style='color:#c9d1d9;'>%3 %5</td>"
                    "<td>Avg ∆:</td><td style='color:#c9d1d9;'>%4 %5/cell</td></tr>"
                    "<tr><td>Flat:</td><td style='color:#c9d1d9;'>%6%</td></tr>"
                    "</table>")
            .arg(vMin, 0, 'g', 4).arg(vMax, 0, 'g', 4)
            .arg(vMean, 0, 'g', 4).arg(avgGrad, 0, 'g', 3)
            .arg(unit.toHtmlEscaped()).arg(flatPct, 0, 'f', 1);
        return html;
    }

    // ── identify_map_purpose ─────────────────────────────────────────────────
    if (toolName == "identify_map_purpose") {
        const QString mapName = obj["map"].toString();
        const QString purpose = obj["purpose"].toString();
        const QString cat     = obj["category"].toString();
        const QString conf    = obj["confidence"].toString();
        const QString desc    = obj["description"].toString();
        QString html =
            QString("<b style='color:#58a6ff;'>🎯 %1</b> &nbsp; %2<br>").arg(mapName.toHtmlEscaped(), confBadge(conf)) +
            QString("<span style='color:#c9d1d9;'>%1</span><br>").arg(purpose.toHtmlEscaped()) +
            QString("<span style='color:#8b949e;font-size:8pt;'>Category: %1").arg(cat.toHtmlEscaped());
        if (!desc.isEmpty())
            html += QString(" &nbsp;|&nbsp; %1").arg(desc.left(80).toHtmlEscaped());
        html += "</span>";
        return html;
    }

    // ── confidence_search ────────────────────────────────────────────────────
    if (toolName == "confidence_search") {
        const QString query = obj["query"].toString();
        const QJsonArray results = obj["results"].toArray();
        if (results.isEmpty())
            return QString("<span style='color:#8b949e;'>🔍 No maps found for &ldquo;%1&rdquo;</span>").arg(query.toHtmlEscaped());
        QString html = QString("<b style='color:#58a6ff;'>🔍 &ldquo;%1&rdquo;</b> — %2 match(es)<br>")
            .arg(query.toHtmlEscaped()).arg(results.size());
        html += "<table style='font-size:8pt;margin-top:4px;'>";
        int shown = 0;
        for (const auto &rv : results) {
            if (++shown > 8) break;
            const QJsonObject r = rv.toObject();
            const int score = r["confidence"].toInt();
            html += QString("<tr>"
                "<td style='padding-right:6px;'>%1</td>"
                "<td style='color:%2;padding-right:6px;'>%3%%</td>"
                "<td>%4</td>"
                "<td style='color:#8b949e;font-size:7pt;padding-left:6px;'>%5</td>"
                "</tr>")
                .arg(r["name"].toString().toHtmlEscaped())
                .arg(confidenceColor(score))
                .arg(score)
                .arg(confidenceBar(score))
                .arg(r["description"].toString().left(50).toHtmlEscaped());
        }
        if (results.size() > 8)
            html += QString("<tr><td colspan='4' style='color:#8b949e;'>…and %1 more</td></tr>").arg(results.size() - 8);
        html += "</table>";
        return html;
    }

    // ── detect_anomalies ─────────────────────────────────────────────────────
    if (toolName == "detect_anomalies") {
        const int scanned   = obj["scanned"].toInt();
        const QJsonArray findings = obj["findings"].toArray();
        if (findings.isEmpty()) {
            return QString("<span style='color:#3fb950;'>✅ No anomalies detected (%1 maps scanned)</span>").arg(scanned);
        }
        QString html = QString("<b style='color:#d29922;'>⚠ %1 anomaly(s) across %2 map(s)</b><br>")
            .arg(findings.size()).arg(scanned);
        for (const auto &fv : findings) {
            QJsonObject f = fv.toObject();
            const QString type   = f["type"].toString();
            const QString mapN   = f["map"].toString();
            const QString detail = f["detail"].toString();
            QString icon = (type == "flat_map") ? "📉" : (type == "outlier_cells") ? "📊" : "⚡";
            html += QString("%1 <b style='color:#e6edf3;'>%2</b>: <span style='color:#8b949e;'>%3</span><br>")
                .arg(icon, mapN.toHtmlEscaped(), detail.toHtmlEscaped());
        }
        return html;
    }

    // ── validate_map_changes ─────────────────────────────────────────────────
    if (toolName == "validate_map_changes") {
        const QString mapN  = obj["map"].toString();
        const bool safe     = obj["safe"].toBool();
        const int viols     = obj["violations"].toInt();
        const int checked   = obj["checked_cells"].toInt();
        const QString bounds = obj["bounds_applied"].toString();
        if (safe) {
            return QString("<span style='color:#3fb950;'>✅ <b>%1</b> — %2 cells all within safe range</span>%3")
                .arg(mapN.toHtmlEscaped()).arg(checked)
                .arg(bounds.isEmpty() ? "" : QString(" <span style='color:#8b949e;font-size:7pt;'>(%1)</span>").arg(bounds.toHtmlEscaped()));
        }
        const QJsonArray vlist = obj["violation_list"].toArray();
        QString html = QString("<b style='color:#f85149;'>⚠ <span style='color:#e6edf3;'>%1</span> — %2 violation(s)</b><br>")
            .arg(mapN.toHtmlEscaped()).arg(viols);
        int shown = 0;
        for (const auto &v : vlist) {
            if (++shown > 5) break;
            QJsonObject vi = v.toObject();
            html += QString("<span style='color:#8b949e;font-size:8pt;'>[%1,%2] %3 — %4</span><br>")
                .arg(vi["row"].toInt()).arg(vi["col"].toInt())
                .arg(vi["value"].toDouble(), 0, 'g', 5)
                .arg(vi["reason"].toString().toHtmlEscaped());
        }
        if (viols > 5) html += QString("<span style='color:#8b949e;font-size:7pt;'>…and %1 more</span>").arg(viols - 5);
        return html;
    }

    // ── summarize_all_differences ────────────────────────────────────────────
    if (toolName == "summarize_all_differences") {
        const int mapsCh = obj["maps_changed"].toInt();
        const int cellsCh = obj["cells_changed"].toInt();
        const QJsonArray maps = obj["maps"].toArray();
        if (mapsCh == 0)
            return "<span style='color:#8b949e;'>📋 No maps modified from original</span>";
        QString html = QString("<b style='color:#58a6ff;'>📋 %1 map(s) modified</b> (%2 cells total)<br>")
            .arg(mapsCh).arg(cellsCh);
        html += "<table style='font-size:8pt;margin-top:4px;'>"
                "<tr style='color:#8b949e;'><td>Map</td><td>Cells</td><td>Min Δ</td><td>Max Δ</td><td>Unit</td></tr>";
        int shown = 0;
        for (const auto &mv : maps) {
            if (++shown > 10) break;
            QJsonObject m = mv.toObject();
            const double minD = m["min_delta"].toDouble(), maxD = m["max_delta"].toDouble();
            const QString col = (minD < 0 && maxD > 0) ? "#d29922" : (maxD > 0) ? "#3fb950" : "#f85149";
            html += QString("<tr><td style='color:#e6edf3;padding-right:8px;'>%1</td>"
                "<td style='color:#8b949e;padding-right:8px;'>%2</td>"
                "<td style='color:%3;'>%4</td>"
                "<td style='color:%3;'>%5</td>"
                "<td style='color:#8b949e;'>%6</td></tr>")
                .arg(m["map"].toString().toHtmlEscaped())
                .arg(m["cells_changed"].toInt())
                .arg(col)
                .arg(minD, 0, 'g', 4).arg(maxD, 0, 'g', 4)
                .arg(m["unit"].toString().toHtmlEscaped());
        }
        if (mapsCh > 10)
            html += QString("<tr><td colspan='5' style='color:#8b949e;'>…and %1 more maps</td></tr>").arg(mapsCh - 10);
        html += "</table>";
        return html;
    }

    // ── log_dyno_result ──────────────────────────────────────────────────────
    if (toolName == "log_dyno_result") {
        const double pwr  = obj["peak_power"].toDouble();
        const QString pu  = obj["power_unit"].toString();
        const double tq   = obj["peak_torque"].toDouble();
        const int rpm     = obj["rpm"].toInt();
        const int runNum  = obj["run_number"].toInt();
        const QString cmp = obj["comparison"].toString();
        QString html = QString("<b style='color:#58a6ff;'>🏁 Dyno run #%1 logged</b><br>").arg(runNum) +
            QString("<span style='font-size:11pt;font-weight:bold;color:#e6edf3;'>%1 %2</span>"
                    " <span style='color:#8b949e;'>|</span> "
                    "<span style='font-size:11pt;font-weight:bold;color:#e6edf3;'>%3 Nm</span>")
            .arg(pwr, 0, 'f', 1).arg(pu.toHtmlEscaped()).arg(tq, 0, 'f', 1);
        if (rpm > 0)
            html += QString(" <span style='color:#8b949e;font-size:8pt;'>@ %1 RPM</span>").arg(rpm);
        if (!cmp.isEmpty())
            html += QString("<br><span style='color:#3fb950;font-size:8pt;'>%1</span>").arg(cmp.toHtmlEscaped());
        return html;
    }

    // ── get_tuning_notes ─────────────────────────────────────────────────────
    if (toolName == "get_tuning_notes") {
        const QJsonArray entries = obj["entries"].toArray();
        const int total = obj["total_log_entries"].toInt();
        if (entries.isEmpty())
            return "<span style='color:#8b949e;'>📋 No tuning log entries yet</span>";
        QString html = QString("<b style='color:#58a6ff;'>📋 Tuning log</b> (%1 total)<br>")
            .arg(total);
        for (const auto &ev : entries) {
            QJsonObject e = ev.toObject();
            const QString cat = e["category"].toString();
            const QString ts  = e.contains("timestamp") ?
                QDateTime::fromString(e["timestamp"].toString(), Qt::ISODate).toString("dd.MM HH:mm") : "";
            const QString msg = e["message"].toString();
            const QString mapN = e["map"].toString();
            QString icon = (cat == "modification") ? "✏" : (cat == "recipe") ? "⚡" :
                           (cat == "anomaly") ? "⚠" : (cat == "dyno") ? "🏁" : "📝";
            QString catColor = (cat == "modification") ? "#58a6ff" : (cat == "recipe") ? "#d29922" :
                               (cat == "anomaly") ? "#f85149" : (cat == "dyno") ? "#3fb950" : "#8b949e";
            html += QString("<span style='color:#484f58;font-size:7pt;'>%1</span> "
                            "%2 <span style='color:%3;font-size:7pt;'>%4</span> ")
                .arg(ts.toHtmlEscaped(), icon, catColor, cat.toHtmlEscaped());
            if (!mapN.isEmpty())
                html += QString("<b style='color:#e6edf3;'>%1</b>: ").arg(mapN.toHtmlEscaped());
            html += QString("<span style='color:#c9d1d9;'>%1</span><br>").arg(msg.left(100).toHtmlEscaped());
        }
        return html;
    }

    // ── append_tuning_note / log success ─────────────────────────────────────
    if (toolName == "append_tuning_note") {
        if (obj["success"].toBool())
            return "<span style='color:#3fb950;'>📝 Note logged to tuning history</span>";
    }

    // ── evaluate_map_expression ──────────────────────────────────────────────
    if (toolName == "evaluate_map_expression") {
        if (obj["success"].toBool()) {
            return QString("<span style='color:#3fb950;'>✅ Expression applied to <b>%1</b>: "
                           "%2 of %3 cells changed</span>")
                .arg(obj["map"].toString().toHtmlEscaped())
                .arg(obj["changed_cells"].toInt())
                .arg(obj["total_cells"].toInt());
        }
    }

    // ── apply_delta_to_rom ───────────────────────────────────────────────────
    if (toolName == "apply_delta_to_rom") {
        if (obj["success"].toBool()) {
            const QJsonArray maps = obj["maps"].toArray();
            QString html = QString("<span style='color:#3fb950;'>✅ Applied delta from <b>%1</b>: %2 maps</span>")
                .arg(obj["source"].toString().toHtmlEscaped()).arg(obj["copied"].toInt());
            if (!maps.isEmpty()) {
                html += "<br><span style='color:#8b949e;font-size:8pt;'>";
                QStringList names;
                for (const auto &mv : maps) names << mv.toString();
                html += names.join(", ").toHtmlEscaped() + "</span>";
            }
            return html;
        }
    }

    // ── undo_with_reason ─────────────────────────────────────────────────────
    if (toolName == "undo_with_reason") {
        if (obj["success"].toBool()) {
            return QString("<span style='color:#3fb950;'>↩ Rolled back to <b>%1</b></span><br>"
                           "<span style='color:#8b949e;font-size:8pt;'>%2</span>")
                .arg(obj["restored_to"].toString().toHtmlEscaped())
                .arg(obj["reason"].toString().toHtmlEscaped());
        }
    }

    // ── describe_related_maps ────────────────────────────────────────────────
    if (toolName == "get_related_maps") {
        const QJsonArray related = obj["related"].toArray();
        const QString mapN = obj["map"].toString();
        if (related.isEmpty())
            return QString("<span style='color:#8b949e;'>No related maps found for <b>%1</b></span>").arg(mapN.toHtmlEscaped());
        QString html = QString("<b style='color:#58a6ff;'>🔗 %1</b> — %2 related map(s)<br>")
            .arg(mapN.toHtmlEscaped()).arg(related.size());
        for (const auto &rv : related) {
            QJsonObject r = rv.toObject();
            html += QString("&nbsp;&nbsp;<span style='color:#c9d1d9;'>%1</span>"
                            " <span style='color:#8b949e;font-size:7pt;'>%2</span><br>")
                .arg(r["name"].toString().toHtmlEscaped())
                .arg(r["reason"].toString().toHtmlEscaped());
        }
        return html;
    }

    return {};  // No custom renderer — caller will use plain text fallback
}

// ── Human-friendly tool call summary ─────────────────────────────────────────

static QString friendlyToolCall(const QString &name, const QJsonObject &input)
{
    struct ToolLabel { const char *tool; const char *icon; const char *label; const char *field; };
    static const ToolLabel labels[] = {
        {"search_maps",           "🔍", "Searching maps:",            "query"},
        {"confidence_search",     "🔍", "Searching with scores:",     "query"},
        {"get_map_values",        "📋", "Reading map values:",        "map_name"},
        {"get_map_info",          "ℹ",  "Getting map info:",          "map_name"},
        {"get_map_statistics",    "📊", "Computing statistics:",      "map_name"},
        {"describe_map_shape",    "📊", "Analyzing shape:",           "map_name"},
        {"get_related_maps",      "🔗", "Finding related maps:",      "map_name"},
        {"identify_map_purpose",  "🎯", "Identifying purpose:",       "map_name"},
        {"validate_map_changes",  "✅", "Validating proposed values:","map_name"},
        {"detect_anomalies",      "⚠",  "Scanning for anomalies:",    "map_name"},
        {"evaluate_map_expression","⚡","Applying expression:",       "map_name"},
        {"compare_map_values",    "↔",  "Comparing to original:",     "map_name"},
        {"compare_with_linked_rom","↔", "Comparing to linked ROM:",   "map_name"},
        {"summarize_all_differences","📋","Summarizing all changes:",  ""},
        {"get_all_changes_summary","📋","Getting change summary:",    ""},
        {"batch_modify_maps",     "⚡", "Batch modification:",        "reason"},
        {"batch_zero_maps",       "⚡", "Batch zero:",                "pattern"},
        {"set_map_values",        "✏",  "Writing map values:",        "map_name"},
        {"zero_map",              "✏",  "Zeroing map:",               "map_name"},
        {"fill_map",              "✏",  "Filling map:",               "map_name"},
        {"scale_map_values",      "✏",  "Scaling map:",               "map_name"},
        {"restore_map",           "↩",  "Restoring map:",             "map_name"},
        {"smooth_map",            "✏",  "Smoothing map:",             "map_name"},
        {"apply_delta_to_rom",    "⚡", "Applying delta from ROM",    ""},
        {"append_tuning_note",    "📝", "Logging note:",              "message"},
        {"get_tuning_notes",      "📋", "Reading tuning log",         ""},
        {"log_dyno_result",       "🏁", "Logging dyno result",        ""},
        {"undo_with_reason",      "↩",  "Rolling back:",              "reason"},
        {"snapshot_version",      "💾", "Creating snapshot:",         "label"},
        {"restore_version",       "↩",  "Restoring version",          ""},
        {"list_maps",             "📋", "Listing all maps",           ""},
        {"get_project_info",      "ℹ",  "Getting project info",       ""},
        {"list_linked_roms",      "🔗", "Listing linked ROMs",        ""},
        {"select_target_rom",     "🎯", "Switching target ROM",       ""},
        {nullptr, nullptr, nullptr, nullptr}
    };

    for (const ToolLabel *tl = labels; tl->tool; ++tl) {
        if (name == tl->tool) {
            QString summary = QString("<span style='color:#58a6ff;'>%1</span>"
                                      "<span style='color:#8b949e;'> %2</span>")
                .arg(QString::fromUtf8(tl->icon))
                .arg(QString::fromUtf8(tl->label));
            if (tl->field[0]) {
                QString val = input[tl->field].toString();
                if (val.isEmpty() && QString(tl->field) == "map_name")
                    val = input["name"].toString();
                if (!val.isEmpty())
                    summary += QString(" <b style='color:#e6edf3;'>%1</b>").arg(val.left(60).toHtmlEscaped());
            }
            return summary;
        }
    }
    // Fallback: show raw name
    return QString("<span style='color:#8b949e;'>🔧 %1</span>").arg(name.toHtmlEscaped());
}

// ── Constructor ────────────────────────────────────────────────────────────────

AIAssistant::AIAssistant(QWidget *parent) : QWidget(parent)
{
    // ── Provider configs ─────────────────────────────────────────────────────
    // Last field is the compatibility tier: 0 green (native, best), 1 amber
    // (OpenAI reference, good), 2 red (third-party / local, limited).
    m_providerConfigs = {
        {"claude",    "Claude (Anthropic)",    "",                                                         "claude-sonnet-4-6",         true,  0},
        {"openai",    "OpenAI (GPT-4o)",       "https://api.openai.com/v1",                                "gpt-4o",                    false, 1},
        {"qwen",      "Qwen (Alibaba)",        "https://dashscope.aliyuncs.com/compatible-mode/v1",        "qwen-plus",                 false, 2},
        {"deepseek",  "DeepSeek",              "https://api.deepseek.com/v1",                              "deepseek-v4-flash",         false, 2},
        {"gemini",    "Gemini (Google)",       "https://generativelanguage.googleapis.com/v1beta/openai/", "gemini-2.0-flash",          false, 2},
        {"groq",      "Groq",                  "https://api.groq.com/openai/v1",                           "llama-3.3-70b-versatile",   false, 2},
        {"ollama",    "Ollama (local)",        "http://localhost:11434/v1",                                "llama3.2",                  false, 2},
        {"lmstudio",  "LM Studio (local)",     "http://localhost:1234/v1",                                 "local-model",               false, 2},
        {"custom",    "Custom OpenAI-compat",  "",                                                         "",                          false, 2},
    };

    setStyleSheet("AIAssistant { background:#0d1117; }");

    auto *masterLayout = new QVBoxLayout(this);
    masterLayout->setContentsMargins(0, 0, 0, 0);
    masterLayout->setSpacing(0);

    // ── Header v2 ────────────────────────────────────────────────────────────
    // Only three controls in the header, left-to-right:
    //   • Claude orb + "Claude" label — identity
    //   • Permission-mode pill — the one piece of state the user actually cares
    //     about, rendered as an unambiguous labelled chip
    //   • ⋯ overflow menu — everything else (logbook, verbose, clear, settings)
    // A dedicated banner below the header carries setup problems so we never
    // have to cram "Set API Key" into the button row.
    m_header = new QWidget(this);
    m_header->setFixedHeight(44);
    m_header->setStyleSheet(
        "background:#161b22; border-bottom:1px solid #30363d;");

    auto *headerLay = new QHBoxLayout(m_header);
    headerLay->setContentsMargins(12, 6, 8, 6);
    headerLay->setSpacing(8);

    m_orbWidget = new ClaudeOrb(26, m_header);
    headerLay->addWidget(m_orbWidget);

    auto *titleLbl = new QLabel(tr("Claude"), m_header);
    titleLbl->setStyleSheet(
        "color:#e6edf3; font-weight:bold; font-size:11pt; background:transparent;");
    headerLay->addWidget(titleLbl);
    headerLay->addStretch();

    // Hidden combos remain for legacy code that reads m_providerCombo /
    // m_projectCombo, but they're never shown — provider/model are managed
    // exclusively via the Settings menu entry.
    m_providerCombo = new QComboBox(m_header);
    for (const ProviderConfig &pc : m_providerConfigs)
        m_providerCombo->addItem(pc.label);
    m_providerCombo->hide();
    m_projectCombo = new QComboBox(m_header);
    m_projectCombo->hide();

    // Permission-mode pill — bigger than before (32px tall, ~80px wide) so it
    // reads as a stateful chip rather than a cramped icon. applyPermissionMode
    // fills in the icon/label/colours.
    m_permissionBtn = new QPushButton(m_header);
    m_permissionBtn->setFixedHeight(32);
    m_permissionBtn->setMinimumWidth(82);
    m_permissionBtn->setCursor(Qt::PointingHandCursor);
    m_permissionBtn->setIconSize(QSize(14, 14));
    headerLay->addWidget(m_permissionBtn);

    // ⋯ overflow menu — single 32×32 square carrying every secondary action.
    m_menuBtn = new QPushButton(m_header);
    m_menuBtn->setFixedSize(32, 32);
    m_menuBtn->setCursor(Qt::PointingHandCursor);
    m_menuBtn->setToolTip(tr("More actions"));
    m_menuBtn->setText(QString::fromUtf8("\xE2\x8B\xAF"));   // ⋯
    m_menuBtn->setStyleSheet(
        "QPushButton { background:#21262d; color:#c9d1d9; border:1px solid #30363d; "
        "              border-radius:6px; font-size:13pt; padding:0 0 6px 0; }"
        "QPushButton:hover { background:#30363d; border-color:#58a6ff; }"
        "QPushButton::menu-indicator { image:none; width:0; }");
    auto *menu = new QMenu(m_menuBtn);
    menu->setStyleSheet(
        "QMenu { background:#161b22; color:#c9d1d9; border:1px solid #30363d; "
        "        border-radius:6px; padding:4px; }"
        "QMenu::item { padding:6px 14px 6px 28px; border-radius:4px; }"
        "QMenu::item:selected { background:#1f3f6a; color:#e6edf3; }"
        "QMenu::separator { height:1px; background:#30363d; margin:4px 6px; }"
        "QMenu::indicator { width:14px; height:14px; left:8px; }");

    m_actSettings = menu->addAction(tr("API key & provider…"));
    m_actLogbook  = menu->addAction(tr("Tuning logbook & dyno history…"));
    m_actVerbose  = menu->addAction(tr("Verbose mode"));
    m_actVerbose->setCheckable(true);
    menu->addSeparator();
    m_actClear    = menu->addAction(tr("Clear conversation"));

    m_menuBtn->setMenu(menu);
    headerLay->addWidget(m_menuBtn);

    masterLayout->addWidget(m_header);

    // ── Setup banner (under header) ──────────────────────────────────────────
    // Surfaces setup problems like a missing API key without fighting the
    // header buttons for space. Shown/hidden by refreshSetupBanner().
    m_setupBanner = new QWidget(this);
    m_setupBanner->setStyleSheet(
        "background:#3d1f1f; border-bottom:1px solid #6e2525;");
    m_setupBanner->hide();
    auto *bannerLay = new QHBoxLayout(m_setupBanner);
    bannerLay->setContentsMargins(14, 8, 10, 8);
    bannerLay->setSpacing(10);
    auto *bannerLbl = new QLabel(
        tr("No API key configured. Set one to start using the AI assistant."),
        m_setupBanner);
    bannerLbl->setStyleSheet("color:#ffb3b3; font-size:9pt; background:transparent;");
    bannerLbl->setWordWrap(true);
    auto *bannerBtn = new QPushButton(tr("Set API key"), m_setupBanner);
    bannerBtn->setCursor(Qt::PointingHandCursor);
    bannerBtn->setStyleSheet(
        "QPushButton { background:#1f6feb; color:#fff; border:none;"
        "              border-radius:5px; padding:5px 12px; font-weight:bold; font-size:9pt; }"
        "QPushButton:hover { background:#388bfd; }");
    bannerLay->addWidget(bannerLbl, 1);
    bannerLay->addWidget(bannerBtn);
    connect(bannerBtn, &QPushButton::clicked, this, &AIAssistant::onSettingsClicked);
    masterLayout->addWidget(m_setupBanner);

    // ── Welcome / Chat stacked widget ─────────────────────────────────────────
    m_stack = new QStackedWidget(this);
    m_stack->setStyleSheet("background:#0d1117;");

    // Page 0: Welcome
    auto *welcomeOuter = new WelcomeWidget();
    m_welcomeWidget = welcomeOuter;
    connect(welcomeOuter, &WelcomeWidget::chipClicked, this, [this](const QString &text){
        m_input->setPlainText(text);
        onSend();
    });
    m_stack->addWidget(welcomeOuter);

    // Page 1: Chat scroll area
    m_chatScroll = new QScrollArea();
    m_chatScroll->setWidgetResizable(true);
    m_chatScroll->setFrameShape(QFrame::NoFrame);
    m_chatScroll->setStyleSheet("QScrollArea { background:#0d1117; border:none; }");
    m_chatScroll->verticalScrollBar()->setStyleSheet(
        "QScrollBar:vertical { background:#0d1117; width:5px; border:none; }"
        "QScrollBar::handle:vertical { background:#30363d; border-radius:2px; min-height:24px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }");

    m_chatContainer = new QWidget();
    m_chatContainer->setStyleSheet("background:#0d1117;");
    m_chatLayout = new QVBoxLayout(m_chatContainer);
    m_chatLayout->setContentsMargins(12, 16, 12, 8);
    m_chatLayout->setSpacing(12);
    m_chatLayout->addStretch();

    m_chatScroll->setWidget(m_chatContainer);
    m_stack->addWidget(m_chatScroll);

    masterLayout->addWidget(m_stack, 1);

    // ── Typing indicator ──────────────────────────────────────────────────────
    m_typingRow = new QWidget(this);
    m_typingRow->setStyleSheet("background:#0d1117;");
    m_typingRow->setFixedHeight(28);
    m_typingRow->setVisible(false);

    auto *typingLay = new QHBoxLayout(m_typingRow);
    typingLay->setContentsMargins(16, 4, 16, 4);
    typingLay->setSpacing(8);

    auto *smallOrb = new ClaudeOrb(16, m_typingRow);
    typingLay->addWidget(smallOrb);

    m_typingDots = new QLabel("●  ●  ●", m_typingRow);
    m_typingDots->setStyleSheet("color:#1f6feb; font-size:9pt; background:transparent; letter-spacing:2px;");
    typingLay->addWidget(m_typingDots);

    m_statusLabel = new QLabel(m_typingRow);
    m_statusLabel->setStyleSheet("color:#8b949e; font-size:7pt; background:transparent;");
    typingLay->addWidget(m_statusLabel, 1);

    masterLayout->addWidget(m_typingRow);

    // Typing animation timer
    m_typingTimer = new QTimer(this);
    m_typingTimer->setInterval(500);
    connect(m_typingTimer, &QTimer::timeout, this, [this]{
        m_typingPhase = (m_typingPhase + 1) % 4;
        const char *frames[] = {"●  ○  ○", "●  ●  ○", "●  ●  ●", "○  ●  ●"};
        m_typingDots->setText(frames[m_typingPhase]);
    });

    // ── Input area ────────────────────────────────────────────────────────────
    auto *inputArea = new QWidget(this);
    inputArea->setStyleSheet(
        "background:#161b22; border-top:1px solid #30363d;");

    auto *inputLay = new QHBoxLayout(inputArea);
    inputLay->setContentsMargins(10, 8, 10, 8);
    inputLay->setSpacing(8);

    m_input = new QPlainTextEdit(inputArea);
    m_input->setPlaceholderText(tr("Ask about this ECU…  (Ctrl+Enter to send)"));
    m_input->setStyleSheet(
        "QPlainTextEdit { background:#0d1117; color:#e6edf3; "
        "  border:1px solid #30363d; border-radius:8px; "
        "  padding:8px 10px; font-size:10pt; }"
        "QPlainTextEdit:focus { border-color:#1f6feb; }");
    QFontMetrics fm(m_input->font());
    m_input->setMinimumHeight(fm.lineSpacing() * 2 + 24);
    m_input->setMaximumHeight(fm.lineSpacing() * 5 + 24);
    inputLay->addWidget(m_input, 1);

    // Send button — arrow-up icon style
    m_sendBtn = new QPushButton("↑", inputArea);
    m_sendBtn->setFixedSize(40, 40);
    m_sendBtn->setStyleSheet(
        "QPushButton { background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "  stop:0 #388bfd,stop:1 #1f6feb); color:#fff; border-radius:20px; "
        "  font-size:16pt; font-weight:bold; border:none; }"
        "QPushButton:hover { background:#58a6ff; }"
        "QPushButton:disabled { background:#21262d; color:#484f58; }");
    m_sendBtn->setToolTip(tr("Send  (Ctrl+Enter)"));
    inputLay->addWidget(m_sendBtn, 0, Qt::AlignBottom);

    // Cancel / Stop button — shown in place of send when working
    m_cancelBtn = new QPushButton();
    m_cancelBtn->setText(tr("Stop"));
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setStyleSheet(
        "QPushButton{background:#da3633;color:white;border:none;border-radius:14px;"
        "min-width:28px;min-height:28px;max-width:28px;max-height:28px;font-size:10pt;font-weight:bold}"
        "QPushButton:hover{background:#f85149}");
    m_cancelBtn->hide();
    inputLay->addWidget(m_cancelBtn, 0, Qt::AlignBottom);

    masterLayout->addWidget(inputArea);

    // ── Executor ──────────────────────────────────────────────────────────────
    m_executor = new AIToolExecutor(this);
    connect(m_executor, &AIToolExecutor::projectModified,
            this,        &AIAssistant::projectModified);

    // ── Connections ───────────────────────────────────────────────────────────
    connect(m_sendBtn,       &QPushButton::clicked,
            this,             &AIAssistant::onSend);
    connect(m_cancelBtn,     &QPushButton::clicked,
            this,             &AIAssistant::onCancel);
    connect(m_providerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,             &AIAssistant::onProviderChanged);
    connect(m_projectCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int){ buildSystemPrompt(); });

    // Overflow-menu actions — one owner per entry keeps the header free of
    // connect boilerplate for every new tool we add.
    connect(m_actSettings, &QAction::triggered, this, &AIAssistant::onSettingsClicked);
    connect(m_actLogbook,  &QAction::triggered, this, [this](){ showLogbookPanel(); });
    connect(m_actClear,    &QAction::triggered, this, &AIAssistant::onClearChat);
    connect(m_actVerbose,  &QAction::toggled,   this, [this](bool on){ setVerboseMode(on); });

    connect(m_permissionBtn, &QPushButton::clicked,
            this, [this](){ cyclePermissionMode(); });
    connect(&AppConfig::instance(), &AppConfig::aiPermissionModeChanged,
            this, [this](AppConfig::PermissionMode m){ applyPermissionMode(m); });

    m_input->installEventFilter(this);

    loadSettings();
    loadUserRecipes();
    applyPermissionMode(AppConfig::instance().aiPermissionMode);
    refreshSetupBanner();
    checkWelcome();
}

// ── Welcome / typing helpers ──────────────────────────────────────────────────

void AIAssistant::checkWelcome()
{
    // Welcome is shown only before the first user message of the session. We
    // used to use m_chatLayout->count() as a proxy for "is the chat empty",
    // but tool-only assistant turns add zero bubbles to the layout, so the
    // heuristic flipped back to the welcome page mid-conversation. Tracking
    // an explicit session bool removes the guesswork.
    m_stack->setCurrentIndex(m_sessionStarted ? 1 : 0);
}

void AIAssistant::manageContext()
{
    // Trim history while preserving:
    //   1. The most recent User message (the active question — without it the
    //      model loses the task and tends to emit a generic greeting; that was
    //      the long-standing "I am your AI assistant…" mid-chat regression).
    //   2. ToolUse/ToolResult pairing — the API rejects orphans, and dropping
    //      half of a pair causes 400 errors mid-loop.
    //
    // Limits intentionally generous: modern models handle 50k+ comfortably and
    // the previous 20k cap was the main reason the agent loop went sideways.

    static constexpr int kMaxMessages = 40;
    static constexpr int kMaxBytes    = 80000;
    static constexpr int kMinKeep     = 6;

    auto sizeOf = [](const AIMessage &m) {
        int n = m.content.size() + m.toolResultJson.size() + m.toolInputJson.size();
        for (const AIToolCall &call : m.toolCalls)
            n += call.id.size() + call.name.size() + call.argumentsJson.size();
        return n;
    };

    int totalSize = 0;
    for (const auto &m : m_history) totalSize += sizeOf(m);

    // Pass 1: shrink overlarge tool results in older turns. Keep the last 4
    // intact since the model is actively reasoning about them.
    int toolResultsSeen = 0;
    for (int i = m_history.size() - 1; i >= 0; --i) {
        if (m_history[i].role != AIMessage::ToolResult) continue;
        ++toolResultsSeen;
        if (toolResultsSeen > 4 && m_history[i].toolResultJson.size() > 600) {
            totalSize -= m_history[i].toolResultJson.size();
            m_history[i].toolResultJson = m_history[i].toolResultJson.left(600)
                + "\n... (older tool result truncated)";
            totalSize += m_history[i].toolResultJson.size();
        }
    }

    if (m_history.size() <= kMinKeep && totalSize <= kMaxBytes) return;

    // Locate the index of the most-recent User message. We must not drop
    // anything from there onward, otherwise the model loses the task.
    int lastUserIdx = -1;
    for (int i = m_history.size() - 1; i >= 0; --i) {
        if (m_history[i].role == AIMessage::User) { lastUserIdx = i; break; }
    }

    // Pass 2: drop oldest messages, but stop before the active task and never
    // split a ToolUse from its matching ToolResult.
    while ((m_history.size() > kMaxMessages || totalSize > kMaxBytes)
           && m_history.size() > kMinKeep)
    {
        if (lastUserIdx == 0) break;          // active task is already at the front
        const AIMessage &front = m_history.first();
        // Tool calls from a single provider response are followed by one or
        // more tool results.  Drop the complete exchange together so an API
        // never receives an orphaned result.
        if (front.role == AIMessage::ToolUse) {
            totalSize -= sizeOf(m_history[0]);
            m_history.removeFirst();
            if (lastUserIdx >= 0) --lastUserIdx;
            while (!m_history.isEmpty() && m_history.first().role == AIMessage::ToolResult) {
                totalSize -= sizeOf(m_history.first());
                m_history.removeFirst();
                if (lastUserIdx >= 0) --lastUserIdx;
            }
        } else {
            totalSize -= sizeOf(front);
            m_history.removeFirst();
            if (lastUserIdx >= 0) --lastUserIdx;
        }
    }
}

void AIAssistant::abortWithError(const QString &msg)
{
    m_statusLabel->setText(tr("Error: ") + msg);
    m_streamingBubble = nullptr;
    showTyping(false);

    // Add error as assistant message so user can see it in chat
    appendMessage("assistant", "⚠ " + msg);
    transitionTo(AssistantState::IDLE);
}

void AIAssistant::showTyping(bool on)
{
    m_typingRow->setVisible(on);
    if (auto *orb = qobject_cast<ClaudeOrb*>(m_orbWidget))
        orb->setThinking(on);
    if (on)  m_typingTimer->start();
    else   { m_typingTimer->stop(); m_typingPhase = 0;
             m_typingDots->setText("●  ●  ●"); }
}

// ── Verbose mode ──────────────────────────────────────────────────────────────

void AIAssistant::setVerboseMode(bool on)
{
    m_verboseMode = on;
    if (m_actVerbose && m_actVerbose->isChecked() != on) {
        QSignalBlocker b(m_actVerbose);
        m_actVerbose->setChecked(on);
    }
    buildSystemPrompt();  // rebuild with/without verbose instructions
    saveSettings();
}

// ── Permission mode (Claude-Code style) ──────────────────────────────────────
//
// Three modes, cycled by the shield button in the header:
//   Ask         — current behavior, every write tool prompts an inline card
//   AutoAccept  — writes execute immediately and emit a "✓ applied" card
//   Plan        — writes are rejected with a synthetic tool result that asks
//                 the model to describe the change instead of performing it
//
// The mode is persisted in AppConfig so it survives session restarts.

void AIAssistant::applyPermissionMode(AppConfig::PermissionMode mode)
{
    if (!m_permissionBtn) return;

    // Chip = painted icon + text label. We paint the icon each call so the
    // glyph colour matches the chip's foreground in every mode.
    HeaderGlyph glyph = HeaderGlyph::Shield;
    QString label, tip, bg, border, fg, bgHover;

    switch (mode) {
    case AppConfig::PermissionMode::Ask:
        glyph  = HeaderGlyph::Shield;
        label  = tr("Ask");
        tip    = tr("Permission: Ask before each change\nClick to cycle \xE2\x86\x92 Auto");
        bg     = "#21262d"; border = "#30363d"; fg = "#c9d1d9"; bgHover = "#30363d";
        break;
    case AppConfig::PermissionMode::AutoAccept:
        glyph  = HeaderGlyph::Bolt;
        label  = tr("Auto");
        tip    = tr("Permission: Auto-accept edits\nClick to cycle \xE2\x86\x92 Plan");
        bg     = "#1a3a1f"; border = "#238636"; fg = "#3fb950"; bgHover = "#22532b";
        break;
    case AppConfig::PermissionMode::Plan:
        glyph  = HeaderGlyph::Clipboard;
        label  = tr("Plan");
        tip    = tr("Permission: Plan only \xE2\x80\x94 no changes will be applied\nClick to cycle \xE2\x86\x92 Ask");
        bg     = "#3a2a14"; border = "#9e6a03"; fg = "#d29922"; bgHover = "#4d3618";
        break;
    }

    m_permissionBtn->setIcon(QIcon(paintHeaderGlyph(glyph, QColor(fg), 12,
                                                    devicePixelRatioF())));
    m_permissionBtn->setText(" " + label);
    m_permissionBtn->setToolTip(tip);
    m_permissionBtn->setStyleSheet(QString(
        "QPushButton { background:%1; color:%2; border:1px solid %3;"
        "              border-radius:5px; padding:0 8px 0 6px;"
        "              font-size:8.5pt; font-weight:bold; text-align:center; }"
        "QPushButton:hover { background:%4; }")
        .arg(bg, fg, border, bgHover));
    // Let the chip size itself to its contents rather than claiming a fixed
    // minimum that wastes horizontal space in the header.
    m_permissionBtn->setMinimumWidth(0);
    m_permissionBtn->adjustSize();
}

void AIAssistant::cyclePermissionMode()
{
    auto &cfg = AppConfig::instance();
    AppConfig::PermissionMode next;
    switch (cfg.aiPermissionMode) {
    case AppConfig::PermissionMode::Ask:        next = AppConfig::PermissionMode::AutoAccept; break;
    case AppConfig::PermissionMode::AutoAccept: next = AppConfig::PermissionMode::Plan;       break;
    case AppConfig::PermissionMode::Plan:       next = AppConfig::PermissionMode::Ask;        break;
    }
    cfg.aiPermissionMode = next;
    cfg.save();
    emit cfg.aiPermissionModeChanged(next);
}

// ── User recipe library ───────────────────────────────────────────────────────
// Format: ~/.config/romHEX14/recipes.json (or AppConfigLocation equivalent)
// [
//   {"keyword":"no_egr","patterns":["AGR*"],"action":"zero","description":"Custom EGR off"},
//   ...
// ]

void AIAssistant::loadUserRecipes()
{
    m_userRecipes.clear();

    // Try standard app config location first, then home directory
    QStringList searchPaths;
    for (const QString &dir : QStandardPaths::standardLocations(QStandardPaths::AppConfigLocation))
        searchPaths << dir + "/recipes.json";
    searchPaths << QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/.romHEX14/recipes.json";

    for (const QString &path : searchPaths) {
        QFile f(path);
        if (!f.exists() || !f.open(QIODevice::ReadOnly)) continue;

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isArray()) continue;

        for (const auto &val : doc.array()) {
            QJsonObject obj = val.toObject();
            TuningRecipe r;
            r.keyword     = obj["keyword"].toString();
            r.action      = obj["action"].toString("zero");
            r.value       = obj["value"].toDouble(0);
            r.description = obj["description"].toString(r.keyword);
            for (const auto &p : obj["patterns"].toArray())
                r.patterns << p.toString();
            if (!r.keyword.isEmpty() && !r.patterns.isEmpty())
                m_userRecipes.append(r);
        }
        break;  // loaded first found file
    }
}

// ── Logbook panel ─────────────────────────────────────────────────────────────

void AIAssistant::showLogbookPanel()
{
    auto *dlg = new QDialog(this);
    dlg->setWindowTitle(tr("Tuning Logbook"));
    dlg->resize(700, 500);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setStyleSheet(
        "QDialog { background:#0d1117; color:#c9d1d9; }"
        "QTabWidget::pane { border:1px solid #30363d; background:#0d1117; }"
        "QTabBar::tab { background:#161b22; color:#8b949e; padding:6px 16px; "
        "  border:1px solid #30363d; border-bottom:none; }"
        "QTabBar::tab:selected { background:#0d1117; color:#e6edf3; }"
        "QTreeWidget, QListWidget { background:#0d1117; color:#c9d1d9; "
        "  border:1px solid #30363d; alternate-background-color:#0a0e14; }"
        "QTreeWidget::item, QListWidget::item { padding:4px 2px; }"
        "QTreeWidget::item:selected, QListWidget::item:selected { background:#1f3f6a; }"
        "QHeaderView::section { background:#161b22; color:#8b949e; "
        "  border:1px solid #30363d; padding:4px; font-size:8pt; }"
        "QLabel { color:#c9d1d9; background:transparent; }"
        "QPushButton { background:#21262d; color:#8b949e; border:1px solid #30363d; "
        "  border-radius:4px; padding:4px 12px; }"
        "QPushButton:hover { background:#30363d; color:#e6edf3; }");

    auto *mainLay = new QVBoxLayout(dlg);
    mainLay->setContentsMargins(12, 12, 12, 12);
    mainLay->setSpacing(8);

    auto *tabs = new QTabWidget(dlg);

    // ── Tab 1: Tuning Log ──────────────────────────────────────────────────────
    {
        auto *logTab = new QWidget();
        auto *lay = new QVBoxLayout(logTab);
        lay->setContentsMargins(4, 8, 4, 4);

        if (!m_project || m_project->tuningLog.isEmpty()) {
            auto *lbl = new QLabel(tr("No tuning log entries yet.\nThe AI logs notes here automatically when you ask it to use append_tuning_note."));
            lbl->setAlignment(Qt::AlignCenter);
            lbl->setStyleSheet("color:#8b949e; font-size:10pt;");
            lay->addWidget(lbl);
        } else {
            auto *tree = new QTreeWidget();
            tree->setColumnCount(4);
            tree->setHeaderLabels({tr("Time"), tr("Category"), tr("Map"), tr("Message")});
            tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
            tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
            tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
            tree->header()->setSectionResizeMode(3, QHeaderView::Stretch);
            tree->setAlternatingRowColors(true);
            tree->setRootIsDecorated(false);
            tree->setSortingEnabled(true);

            // Add entries in reverse-chronological order
            const auto &log = m_project->tuningLog;
            for (int i = log.size() - 1; i >= 0; --i) {
                const TuningLogEntry &e = log[i];
                auto *item = new QTreeWidgetItem({
                    e.timestamp.toString("dd.MM.yy HH:mm:ss"),
                    e.category,
                    e.mapName,
                    e.message
                });
                // Category color
                QString catColor = (e.category == "modification") ? "#58a6ff" :
                                   (e.category == "recipe")       ? "#d29922" :
                                   (e.category == "anomaly")      ? "#f85149" :
                                   (e.category == "observation")  ? "#3fb950" : "#8b949e";
                item->setForeground(1, QColor(catColor));
                tree->addTopLevelItem(item);
            }
            lay->addWidget(tree);
        }
        tabs->addTab(logTab, tr("📝 Tuning Log (%1)").arg(m_project ? m_project->tuningLog.size() : 0));
    }

    // ── Tab 2: Dyno History ───────────────────────────────────────────────────
    {
        auto *dynoTab = new QWidget();
        auto *lay = new QVBoxLayout(dynoTab);
        lay->setContentsMargins(4, 8, 4, 4);

        if (!m_project || m_project->dynoLog.isEmpty()) {
            auto *lbl = new QLabel(tr("No dyno runs logged yet.\nAsk the AI to log a dyno result after a power run."));
            lbl->setAlignment(Qt::AlignCenter);
            lbl->setStyleSheet("color:#8b949e; font-size:10pt;");
            lay->addWidget(lbl);
        } else {
            const auto &dynoLog = m_project->dynoLog;

            // Summary header
            const DynoResult &latest = dynoLog.last();
            double bestPwr = 0;
            for (const auto &r : dynoLog) bestPwr = qMax(bestPwr, r.peakPower);
            auto *summLbl = new QLabel(
                QString("<b style='font-size:14pt;'>%1 %2</b>"
                        "  <span style='color:#8b949e;font-size:9pt;'>latest</span>"
                        "  &nbsp;&nbsp;"
                        "<b style='font-size:14pt;'>%3 %2</b>"
                        "  <span style='color:#8b949e;font-size:9pt;'>best</span>"
                        "  &nbsp;&nbsp;"
                        "<span style='color:#8b949e;font-size:9pt;'>%4 run(s)</span>")
                .arg(latest.peakPower, 0, 'f', 1).arg(latest.powerUnit.toHtmlEscaped())
                .arg(bestPwr, 0, 'f', 1).arg(dynoLog.size()));
            summLbl->setTextFormat(Qt::RichText);
            summLbl->setStyleSheet("padding:8px 4px; background:#161b22; border-radius:6px;");
            lay->addWidget(summLbl);

            auto *tree = new QTreeWidget();
            tree->setColumnCount(6);
            tree->setHeaderLabels({tr("Run"), tr("Date"), tr("Power"), tr("Torque"), tr("RPM"), tr("Notes")});
            tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
            tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
            tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
            tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
            tree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
            tree->header()->setSectionResizeMode(5, QHeaderView::Stretch);
            tree->setAlternatingRowColors(true);
            tree->setRootIsDecorated(false);

            for (int i = dynoLog.size() - 1; i >= 0; --i) {
                const DynoResult &r = dynoLog[i];
                auto *item = new QTreeWidgetItem({
                    QString("#%1").arg(i + 1),
                    r.timestamp.toString("dd.MM.yy HH:mm"),
                    QString("%1 %2").arg(r.peakPower, 0, 'f', 1).arg(r.powerUnit),
                    r.peakTorque > 0 ? QString("%1 Nm").arg(r.peakTorque, 0, 'f', 0) : "-",
                    r.rpmAtPower > 0 ? QString("%1 RPM").arg(r.rpmAtPower) : "-",
                    r.notes
                });
                // Highlight best run
                if (qAbs(r.peakPower - bestPwr) < 0.1) {
                    item->setForeground(2, QColor("#ffd700"));
                    item->setToolTip(0, tr("Best run"));
                } else if (i > 0) {
                    double delta = r.peakPower - dynoLog[i-1].peakPower;
                    item->setForeground(2, QColor(delta >= 0 ? "#3fb950" : "#f85149"));
                }
                tree->addTopLevelItem(item);
            }
            lay->addWidget(tree, 1);
        }
        tabs->addTab(dynoTab, tr("🏁 Dyno Log (%1)").arg(m_project ? m_project->dynoLog.size() : 0));
    }

    // ── Tab 3: Recipe Library ─────────────────────────────────────────────────
    {
        auto *recTab = new QWidget();
        auto *lay = new QVBoxLayout(recTab);
        lay->setContentsMargins(4, 8, 4, 4);

        auto *tree = new QTreeWidget();
        tree->setColumnCount(3);
        tree->setHeaderLabels({tr("Keyword"), tr("Action"), tr("Patterns")});
        tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
        tree->setAlternatingRowColors(true);
        tree->setRootIsDecorated(false);

        // User recipes first
        if (!m_userRecipes.isEmpty()) {
            auto *userHdr = new QTreeWidgetItem(tree, {tr("User Recipes (from recipes.json)"), {}, {}});
            userHdr->setForeground(0, QColor("#3fb950"));
            userHdr->setFlags(Qt::ItemIsEnabled);
            for (const auto &r : m_userRecipes) {
                auto *item = new QTreeWidgetItem({r.keyword, r.action, r.patterns.join(", ")});
                item->setForeground(0, QColor("#58a6ff"));
                userHdr->addChild(item);
            }
            tree->expandItem(userHdr);
        }

        auto *builtinHdr = new QTreeWidgetItem(tree, {tr("Built-in Recipes"), {}, {}});
        builtinHdr->setForeground(0, QColor("#d29922"));
        builtinHdr->setFlags(Qt::ItemIsEnabled);
        // Show unique keywords (dedup)
        QSet<QString> seen;
        for (const auto &r : builtinRecipes()) {
            if (seen.contains(r.keyword)) continue;
            seen.insert(r.keyword);
            auto *item = new QTreeWidgetItem({r.keyword, r.action, r.patterns.join(", ")});
            item->setForeground(0, QColor("#c9d1d9"));
            builtinHdr->addChild(item);
        }
        tree->expandItem(builtinHdr);

        auto *hintLbl = new QLabel(tr("💡 Add custom recipes at <code>~/.romHEX14/recipes.json</code> — they take priority over built-ins."));
        hintLbl->setTextFormat(Qt::RichText);
        hintLbl->setStyleSheet("color:#8b949e; font-size:8pt; padding:4px;");
        lay->addWidget(tree, 1);
        lay->addWidget(hintLbl);

        tabs->addTab(recTab, tr("⚡ Recipes (%1 user + %2 built-in)")
            .arg(m_userRecipes.size()).arg(seen.size()));
    }

    mainLay->addWidget(tabs, 1);

    // Close button
    auto *closeLay = new QHBoxLayout();
    closeLay->addStretch();
    auto *closeBtn = new QPushButton(tr("Close"), dlg);
    closeBtn->setFixedWidth(80);
    connect(closeBtn, &QPushButton::clicked, dlg, &QDialog::accept);
    closeLay->addWidget(closeBtn);
    mainLay->addLayout(closeLay);

    dlg->exec();
}

// ── Event filter (Enter / Ctrl+Enter to send, Shift+Enter for newline) ───────

bool AIAssistant::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_input && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            if (ke->modifiers() & Qt::ShiftModifier) {
                // Shift+Enter: insert newline (let the text edit handle it)
                return false;
            }
            // Enter or Ctrl+Enter: send message
            onSend();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

// ── Public setters ────────────────────────────────────────────────────────────

void AIAssistant::setProject(Project *project)
{
    m_project = project;
    if (m_executor) m_executor->setProject(project);
    refreshProjectCombo();
    buildSystemPrompt();
}

void AIAssistant::refreshProjectCombo()
{
    m_projectCombo->blockSignals(true);
    m_projectCombo->clear();

    if (m_project) {
        m_projectCombo->addItem(m_project->fullTitle() + tr(" (Current)"));
        for (const ProjectVersion &v : m_project->versions)
            m_projectCombo->addItem(tr("Version: %1").arg(v.name));
    } else {
        m_projectCombo->addItem(tr("No project loaded"));
    }

    m_projectCombo->setCurrentIndex(0);
    m_projectCombo->blockSignals(false);
}

void AIAssistant::setAllProjects(const QVector<Project*> &projects)
{
    m_allProjects = projects;
}

void AIAssistant::setSelectedMap(const MapInfo &map)
{
    m_selectedMap    = map;
    m_hasSelectedMap = true;
    buildSystemPrompt();
}

void AIAssistant::retranslateUi()
{
    m_input->setPlaceholderText(tr("Ask about this ECU…  (Ctrl+Enter to send)"));
    if (m_menuBtn)  m_menuBtn->setToolTip(tr("More actions"));
    if (m_actSettings) m_actSettings->setText(tr("API key & provider…"));
    if (m_actLogbook)  m_actLogbook->setText(tr("Tuning logbook & dyno history…"));
    if (m_actVerbose)  m_actVerbose->setText(tr("Verbose mode"));
    if (m_actClear)    m_actClear->setText(tr("Clear conversation"));
    if (AppConfig::PermissionMode m = AppConfig::instance().aiPermissionMode; m_permissionBtn)
        applyPermissionMode(m);
    refreshSetupBanner();

    // Update header labels
    for (auto *lbl : m_header->findChildren<QLabel*>()) {
        if (lbl->font().bold() && lbl->font().pointSize() >= 10)
            lbl->setText(tr("Claude for RX14"));
        else if (lbl->styleSheet().contains("7pt"))
            lbl->setText(tr("ECU calibration assistant"));
    }

    // Rebuild welcome widget so all tr() strings are re-resolved
    if (m_welcomeWidget && m_stack) {
        bool wasVisible = (m_stack->currentWidget() == m_welcomeWidget);
        int idx = m_stack->indexOf(m_welcomeWidget);
        if (idx < 0) idx = 0;

        m_stack->removeWidget(m_welcomeWidget);
        delete m_welcomeWidget;

        auto *w = new WelcomeWidget();
        m_welcomeWidget = w;
        connect(w, &WelcomeWidget::chipClicked, this, [this](const QString &text){
            m_input->setPlainText(text);
            onSend();
        });
        m_stack->insertWidget(idx, w);
        if (wasVisible)
            m_stack->setCurrentIndex(idx);
    }

    buildSystemPrompt();
}

// ── System prompt ─────────────────────────────────────────────────────────────

void AIAssistant::buildSystemPrompt()
{
    QString projectTitle = m_project ? m_project->fullTitle() : "No project loaded";
    int romSize   = m_project ? m_project->currentData.size() : 0;
    int mapCount  = m_project ? m_project->maps.size() : 0;
    QString selMap = m_hasSelectedMap ? m_selectedMap.name : "none";

    // Determine which ROM version the user is asking about
    QString romVersion = "Current (working)";
    if (m_projectCombo && m_projectCombo->currentIndex() > 0 && m_project) {
        int vIdx = m_projectCombo->currentIndex() - 1;
        if (vIdx >= 0 && vIdx < m_project->versions.size())
            romVersion = m_project->versions[vIdx].name;
    }

    m_systemPrompt = QString(
        "You are an ECU tuning assistant. Use tools immediately — never just talk.\n\n"
        "Project: %1 | ROM version: %5 | ROM: %2 bytes | Maps: %3 | Selected: %4\n\n"
        "RULES:\n"
        "1. For ANY tuning/modification task: call search_maps or confidence_search first to locate maps, "
        "then call the appropriate write tool (batch_modify_maps, set_map_values, zero_map, etc.) immediately. "
        "Do NOT ask permission, do NOT wait — call the tool now. "
        "The host application gates writes via a permission mode and surfaces a confirmation card if needed.\n"
        "2. For map questions: inspect the project with list_maps or search_maps, then use get_map_values, "
        "get_map_info, get_axis_values, or describe_map_shape — never guess values or a map's use. "
        "For duplicate names, list the matching maps first and use the returned address/mapId as an exact selector "
        "(name '@0x...').\n"
        "3. NEVER say 'I am ready', 'I am your AI assistant', 'how can I help', introduce yourself, "
        "ask what the user wants, or list your capabilities. This applies on every turn, including after "
        "tool results — keep working on the task in the most recent user message until it is complete.\n"
        "4. WRITE PROTOCOL: Before each write tool call, output ≤2 sentences describing what you will "
        "change and why — then IMMEDIATELY call the write tool in that same response. "
        "Do NOT stop and wait. The system intercepts the tool call.\n"
        "5. After any modification: call append_tuning_note to log what changed and why, then summarize "
        "what was done in ≤2 sentences. Do not ask a follow-up question unless the user explicitly asked for one.\n"
        "6. Answers: ≤3 sentences unless explaining map data.\n"
        "7. If the most recent tool result contains an error, address it directly — never restart the conversation.\n"
        "8. Never claim that map data or tools are unavailable unless an attempted tool call returned that error. "
        "Do not invent A2L/XDF data, disassembler cross-references, routines, or datasets: they are not available "
        "through this assistant. State only what tool results establish and label any general ECU knowledge as general.\n\n"
    )
    .arg(projectTitle)
    .arg(romSize)
    .arg(mapCount)
    .arg(selMap)
    .arg(romVersion);

    // Verbose mode: instruct the AI to narrate its reasoning
    if (m_verboseMode) {
        m_systemPrompt +=
            "VERBOSE MODE ON: Before each tool call, briefly explain in one sentence what you are "
            "about to do and why. After getting a result, interpret it before proceeding. "
            "This helps the user follow your reasoning step by step.\n\n";
    }

    // Add tuning knowledge for write intents
    if (m_selectedTools.contains("batch_modify_maps")) {
        m_systemPrompt +=
            "Bosch ECU map patterns:\n"
            "- Catalyst/decat: KAT*, DKAT*, *DKAT*, OSCKAT*, DFC*KAT* (zero monitoring/diagnostic maps only, keep heating/temp maps)\n"
            "- DPF: DPF*, *DPF*, RUSS*, PARMON* (zero monitoring maps)\n"
            "- EGR: AGR*, *AGR*, DAGR*, EGR* (zero flow maps)\n"
            "- Lambda/O2: LSMON*, LSHFM*, LAMFA* (zero monitoring)\n"
            "- Speed limiter: VMAX*, VFIL* (set to max)\n"
            "- Rev limiter: NMAX*, NMOT_MAX* (increase)\n"
            "- Pops & bangs: SAK*, KFZWSA* (modify overrun maps)\n"
            "- AdBlue: SCR*, *SCR*, ADBLUE*, HARNST*, DOSMOD*\n\n"
            "IMPORTANT: Search multiple patterns for each task. Read descriptions to filter out unrelated maps.\n";
    }

    // Analysis/investigation tools context
    if (m_selectedTools.contains("describe_map_shape") || m_selectedTools.contains("detect_anomalies")) {
        m_systemPrompt +=
            "Analysis tools available:\n"
            "- describe_map_shape: topology analysis (monotonic/flat/peak/valley/cliff)\n"
            "- get_related_maps: find maps sharing axis signals or groups\n"
            "- identify_map_purpose: guess what a map does with confidence level\n"
            "- detect_anomalies: scan for flat maps, outliers, raw-limit cells\n"
            "- confidence_search: search with relevance scores\n\n";
    }
}

// ── State machine ──────────────────────────────────────────────────────────

void AIAssistant::transitionTo(AssistantState newState)
{
    m_state = newState;
    switch (newState) {
    case AssistantState::IDLE:
        m_sendBtn->setEnabled(true);
        m_sendBtn->show();
        m_cancelBtn->hide();
        m_statusLabel->clear();
        showTyping(false);
        break;
    case AssistantState::WORKING:
        m_sendBtn->setEnabled(false);
        m_sendBtn->hide();
        m_cancelBtn->show();
        break;
    case AssistantState::AWAITING_CONFIRMATION:
        m_sendBtn->setEnabled(false);
        m_cancelBtn->show();
        showTyping(false);
        break;
    }
}

void AIAssistant::onCancel()
{
    if (m_provider) m_provider->abort();
    if (m_requestTimeout) m_requestTimeout->stop();
    if (m_chunkTimer) m_chunkTimer->stop();
    m_streamingBubble = nullptr;
    m_pendingToolCalls.clear();
    showTyping(false);
    appendMessage("assistant", tr("Operation cancelled."));
    transitionTo(AssistantState::IDLE);
}

// ── Intent classification ─────────────────────────────────────────────────

QString AIAssistant::classifyIntent(const QString &msg)
{
    QString lower = msg.toLower();

    // Batch write recipes
    QStringList batchKeywords = {"decat", "cat off", "cat delete", "dpf off", "dpf delete",
        "dpf removal", "egr off", "egr delete", "egr removal", "pops and bangs", "pops & bangs",
        "crackle", "burble", "anti-lag", "speed limiter", "vmax", "rev limiter",
        "adblue", "lambda", "o2 off", "o2 delete", "nox off", "swirl flap",
        "start stop", "immo off", "torque monitor", "dtc", "fault code"};
    for (const auto &kw : batchKeywords)
        if (lower.contains(kw)) return "batch_write";

    // Dyno / logbook operations
    QStringList dynoKeywords = {"dyno", "power run", "horsepower", "bhp", "ps", "torque run",
        "log dyno", "dyno result", "run result", "power figure"};
    for (const auto &kw : dynoKeywords)
        if (lower.contains(kw)) return "dyno";

    // Analysis / investigation operations
    QStringList analyzeKeywords = {"analyze", "analyse", "anomal", "shape", "topology",
        "related map", "dependency", "purpose", "identify map", "what does this map",
        "safe", "validate", "sanity", "check values", "suspicious", "outlier"};
    for (const auto &kw : analyzeKeywords)
        if (lower.contains(kw)) return "analyze";

    // Expression editing
    QStringList exprKeywords = {"formula", "expression", "calculate", "apply math", "math operation",
        "multiply each", "add to each", "scale with formula"};
    for (const auto &kw : exprKeywords)
        if (lower.contains(kw)) return "expression";

    // Write / tuning operations
    QStringList writeKeywords = {"set", "change", "modify", "write", "increase", "decrease",
        "scale", "zero", "fill", "offset", "clamp", "restore", "adjust",
        "tune", "tuning", "optimize", "optimise", "improve", "fix", "correct",
        "disable", "enable", "calibrat", "suggest", "recommend",
        "what should", "how should", "should i", "can you change",
        "performance", "catalyst", "emissions", "o2 sensor", "oxygen"};
    for (const auto &kw : writeKeywords)
        if (lower.contains(kw)) return "write";

    // Compare operations
    QStringList compareKeywords = {"compare", "diff", "difference", "versus", "vs", "original",
        "what changed", "summarize changes", "apply delta"};
    for (const auto &kw : compareKeywords)
        if (lower.contains(kw)) return "compare";

    // Explain — pure knowledge, no tools
    QStringList explainKeywords = {"explain", "what is", "what does", "how does", "why", "meaning",
        "purpose", "describe", "tell me about", "help me understand"};
    for (const auto &kw : explainKeywords)
        if (lower.contains(kw)) return "explain";

    return "read"; // default: search/list/get
}

QStringList AIAssistant::toolsForCategory(const QString &cat)
{
    // Explanations about this project still need evidence from its maps.  Sending
    // a focused read set prevents the provider from substituting generic ECU
    // knowledge for the actual ROM data.
    if (cat == "explain")
        return {"list_maps", "search_maps", "confidence_search", "get_map_info",
                "get_map_values", "get_original_values", "get_axis_values",
                "get_map_statistics", "get_modified_maps", "get_project_info"};

    if (cat == "read")
        return {"list_maps", "search_maps", "confidence_search", "get_map_info", "get_map_values",
                "get_original_values", "get_map_statistics", "get_modified_maps", "get_project_info",
                "get_tuning_notes"};

    if (cat == "write")
        return {"search_maps", "confidence_search", "get_map_info", "get_map_values",
                "get_original_values", "get_map_statistics", "get_modified_maps",
                "identify_map_purpose", "describe_map_shape", "get_related_maps",
                "validate_map_changes", "detect_anomalies",
                "set_map_values", "zero_map", "scale_map_values", "fill_map",
                "offset_map_values", "batch_modify_maps", "evaluate_map_expression",
                "append_tuning_note", "get_tuning_notes"};

    if (cat == "batch_write")
        return {"search_maps", "confidence_search", "get_map_info", "identify_map_purpose",
                "get_map_values", "batch_modify_maps", "get_modified_maps", "append_tuning_note"};

    if (cat == "compare")
        return {"search_maps", "get_map_values", "get_original_values",
                "compare_map_values", "get_all_changes_summary", "summarize_all_differences",
                "compare_two_maps", "list_linked_roms", "compare_with_linked_rom",
                "apply_delta_to_rom"};

    if (cat == "analyze")
        return {"search_maps", "confidence_search", "describe_map_shape", "get_related_maps",
                "identify_map_purpose", "validate_map_changes", "detect_anomalies",
                "get_map_statistics", "get_map_info", "get_tuning_notes"};

    if (cat == "expression")
        return {"search_maps", "get_map_values", "describe_map_shape",
                "evaluate_map_expression", "append_tuning_note"};

    if (cat == "dyno")
        return {"log_dyno_result", "get_tuning_notes", "get_modified_maps",
                "get_all_changes_summary", "append_tuning_note"};

    return {"search_maps", "confidence_search", "get_map_values", "get_map_info",
            "get_project_info", "get_tuning_notes"}; // fallback
}

// ── Provider creation ─────────────────────────────────────────────────────────

AIProvider *AIAssistant::createProvider(int index)
{
    if (index < 0 || index >= m_providerConfigs.size()) return nullptr;
    const ProviderConfig &cfg = m_providerConfigs[index];

    QSettings s("CT14", "romHEX14");
    s.beginGroup(kSettingsGroup);
    QString apiKey = QString::fromUtf8(deobfuscate(s.value(cfg.name + "/apiKey").toByteArray()));
    QString model  = s.value(cfg.name + "/model", cfg.defaultModel).toString();
    QString baseUrl= s.value(cfg.name + "/baseUrl", cfg.baseUrl).toString();
    s.endGroup();

    if (model.isEmpty()) model = cfg.defaultModel;

    AIProvider *prov;
    if (cfg.isClaude) {
        auto *c = new ClaudeProvider(this);
        c->setApiKey(apiKey);
        c->setModel(model);
        if (!baseUrl.isEmpty()) c->setBaseUrl(baseUrl);
        prov = c;
    } else {
        auto *c = new OpenAICompatProvider(this);
        c->setApiKey(apiKey);
        c->setModel(model);
        c->setBaseUrl(baseUrl.isEmpty() ? cfg.baseUrl : baseUrl);
        c->setProviderLabel(cfg.label);
        c->setDeepSeek(cfg.name == "deepseek");
        prov = c;
    }
    return prov;
}

AIProvider *AIAssistant::createOneShotProvider(QObject *parent)
{
    // Static provider configs matching the instance ones
    struct PC { QString name; QString baseUrl; QString model; bool isClaude; };
    static const PC configs[] = {
        {"claude",   "",                                    "claude-sonnet-4-6",       true},
        {"openai",   "https://api.openai.com/v1",           "gpt-4o",                  false},
        {"qwen",     "https://dashscope.aliyuncs.com/compatible-mode/v1", "qwen-plus", false},
        {"deepseek", "https://api.deepseek.com/v1",         "deepseek-v4-flash",       false},
        {"gemini",   "https://generativelanguage.googleapis.com/v1beta/openai/", "gemini-2.0-flash", false},
        {"groq",     "https://api.groq.com/openai/v1",      "llama-3.3-70b-versatile", false},
        {"ollama",   "http://localhost:11434/v1",           "llama3.2",                false},
        {"lmstudio", "http://localhost:1234/v1",            "local-model",             false},
        {"custom",   "",                                    "",                        false},
    };

    QSettings s("CT14", "romHEX14");
    s.beginGroup(kSettingsGroup);
    int idx = s.value("provider", 0).toInt();
    s.endGroup();

    if (idx < 0 || idx >= (int)(sizeof(configs)/sizeof(configs[0]))) idx = 0;
    const PC &cfg = configs[idx];

    s.beginGroup(kSettingsGroup);
    QString apiKey = QString::fromUtf8(deobfuscate(s.value(cfg.name + "/apiKey").toByteArray()));
    QString model  = s.value(cfg.name + "/model", cfg.model).toString();
    QString baseUrl= s.value(cfg.name + "/baseUrl", cfg.baseUrl).toString();
    s.endGroup();

    if (model.isEmpty()) model = cfg.model;

    if (cfg.isClaude) {
        auto *c = new ClaudeProvider(parent);
        c->setApiKey(apiKey);
        c->setModel(model);
        if (!baseUrl.isEmpty()) c->setBaseUrl(baseUrl);
        return c;
    } else {
        auto *c = new OpenAICompatProvider(parent);
        c->setApiKey(apiKey);
        c->setModel(model);
        c->setBaseUrl(baseUrl.isEmpty() ? cfg.baseUrl : baseUrl);
        c->setDeepSeek(cfg.name == "deepseek");
        return c;
    }
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void AIAssistant::onProviderChanged(int index)
{
    m_currentProvider = index;
    if (m_provider) {
        m_provider->abort();
        m_provider->deleteLater();
        m_provider.clear();
    }
    saveSettings();
}

void AIAssistant::onClearChat()
{
    m_history.clear();
    m_streamingBubble = nullptr;
    m_accumulatedText.clear();
    m_pendingReasoningContent.clear();
    m_pendingToolCalls.clear();
    m_hadToolCalls = false;
    m_sessionStarted = false;   // back to welcome until next user send

    while (m_chatLayout->count() > 1) {
        QLayoutItem *item = m_chatLayout->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    m_statusLabel->clear();
    showTyping(false);
    transitionTo(AssistantState::IDLE);
    checkWelcome();
}

void AIAssistant::onSend()
{
    QString text = m_input->toPlainText().trimmed();
    if (text.isEmpty()) return;
    if (m_state != AssistantState::IDLE) return;

    m_input->clear();
    m_sessionStarted = true;
    checkWelcome();
    appendMessage("user", text);
    m_history.append({AIMessage::User, text, {}, {}, {}, {}, {}});

    if (!m_project) {
        appendMessage("assistant", tr("No project loaded. Open a ROM file first."));
        return;
    }

    // Classify intent and select tools for AI
    QString category = classifyIntent(text);
    m_activeCategory = category;
    m_selectedTools = toolsForCategory(category);

    // Create provider if needed
    if (!m_provider) {
        m_provider = createProvider(m_currentProvider);
        if (!m_provider) {
            abortWithError(tr("No API key configured. Click ⚙ to set up."));
            return;
        }
    }

    const ProviderConfig &cfg = m_providerConfigs[m_currentProvider];
    QSettings s("CT14", "romHEX14");
    s.beginGroup(kSettingsGroup);
    QString apiKey = QString::fromUtf8(deobfuscate(s.value(cfg.name + "/apiKey").toByteArray()));
    s.endGroup();
    bool needsKey = !cfg.isClaude
                    ? (cfg.baseUrl.startsWith("https://api.") || cfg.baseUrl.startsWith("https://dash"))
                    : true;
    if (needsKey && apiKey.isEmpty()) {
        m_statusLabel->setText(tr("No API key set. Click ⚙ to configure."));
        return;
    }

    buildSystemPrompt();
    m_toolRound = 0;
    m_retryCount = 0;
    transitionTo(AssistantState::WORKING);
    doSend();
}

// ── Built-in tuning recipes ──────────────────────────────────────────────────

QVector<AIAssistant::TuningRecipe> AIAssistant::builtinRecipes()
{
    return {
        // Catalyst / Decat — broad patterns to catch all ECU naming conventions
        {"decat",          {"KAT*", "DKAT*", "*DKAT*", "*KATDIAG*", "OSCKAT*", "DFC*KAT*"},  "zero", 0, tr("Catalyst monitoring off (decat)")},
        {"cat off",        {"KAT*", "DKAT*", "*DKAT*", "*KATDIAG*", "OSCKAT*", "DFC*KAT*"},  "zero", 0, tr("Catalyst monitoring off")},
        {"cat delete",     {"KAT*", "DKAT*", "*DKAT*", "*KATDIAG*", "OSCKAT*", "DFC*KAT*"},  "zero", 0, tr("Catalyst monitoring off")},
        // DPF
        {"dpf off",        {"DPF*", "*DPF*", "RUSS*", "PARMON*", "DFC*DPF*"},  "zero", 0, tr("DPF monitoring off")},
        {"dpf delete",     {"DPF*", "*DPF*", "RUSS*", "PARMON*", "DFC*DPF*"},  "zero", 0, tr("DPF monitoring off")},
        {"dpf removal",    {"DPF*", "*DPF*", "RUSS*", "PARMON*", "DFC*DPF*"},  "zero", 0, tr("DPF monitoring off")},
        // EGR
        {"egr off",        {"AGR*", "*AGR*", "DAGR*", "EGR*", "DFC*EGR*"},    "zero", 0, tr("EGR valve off")},
        {"egr delete",     {"AGR*", "*AGR*", "DAGR*", "EGR*", "DFC*EGR*"},    "zero", 0, tr("EGR valve off")},
        {"egr removal",    {"AGR*", "*AGR*", "DAGR*", "EGR*", "DFC*EGR*"},    "zero", 0, tr("EGR valve off")},
        // Lambda / O2
        {"lambda off",     {"LSMON*", "LSHFM*", "LAMFA*", "*LSMON*"},         "zero", 0, tr("Lambda/O2 monitoring off")},
        {"o2 off",         {"LSMON*", "LSHFM*", "LAMFA*", "*LSMON*"},         "zero", 0, tr("Lambda/O2 monitoring off")},
        {"o2 delete",      {"LSMON*", "LSHFM*", "LAMFA*", "*LSMON*"},         "zero", 0, tr("Lambda/O2 monitoring off")},
        // Others
        {"adblue off",     {"SCR*", "*SCR*", "ADBLUE*", "HARNST*", "DOSMOD*"},"zero", 0, tr("AdBlue/SCR off")},
        {"swirl flap",     {"DKBA*", "SWIRL*", "DKSBA*"},                     "zero", 0, tr("Swirl flap delete")},
        {"start stop off", {"SSA*", "STST*", "STARTSTOP*"},                   "zero", 0, tr("Start-stop disable")},
        // Speed / Rev limiter
        {"speed limiter",  {"VMAX*", "VFIL*", "VBEG*", "*VMAX*"},             "zero", 0, tr("Speed limiter off")},
        {"vmax",           {"VMAX*", "VFIL*", "VBEG*", "*VMAX*"},             "zero", 0, tr("Speed limiter off")},
        {"rev limiter",    {"NMAX*", "NMOT_MAX*", "*NMAX*"},                   "zero", 0, tr("Rev limiter off")},
        // Pops & bangs
        {"pops and bangs", {"SAK*", "KFZWSA*", "*SAKUE*", "*SAKUB*"},          "zero", 0, tr("Pops & bangs (overrun)")},
        {"pops & bangs",   {"SAK*", "KFZWSA*", "*SAKUE*", "*SAKUB*"},          "zero", 0, tr("Pops & bangs (overrun)")},
        {"crackle",        {"SAK*", "KFZWSA*", "*SAKUE*", "*SAKUB*"},          "zero", 0, tr("Pops & bangs (overrun)")},
        {"burble",         {"SAK*", "KFZWSA*", "*SAKUE*", "*SAKUB*"},          "zero", 0, tr("Pops & bangs (overrun)")},
    };
}

AIAssistant::TuningRecipe *AIAssistant::matchRecipe(const QString &userText)
{
    static QVector<TuningRecipe> recipes = builtinRecipes();
    QString lower = userText.toLower();

    // Check user recipes first (higher priority)
    for (auto &r : m_userRecipes) {
        if (lower.contains(r.keyword.toLower()))
            return &r;
    }

    // Then built-ins
    for (auto &r : recipes) {
        if (lower.contains(r.keyword))
            return &r;
    }
    return nullptr;
}

void AIAssistant::executeRecipe(const TuningRecipe &recipe)
{
    m_activeRecipe = recipe;
    m_foundMaps.clear();

    appendMessage("assistant", tr("Searching for %1 maps...").arg(recipe.description));
    transitionTo(AssistantState::WORKING);
    m_statusLabel->setText(tr("Searching..."));

    // Step 1: Direct pattern search to find candidates
    QStringList candidates;
    QStringList candidateDescs;
    if (m_executor) {
        for (const QString &pattern : recipe.patterns) {
            QJsonObject input;
            input["query"] = pattern;
            QString result = m_executor->execute("search_maps", input);
            QJsonDocument doc = QJsonDocument::fromJson(result.toUtf8());
            QJsonArray results = doc.object()["results"].toArray();
            for (const auto &r : results) {
                QString name = r.toObject()["name"].toString();
                QString desc = r.toObject()["description"].toString();
                if (!name.isEmpty() && !candidates.contains(name)) {
                    candidates.append(name);
                    candidateDescs.append(desc);
                }
            }
        }
    }

    if (candidates.isEmpty()) {
        onRecipeSearchDone(recipe);
        return;
    }

    // Step 2: Use AI to filter candidates (one fast API call)
    if (m_provider) {
        m_statusLabel->setText(tr("AI is filtering %1 candidates...").arg(candidates.size()));

        // Build a compact list for the AI
        QString mapList;
        for (int i = 0; i < candidates.size(); ++i)
            mapList += QString("%1: %2\n").arg(candidates[i], candidateDescs[i]);

        QString filterPrompt = QString(
            "Filter this list of ECU maps. The user wants: %1\n\n"
            "Return ONLY the map names that should be ZEROED for this task. "
            "Exclude maps for heating, temperature models, operation control, or unrelated functions. "
            "Only include MONITORING, DIAGNOSTIC, THRESHOLD, and DTC MASK maps.\n\n"
            "Maps found:\n%2\n\n"
            "Respond with ONLY a JSON array: [\"MAP1\", \"MAP2\", ...]"
        ).arg(recipe.description, mapList);

        m_accumulatedText.clear();
        m_provider->send(
            filterPrompt, {}, {}, // no tools needed, no history
            [this](const QString &chunk) {
                QMetaObject::invokeMethod(this, [this, chunk]() {
                    m_accumulatedText += chunk;
                }, Qt::QueuedConnection);
            },
            [](const QString &) {}, // reasoning is not needed without tools
            [](const QString &, const QString &, const QJsonObject &) {}, // no tool calls
            [this, candidates, recipe]() {
                QMetaObject::invokeMethod(this, [this, candidates, recipe]() {
                    // Parse AI response — extract JSON array of map names
                    QRegularExpression jsonRe(R"(\[[\s\S]*?\])");
                    auto match = jsonRe.match(m_accumulatedText);
                    if (match.hasMatch()) {
                        QJsonDocument doc = QJsonDocument::fromJson(match.captured(0).toUtf8());
                        if (doc.isArray()) {
                            for (const auto &v : doc.array()) {
                                QString name = v.toString();
                                if (!name.isEmpty() && candidates.contains(name) && !m_foundMaps.contains(name))
                                    m_foundMaps.append(name);
                            }
                        }
                    }
                    // If AI filtering failed, use all candidates
                    if (m_foundMaps.isEmpty())
                        m_foundMaps = candidates;
                    m_accumulatedText.clear();
                    onRecipeSearchDone(recipe);
                }, Qt::QueuedConnection);
            },
            [this, candidates, recipe](const QString &) {
                QMetaObject::invokeMethod(this, [this, candidates, recipe]() {
                    // AI failed — use all candidates as fallback
                    m_foundMaps = candidates;
                    onRecipeSearchDone(recipe);
                }, Qt::QueuedConnection);
            }
        );
    } else {
        // No AI provider — use all candidates
        m_foundMaps = candidates;
        onRecipeSearchDone(recipe);
    }
}

void AIAssistant::onRecipeSearchDone(const TuningRecipe &recipe)
{
    showTyping(false);
    if (m_foundMaps.isEmpty()) {
        appendMessage("assistant", tr("No relevant maps found for %1. This ECU may use different naming conventions.")
            .arg(recipe.description));
        transitionTo(AssistantState::IDLE);
        return;
    }

    // If linked ROMs exist, auto-select the first one (it's the ECU readout to modify)
    // Main project ROM is the factory file for comparison — never modify it
    if (m_project && !m_project->linkedRoms.isEmpty()) {
        showRecipeConfirmation(m_foundMaps, recipe, 0); // always target first linked ROM
    } else {
        showRecipeConfirmation(m_foundMaps, recipe, -1); // no linked ROM, modify main
    }
}

void AIAssistant::showTargetSelection(const QStringList &mapNames, const TuningRecipe &recipe)
{
    auto *card = new QWidget(m_chatContainer);
    card->setObjectName("targetCard");
    card->setStyleSheet("QWidget#targetCard{background:#0c1524; border:1px solid #1f6feb; border-radius:12px;}");
    auto *cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(16, 14, 16, 14);
    cardLay->setSpacing(10);

    auto *titleLabel = new QLabel(tr("Which ROM do you want to modify?"));
    titleLabel->setStyleSheet("color:#58a6ff; font-size:11pt; font-weight:bold; background:transparent;");
    cardLay->addWidget(titleLabel);

    auto *sep = new QFrame();
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("background:#1f6feb; max-height:1px; border:none;");
    cardLay->addWidget(sep);

    // Main ROM button
    auto *mainBtn = new QPushButton(tr("Main Project ROM"));
    mainBtn->setCursor(Qt::PointingHandCursor);
    mainBtn->setStyleSheet(
        "QPushButton{background:#162040;color:#c9d1d9;border:1px solid #30363d;"
        "border-radius:8px;padding:12px 16px;font-size:9pt;text-align:left}"
        "QPushButton:hover{border-color:#1f6feb;background:#1a2a50}");
    cardLay->addWidget(mainBtn);

    connect(mainBtn, &QPushButton::clicked, this, [this, card, mapNames, recipe]() {
        card->setEnabled(false);
        card->setStyleSheet("QWidget#targetCard{background:#0c1524; border:1px solid #30363d; border-radius:12px;}");
        showRecipeConfirmation(mapNames, recipe, -1);
    });

    // Linked ROM buttons
    for (int i = 0; i < m_project->linkedRoms.size(); ++i) {
        const auto &lr = m_project->linkedRoms[i];
        QString label = lr.label.isEmpty() ? QFileInfo(lr.filePath).fileName() : lr.label;
        auto *btn = new QPushButton(tr("Linked: %1").arg(label));
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            "QPushButton{background:#162040;color:#58a6ff;border:1px solid #1f4a7a;"
            "border-radius:8px;padding:12px 16px;font-size:9pt;text-align:left}"
            "QPushButton:hover{border-color:#388bfd;background:#1a2a50}");
        cardLay->addWidget(btn);

        connect(btn, &QPushButton::clicked, this, [this, card, mapNames, recipe, i]() {
            card->setEnabled(false);
            card->setStyleSheet("QWidget#targetCard{background:#0c1524; border:1px solid #30363d; border-radius:12px;}");
            showRecipeConfirmation(mapNames, recipe, i);
        });
    }

    int stretchIdx = m_chatLayout->count() - 1;
    m_chatLayout->insertWidget(stretchIdx, card);
    scrollToBottom();
    transitionTo(AssistantState::AWAITING_CONFIRMATION);
}

// ── Permission-mode helpers ─────────────────────────────────────────────────
//
// AutoAccept and Plan share the "feed result back to the model and resume the
// agent loop" tail with the inline-confirmation accept/reject paths. Keeping it
// in one place avoids the four near-duplicates we used to have.

void AIAssistant::continueAfterWriteResolution()
{
    m_toolRound = 0;   // fresh budget — the follow-up turn is logically a new round
    transitionTo(AssistantState::WORKING);
    QTimer::singleShot(600, this, [this]() {
        if (m_state == AssistantState::WORKING) doSend();
    });
}

void AIAssistant::executeWriteToolImmediately(const PendingToolCall &ptc, bool announce)
{
    QString result = m_executor->execute(ptc.name, ptc.input);

    AIMessage resMsg;
    resMsg.role           = AIMessage::ToolResult;
    resMsg.toolResultId   = ptc.callId;
    resMsg.toolResultName = ptc.name;
    resMsg.toolResultJson = (result.size() > 8000)
        ? result.left(8000) + "\n... (truncated, " + QString::number(result.size()) + " bytes total)"
        : result;
    m_history.append(resMsg);

    auto *resBubble = new BubbleWidget(BubbleWidget::ToolResult, m_chatContainer);
    QString richHtml = richToolResultHtml(ptc.name, result);
    if (!richHtml.isEmpty()) {
        resBubble->setRichContent(richHtml);
    } else if (announce) {
        resBubble->setRichContent(
            tr("<span style='color:#3fb950;'>\xE2\x9A\xA1 Auto-accepted: change applied</span>"));
    } else {
        resBubble->setRichContent(
            tr("<span style='color:#3fb950;'>\xE2\x9C\x93 Change applied</span>"));
    }
    m_chatLayout->insertWidget(m_chatLayout->count() - 1, resBubble);
    scrollToBottom();

    emit projectModified();
    continueAfterWriteResolution();
}

void AIAssistant::rejectWriteToolForPlanMode(const PendingToolCall &ptc)
{
    AIMessage resMsg;
    resMsg.role           = AIMessage::ToolResult;
    resMsg.toolResultId   = ptc.callId;
    resMsg.toolResultName = ptc.name;
    // Synthetic error nudges the model into describe-only behavior without
    // making this look like a user rejection.
    resMsg.toolResultJson =
        R"({"error":"Plan-only mode is active. No changes were applied. )"
        R"(Describe what you would do, why, and which maps/cells would be )"
        R"(affected — but do not call any write tools."})";
    m_history.append(resMsg);

    auto *resBubble = new BubbleWidget(BubbleWidget::ToolResult, m_chatContainer);
    resBubble->setRichContent(
        tr("<span style='color:#d29922;'>\xF0\x9F\x93\x8B Plan mode \xE2\x80\x94 "
           "no changes applied</span>"));
    m_chatLayout->insertWidget(m_chatLayout->count() - 1, resBubble);
    scrollToBottom();

    continueAfterWriteResolution();
}

void AIAssistant::showWriteConfirmation(const PendingToolCall &ptc)
{
    m_confirmingCall = ptc;
    transitionTo(AssistantState::AWAITING_CONFIRMATION);

    const QString &name    = ptc.name;
    const QJsonObject &inp = ptc.input;

    // Build human-readable description of the proposed change
    QString opIcon = "\xe2\x9c\x8f";  // ✏
    QString opDesc;

    if (name == "set_map_values") {
        int count = inp["values"].toArray().size();
        opDesc = tr("Write <b>%1</b> new value(s) to map <b>%2</b>.")
                     .arg(count).arg(inp["map_name"].toString().toHtmlEscaped());
    } else if (name == "zero_map") {
        opIcon = "\xe2\xac\x9b";  // ⬛
        opDesc = tr("Set every cell to <b>0</b> in map <b>%1</b>.")
                     .arg(inp["map_name"].toString().toHtmlEscaped());
    } else if (name == "scale_map_values") {
        opDesc = tr("Scale all values in map <b>%1</b> by <b>%2\xc3\x97</b>.")
                     .arg(inp["map_name"].toString().toHtmlEscaped())
                     .arg(inp["factor"].toDouble());
    } else if (name == "fill_map") {
        opDesc = tr("Fill all cells in map <b>%1</b> with constant <b>%2</b>.")
                     .arg(inp["map_name"].toString().toHtmlEscaped())
                     .arg(inp["value"].toDouble());
    } else if (name == "offset_map_values") {
        double delta = inp["delta"].toDouble();
        opIcon = delta >= 0 ? "\xe2\x86\x91" : "\xe2\x86\x93";  // ↑ / ↓
        opDesc = tr("%1 all values in map <b>%2</b> by <b>%3</b>.")
                     .arg(delta >= 0 ? tr("Increase") : tr("Decrease"))
                     .arg(inp["map_name"].toString().toHtmlEscaped())
                     .arg(qAbs(delta));
    } else if (name == "batch_modify_maps") {
        opIcon = "\xe2\x9a\xa1";  // ⚡
        int count = inp["maps"].toArray().size();
        opDesc = tr("Batch-modify <b>%1</b> map(s).<br>"
                    "<span style='color:#8b949e;font-size:8pt;'>%2</span>")
                     .arg(count).arg(inp["reason"].toString().toHtmlEscaped());
    } else if (name == "evaluate_map_expression") {
        opIcon = "\xc6\x92";  // ƒ
        opDesc = tr("Apply expression <code style='background:#21262d;padding:1px 4px;"
                    "border-radius:3px;'>%1</code> to map <b>%2</b>.")
                     .arg(inp["expression"].toString().toHtmlEscaped())
                     .arg(inp["map_name"].toString().toHtmlEscaped());
    } else if (name == "apply_delta_to_rom") {
        opIcon = "\xe2\x86\x94";  // ↔
        int count = inp["map_names"].toArray().size();
        opDesc = tr("Copy <b>%1</b> map(s) from linked ROM <b>%2</b>.")
                     .arg(count).arg(inp["source_rom"].toString().toHtmlEscaped());
    } else if (name == "undo_with_reason") {
        opIcon = "\xe2\x86\xa9";  // ↩
        opDesc = tr("Undo changes \xe2\x80\x94 <i>%1</i>")
                     .arg(inp["reason"].toString().toHtmlEscaped());
    } else {
        opDesc = tr("Execute: <b>%1</b>").arg(name.toHtmlEscaped());
    }

    bool isDestructive = (name == "zero_map" || name == "fill_map" || name == "batch_modify_maps");

    // Build confirmation card — green-bordered, consistent with recipe confirmation style
    auto *card = new QWidget(m_chatContainer);
    card->setStyleSheet("QWidget#aiWriteCard{background:#0d1f14; border:1px solid #238636; border-radius:12px;}");
    card->setObjectName("aiWriteCard");
    auto *cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(16, 14, 16, 14);
    cardLay->setSpacing(10);

    // Header
    auto *headerLay = new QHBoxLayout;
    auto *iconLbl = new QLabel(opIcon);
    iconLbl->setStyleSheet("font-size:16pt; background:transparent;");
    auto *titleLbl = new QLabel(tr("AI proposes a change"));
    titleLbl->setStyleSheet("color:#3fb950; font-size:11pt; font-weight:bold; background:transparent;");
    headerLay->addWidget(iconLbl);
    headerLay->addWidget(titleLbl, 1);
    cardLay->addLayout(headerLay);

    auto *sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("background:#238636; max-height:1px; border:none;");
    cardLay->addWidget(sep);

    auto *descLbl = new QLabel(opDesc);
    descLbl->setWordWrap(true);
    descLbl->setTextFormat(Qt::RichText);
    descLbl->setStyleSheet("color:#c9d1d9; font-size:9pt; background:transparent; padding:2px 0;");
    cardLay->addWidget(descLbl);

    if (isDestructive) {
        auto *warnLbl = new QLabel(tr("\xe2\x9a\xa0 This will overwrite existing values. "
                                      "Consider saving a snapshot first."));
        warnLbl->setWordWrap(true);
        warnLbl->setStyleSheet("color:#d29922; font-size:8pt; background:transparent;");
        cardLay->addWidget(warnLbl);
    }

    auto *btnLay = new QHBoxLayout;
    btnLay->addStretch();

    auto *rejectBtn = new QPushButton(tr("\xe2\x9c\x97 Reject"));
    rejectBtn->setCursor(Qt::PointingHandCursor);
    rejectBtn->setStyleSheet(
        "QPushButton{background:#21262d;color:#f85149;border:1px solid #f85149;"
        "border-radius:8px;padding:10px 28px;font-weight:bold;font-size:9pt}"
        "QPushButton:hover{background:#3d1c1c}");

    auto *acceptBtn = new QPushButton(tr("\xe2\x9c\x93 Accept & Apply"));
    acceptBtn->setCursor(Qt::PointingHandCursor);
    acceptBtn->setStyleSheet(
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #238636,stop:1 #2ea043);color:white;border:none;"
        "border-radius:8px;padding:10px 28px;font-weight:bold;font-size:9pt}"
        "QPushButton:hover{background:#2ea043}");

    btnLay->addWidget(rejectBtn);
    btnLay->addWidget(acceptBtn);
    cardLay->addLayout(btnLay);

    int stretchIdx = m_chatLayout->count() - 1;
    m_chatLayout->insertWidget(stretchIdx, card);
    scrollToBottom();

    connect(acceptBtn, &QPushButton::clicked, this, [this, card]() {
        card->setEnabled(false);
        card->setStyleSheet("QWidget#aiWriteCard{background:#0d1f14; border:1px solid #238636;"
                            "border-radius:12px;}");
        // Reuse the AutoAccept path so the result/loop plumbing stays in one place.
        executeWriteToolImmediately(m_confirmingCall, /*announce=*/false);
    });

    connect(rejectBtn, &QPushButton::clicked, this, [this, card]() {
        card->setEnabled(false);
        card->setStyleSheet("QWidget#aiWriteCard{background:#1a1117; border:1px solid #6e7681;"
                            "border-radius:12px;}");

        AIMessage resMsg;
        resMsg.role           = AIMessage::ToolResult;
        resMsg.toolResultId   = m_confirmingCall.callId;
        resMsg.toolResultName = m_confirmingCall.name;
        resMsg.toolResultJson =
            R"({"error":"User rejected this change. Explain what you were going to do )"
            R"(and ask if they would like a different approach or adjusted values."})";
        m_history.append(resMsg);

        auto *resBubble = new BubbleWidget(BubbleWidget::ToolResult, m_chatContainer);
        resBubble->setRichContent(
            tr("<span style='color:#f85149;'>\xe2\x9c\x97 Change rejected by user</span>"));
        m_chatLayout->insertWidget(m_chatLayout->count() - 1, resBubble);
        scrollToBottom();

        continueAfterWriteResolution();
    });
}

void AIAssistant::showRecipeConfirmation(const QStringList &mapNames, const TuningRecipe &recipe, int targetRomIndex)
{
    // Create a styled confirmation card
    auto *card = new QWidget(m_chatContainer);
    card->setStyleSheet(
        "QWidget#recipeCard{background:#0c1524; border:1px solid #1f6feb; border-radius:12px;}");
    card->setObjectName("recipeCard");
    auto *cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(16, 14, 16, 14);
    cardLay->setSpacing(10);

    // Header with icon
    auto *headerLay = new QHBoxLayout();
    auto *iconLabel = new QLabel("\xf0\x9f\x94\xa7");
    iconLabel->setStyleSheet("font-size:18pt; background:transparent;");
    auto *titleLabel = new QLabel(tr("%1 \xe2\x80\x94 %2 maps found").arg(recipe.description).arg(mapNames.size()));
    titleLabel->setStyleSheet("color:#58a6ff; font-size:11pt; font-weight:bold; background:transparent;");
    headerLay->addWidget(iconLabel);
    headerLay->addWidget(titleLabel, 1);
    cardLay->addLayout(headerLay);

    // Separator
    auto *sep = new QFrame();
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("background:#1f6feb; max-height:1px; border:none;");
    cardLay->addWidget(sep);

    // Map list as rich text label (wraps naturally)
    QString mapHtml = "<div style='line-height:1.8;'>";
    for (const QString &name : mapNames) {
        mapHtml += QString("<span style='background:#162040; color:#58a6ff; "
            "border:1px solid #1f4a7a; border-radius:4px; padding:2px 6px; "
            "font-family:Consolas; font-size:8pt; margin:2px; "
            "display:inline-block;'>%1</span> ").arg(name.toHtmlEscaped());
    }
    mapHtml += "</div>";
    auto *mapLabel = new QLabel(mapHtml);
    mapLabel->setWordWrap(true);
    mapLabel->setTextFormat(Qt::RichText);
    mapLabel->setStyleSheet("background:transparent; padding:4px 0;");
    cardLay->addWidget(mapLabel);

    // Action description
    auto *actionLabel = new QLabel(tr("Action: %1 all %2 maps").arg(recipe.action.toUpper()).arg(mapNames.size()));
    actionLabel->setStyleSheet("color:#8b949e; font-size:9pt; background:transparent;");
    cardLay->addWidget(actionLabel);

    // Buttons
    auto *btnLay = new QHBoxLayout();
    btnLay->addStretch();

    auto *cancelBtn = new QPushButton(tr("Cancel"));
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setStyleSheet(
        "QPushButton{background:#21262d;color:#c9d1d9;border:1px solid #30363d;"
        "border-radius:8px;padding:10px 28px;font-weight:bold;font-size:9pt}"
        "QPushButton:hover{background:#30363d;border-color:#8b949e}");

    auto *applyBtn = new QPushButton(tr("\xe2\x9c\x93 Apply %1 Changes").arg(mapNames.size()));
    applyBtn->setCursor(Qt::PointingHandCursor);
    applyBtn->setStyleSheet(
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #1f6feb,stop:1 #388bfd);color:white;border:none;"
        "border-radius:8px;padding:10px 28px;font-weight:bold;font-size:9pt}"
        "QPushButton:hover{background:#388bfd}");

    btnLay->addWidget(cancelBtn);
    btnLay->addWidget(applyBtn);
    cardLay->addLayout(btnLay);

    int stretchIdx = m_chatLayout->count() - 1;
    m_chatLayout->insertWidget(stretchIdx, card);
    scrollToBottom();

    transitionTo(AssistantState::AWAITING_CONFIRMATION);

    connect(applyBtn, &QPushButton::clicked, this, [this, card, mapNames, recipe, targetRomIndex]() {
        card->setEnabled(false);
        card->setStyleSheet("QWidget#recipeCard{background:#0c1524; border:1px solid #238636; border-radius:12px;}");
        applyRecipe(mapNames, recipe, targetRomIndex);
    });
    connect(cancelBtn, &QPushButton::clicked, this, [this, card]() {
        card->setEnabled(false);
        card->setStyleSheet("QWidget#recipeCard{background:#0c1524; border:1px solid #30363d; border-radius:12px;}");
        appendMessage("assistant", tr("Cancelled."));
        transitionTo(AssistantState::IDLE);
    });
}

void AIAssistant::applyRecipe(const QStringList &mapNames, const TuningRecipe &recipe, int targetRomIndex)
{
    if (!m_project) {
        appendMessage("assistant", tr("Error: no project loaded."));
        transitionTo(AssistantState::IDLE);
        return;
    }

    // Find the linked ROM's Project object — it's a separate Project with isLinkedRom=true
    Project *targetProject = m_project; // default: main project
    QString romLabel = tr("Main ROM");

    if (targetRomIndex >= 0 && targetRomIndex < m_project->linkedRoms.size()) {
        // Find the linked ROM Project in all open projects
        const QString &linkedPath = m_project->linkedRoms[targetRomIndex].filePath;
        const QString &linkedLabel = m_project->linkedRoms[targetRomIndex].label;
        romLabel = linkedLabel.isEmpty() ? QFileInfo(linkedPath).fileName() : linkedLabel;

        for (Project *p : m_allProjects) {
            if (p->isLinkedRom && (p->filePath == linkedPath
                || p->displayName() == linkedLabel
                || p->currentData.size() == m_project->linkedRoms[targetRomIndex].data.size())) {
                targetProject = p;
                break;
            }
        }

        if (targetProject == m_project) {
            // Linked ROM Project not found in open projects — fall back to data-only modification
            appendMessage("assistant", tr("Warning: linked ROM project not open. Modifying data directly."));
        }
    }

    QByteArray &data = targetProject->currentData;

    // Build a descriptive version name (e.g., "CAT OFF", "EGR OFF + CAT OFF")
    QString versionName = recipe.description.toUpper()
        .replace("CATALYST MONITORING OFF", "CAT OFF")
        .replace("(DECAT)", "")
        .replace("DPF MONITORING OFF", "DPF OFF")
        .replace("EGR VALVE OFF", "EGR OFF")
        .replace("LAMBDA/O2 MONITORING OFF", "LAMBDA OFF")
        .replace("ADBLUE/SCR OFF", "ADBLUE OFF")
        .replace("SWIRL FLAP DELETE", "SWIRL OFF")
        .replace("START-STOP DISABLE", "START-STOP OFF")
        .trimmed();

    // Check if there's a previous version and append to build chain
    if (!targetProject->versions.isEmpty()) {
        QString lastVersion = targetProject->versions.last().name;
        if (!lastVersion.isEmpty() && lastVersion != versionName
            && !lastVersion.contains(versionName)) {
            versionName = lastVersion + " + " + versionName;
        }
    }

    // Create version on the TARGET project (linked ROM project, not main)
    targetProject->snapshotVersion(versionName);

    // Apply modifications directly (user already confirmed via inline buttons)
    int applied = 0;

    for (const QString &name : mapNames) {
        MapInfo *m = nullptr;
        for (auto &map : m_project->maps) {
            if (map.name.compare(name, Qt::CaseInsensitive) == 0) {
                m = &map; break;
            }
        }
        if (!m) continue;

        int cellSize = m->dataSize > 0 ? m->dataSize : 2;
        int totalCells = m->dimensions.x * m->dimensions.y;
        uint32_t offset = m->address + m->mapDataOffset;

        if (offset + totalCells * cellSize > (uint32_t)data.size()) continue;

        if (recipe.action == "zero") {
            for (int i = 0; i < totalCells * cellSize; ++i)
                data[offset + i] = 0;
            ++applied;
        } else if (recipe.action == "fill") {
            uint32_t raw = (uint32_t)(int32_t)recipe.value;
            for (int i = 0; i < totalCells; ++i) {
                for (int b = cellSize - 1; b >= 0; --b) {
                    data[offset + i * cellSize + b] = (uint8_t)(raw & 0xFF);
                    raw >>= 8;
                }
                raw = (uint32_t)(int32_t)recipe.value;
            }
            ++applied;
        }
    }

    if (applied > 0) {
        targetProject->modified = true;
        emit targetProject->dataChanged();
        emit projectModified();
    }

    appendMessage("assistant",
        tr("**Done!** %1 of %2 maps zeroed on **%3**.\n\n"
           "Version **%4** created — you can undo anytime from the version list.")
        .arg(applied).arg(mapNames.size()).arg(romLabel).arg(versionName));

    transitionTo(AssistantState::IDLE);
}

// ── Agent loop ────────────────────────────────────────────────────────────────

void AIAssistant::doSend()
{
    if (m_state != AssistantState::WORKING) return;
    if (m_toolRound >= kMaxAgentLoops) {
        abortWithError(tr("Reached maximum tool rounds (%1). Stopping.").arg(kMaxAgentLoops));
        return;
    }
    ++m_toolRound;

    manageContext();

    // Build tool list based on classified intent
    QVector<AIToolDef> allDefs = AIToolExecutor::toolDefinitions();
    QVector<AIToolDef> selectedDefs;
    if (m_selectedTools.isEmpty()) {
        selectedDefs = allDefs;  // explain mode or fallback: send all (provider ignores if empty)
    } else {
        for (const auto &d : allDefs) {
            if (m_selectedTools.contains(d.name))
                selectedDefs.append(d);
        }
    }

    m_accumulatedText.clear();
    m_pendingToolCalls.clear();
    m_hadToolCalls = false;

    startAssistantBubble();
    showTyping(true);
    m_statusLabel->setText(m_toolRound > 1 ? tr("Working… (round %1/%2)").arg(m_toolRound).arg(kMaxAgentLoops) : tr("Thinking…"));

    // Start timeout
    if (!m_requestTimeout) {
        m_requestTimeout = new QTimer(this);
        m_requestTimeout->setSingleShot(true);
        connect(m_requestTimeout, &QTimer::timeout, this, [this]() {
            if (m_provider) m_provider->abort();
            abortWithError(tr("Request timed out (60s)."));
        });
    }
    m_requestTimeout->start(60000);

    if (!m_provider) {
        abortWithError(tr("Provider unavailable."));
        return;
    }
    m_provider->send(
        m_systemPrompt,
        m_history,
        selectedDefs,  // ONLY selected tools

        // onChunk
        [this](const QString &chunk) {
            QMetaObject::invokeMethod(this, [this, chunk]() {
                m_accumulatedText += chunk;
                appendChunk(chunk);
            }, Qt::QueuedConnection);
        },

        [this](const QString &reasoning) {
            QMetaObject::invokeMethod(this, [this, reasoning]() {
                m_pendingReasoningContent = reasoning;
            }, Qt::QueuedConnection);
        },

        // onToolCall
        [this](const QString &callId, const QString &name, const QJsonObject &input) {
            QMetaObject::invokeMethod(this, [this, callId, name, input]() {
                PendingToolCall ptc{callId, name, input};
                m_pendingToolCalls.append(ptc);
                m_hadToolCalls = true;
            }, Qt::QueuedConnection);
        },

        // onDone
        [this]() {
            QMetaObject::invokeMethod(this, [this]() {
                if (m_requestTimeout) m_requestTimeout->stop();
                m_retryCount = 0;

                // Flush debounced render
                if (m_chunkTimer && m_chunkTimer->isActive()) {
                    m_chunkTimer->stop();
                    if (m_streamingBubble && !m_accumulatedText.isEmpty())
                        m_streamingBubble->setMarkdownContent(m_accumulatedText);
                }

                m_streamingBubble = nullptr;
                showTyping(false);

                if (m_hadToolCalls && !m_pendingToolCalls.isEmpty()) {
                    // Execute tool calls — stop if one triggers a confirmation card
                    QVector<PendingToolCall> batch = m_pendingToolCalls;
                    m_pendingToolCalls.clear();
                    m_hadToolCalls = false;

                    // Preserve all tool calls from one provider response as a
                    // single assistant turn.  DeepSeek requires every matching
                    // tool result to follow that exact turn.
                    AIMessage useMsg;
                    useMsg.role = AIMessage::ToolUse;
                    useMsg.content = m_accumulatedText;
                    useMsg.reasoningContent = m_pendingReasoningContent;
                    for (const auto &ptc : batch) {
                        useMsg.toolCalls.append({ptc.callId, ptc.name,
                            QString::fromUtf8(QJsonDocument(ptc.input).toJson(QJsonDocument::Compact))});
                    }
                    m_history.append(useMsg);
                    m_accumulatedText.clear();
                    m_pendingReasoningContent.clear();

                    for (const auto &ptc : batch) {
                        handleToolCall(ptc.callId, ptc.name, ptc.input, false);
                        if (m_state == AssistantState::AWAITING_CONFIRMATION)
                            return;  // Confirmation accept/reject handler will resume
                    }

                    // All non-write calls done — schedule next agent round
                    int delay = 2000 + (m_toolRound * 500);
                    QTimer::singleShot(delay, this, [this]() {
                        if (m_state == AssistantState::WORKING) doSend();
                    });
                } else {
                    if (!m_accumulatedText.isEmpty())
                        m_history.append({AIMessage::Assistant, m_accumulatedText, {}, {}, {}, {}, {}});
                    m_accumulatedText.clear();
                    m_pendingReasoningContent.clear();
                    // No more tool calls — done
                    transitionTo(AssistantState::IDLE);
                }
            }, Qt::QueuedConnection);
        },

        // onError
        [this](const QString &err) {
            QMetaObject::invokeMethod(this, [this, err]() {
                if (m_requestTimeout) m_requestTimeout->stop();
                m_streamingBubble = nullptr;
                showTyping(false);

                // Rate limit retry with exponential backoff
                if (err.contains("429") && m_retryCount < kMaxRetries) {
                    ++m_retryCount;
                    int baseMs = 2000 * (1 << qMin(m_retryCount - 1, 5));
                    int jitter = QRandomGenerator::global()->bounded(1000);
                    int waitMs = baseMs + jitter;
                    m_statusLabel->setText(tr("Rate limited — retrying in %1s… (%2/%3)")
                        .arg(waitMs / 1000).arg(m_retryCount).arg(kMaxRetries));
                    --m_toolRound;
                    QTimer::singleShot(waitMs, this, [this]() {
                        if (m_state == AssistantState::WORKING) doSend();
                    });
                    return;
                }

                // Context too large — trim aggressively and retry
                if (err.contains("400") && m_retryCount < 3) {
                    ++m_retryCount;
                    while (m_history.size() > 4) m_history.removeFirst();
                    m_statusLabel->setText(tr("Context too large — trimming…"));
                    --m_toolRound;
                    QTimer::singleShot(1000, this, [this]() {
                        if (m_state == AssistantState::WORKING) doSend();
                    });
                    return;
                }

                abortWithError(err);
            }, Qt::QueuedConnection);
        }
    );
}

void AIAssistant::handleToolCall(const QString &callId, const QString &name, const QJsonObject &input,
                                 bool recordToolUse)
{
    QString inputJson = QString::fromUtf8(QJsonDocument(input).toJson(QJsonDocument::Compact));
    if (recordToolUse) {
        AIMessage useMsg;
        useMsg.role          = AIMessage::ToolUse;
        useMsg.toolCallId    = callId;
        useMsg.toolName      = name;
        useMsg.toolInputJson = inputJson;
        m_history.append(useMsg);
    }

    // Show friendly tool call label
    {
        QString summary = friendlyToolCall(name, input);
        QString detail;
        if (m_verboseMode) {
            // In verbose mode, show full JSON in the expandable detail section
            detail = QString("<span style='color:#484f58;'>%1</span>")
                .arg(inputJson.toHtmlEscaped().replace("\n", "<br>"));
        }
        // Create bubble and set tool call
        if (!m_sessionStarted) { m_sessionStarted = true; checkWelcome(); }
        auto *bubble = new BubbleWidget(BubbleWidget::Tool, m_chatContainer);
        bubble->setToolCall(summary, detail);
        int stretchIdx = m_chatLayout->count() - 1;
        m_chatLayout->insertWidget(stretchIdx, bubble);
        scrollToBottom();
    }

    // Intercept write/destructive tools — gate by current permission mode
    static const QStringList kWriteTools = {
        "set_map_values", "zero_map", "scale_map_values", "fill_map",
        "offset_map_values", "batch_modify_maps", "evaluate_map_expression",
        "apply_delta_to_rom", "undo_with_reason"
    };
    if (kWriteTools.contains(name)) {
        const PendingToolCall ptc{callId, name, input};
        switch (AppConfig::instance().aiPermissionMode) {
        case AppConfig::PermissionMode::Ask:
            showWriteConfirmation(ptc);
            return;
        case AppConfig::PermissionMode::AutoAccept:
            executeWriteToolImmediately(ptc, /*announce=*/true);
            return;
        case AppConfig::PermissionMode::Plan:
            rejectWriteToolForPlanMode(ptc);
            return;
        }
    }

    QString result = m_executor->execute(name, input);

    // Truncate only very large tool results — modern context windows handle 8k
    // easily, and the previous 2k cap was cutting JSON mid-array, which made
    // the model think operations had failed.
    QString truncResult = result;
    if (truncResult.size() > 8000) {
        truncResult = truncResult.left(8000) + "\n... (truncated, "
            + QString::number(result.size()) + " bytes total)";
    }

    AIMessage resMsg;
    resMsg.role           = AIMessage::ToolResult;
    resMsg.toolResultId   = callId;
    resMsg.toolResultName = name;
    resMsg.toolResultJson = truncResult;
    m_history.append(resMsg);

    // Try rich HTML rendering; fall back to plain text
    QString richHtml = richToolResultHtml(name, result);
    if (!m_sessionStarted) { m_sessionStarted = true; checkWelcome(); }
    auto *resBubble = new BubbleWidget(BubbleWidget::ToolResult, m_chatContainer);
    if (!richHtml.isEmpty()) {
        resBubble->setRichContent(richHtml);
    } else if (m_verboseMode) {
        // Verbose fallback: show JSON pretty-printed
        QString preview = result.left(500) + (result.size() > 500 ? "\n…" : "");
        resBubble->setRichContent(QString("<span style='font-family:Consolas;font-size:7pt;color:#8b949e;'>%1</span>")
            .arg(preview.toHtmlEscaped()));
    } else {
        // Normal fallback: compact single-line summary
        QJsonDocument doc = QJsonDocument::fromJson(result.toUtf8());
        QString summary;
        if (!doc.isNull()) {
            QJsonObject o = doc.object();
            if (o.contains("error"))
                summary = QString("⚠ %1").arg(o["error"].toString());
            else if (o.contains("success") && o["success"].toBool())
                summary = "✓ Done";
            else if (o.contains("count"))
                summary = QString("✓ %1 result(s)").arg(o["count"].toInt());
            else if (o.contains("results"))
                summary = QString("✓ %1 result(s)").arg(o["results"].toArray().size());
            else
                summary = result.left(200);
        } else {
            summary = result.left(200);
        }
        resBubble->setRichContent(QString("<span style='color:#8b949e;font-size:8pt;'>%1</span>")
            .arg(summary.toHtmlEscaped()));
    }
    int stretchIdx = m_chatLayout->count() - 1;
    m_chatLayout->insertWidget(stretchIdx, resBubble);
    scrollToBottom();
}

// ── Chat bubble helpers ───────────────────────────────────────────────────────

void AIAssistant::scrollToBottom()
{
    QTimer::singleShot(50, m_chatScroll, [this](){
        auto *sb = m_chatScroll->verticalScrollBar();
        sb->setValue(sb->maximum());
    });
}

void AIAssistant::appendMessage(const QString &role, const QString &text)
{
    if (!m_sessionStarted) { m_sessionStarted = true; checkWelcome(); }

    BubbleWidget::Role bRole = BubbleWidget::Assistant;
    if      (role == "user")        bRole = BubbleWidget::User;
    else if (role == "tool")        bRole = BubbleWidget::Tool;
    else if (role == "tool_result") bRole = BubbleWidget::ToolResult;

    auto *bubble = new BubbleWidget(bRole, m_chatContainer);
    if (bRole == BubbleWidget::Assistant)
        bubble->setMarkdownContent(text);
    else if (bRole == BubbleWidget::Tool)
        bubble->setToolCall(text.toHtmlEscaped());
    else if (bRole == BubbleWidget::ToolResult)
        bubble->setRichContent(text.toHtmlEscaped());
    else
        bubble->setPlainContent(text);

    int stretchIdx = m_chatLayout->count() - 1;
    m_chatLayout->insertWidget(stretchIdx, bubble);
    scrollToBottom();
}

void AIAssistant::startAssistantBubble()
{
    auto *bubble = new BubbleWidget(BubbleWidget::Assistant, m_chatContainer);
    bubble->setThinking(true);

    int stretchIdx = m_chatLayout->count() - 1;
    m_chatLayout->insertWidget(stretchIdx, bubble);
    m_streamingBubble = bubble;
    scrollToBottom();
}

void AIAssistant::appendChunk(const QString &chunk)
{
    if (!m_streamingBubble) return;

    // Stop the thinking animation on the first real text chunk
    if (!chunk.isEmpty() && m_accumulatedText.size() == chunk.size()) {
        m_streamingBubble->setThinking(false);
    }

    // Debounce markdown re-renders — batch chunks, render at most every 150ms
    if (!m_chunkTimer) {
        m_chunkTimer = new QTimer(this);
        m_chunkTimer->setSingleShot(true);
        m_chunkTimer->setInterval(150);
        connect(m_chunkTimer, &QTimer::timeout, this, [this]() {
            if (m_streamingBubble) {
                m_streamingBubble->setMarkdownContent(m_accumulatedText);
                scrollToBottom();
            }
        });
    }
    if (!m_chunkTimer->isActive())
        m_chunkTimer->start();
}

// ── Settings ──────────────────────────────────────────────────────────────────

void AIAssistant::loadSettings()
{
    QSettings s("CT14", "romHEX14");
    s.beginGroup(kSettingsGroup);
    int providerIdx = s.value("provider", 0).toInt();
    bool verbose    = s.value("verboseMode", false).toBool();
    s.endGroup();

    providerIdx = qBound(0, providerIdx, m_providerConfigs.size() - 1);
    m_currentProvider = providerIdx;

    m_providerCombo->blockSignals(true);
    m_providerCombo->setCurrentIndex(providerIdx);
    m_providerCombo->blockSignals(false);

    if (verbose) setVerboseMode(true);
    refreshSetupBanner();
}

void AIAssistant::refreshSetupBanner()
{
    if (!m_setupBanner) return;
    if (m_currentProvider < 0 || m_currentProvider >= m_providerConfigs.size()) {
        m_setupBanner->hide();
        return;
    }
    const ProviderConfig &cfg = m_providerConfigs[m_currentProvider];
    QSettings s("CT14", "romHEX14");
    s.beginGroup(kSettingsGroup);
    QByteArray apiKey = deobfuscate(s.value(cfg.name + "/apiKey").toByteArray());
    s.endGroup();
    const bool needsKey = cfg.isClaude
        || cfg.baseUrl.startsWith("https://api.")
        || cfg.baseUrl.startsWith("https://dash");
    m_setupBanner->setVisible(needsKey && apiKey.isEmpty());
}

void AIAssistant::saveSettings()
{
    QSettings s("CT14", "romHEX14");
    s.beginGroup(kSettingsGroup);
    s.setValue("provider", m_currentProvider);
    s.setValue("verboseMode", m_verboseMode);
    s.endGroup();
}

void AIAssistant::onSettingsClicked()
{
    int idx = m_currentProvider;
    if (idx < 0 || idx >= m_providerConfigs.size()) return;
    const ProviderConfig &cfg = m_providerConfigs[idx];

    QSettings s("CT14", "romHEX14");
    s.beginGroup(kSettingsGroup);
    QString savedKey     = QString::fromUtf8(deobfuscate(s.value(cfg.name + "/apiKey").toByteArray()));
    QString savedModel   = s.value(cfg.name + "/model", cfg.defaultModel).toString();
    QString savedBaseUrl = s.value(cfg.name + "/baseUrl", cfg.baseUrl).toString();
    s.endGroup();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("AI Provider Settings"));
    dlg.setMinimumWidth(460);
    dlg.setStyleSheet(
        "QDialog    { background:#0d1117; color:#c9d1d9; }"
        "QLabel     { color:#c9d1d9; background:transparent; }"
        "QLineEdit  { background:#161b22; color:#c9d1d9; border:1px solid #30363d; "
        "             border-radius:4px; padding:4px 8px; }"
        "QLineEdit:focus { border-color:#1f6feb; }"
        "QComboBox  { background:#21262d; color:#c9d1d9; border:1px solid #30363d; border-radius:4px; padding:3px 8px; }"
        "QComboBox QAbstractItemView { background:#21262d; color:#c9d1d9; }"
        "QPushButton { background:#1f6feb; color:#fff; border-radius:5px; padding:5px 16px; border:none; }"
        "QPushButton:hover { background:#388bfd; }"
        "QPushButton[text='Cancel'] { background:#21262d; color:#c9d1d9; border:1px solid #30363d; }"
    );

    auto *vlay = new QVBoxLayout(&dlg);
    vlay->setSpacing(12);

    // Header
    auto *hdr = new QLabel(tr("Configure AI provider"), &dlg);
    hdr->setStyleSheet("color:#e6edf3; font-size:11pt; font-weight:bold;");
    vlay->addWidget(hdr);

    auto *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);
    form->setSpacing(8);

    auto *provCombo = new QComboBox(&dlg);
    // Each item gets a coloured dot reflecting its compatibility tier. The
    // full tooltip spells out what the colour means so hover gives users the
    // same information the legend below the form provides.
    auto tierTooltip = [](int tier) {
        switch (tier) {
        case 0: return tr("Green \xE2\x80\x94 best compatibility: native API, full tool-calling and streaming.");
        case 1: return tr("Orange \xE2\x80\x94 good compatibility: OpenAI-compatible, most tools work.");
        default: return tr("Red \xE2\x80\x94 limited compatibility: some features may not work.");
        }
    };
    for (const ProviderConfig &pc : m_providerConfigs) {
        provCombo->addItem(tierDotIcon(pc.tier), pc.label);
        provCombo->setItemData(provCombo->count() - 1,
                               tierTooltip(pc.tier), Qt::ToolTipRole);
    }
    provCombo->setCurrentIndex(idx);
    form->addRow(tr("Provider:"), provCombo);

    auto *keyEdit = new QLineEdit(&dlg);
    keyEdit->setEchoMode(QLineEdit::Password);
    keyEdit->setText(savedKey);
    keyEdit->setPlaceholderText("sk-…");
    form->addRow(tr("API Key:"), keyEdit);

    auto *modelEdit = new QLineEdit(&dlg);
    modelEdit->setText(savedModel.isEmpty() ? cfg.defaultModel : savedModel);
    modelEdit->setPlaceholderText(cfg.defaultModel);
    form->addRow(tr("Model:"), modelEdit);

    auto *urlEdit = new QLineEdit(&dlg);
    urlEdit->setText(savedBaseUrl.isEmpty() ? cfg.baseUrl : savedBaseUrl);
    urlEdit->setPlaceholderText(cfg.baseUrl);
    urlEdit->setPlaceholderText(cfg.isClaude ? "https://api.anthropic.com" : cfg.baseUrl);
    form->addRow(tr("Base URL:"), urlEdit);

    vlay->addLayout(form);

    // ── Compatibility legend ────────────────────────────────────────────────
    // The coloured dot next to each provider tells the user, at a glance, how
    // well it works with the assistant. The legend says it out loud so users
    // never have to wonder what the dot means.
    auto *legend = new QFrame(&dlg);
    legend->setStyleSheet(
        "QFrame { background:#0d1117; border:1px solid #21262d; border-radius:6px; }");
    auto *legendLay = new QVBoxLayout(legend);
    legendLay->setContentsMargins(12, 10, 12, 10);
    legendLay->setSpacing(6);

    auto *legendTitle = new QLabel(
        tr("What the coloured dot next to each provider means:"), legend);
    legendTitle->setStyleSheet("color:#c9d1d9; font-size:9pt; font-weight:bold; background:transparent;");
    legendTitle->setWordWrap(true);
    legendLay->addWidget(legendTitle);

    auto legendRow = [&](int tier, const QString &title, const QString &desc) {
        auto *row = new QHBoxLayout();
        row->setSpacing(8);
        auto *dot = new QLabel(legend);
        QPixmap pm(12, 12);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(kTierColors[qBound(0, tier, 2)]);
        p.setPen(Qt::NoPen);
        p.drawEllipse(1, 1, 10, 10);
        p.end();
        dot->setPixmap(pm);
        dot->setFixedWidth(14);
        auto *text = new QLabel(
            QString("<b style='color:#e6edf3;'>%1</b> "
                    "<span style='color:#8b949e;'>%2</span>")
                .arg(title.toHtmlEscaped(), desc.toHtmlEscaped()),
            legend);
        text->setTextFormat(Qt::RichText);
        text->setWordWrap(true);
        text->setStyleSheet("font-size:9pt; background:transparent;");
        row->addWidget(dot, 0, Qt::AlignTop);
        row->addWidget(text, 1);
        legendLay->addLayout(row);
    };
    legendRow(0,
        tr("Green — best"),
        tr("Native API. Full tool-calling, streaming, and every assistant feature works. Recommended."));
    legendRow(1,
        tr("Orange — good"),
        tr("OpenAI-compatible. Most tools work, but some advanced behaviours may differ."));
    legendRow(2,
        tr("Red — limited"),
        tr("Community or local back-end. Tool-calling and streaming may behave unexpectedly."));

    vlay->addWidget(legend);

    auto *hint = new QLabel(tr("API keys are stored in application settings only."), &dlg);
    hint->setStyleSheet("color:#8b949e; font-size:8pt;");
    hint->setWordWrap(true);
    vlay->addWidget(hint);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    vlay->addWidget(btns);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    connect(provCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this, &dlg, provCombo, keyEdit, modelEdit, urlEdit](int newIdx) {
        if (newIdx < 0 || newIdx >= m_providerConfigs.size()) return;
        const ProviderConfig &nc = m_providerConfigs[newIdx];
        QSettings ss("CT14", "romHEX14");
        ss.beginGroup(kSettingsGroup);
        keyEdit->setText(QString::fromUtf8(deobfuscate(ss.value(nc.name + "/apiKey").toByteArray())));
        modelEdit->setText(ss.value(nc.name + "/model", nc.defaultModel).toString());
        urlEdit->setText(ss.value(nc.name + "/baseUrl", nc.baseUrl).toString());
        urlEdit->setEnabled(!nc.isClaude);
        ss.endGroup();
    });

    if (dlg.exec() != QDialog::Accepted) return;

    int chosenIdx = provCombo->currentIndex();
    if (chosenIdx < 0 || chosenIdx >= m_providerConfigs.size()) return;
    const ProviderConfig &chosen = m_providerConfigs[chosenIdx];

    QSettings ss("CT14", "romHEX14");
    ss.beginGroup(kSettingsGroup);
    ss.setValue(chosen.name + "/apiKey",  QString::fromLatin1(obfuscate(keyEdit->text().trimmed().toUtf8())));
    ss.setValue(chosen.name + "/model",   modelEdit->text().trimmed());
    ss.setValue(chosen.name + "/baseUrl", urlEdit->text().trimmed());
    ss.endGroup();

    // Switch provider combo in header
    m_providerCombo->blockSignals(true);
    m_providerCombo->setCurrentIndex(chosenIdx);
    m_providerCombo->blockSignals(false);
    m_currentProvider = chosenIdx;

    if (m_provider) { m_provider->abort(); m_provider->deleteLater(); m_provider.clear(); }
    saveSettings();
    loadSettings();  // re-runs refreshSetupBanner() with new provider state
}
