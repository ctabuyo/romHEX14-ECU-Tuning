/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "diffpanel.h"

#include "debug/DebugLog.h"
#include "project.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QColor>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QShowEvent>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <cstring>

namespace {

// ── Diff colour: red for B>A (positive delta), blue for B<A ────────────────
//
// First pass used a 5-level severity gradient (1-bit / small / medium / large
// / random) — user feedback: visually noisy, harder to scan than the WinOLS
// red/blue convention they're used to.  Two-colour mapping keeps the value
// columns immediately readable and matches the "I added 30 to this map"
// mental model better than a percentage bucket.
enum class DiffSign { Positive, Negative, Zero };

inline DiffSign classify(quint32 a, quint32 b, int wordSize, bool isSigned)
{
    if (a == b) return DiffSign::Zero;
    qint64 sa, sb;
    if (isSigned) {
        if      (wordSize == 1) { sa = static_cast<qint8>(a);  sb = static_cast<qint8>(b);  }
        else if (wordSize == 2) { sa = static_cast<qint16>(a); sb = static_cast<qint16>(b); }
        else                    { sa = static_cast<qint32>(a); sb = static_cast<qint32>(b); }
    } else {
        sa = static_cast<qint64>(a);
        sb = static_cast<qint64>(b);
    }
    return (sb > sa) ? DiffSign::Positive : DiffSign::Negative;
}

inline QColor colorFor(DiffSign s)
{
    switch (s) {
    case DiffSign::Positive: return QColor(0xff, 0x6b, 0x6b);   // red — B has more
    case DiffSign::Negative: return QColor(0x6b, 0xa8, 0xff);   // blue — B has less
    case DiffSign::Zero:     return QColor(0xe7, 0xee, 0xfc);   // neutral (unused — zero deltas not in list)
    }
    return QColor(0xe7, 0xee, 0xfc);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
//  ctor / UI
// ─────────────────────────────────────────────────────────────────────────────

DiffPanel::DiffPanel(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);   // for keyPressEvent (arrow nudges)
    buildUi();
}

void DiffPanel::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // ── Row 1: A vs B selectors ───────────────────────────────────────
    auto *rowAB = new QHBoxLayout();
    rowAB->setSpacing(4);
    rowAB->addWidget(new QLabel(tr("A:")));
    m_pickA = new QComboBox();
    m_pickA->setMinimumWidth(120);
    rowAB->addWidget(m_pickA, 1);
    rowAB->addWidget(new QLabel(tr("B:")));
    m_pickB = new QComboBox();
    m_pickB->setMinimumWidth(120);
    rowAB->addWidget(m_pickB, 1);
    root->addLayout(rowAB);

    // ── Row: B alignment (offset + nudges) ────────────────────────────
    //
    //   B offset:  [-256] [-16] [-1]  [   0   ]  [+1] [+16] [+256]   [Reset]
    //
    auto makeAlignRow = [this](const QString &label, QSpinBox *&spinOut,
                               bool forC) -> QWidget *
    {
        auto *holder = new QWidget();
        auto *lay = new QHBoxLayout(holder);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(2);

        auto *lbl = new QLabel(label);
        lbl->setStyleSheet("color:#8b949e;");
        lay->addWidget(lbl);

        auto makeBtn = [&](const QString &text, int delta) {
            auto *b = new QPushButton(text);
            b->setFixedWidth(28);
            b->setFocusPolicy(Qt::NoFocus);   // panel keeps focus for arrow keys
            b->setToolTip(tr("Nudge by %1 byte%2")
                              .arg(delta).arg(qAbs(delta) == 1 ? "" : "s"));
            connect(b, &QPushButton::clicked, this,
                    [this, delta, forC]() { onNudge(delta, forC); });
            lay->addWidget(b);
        };
        makeBtn(QStringLiteral("◀◀◀"), -256);
        makeBtn(QStringLiteral("◀◀"),  -16);
        makeBtn(QStringLiteral("◀"),   -1);

        spinOut = new QSpinBox();
        spinOut->setRange(-0x7FFFFFF, 0x7FFFFFF);
        spinOut->setSingleStep(1);
        spinOut->setAlignment(Qt::AlignRight);
        spinOut->setFixedWidth(90);
        spinOut->setKeyboardTracking(false);
        spinOut->setToolTip(tr("Offset in bytes; %1 = %1 + offset")
                                .arg(forC ? "C[address]" : "B[address]"));
        lay->addWidget(spinOut);

        makeBtn(QStringLiteral("▶"),    1);
        makeBtn(QStringLiteral("▶▶"),   16);
        makeBtn(QStringLiteral("▶▶▶"), 256);
        lay->addStretch(0);
        return holder;
    };

