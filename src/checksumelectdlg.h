/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#include <QDialog>
#include <QComboBox>
#include <QLabel>
#include "checksummanager.h"

class QCloseEvent;

class ChecksumSelectDlg : public QDialog {
    Q_OBJECT
public:
    explicit ChecksumSelectDlg(const QByteArray& rom, const QString& ecuType, QWidget* parent = nullptr);
    ChecksumDllInfo selectedDll() const;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    QComboBox* m_combo = nullptr;
    QVector<ChecksumDllInfo> m_dlls; // only non-empty entries
};
