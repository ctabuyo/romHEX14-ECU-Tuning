/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QLabel>
#include <QStringList>

class FolderPropertiesDialog : public QDialog {
    Q_OBJECT

public:
    struct FolderInfo {
        QString name;
        QString parentPath;
        QString fullPath;
        QString description;
        int mapCount = 0;
    };

    explicit FolderPropertiesDialog(const FolderInfo &info,
                                   const QStringList &availableParentPaths,
                                   QWidget *parent = nullptr);

    FolderInfo result() const;

private slots:
    void updateFullPathPreview();

private:
    FolderInfo     m_initialInfo;
    QLineEdit     *m_nameEdit = nullptr;
    QComboBox     *m_parentCombo = nullptr;
    QLabel        *m_fullPathLabel = nullptr;
    QLabel        *m_mapCountLabel = nullptr;
    QPlainTextEdit *m_descEdit = nullptr;
};