    root->addWidget(makeAlignRow(tr("B offset:"), m_spinOffsetB, /*forC*/false));
    m_rowAlignC = makeAlignRow(tr("C offset:"), m_spinOffsetC, /*forC*/true);
    m_rowAlignC->hide();   // shown when a target C is picked
    root->addWidget(m_rowAlignC);

    connect(m_spinOffsetB, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &DiffPanel::onOffsetSpinBChanged);
    connect(m_spinOffsetC, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &DiffPanel::onOffsetSpinCChanged);

    // ── Row 2: Target (C) + source A/B + Reset alignment ──────────────
    auto *rowC = new QHBoxLayout();
    rowC->setSpacing(4);
    rowC->addWidget(new QLabel(tr("Target:")));
    m_pickC = new QComboBox();
    m_pickC->setMinimumWidth(120);
    rowC->addWidget(m_pickC, 1);
    rowC->addWidget(new QLabel(tr("Source:")));
    m_srcA = new QRadioButton(tr("A"));
    m_srcB = new QRadioButton(tr("B"));
    m_srcA->setChecked(true);
    rowC->addWidget(m_srcA);
    rowC->addWidget(m_srcB);
    m_btnReset = new QPushButton(tr("Reset align"));
    m_btnReset->setFocusPolicy(Qt::NoFocus);
    m_btnReset->setToolTip(tr("Clear all alignment regions for this pair"));
    rowC->addWidget(m_btnReset);
    root->addLayout(rowC);

    // ── Row 3: Copy buttons ───────────────────────────────────────────
    auto *rowBtn = new QHBoxLayout();
    rowBtn->setSpacing(4);
    m_btnCopySel = new QPushButton(tr("Copy selected → Target"));
    m_btnCopyAll = new QPushButton(tr("Copy all → Target"));
    m_btnCopySel->setEnabled(false);
    m_btnCopyAll->setEnabled(false);
    rowBtn->addWidget(m_btnCopySel);
    rowBtn->addWidget(m_btnCopyAll);
    root->addLayout(rowBtn);

    // ── Severity legend ──────────────────────────────────────────────
    auto *legend = new QLabel(tr(
        "<span style='color:#ff6b6b;'>● B &gt; A (+)</span>&nbsp;&nbsp;"
        "<span style='color:#6ba8ff;'>● B &lt; A (−)</span>"));
    legend->setStyleSheet("font-size:8pt; color:#8b949e;");
    root->addWidget(legend);

    // ── Summary label ─────────────────────────────────────────────────
    m_summary = new QLabel(tr("No differences"));
    m_summary->setStyleSheet("color:#8b949e;");
    root->addWidget(m_summary);

    // ── Diff table ────────────────────────────────────────────────────
    m_table = new QTableWidget(0, 4);
    m_table->setHorizontalHeaderLabels(
        QStringList() << tr("Address") << tr("A") << tr("B") << tr("Δ"));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(16);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->setAlternatingRowColors(false);
    m_table->setShowGrid(false);
    QFont mono("Consolas", 9);
    mono.setStyleHint(QFont::Monospace);
    m_table->setFont(mono);
    m_table->setStyleSheet(
        "QTableWidget {"
        "  background: #0d1117;"
        "  color: #e7eefc;"
        "  gridline-color: transparent;"
        "  selection-background-color: #1f6feb;"
        "  selection-color: white;"
        "}"
        "QTableWidget::item { padding: 1px 6px; border: none; }"
        "QTableWidget::item:selected { background: #1f6feb; color: white; }"
        "QHeaderView::section {"
        "  background: #161b22;"
        "  color: #8b949e;"
        "  border: none;"
        "  padding: 2px 6px;"
        "  font-weight: normal;"
        "}");
    root->addWidget(m_table, 1);

