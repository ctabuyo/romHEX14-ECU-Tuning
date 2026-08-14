/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "checksumelectdlg.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <QComboBox>
#include <QSettings>
#include <QCloseEvent>
#include <algorithm>

ChecksumSelectDlg::ChecksumSelectDlg(const QByteArray& rom, const QString& ecuType, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Select Checksum Algorithm"));
    setMinimumWidth(520);

    // Collect all non-empty DLL entries
    const auto& allDlls = ChecksumManager::instance()->allDlls();
    QVector<ChecksumDllInfo> nonEmpty;
    nonEmpty.reserve(allDlls.size());
    for (const auto& d : allDlls) {
        if (!d.description.isEmpty())
            nonEmpty.append(d);
    }

    // Find best match
    ChecksumDllInfo bestMatch = ChecksumManager::instance()->autoDetect(rom, ecuType);

    // Build sorted list: best match first, then the rest alphabetically
    QVector<ChecksumDllInfo> sorted;
    sorted.reserve(nonEmpty.size());
    if (bestMatch.devNum != 0) {
        sorted.append(bestMatch);
    }
    QVector<ChecksumDllInfo> rest;
    for (const auto& d : nonEmpty) {
        if (d.devNum != bestMatch.devNum)
            rest.append(d);
    }
    std::sort(rest.begin(), rest.end(), [](const ChecksumDllInfo& a, const ChecksumDllInfo& b) {
        return a.description.compare(b.description, Qt::CaseInsensitive) < 0;
    });
    for (const auto& d : rest)
        sorted.append(d);

    m_dlls = sorted;

    // ── Layout ────────────────────────────────────────────────────────────────
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(8);

    // ECU type label
    const QString ecuDisplay = ecuType.isEmpty() ? tr("(not set)") : ecuType;
    auto* ecuLabel = new QLabel(tr("ECU type: <b>%1</b>").arg(ecuDisplay.toHtmlEscaped()), this);
    layout->addWidget(ecuLabel);

    // Auto-detected algorithm label
    QString autoDetectText;
    if (bestMatch.devNum != 0) {
        autoDetectText = tr("Auto-detected algorithm: <b>%1</b>").arg(bestMatch.description.toHtmlEscaped());
    } else {
        autoDetectText = tr("Auto-detected algorithm: <b>(no match found)</b>");
    }
    auto* autoLabel = new QLabel(autoDetectText, this);
    autoLabel->setWordWrap(true);
    layout->addWidget(autoLabel);

    layout->addSpacing(4);

    // Select algorithm label
    auto* selectLabel = new QLabel(tr("Select algorithm:"), this);
    layout->addWidget(selectLabel);

    // Combo box
    m_combo = new QComboBox(this);
    m_combo->setMinimumWidth(460);
    for (const auto& d : m_dlls) {
        QString text = d.description;
        if (d.hasNative)
            text += tr(" (built-in)");
        m_combo->addItem(text);
    }
    layout->addWidget(m_combo);

    layout->addSpacing(4);

    // Platform note
#ifndef Q_OS_WIN
    auto* noteLabel = new QLabel(
        tr("\xe2\x9a\xa0 Some algorithms are only available on Windows."),
        this);
    noteLabel->setStyleSheet("color: orange; font-size: 11px;");
    noteLabel->setWordWrap(true);
    layout->addWidget(noteLabel);
#endif

    layout->addSpacing(8);

    // OK / Cancel buttons
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // Pre-select best match (index 0 if it exists)
    if (bestMatch.devNum != 0 && !m_dlls.isEmpty())
        m_combo->setCurrentIndex(0);

    restoreGeometry(QSettings("CT14", "RX14")
                    .value("dialogGeometry/ChecksumSelectDlg").toByteArray());
}

void ChecksumSelectDlg::closeEvent(QCloseEvent* event)
{
    QSettings("CT14", "RX14")
        .setValue("dialogGeometry/ChecksumSelectDlg", saveGeometry());
    QDialog::closeEvent(event);
}

ChecksumDllInfo ChecksumSelectDlg::selectedDll() const
{
    const int idx = m_combo ? m_combo->currentIndex() : -1;
    if (idx < 0 || idx >= m_dlls.size())
        return {};
    return m_dlls.at(idx);
}
