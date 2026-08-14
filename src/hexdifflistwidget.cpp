/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hexdifflistwidget.h"

#include <QAbstractTableModel>
#include <QTableView>
#include <QHeaderView>
#include <QLabel>
#include <QVBoxLayout>
#include <QStackedLayout>
#include <QFont>
#include <QFontDatabase>
#include <QLocale>
#include <QStringBuilder>
#include <QVariant>

namespace {

// Build a hex preview like "AB CD EF" (space separated, uppercase).
// If the run length is larger than `n`, append " ..." so callers can signal truncation.
static QString hexBytes(const char *p, qint64 n, bool moreAvailable)
{
    if (!p || n <= 0)
        return QString();

    static const char kHex[] = "0123456789ABCDEF";

    // Each byte -> 2 chars, plus a separating space between bytes.
    QString out;
    out.reserve(int(n * 3 + (moreAvailable ? 4 : 0)));
    for (qint64 i = 0; i < n; ++i) {
        if (i != 0)
            out.append(QLatin1Char(' '));
        const unsigned char b = static_cast<unsigned char>(p[i]);
        out.append(QLatin1Char(kHex[(b >> 4) & 0x0F]));
        out.append(QLatin1Char(kHex[b & 0x0F]));
    }
    if (moreAvailable)
        out.append(QStringLiteral(" ..."));
    return out;
}

static const QFont &monoFont()
{
    static QFont f = []() {
        QFont ff(QStringLiteral("Consolas"));
        ff.setStyleHint(QFont::Monospace);
        ff.setFixedPitch(true);
        ff.setPointSize(9);
        return ff;
    }();
    return f;
}

} // namespace

// ---------------------------------------------------------------------------
// Private model: virtualized table backing the diff-runs list.
// ---------------------------------------------------------------------------
class DiffRunModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit DiffRunModel(QObject *parent = nullptr)
        : QAbstractTableModel(parent) {}

    void setData(const QByteArray &ref, const QByteArray &cmp,
                 const QVector<DiffRun> &runs)
    {
        beginResetModel();
        m_ref  = ref;
        m_cmp  = cmp;
        m_runs = runs;
        endResetModel();
    }

    void clear()
    {
        beginResetModel();
        m_ref.clear();
        m_cmp.clear();
        m_runs.clear();
        endResetModel();
    }

    qint64 offsetAt(int row) const
    {
        if (row < 0 || row >= m_runs.size())
            return -1;
        return m_runs.at(row).offset;
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        if (parent.isValid())
            return 0;
        return m_runs.size();
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        if (parent.isValid())
            return 0;
        return 3;
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid())
            return {};
        const int row = index.row();
        const int col = index.column();
        if (row < 0 || row >= m_runs.size())
            return {};

        const DiffRun &run = m_runs.at(row);

        if (role == Qt::DisplayRole) {
            switch (col) {
            case 0:
                return QString::asprintf("0x%08llX",
                                         static_cast<unsigned long long>(run.offset));
            case 1:
                return QLocale().toString(static_cast<qint64>(run.length))
                     + QStringLiteral(" B");
            case 2: {
                const qint64 previewCount = qMin<qint64>(6, run.length);
                const bool more = run.length > 6;

                QString refHex, cmpHex;
                if (run.offset >= 0
                    && run.offset + previewCount <= m_ref.size()) {
                    refHex = hexBytes(m_ref.constData() + run.offset,
                                      previewCount, more);
                }
                if (run.offset >= 0
                    && run.offset + previewCount <= m_cmp.size()) {
                    cmpHex = hexBytes(m_cmp.constData() + run.offset,
                                      previewCount, more);
                }
                if (refHex.isEmpty() && cmpHex.isEmpty())
                    return QString();
                return QString(refHex % QStringLiteral("  \u2192  ") % cmpHex);
            }
            default:
                break;
            }
            return {};
        }

        if (role == Qt::TextAlignmentRole) {
            if (col == 0 || col == 1)
                return QVariant::fromValue(
                    int(Qt::AlignLeft | Qt::AlignVCenter));
            return QVariant::fromValue(
                int(Qt::AlignLeft | Qt::AlignVCenter));
        }

        if (role == Qt::FontRole) {
            return monoFont();
        }

        return {};
    }

    QVariant headerData(int section, Qt::Orientation orientation,
                        int role) const override
    {
        if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
            switch (section) {
            case 0: return HexDiffListWidget::tr("Address");
            case 1: return HexDiffListWidget::tr("Length");
            case 2: return HexDiffListWidget::tr("Preview");
            default: break;
            }
        }
        return QAbstractTableModel::headerData(section, orientation, role);
    }