    // ── Wiring ────────────────────────────────────────────────────────
    connect(m_pickA, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DiffPanel::onPickerChanged);
    connect(m_pickB, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DiffPanel::onPickerChanged);
    connect(m_pickC, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DiffPanel::onPickerChanged);
    connect(m_srcA, &QRadioButton::toggled,
            this, &DiffPanel::onPickerChanged);
    connect(m_btnCopySel, &QPushButton::clicked,
            this, &DiffPanel::onCopyClicked);
    connect(m_btnCopyAll, &QPushButton::clicked, this, [this]() {
        doCopy(false);
    });
    connect(m_btnReset, &QPushButton::clicked, this, &DiffPanel::onResetAlignment);
    connect(m_table, &QTableWidget::itemSelectionChanged,
            this, &DiffPanel::onSelectionChanged);
    connect(m_table, &QTableWidget::cellClicked,
            this, &DiffPanel::onTableActivated);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Public API
// ─────────────────────────────────────────────────────────────────────────────

void DiffPanel::setProjects(const QVector<Project *> &projects)
{
    // Drop alignment entries whose A or B project no longer exists — keeps
    // m_alignments from leaking memory across many open/close cycles.
    QSet<Project *> live;
    for (auto *p : projects) live.insert(p);
    for (auto it = m_alignments.begin(); it != m_alignments.end(); ) {
        if (!live.contains(it.key().first) || !live.contains(it.key().second))
            it = m_alignments.erase(it);
        else
            ++it;
    }

    m_projects = projects;
    rebuildPickers();
    syncOffsetSpinsFromMap();
    recompute();
    // Comparison overlays in waveform / hex depend on the currently picked
    // pair AND on every byte of both ROMs — anything that calls setProjects
    // (project opens/closes, data edits) must also refresh those overlays
    // so the user keeps seeing live colour feedback.
    emit alignmentChanged();
}

void DiffPanel::setActiveProject(Project *p)
{
    m_active = p;
    if (!m_pickA->count() || !projectFromCombo(m_pickA))
        rebuildPickers();
}

void DiffPanel::setDisplayParams(int dataSize, ByteOrder bo,
                                 int displayFmt, bool isSigned)
{
    if (dataSize    == m_dataSize
        && bo       == m_byteOrder
        && displayFmt == m_displayFmt
        && isSigned == m_isSigned) {
        return;
    }
    m_dataSize   = dataSize;
    m_byteOrder  = bo;
    m_displayFmt = displayFmt;
    m_isSigned   = isSigned;
    recompute();
}

Project *DiffPanel::projectA() const { return projectFromCombo(m_pickA); }
Project *DiffPanel::projectB() const { return projectFromCombo(m_pickB); }
Project *DiffPanel::projectC() const { return projectFromCombo(m_pickC); }

qint64 DiffPanel::mapAtoB(qint64 addrA) const
{
    auto it = m_alignments.constFind(makePairKey());
    if (it == m_alignments.constEnd() || it->isEmpty()) return addrA;
    qint64 r = it->mapAtoB(addrA);
    return r >= 0 ? r : addrA;
}

qint64 DiffPanel::mapAtoC(qint64 addrA) const
{
    auto it = m_alignments.constFind(makePairKey());
    if (it == m_alignments.constEnd() || it->isEmpty()) return addrA;
    qint64 r = it->mapAtoC(addrA);
    return r >= 0 ? r : addrA;
}

qint64 DiffPanel::globalDeltaToA(Project *p) const
{
    if (!p) return -1;
    if (p == projectA()) return 0;
    auto it = m_alignments.constFind(makePairKey());
    if (it == m_alignments.constEnd() || !it->isGlobal()) return -1;
    if (p == projectB()) return it->globalDeltaB();
    if (p == projectC()) return it->globalDeltaC();
    return -1;
}

qint64 DiffPanel::translate(Project *fromProj, qint64 fromAddr,
                            Project *toProj) const
{
    if (!fromProj || !toProj) return -1;
    if (fromProj == toProj) return fromAddr;
    qint64 fromDelta = globalDeltaToA(fromProj);
    qint64 toDelta   = globalDeltaToA(toProj);
    if (fromDelta < 0 || toDelta < 0) return -1;
    // fromAddr in fromProj corresponds to (fromAddr - fromDelta) in A.
    // That A-address corresponds to (A + toDelta) in toProj.
    return fromAddr - fromDelta + toDelta;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Pickers
// ─────────────────────────────────────────────────────────────────────────────

Project *DiffPanel::projectFromCombo(QComboBox *cb) const
{
    if (!cb) return nullptr;
    const int idx = cb->currentIndex();
    if (idx < 0) return nullptr;
    const auto v = cb->itemData(idx);
    return reinterpret_cast<Project *>(v.value<quintptr>());
}

void DiffPanel::rebuildPickers()
{
    auto fillCombo = [this](QComboBox *cb, bool allowNone) {
        Project *prev = projectFromCombo(cb);
        QSignalBlocker blk(cb);
        cb->clear();
        if (allowNone)
            cb->addItem(tr("— none —"), QVariant::fromValue<quintptr>(0));
        int wantedIdx = -1;
        for (int i = 0; i < m_projects.size(); ++i) {
            Project *p = m_projects[i];
            if (!p) continue;
            QString label = p->name;
            if (label.isEmpty()) label = QFileInfo(p->filePath).completeBaseName();
            cb->addItem(label, QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(p)));
            if (p == prev) wantedIdx = cb->count() - 1;
        }
        if (wantedIdx >= 0) cb->setCurrentIndex(wantedIdx);
    };

    fillCombo(m_pickA, /*allowNone*/false);
    fillCombo(m_pickB, /*allowNone*/false);
    fillCombo(m_pickC, /*allowNone*/true);

    if (!projectA() && m_pickA->count() > 0) {
        QSignalBlocker blk(m_pickA);
        int idx = 0;
        for (int i = 0; i < m_pickA->count(); ++i) {
            auto *p = reinterpret_cast<Project *>(m_pickA->itemData(i).value<quintptr>());
            if (p == m_active) { idx = i; break; }
        }
        m_pickA->setCurrentIndex(idx);
    }
    if (m_pickB->count() > 1 && projectFromCombo(m_pickB) == projectA()) {
        QSignalBlocker blk(m_pickB);
        for (int i = 0; i < m_pickB->count(); ++i) {
            auto *p = reinterpret_cast<Project *>(m_pickB->itemData(i).value<quintptr>());
            if (p && p != projectA()) { m_pickB->setCurrentIndex(i); break; }
        }
    }
    // C row only shown when C is picked
    m_rowAlignC->setVisible(projectC() != nullptr);
}

void DiffPanel::onPickerChanged()
{
    m_rowAlignC->setVisible(projectC() != nullptr);
    syncOffsetSpinsFromMap();
    recompute();
    // The pair (A, B) just changed — MainWindow has to refresh both
    // comparison-data overlays and any sync-driven scroll alignment.
    emit alignmentChanged();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Alignment
// ─────────────────────────────────────────────────────────────────────────────

DiffPanel::PairKey DiffPanel::makePairKey() const
{
    return qMakePair(projectA(), projectB());
}

AlignmentMap *DiffPanel::currentMap()
{
    Project *a = projectA();
    Project *b = projectB();
    if (!a || !b || a == b) return nullptr;
    PairKey k = qMakePair(a, b);
    auto it = m_alignments.find(k);
    if (it == m_alignments.end())
        it = m_alignments.insert(k, AlignmentMap{});
    return &it.value();
}

void DiffPanel::ensureGlobalRegion()
{
    AlignmentMap *m = currentMap();
    if (!m) return;
    if (!m->isEmpty()) return;
    Project *a = projectA();
    Project *b = projectB();
    if (!a || !b) return;
    const qint64 sz = qMin<qint64>(a->currentData.size(), b->currentData.size());
    m->setGlobal(0, 0, sz);
}

void DiffPanel::syncOffsetSpinsFromMap()
{
    QSignalBlocker blkB(m_spinOffsetB);
    QSignalBlocker blkC(m_spinOffsetC);
    AlignmentMap *m = currentMap();
    if (!m || !m->isGlobal()) {
        m_spinOffsetB->setValue(0);
        m_spinOffsetC->setValue(0);
        return;
    }
    m_spinOffsetB->setValue(static_cast<int>(m->globalDeltaB()));
    m_spinOffsetC->setValue(static_cast<int>(m->globalDeltaC()));
}

void DiffPanel::onOffsetSpinBChanged(int v)
{
    ensureGlobalRegion();
    AlignmentMap *m = currentMap();
    if (!m || !m->isGlobal()) return;
    AlignRegion r = m->regions()[0];
    r.deltaB = v;
    m->replaceRegion(0, r);
    recompute();
    emit alignmentChanged();   // MainWindow re-syncs B's view live
}

void DiffPanel::onOffsetSpinCChanged(int v)
{
    ensureGlobalRegion();
    AlignmentMap *m = currentMap();
    if (!m || !m->isGlobal()) return;
    AlignRegion r = m->regions()[0];
    r.deltaC = v;
    m->replaceRegion(0, r);
    recompute();
    emit alignmentChanged();
}

void DiffPanel::onNudge(int delta, bool forC)
{
    if (forC) m_spinOffsetC->setValue(m_spinOffsetC->value() + delta);
    else      m_spinOffsetB->setValue(m_spinOffsetB->value() + delta);
}

void DiffPanel::onResetAlignment()
{
    AlignmentMap *m = currentMap();
    if (!m) return;
    m->clear();
    syncOffsetSpinsFromMap();
    recompute();
    emit alignmentChanged();
}

void DiffPanel::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    if (m_recomputePending)
        recompute();
}

void DiffPanel::keyPressEvent(QKeyEvent *e)
{
    // Arrow keys nudge B's offset (the typical case).  Hold Shift for C.
    const bool forC = (e->modifiers() & Qt::ShiftModifier);
    int step = 1;
    if (e->modifiers() & Qt::ControlModifier) step = 16;
    if (e->modifiers() & (Qt::AltModifier))   step = 256;

    switch (e->key()) {
    case Qt::Key_Left:  onNudge(-step, forC); e->accept(); return;
    case Qt::Key_Right: onNudge(+step, forC); e->accept(); return;
    default: break;
    }
    QWidget::keyPressEvent(e);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Diff computation
// ─────────────────────────────────────────────────────────────────────────────

quint32 DiffPanel::readWord(const QByteArray &d, qint64 addr) const
{
    const int sz = wordSize();
    quint32 v = 0;
    if (m_byteOrder == ByteOrder::BigEndian) {
        for (int i = 0; i < sz; ++i)
            v = (v << 8) | static_cast<quint8>(d[addr + i]);
    } else {
        for (int i = sz - 1; i >= 0; --i)
            v = (v << 8) | static_cast<quint8>(d[addr + i]);
    }
    return v;
}

QString DiffPanel::formatAddr(qint64 addr) const
{
    return QString("0x%1").arg(addr, 8, 16, QChar('0')).toUpper().replace("0X", "0x");
}

QString DiffPanel::formatWord(quint32 v) const
{
    const int sz = wordSize();
    if (m_displayFmt == 1) {
        return QString("0x%1").arg(v, sz * 2, 16, QChar('0')).toUpper().replace("0X", "0x");
    }
    if (m_displayFmt == 2) {
        return QString("0b%1").arg(v, sz * 8, 2, QChar('0'));
    }
    if (m_isSigned) {
        qint32 sv = (sz == 1) ? static_cast<qint8>(v)
                : (sz == 2)   ? static_cast<qint16>(v)
                              : static_cast<qint32>(v);
        return QString::number(sv);
    }
    return QString::number(v);
}

QString DiffPanel::formatDelta(quint32 a, quint32 b) const
{
    const int sz = wordSize();
    auto signedOrRaw = [sz, this](quint32 v) -> qint64 {
        if (!m_isSigned) return static_cast<qint64>(v);
        if (sz == 1) return static_cast<qint8>(v);
        if (sz == 2) return static_cast<qint16>(v);
        return static_cast<qint32>(v);
    };
    qint64 sa = signedOrRaw(a);
    qint64 sb = signedOrRaw(b);
    qint64 d = sb - sa;
    return (d > 0 ? "+" : "") + QString::number(d);
}

void DiffPanel::recompute()
{
    // Defer expensive work while the panel is hidden.  Project list changes
    // (open / close / save / data edits) all funnel through here, and a full
    // byte-scan of two multi-MB ROMs blocks the UI thread for seconds.  When
    // the user actually shows the panel, showEvent() picks up m_recomputePending
    // and runs us once.
    if (!isVisible()) {
        m_recomputePending = true;
        return;
    }
    m_recomputePending = false;

    m_table->setRowCount(0);
    m_btnCopySel->setEnabled(false);
    m_btnCopyAll->setEnabled(false);

    Project *a = projectA();
    Project *b = projectB();

    if (!a || !b || a == b) {
        m_summary->setText(tr("Pick two different projects to compare"));
        return;
    }

    const QByteArray &da = a->currentData;
    const QByteArray &db = b->currentData;
    if (da.isEmpty() || db.isEmpty()) {
        m_summary->setText(tr("Empty data — nothing to compare"));
        return;
    }

    // Use the alignment map: for each addrA in A, look up addrB in B.
    // If no alignment region exists yet, fabricate a global one with
    // delta=0 so the first compare "just works" without any setup.
    ensureGlobalRegion();
    AlignmentMap *amap = currentMap();
    if (!amap) {
        m_summary->setText(tr("No alignment available"));
        return;
    }

    QElapsedTimer __t; __t.start();

    // QTableWidget bogs down past ~50k rows (each row = 4 QTableWidgetItem
    // allocations + 4 model signals).  Two unrelated ROMs can produce
    // millions of byte-level diffs, which would freeze the UI for tens of
    // seconds.  Cap what we display; the summary line tells the user the
    // real total.
    constexpr int kMaxDisplayRows = 50000;

    const int sz = wordSize();
    QVector<qint64>  diffAddrs;
    QVector<quint32> diffA, diffB;
    QVector<DiffSign> diffSign;
    diffAddrs.reserve(kMaxDisplayRows);
    diffA.reserve(kMaxDisplayRows);
    diffB.reserve(kMaxDisplayRows);
    diffSign.reserve(kMaxDisplayRows);

    // Total scan: counts every diff even past the display cap, so the
    // summary line shows the full picture.
    qint64 totalDiffs = 0;
    bool capped = false;

    for (const AlignRegion &reg : amap->regions()) {
        const qint64 aStart = reg.rangeAStart;
        const qint64 aEnd   = qMin<qint64>(reg.rangeAStart + reg.length, da.size());
        for (qint64 ai = aStart; ai + sz <= aEnd; ai += sz) {
            const qint64 bi = ai + reg.deltaB;
            if (bi < 0 || bi + sz > db.size()) continue;
            if (std::memcmp(da.constData() + ai, db.constData() + bi, sz) == 0)
                continue;
            ++totalDiffs;
            if (diffAddrs.size() >= kMaxDisplayRows) {
                capped = true;
                continue;
            }
            const quint32 va = readWord(da, ai);
            const quint32 vb = readWord(db, bi);
            diffAddrs.append(ai);
            diffA.append(va);
            diffB.append(vb);
            diffSign.append(classify(va, vb, sz, m_isSigned));
        }
    }
    const qint64 walkMs = __t.restart();

    m_table->setUpdatesEnabled(false);
    m_table->setRowCount(diffAddrs.size());
    for (int row = 0; row < diffAddrs.size(); ++row) {
        const qint64 addr = diffAddrs[row];
        const quint32 va = diffA[row];
        const quint32 vb = diffB[row];
        const QBrush fg(colorFor(diffSign[row]));

        auto *iAddr = new QTableWidgetItem(formatAddr(addr));
        iAddr->setData(Qt::UserRole, QVariant::fromValue<qint64>(addr));
        iAddr->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        auto *iA = new QTableWidgetItem(formatWord(va));
        auto *iB = new QTableWidgetItem(formatWord(vb));
        auto *iD = new QTableWidgetItem(formatDelta(va, vb));
        iA->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        iB->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        iD->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

        iA->setForeground(fg);
        iB->setForeground(fg);
        iD->setForeground(fg);

        m_table->setItem(row, 0, iAddr);
        m_table->setItem(row, 1, iA);
        m_table->setItem(row, 2, iB);
        m_table->setItem(row, 3, iD);
    }
    m_table->setUpdatesEnabled(true);
    qCInfo(catFind) << "DiffPanel::recompute walk=" << walkMs
                    << "ms populate=" << __t.elapsed()
                    << "ms diffs=" << totalDiffs
                    << " shown=" << diffAddrs.size();

    if (totalDiffs == 0) {
        m_summary->setText(tr("No differences"));
    } else {
        QString txt;
        if (capped) {
            txt = tr("%1 differences  (showing first %2 — word size %3)")
                      .arg(totalDiffs)
                      .arg(diffAddrs.size())
                      .arg(sz);
        } else {
            txt = tr("%1 differences  (word size: %2)")
                      .arg(totalDiffs).arg(sz);
        }
        if (amap->isGlobal()
            && (amap->globalDeltaB() != 0 || amap->globalDeltaC() != 0)) {
            txt += tr("   ·   ΔB=%1, ΔC=%2")
                       .arg(amap->globalDeltaB()).arg(amap->globalDeltaC());
        } else if (!amap->isGlobal()) {
            txt += tr("   ·   %1 region(s)").arg(amap->regionCount());
        }
        m_summary->setText(txt);
    }

    Project *c = projectC();
    m_btnCopyAll->setEnabled(!diffAddrs.isEmpty() && c && c->currentData.size() > 0);
    onSelectionChanged();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Selection / clicks
// ─────────────────────────────────────────────────────────────────────────────

void DiffPanel::onSelectionChanged()
{
    Project *c = projectC();
    bool anySelected = !m_table->selectionModel()->selectedRows().isEmpty();
    m_btnCopySel->setEnabled(anySelected && c && c->currentData.size() > 0);
}

void DiffPanel::onTableActivated(int row, int /*col*/)
{
    auto *itm = m_table->item(row, 0);
    if (!itm) return;
    const qint64 addr = itm->data(Qt::UserRole).toLongLong();
    if (addr < 0) return;
    emit rowActivated(static_cast<quint32>(addr));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Copy
// ─────────────────────────────────────────────────────────────────────────────

void DiffPanel::onCopyClicked()
{
    doCopy(true);
}

void DiffPanel::doCopy(bool selectedOnly)
{
    Project *src = m_srcA->isChecked() ? projectA() : projectB();
    Project *dst = projectC();
    if (!src || !dst || src == dst) return;
    if (src->currentData.isEmpty() || dst->currentData.isEmpty()) return;

    // Each diff row is keyed by an A-side address.  Translate it to the
    // src-side and dst-side addresses via the alignment map, so copying
    // works correctly even when src is B (which sits at a different
    // delta) or dst is C (independent delta).
    AlignmentMap *amap = currentMap();
    if (!amap) return;
    const bool srcIsB = (src == projectB());

    QVector<qint64> rowsA;
    if (selectedOnly) {
        const auto rows = m_table->selectionModel()->selectedRows(0);
        rowsA.reserve(rows.size());
        for (const QModelIndex &mi : rows) {
            auto *itm = m_table->item(mi.row(), 0);
            if (itm) rowsA.append(itm->data(Qt::UserRole).toLongLong());
        }
    } else {
        rowsA.reserve(m_table->rowCount());
        for (int r = 0; r < m_table->rowCount(); ++r) {
            auto *itm = m_table->item(r, 0);
            if (itm) rowsA.append(itm->data(Qt::UserRole).toLongLong());
        }
    }
    if (rowsA.isEmpty()) return;

    const int sz = wordSize();
    QVector<QPair<int, QByteArray>> patches;
    for (qint64 addrA : rowsA) {
        qint64 srcAddr = srcIsB ? amap->mapAtoB(addrA) : addrA;
        qint64 dstAddr = amap->mapAtoC(addrA);
        if (srcAddr < 0 || dstAddr < 0) continue;
        if (srcAddr + sz > src->currentData.size())  continue;
        if (dstAddr + sz > dst->currentData.size())  continue;
        const QByteArray bytes = src->currentData.mid(int(srcAddr), sz);
        patches.append({int(dstAddr), bytes});
    }
    if (patches.isEmpty()) return;

    dst->applyRomPatches(patches, selectedOnly ? tr("Apply selected diffs (%1 items)").arg(patches.size())
                                               : tr("Apply all diffs (%1 items)").arg(patches.size()));
    emit copyApplied(dst, patches.size());

    if (dst == projectA() || dst == projectB())
        recompute();
}
