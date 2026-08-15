/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "aboutdialog.h"
#include "appconstants.h"
#include <QCoreApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QFont>
#include <QWidget>
#include <QSettings>
#include <QCloseEvent>

// ── Helpers ──────────────────────────────────────────────────────────────────
namespace {

QFrame *makeCard(const QString &extraStyle = QString())
{
    auto *card = new QFrame();
    card->setObjectName("aboutCard");
    card->setStyleSheet(
        QString("QFrame#aboutCard {"
                "  background: #161b22;"
                "  border: 1px solid #30363d;"
                "  border-radius: 10px;"
                "} %1").arg(extraStyle));
    return card;
}

QLabel *makeChip(const QString &text)
{
    auto *chip = new QLabel(text);
    chip->setAlignment(Qt::AlignCenter);
    chip->setStyleSheet(
        "QLabel {"
        "  color: #8b949e;"
        "  background: rgba(88, 166, 255, 0.08);"
        "  border: 1px solid #30363d;"
        "  border-radius: 9px;"
        "  padding: 2px 10px;"
        "  font-size: 8pt;"
        "  font-weight: bold;"
        "  letter-spacing: 0.8px;"
        "}");
    return chip;
}

QPixmap makeAvatar(int D)
{
    QPixmap src(":/author-1.jpg");
    QPixmap out(D, D);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    QPainterPath clip;
    clip.addEllipse(2, 2, D - 4, D - 4);

    if (!src.isNull()) {
        p.setClipPath(clip);
        QPixmap scaled = src.scaled(D - 4, D - 4,
                                    Qt::KeepAspectRatioByExpanding,
                                    Qt::SmoothTransformation);
        int xo = (scaled.width()  - (D - 4)) / 2;
        int yo = (scaled.height() - (D - 4)) / 2;
        p.drawPixmap(2 - xo, 2 - yo, scaled);
        p.setClipping(false);
    } else {
        // Fallback: gradient circle with initials "CT"
        QLinearGradient g(0, 0, D, D);
        g.setColorAt(0.0, QColor("#1f6feb"));
        g.setColorAt(1.0, QColor("#0d1117"));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(2, 2, D - 4, D - 4);
        p.setPen(QColor("#c9d1d9"));
        QFont f("Segoe UI", D / 4, QFont::Bold);
        p.setFont(f);
        p.drawText(QRect(0, 0, D, D), Qt::AlignCenter, "CT");
    }

    // Outer ring (gradient)
    QLinearGradient ringGrad(0, 0, D, D);
    ringGrad.setColorAt(0.0, QColor("#58a6ff"));
    ringGrad.setColorAt(1.0, QColor("#1f6feb"));
    QPen ringPen(QBrush(ringGrad), 2.0);
    p.setPen(ringPen);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(1, 1, D - 2, D - 2);

    return out;
}

} // namespace

