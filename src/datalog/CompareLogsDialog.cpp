#include "CompareLogsDialog.h"
#include "LogReader.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>
#include <QFileDialog>
#include <QFileInfo>

namespace datalog {

CompareLogsDialog::CompareLogsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Compare datalogs"));
    resize(720, 480);
    buildUi();
}

void CompareLogsDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    auto *picks = new QHBoxLayout;
    m_pickA = new QPushButton(tr("Pick log A…"), this);
    m_pickB = new QPushButton(tr("Pick log B…"), this);
    picks->addWidget(m_pickA);
    picks->addWidget(m_pickB);
    root->addLayout(picks);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setWordWrap(true);
    root->addWidget(m_summaryLabel);

    m_diffTable = new QTableWidget(this);
    m_diffTable->setColumnCount(4);
    m_diffTable->setHorizontalHeaderLabels({tr("Metric"), tr("Log A"), tr("Log B"), tr("Δ (B − A)")});
    m_diffTable->horizontalHeader()->setStretchLastSection(true);
    m_diffTable->verticalHeader()->setVisible(false);
    m_diffTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(m_diffTable);

    auto *btns = new QHBoxLayout;
    btns->addStretch();
    auto *closeBtn = new QPushButton(tr("Close"), this);
    btns->addWidget(closeBtn);
    root->addLayout(btns);

    connect(m_pickA, &QPushButton::clicked, this, &CompareLogsDialog::onPickA);
    connect(m_pickB, &QPushButton::clicked, this, &CompareLogsDialog::onPickB);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void CompareLogsDialog::onPickA()
{
    QString p = QFileDialog::getOpenFileName(this, tr("Pick log A"),
                                             QString(),
                                             tr("Datalog CSV — VehiCAL / Autotuner / VCDS (*.csv);;All files (*)"));
    if (p.isEmpty()) return;
    m_pathA = p;
    QString err;
    m_a = LogReader::read(p, &err);
    m_familyA = detectFamily(m_a);
    m_pullsA  = PullDetector::detect(m_a, m_familyA);
    m_statsA  = PullDetector::statsForAll(m_pullsA, m_a, m_familyA);
    m_pickA->setText(tr("A: %1").arg(QFileInfo(p).fileName()));
    recompute();
}

void CompareLogsDialog::onPickB()
{
    QString p = QFileDialog::getOpenFileName(this, tr("Pick log B"),
                                             QString(),
                                             tr("Datalog CSV — VehiCAL / Autotuner / VCDS (*.csv);;All files (*)"));
    if (p.isEmpty()) return;
    m_pathB = p;
    QString err;
    m_b = LogReader::read(p, &err);
    m_familyB = detectFamily(m_b);
    m_pullsB  = PullDetector::detect(m_b, m_familyB);
    m_statsB  = PullDetector::statsForAll(m_pullsB, m_b, m_familyB);
    m_pickB->setText(tr("B: %1").arg(QFileInfo(p).fileName()));
    recompute();
}

bool CompareLogsDialog::loadPair(const QString &pathA, const QString &pathB, QString *err)
{
    m_pathA = pathA; m_pathB = pathB;
    m_a = LogReader::read(pathA, err);
    if (m_a.isEmpty()) return false;
    m_b = LogReader::read(pathB, err);
    if (m_b.isEmpty()) return false;
    m_familyA = detectFamily(m_a);
    m_familyB = detectFamily(m_b);
    m_pullsA  = PullDetector::detect(m_a, m_familyA);
    m_pullsB  = PullDetector::detect(m_b, m_familyB);
    m_statsA  = PullDetector::statsForAll(m_pullsA, m_a, m_familyA);
    m_statsB  = PullDetector::statsForAll(m_pullsB, m_b, m_familyB);
    m_pickA->setText(tr("A: %1").arg(QFileInfo(pathA).fileName()));
    m_pickB->setText(tr("B: %1").arg(QFileInfo(pathB).fileName()));
    recompute();
    return true;
}

int CompareLogsDialog::bestPullIndex(const QVector<Pull> &pulls) const
{
    if (pulls.isEmpty()) return -1;
    int best = 0;
    double bestDuration = pulls[0].timeEndMs - pulls[0].timeStartMs;
    for (int i = 1; i < pulls.size(); ++i) {
        double d = pulls[i].timeEndMs - pulls[i].timeStartMs;
        if (d > bestDuration) { bestDuration = d; best = i; }
    }
    return best;
}

void CompareLogsDialog::recompute()
{
    m_diffTable->setRowCount(0);

    if (m_a.isEmpty() || m_b.isEmpty()) {
        m_summaryLabel->setText(tr("Pick two log files to compare."));
        return;
    }

    int bestA = bestPullIndex(m_pullsA);
    int bestB = bestPullIndex(m_pullsB);
    if (bestA < 0 || bestB < 0) {
        m_summaryLabel->setText(tr("No pulls detected in one or both logs — cannot compare."));
        return;
    }

    const PullStats &sa = m_statsA[bestA];
    const PullStats &sb = m_statsB[bestB];

    m_summaryLabel->setText(tr("Comparing best pull from each log (max-duration pull picked).\n"
                               "A family: %1 · B family: %2")
                            .arg(familyName(m_familyA), familyName(m_familyB)));

    auto add = [&](const QString &metric, double a, double b, const QString &unit, int prec){
        int r = m_diffTable->rowCount();
        m_diffTable->insertRow(r);
        m_diffTable->setItem(r, 0, new QTableWidgetItem(metric));
        m_diffTable->setItem(r, 1, new QTableWidgetItem(QStringLiteral("%1 %2").arg(a, 0, 'f', prec).arg(unit)));
        m_diffTable->setItem(r, 2, new QTableWidgetItem(QStringLiteral("%1 %2").arg(b, 0, 'f', prec).arg(unit)));
        double d = b - a;
        QString sign = (d > 0) ? QStringLiteral("+") : QString();
        m_diffTable->setItem(r, 3, new QTableWidgetItem(QStringLiteral("%1%2 %3").arg(sign).arg(d, 0, 'f', prec).arg(unit)));
    };

    add(tr("Peak boost"),     sa.peakBoostBar,        sb.peakBoostBar,        QStringLiteral("bar"), 2);
    add(tr("Peak torque"),    sa.peakTorqueNm,        sb.peakTorqueNm,        QStringLiteral("Nm"),  1);
    add(tr("Peak IAT"),       sa.peakIntakeAirTempC,  sb.peakIntakeAirTempC,  QStringLiteral("°C"),  1);
    add(tr("AFR @ peak boost"), sa.afrAtPeakBoost,    sb.afrAtPeakBoost,      QString(),             2);
    add(tr("Knock retard ∑"), sa.knockRetardSumDeg,   sb.knockRetardSumDeg,   QStringLiteral("°·s"), 2);
    add(tr("Pull duration"),  (sa.span.timeEndMs - sa.span.timeStartMs) / 1000.0,
                              (sb.span.timeEndMs - sb.span.timeStartMs) / 1000.0,
                              QStringLiteral("s"), 2);
    add(tr("Peak RPM"),       sa.span.rpmPeak,        sb.span.rpmPeak,        QStringLiteral("rpm"), 0);

    m_diffTable->resizeColumnsToContents();
    m_diffTable->horizontalHeader()->setStretchLastSection(true);
}

} // namespace datalog
