/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "folderpropertiesdlg.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QGroupBox>

FolderPropertiesDialog::FolderPropertiesDialog(const FolderInfo &info,
                                               const QStringList &availableParentPaths,
                                               QWidget *parent)
    : QDialog(parent)
    , m_initialInfo(info)
{
    setWindowTitle(tr("Folder Properties - %1").arg(info.name.isEmpty() ? info.fullPath : info.name));
    resize(500, 380);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QGroupBox *group = new QGroupBox(tr("Folder Information"), this);
    QFormLayout *form = new QFormLayout(group);

    m_nameEdit = new QLineEdit(info.name, this);
    form->addRow(tr("Folder Name:"), m_nameEdit);

    m_parentCombo = new QComboBox(this);
    m_parentCombo->addItem(tr("(Root)"), QString());
    for (const QString &path : availableParentPaths) {
        if (path.isEmpty() || path == info.fullPath || path.startsWith(info.fullPath + "/"))
            continue; // Prevent cyclic hierarchy
        m_parentCombo->addItem(path, path);
    }
    int idx = m_parentCombo->findData(info.parentPath);
    if (idx >= 0) m_parentCombo->setCurrentIndex(idx);
    form->addRow(tr("Parent Folder:"), m_parentCombo);

    m_fullPathLabel = new QLabel(info.fullPath, this);
    m_fullPathLabel->setStyleSheet(QStringLiteral("font-weight: bold; color: #808080;"));
    form->addRow(tr("Full Path:"), m_fullPathLabel);

    m_mapCountLabel = new QLabel(QString::number(info.mapCount), this);
    form->addRow(tr("Direct Maps:"), m_mapCountLabel);

    mainLayout->addWidget(group);

    QGroupBox *descGroup = new QGroupBox(tr("Folder Comment / Description"), this);
    QVBoxLayout *descLayout = new QVBoxLayout(descGroup);
    m_descEdit = new QPlainTextEdit(info.description, this);
    m_descEdit->setPlaceholderText(tr("Enter documentation notes for this map folder..."));
    descLayout->addWidget(m_descEdit);
    mainLayout->addWidget(descGroup);

    QDialogButtonBox *btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(btnBox);

    connect(m_nameEdit, &QLineEdit::textChanged, this, &FolderPropertiesDialog::updateFullPathPreview);
    connect(m_parentCombo, &QComboBox::currentIndexChanged, this, &FolderPropertiesDialog::updateFullPathPreview);

    updateFullPathPreview();
}

void FolderPropertiesDialog::updateFullPathPreview()
{
    QString parent = m_parentCombo->currentData().toString().trimmed();
    QString name = m_nameEdit->text().trimmed();

    QString full;
    if (parent.isEmpty()) {
        full = name;
    } else if (name.isEmpty()) {
        full = parent;
    } else {
        full = parent + "/" + name;
    }
    m_fullPathLabel->setText(full);
}

FolderPropertiesDialog::FolderInfo FolderPropertiesDialog::result() const
{
    FolderInfo res = m_initialInfo;
    res.name = m_nameEdit->text().trimmed();
    res.parentPath = m_parentCombo->currentData().toString().trimmed();
    res.fullPath = m_fullPathLabel->text().trimmed();
    res.description = m_descEdit->toPlainText().trimmed();
    return res;
}