private:
    QByteArray       m_ref;
    QByteArray       m_cmp;
    QVector<DiffRun> m_runs;
};

// ---------------------------------------------------------------------------
// HexDiffListWidget
// ---------------------------------------------------------------------------
HexDiffListWidget::HexDiffListWidget(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral("HexDiffListWidget { background:#0d1117; }"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_titleLabel = new QLabel(tr("Differences (0)"), this);
    m_titleLabel->setStyleSheet(QStringLiteral(
        "QLabel { background:#0d1117; color:#58a6ff; font-weight:bold;"
        " font-size:11pt; padding:4px;"
        " border-bottom:1px solid #30363d; }"));
    root->addWidget(m_titleLabel);

    // Stacked area: table page + empty-state page.
    auto *stackHost = new QWidget(this);
    auto *stack = new QStackedLayout(stackHost);
    stack->setContentsMargins(0, 0, 0, 0);
    stack->setSpacing(0);

    m_view = new QTableView(stackHost);
    m_model = new DiffRunModel(this);
    m_view->setModel(m_model);

    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setAlternatingRowColors(true);
    m_view->setShowGrid(false);
    m_view->setWordWrap(false);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_view->verticalHeader()->setVisible(false);
    m_view->verticalHeader()->setDefaultSectionSize(22);
    m_view->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);

    QHeaderView *hh = m_view->horizontalHeader();
    hh->setStretchLastSection(true);
    hh->setSectionsClickable(false);
    hh->setHighlightSections(false);
    hh->setFixedHeight(24);
    m_view->setColumnWidth(0, 110);
    m_view->setColumnWidth(1, 80);
    hh->setSectionResizeMode(0, QHeaderView::Interactive);
    hh->setSectionResizeMode(1, QHeaderView::Interactive);
    hh->setSectionResizeMode(2, QHeaderView::Stretch);

    m_view->setStyleSheet(QStringLiteral(
        "QTableView { background:#0d1117; color:#c9d1d9;"
        " border:1px solid #30363d;"
        " alternate-background-color:#161b22;"
        " gridline-color:transparent; }"
        "QTableView::item:selected { background:#1f6feb; color:white; }"
        "QHeaderView::section { background:#161b22; color:#8b949e;"
        " padding:4px 6px; border:none;"
        " border-bottom:1px solid #30363d; font-weight:normal; }"));

    m_emptyLabel = new QLabel(tr("No differences to list."), stackHost);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet(QStringLiteral(
        "QLabel { background:#0d1117; color:#8b949e;"
        " font-size:10pt; padding:12px; }"));

    stack->addWidget(m_view);        // index 0
    stack->addWidget(m_emptyLabel);  // index 1
    stack->setCurrentIndex(1);       // start empty

    root->addWidget(stackHost, 1);

    auto activate = [this](const QModelIndex &idx) {
        if (!idx.isValid())
            return;
        const qint64 off = m_model->offsetAt(idx.row());
        if (off >= 0)
            emit offsetActivated(off);
    };

    connect(m_view, &QTableView::activated, this, activate);
    connect(m_view, &QTableView::clicked,   this, activate);
}

void HexDiffListWidget::setData(const QByteArray &ref,
                                const QByteArray &cmp,
                                const QVector<DiffRun> &runs)
{
    m_model->setData(ref, cmp, runs);
    m_titleLabel->setText(
        tr("Differences (%1)")
            .arg(QLocale().toString(static_cast<qint64>(runs.size()))));

    auto *stack = qobject_cast<QStackedLayout *>(
        m_view->parentWidget()->layout());
    if (stack)
        stack->setCurrentIndex(runs.isEmpty() ? 1 : 0);
}

void HexDiffListWidget::clear()
{
    m_model->clear();
    m_titleLabel->setText(tr("Differences (0)"));

    auto *stack = qobject_cast<QStackedLayout *>(
        m_view->parentWidget()->layout());
    if (stack)
        stack->setCurrentIndex(1);
}

#include "hexdifflistwidget.moc"