// ── Dialog ───────────────────────────────────────────────────────────────────
AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent, Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint)
{
    setWindowTitle(tr("About romHEX14"));
    setMinimumSize(700, 600);
    resize(700, 600);
    setStyleSheet("QDialog { background: #0d1117; }");

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Header strip ──────────────────────────────────────────────────────────
    auto *header = new QWidget();
    header->setObjectName("aboutHeader");
    header->setFixedHeight(68);
    header->setStyleSheet(
        "QWidget#aboutHeader {"
        "  background: #0d1117;"
        "  border-bottom: 1px solid #21262d;"
        "}");
    auto *hLay = new QHBoxLayout(header);
    hLay->setContentsMargins(24, 14, 24, 14);
    hLay->setSpacing(12);

    // App icon tile with hex glyph
    auto *tile = new QLabel(QString::fromUtf8("\xE2\xAC\xA2")); // ⬢
    tile->setFixedSize(40, 40);
    tile->setAlignment(Qt::AlignCenter);
    tile->setStyleSheet(
        "QLabel {"
        "  color: #58a6ff;"
        "  background: rgba(88, 166, 255, 0.12);"
        "  border: 1px solid rgba(88, 166, 255, 0.35);"
        "  border-radius: 10px;"
        "  font-size: 18pt;"
        "  font-weight: bold;"
        "}");
    hLay->addWidget(tile, 0, Qt::AlignVCenter);

    // Wordmark + tagline stack
    auto *wm = new QVBoxLayout();
    wm->setSpacing(2);
    wm->setContentsMargins(0, 0, 0, 0);

    auto *wordmark = new QLabel("romHEX14");
    wordmark->setStyleSheet(
        "color: #c9d1d9; font-size: 15pt; font-weight: 800; "
        "letter-spacing: 1px; background: transparent;");
    wm->addWidget(wordmark);

    auto *tagline = new QLabel(
        tr("AI Assisted Hex Editor  \u00B7  Powered by CT14 Garage"));
    tagline->setStyleSheet(
        "color: #8b949e; font-size: 8pt; background: transparent; "
        "letter-spacing: 0.4px;");
    wm->addWidget(tagline);

    hLay->addLayout(wm);
    hLay->addStretch();

    // Version pill
    auto *verPill = new QLabel("v " + QCoreApplication::applicationVersion());
    verPill->setAlignment(Qt::AlignCenter);
    verPill->setStyleSheet(
        "QLabel {"
        "  color: #3fb950;"
        "  background: rgba(63, 185, 80, 0.12);"
        "  border: 1px solid rgba(63, 185, 80, 0.35);"
        "  border-radius: 9px;"
        "  padding: 2px 10px;"
        "  font-size: 9pt;"
        "  font-weight: bold;"
        "  letter-spacing: 0.5px;"
        "}");
    hLay->addWidget(verPill, 0, Qt::AlignVCenter);

    root->addWidget(header);

    // ── Content area ──────────────────────────────────────────────────────────
    auto *content = new QWidget();
    content->setStyleSheet("background: #0d1117;");
    auto *cl = new QVBoxLayout(content);
    cl->setContentsMargins(24, 20, 24, 16);
    cl->setSpacing(14);

    // Description line (below header)
    auto *descL = new QLabel(
        tr("Multi-project ECU ROM editor with A2L support."));
    descL->setStyleSheet(
        "color: #c9d1d9; font-size: 10pt; background: transparent;");
    descL->setWordWrap(true);
    cl->addWidget(descL);

    // ── Features card ─────────────────────────────────────────────────────────
    auto *featCard = makeCard();
    auto *featLay = new QVBoxLayout(featCard);
    featLay->setContentsMargins(20, 14, 20, 14);
    featLay->setSpacing(10);

    auto *featHdr = new QLabel(tr("HIGHLIGHTS"));
    featHdr->setStyleSheet(
        "color: #8b949e; font-size: 8pt; font-weight: bold; "
        "letter-spacing: 2px; background: transparent;");
    featLay->addWidget(featHdr);

    struct Feature { QString text; };
    const Feature features[] = {
        { tr("Multi-project MDI workspace with ROM comparison") },
        { tr("A2L map import — group structure & auto-grouping") },
        { tr("Version snapshots — save and restore ROM states")  },
        { tr("2D waveform & interactive 3D map visualization")   },
        { tr("Heat-map cell display with custom scaling factors") },
        { tr("Hex editor with map region highlighting")          },
    };

    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(8);
    grid->setContentsMargins(0, 0, 0, 0);

    for (int i = 0; i < 6; ++i) {
        auto *row = new QWidget();
        row->setStyleSheet("background: transparent;");
        auto *rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(8);

        auto *glyph = new QLabel("\u2B22"); // ⬢ black hexagon
        glyph->setFixedWidth(16);
        glyph->setStyleSheet(
            "color: #58a6ff; font-size: 11pt; background: transparent; "
            "font-weight: bold;");
        glyph->setAlignment(Qt::AlignCenter);

        auto *txt = new QLabel(features[i].text);
        txt->setStyleSheet(
            "color: #c9d1d9; font-size: 9pt; background: transparent;");
        txt->setWordWrap(true);

        rl->addWidget(glyph, 0, Qt::AlignTop);
        rl->addWidget(txt, 1);

        grid->addWidget(row, i % 3, i / 3);
    }
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    featLay->addLayout(grid);

    cl->addWidget(featCard);

    // ── Author card ───────────────────────────────────────────────────────────
    auto *authCard = new QFrame();
    authCard->setObjectName("authorCard");
    authCard->setStyleSheet(
        "QFrame#authorCard {"
        "  border: 1px solid #30363d;"
        "  border-radius: 12px;"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1, "
        "    stop:0 #161b22, stop:1 #0d1117);"
        "}");
    auto *aLay = new QHBoxLayout(authCard);
    aLay->setContentsMargins(20, 18, 20, 18);
    aLay->setSpacing(18);

    // Avatar
    const int D = 96;
    auto *avatar = new QLabel();
    avatar->setFixedSize(D, D);
    avatar->setStyleSheet("background: transparent; border: none;");
    avatar->setPixmap(makeAvatar(D));
    aLay->addWidget(avatar, 0, Qt::AlignTop);

    // Right column
    auto *aRight = new QVBoxLayout();
    aRight->setSpacing(3);
    aRight->setContentsMargins(0, 0, 0, 0);

    auto *nameRow = new QHBoxLayout();
    nameRow->setSpacing(10);

    auto *enName = new QLabel("CTABUYO");
    enName->setStyleSheet(
        "color: #c9d1d9; font-size: 16pt; font-weight: 800; "
        "letter-spacing: 2px; background: transparent;");
    nameRow->addWidget(enName, 0, Qt::AlignBottom);

    auto *cnName = new QLabel(QString::fromUtf8("\xE5\x88\x98\xE5\xBD\xA6\xE9\xB9\x8F")); // 刘彦鹏
    cnName->setStyleSheet(
        "color: #f0883e; font-size: 13pt; font-weight: 700; "
        "background: transparent; padding-bottom: 2px;");
    nameRow->addWidget(cnName, 0, Qt::AlignBottom);
    nameRow->addStretch();
    aRight->addLayout(nameRow);

    auto *role = new QLabel(tr("Creator  \u00B7  Lead Developer"));
    role->setStyleSheet(
        "color: #8b949e; font-size: 9pt; background: transparent; "
        "letter-spacing: 1px; font-weight: 600;");
    aRight->addWidget(role);

    aRight->addSpacing(6);

    auto *bio = new QLabel(tr(
        "Automotive software engineer and founder of CT14 Garage. "
        "Passionate about ECU tuning, reverse engineering, and building "
        "tools that bridge AI with deep automotive expertise."));
    bio->setStyleSheet(
        "color: #c9d1d9; font-size: 9pt; background: transparent;");
    bio->setWordWrap(true);
    aRight->addWidget(bio);

    aRight->addSpacing(8);

    // Chips row
    auto *chipsRow = new QHBoxLayout();
    chipsRow->setSpacing(6);
    chipsRow->setContentsMargins(0, 0, 0, 0);
    chipsRow->addWidget(makeChip(tr("CT14 GARAGE")));
    chipsRow->addWidget(makeChip(tr("BANGKOK")));
    chipsRow->addStretch();
    aRight->addLayout(chipsRow);

    aLay->addLayout(aRight, 1);

    cl->addWidget(authCard);

    // ── Easter egg card (Chinese-only) ────────────────────────────────────────
    const QString ee = tr("%ABOUT_EXTRA%");
    if (ee != "%ABOUT_EXTRA%" && !ee.isEmpty()) {
        auto *eeCard = new QFrame();
        eeCard->setObjectName("eeCard");
        eeCard->setStyleSheet(
            "QFrame#eeCard {"
            "  background: #161b22;"
            "  border: 1px solid #30363d;"
            "  border-left: 3px solid #f0883e;"
            "  border-radius: 8px;"
            "}");
        auto *eeLay = new QHBoxLayout(eeCard);
        eeLay->setContentsMargins(14, 10, 14, 10);
        eeLay->setSpacing(10);

        auto *eeIcon = new QLabel("\u2B22"); // hex glyph
        eeIcon->setStyleSheet(
            "color: #f0883e; font-size: 14pt; background: transparent;");
        eeLay->addWidget(eeIcon, 0, Qt::AlignVCenter);

        auto *eeL = new QLabel(ee);
        eeL->setStyleSheet(
            "color: #f0883e; font-size: 11pt; font-weight: bold; "
            "background: transparent;");
        eeL->setWordWrap(true);
        eeLay->addWidget(eeL, 1);

        cl->addWidget(eeCard);
    }

    // ── Contributors ─────────────────────────────────────────────────────────
    {
        auto *contribCard = new QFrame();
        contribCard->setObjectName("contribCard");
        contribCard->setStyleSheet(
            "QFrame#contribCard {"
            "  background: #161b22;"
            "  border: 1px solid #30363d;"
            "  border-radius: 8px;"
            "}");
        auto *contribLay = new QVBoxLayout(contribCard);
        contribLay->setContentsMargins(16, 12, 16, 12);
        contribLay->setSpacing(8);

        auto *contribTitle = new QLabel(tr("Contributors"));
        contribTitle->setStyleSheet(
            "color: #e6edf3; font-size: 12pt; font-weight: bold; background: transparent;");
        contribLay->addWidget(contribTitle);

        struct Contributor { QString name; QString desc; };
        QVector<Contributor> contributors = {
            { QStringLiteral("ogorek"),
              tr("Edit ops, annotations, savepoints, diff panel, map fingerprint, bulk edit") },
            { QStringLiteral("Mordaur"),
              tr("ADS docking workspace, centralized patch engine, multi-view sync, versioned KP codec, Dutch translation") },
        };

        for (const auto &c : contributors) {
            auto *row = new QHBoxLayout();
            row->setSpacing(8);
            auto *nameLabel = new QLabel(QStringLiteral("\u2022 <b>%1</b>").arg(c.name));
            nameLabel->setStyleSheet("color: #58a6ff; font-size: 10pt; background: transparent;");
            nameLabel->setTextFormat(Qt::RichText);
            auto *descLabel = new QLabel(QStringLiteral("\u2014 %1").arg(c.desc));
            descLabel->setStyleSheet("color: #8b949e; font-size: 10pt; background: transparent;");
            descLabel->setWordWrap(true);
            row->addWidget(nameLabel);
            row->addWidget(descLabel, 1);
            contribLay->addLayout(row);
        }

        cl->addWidget(contribCard);
    }

    cl->addStretch();

    // ── Footer ────────────────────────────────────────────────────────────────
    auto *footer = new QHBoxLayout();
    footer->setContentsMargins(0, 4, 0, 0);
    auto *copy = new QLabel(
        tr("\u00A9 2025 CT14 GARAGE CO., LTD — Bangkok, Thailand. All rights reserved."));
    copy->setStyleSheet(
        "color: #484f58; font-size: 8pt; background: transparent;");
    footer->addWidget(copy);
    footer->addStretch();

    auto *closeBtn = new QPushButton(tr("Close"));
    closeBtn->setFixedWidth(100);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(
        "QPushButton {"
        "  background: #1f6feb; color: #ffffff;"
        "  border: 1px solid #1f6feb; border-radius: 6px;"
        "  padding: 7px 18px; font-size: 9pt; font-weight: bold;"
        "  letter-spacing: 0.5px; }"
        "QPushButton:hover  { background: #388bfd; border-color: #388bfd; }"
        "QPushButton:pressed{ background: #1158c7; border-color: #1158c7; }");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    footer->addWidget(closeBtn);
    cl->addLayout(footer);

    root->addWidget(content, 1);

    restoreGeometry(rx14::appSettings()
                    .value("dialogGeometry/AboutDialog").toByteArray());
}

void AboutDialog::closeEvent(QCloseEvent *event)
{
    rx14::appSettings()
        .setValue("dialogGeometry/AboutDialog", saveGeometry());
    QDialog::closeEvent(event);
}
