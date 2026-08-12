/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QLabel>
#include <QPushButton>
#include "project.h"
#include "ecudetector.h"

class QCloseEvent;

class ProjectPropertiesDialog : public QDialog {
    Q_OBJECT

public:
    explicit ProjectPropertiesDialog(Project *project, QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void autoFill();
    void autoDetectEcu();
    void accept() override;
    void onBrandChanged(const QString &brand);
    void validateRequiredFields();

private:
    QWidget *buildSection(const QString &label, const QColor &color, QWidget *content);
    QLineEdit *field(const QString &placeholder = {});
    void setupCompleter(QComboBox *combo);

    void populate();
    void collect();
    QPushButton *m_okBtn = nullptr;

    // Auto-fill helpers
    QString detectProducer(const QString &ecuFamily) const;
    QString detectProcessor(const QString &ecuFamily) const;
    QString scanSwNumber() const;
    QString computeChecksum() const;

    Project *m_project = nullptr;

    // Client
    QLineEdit *m_clientName    = nullptr;
    QLineEdit *m_clientNr      = nullptr;
    QLineEdit *m_clientLic     = nullptr;
    QLineEdit *m_vin           = nullptr;

    // Vehicle
    QComboBox *m_vehicleType   = nullptr;
    QComboBox *m_brand         = nullptr;
    QComboBox *m_model         = nullptr;
    QLineEdit *m_vehicleBuild  = nullptr;
    QLineEdit *m_vehicleModel  = nullptr;
    QLineEdit *m_vehicleChar   = nullptr;
    QSpinBox  *m_year          = nullptr;

    // ECU
    QComboBox *m_ecuUse        = nullptr;
    QComboBox *m_ecuProducer   = nullptr;
    QComboBox *m_ecuType       = nullptr;
    QLineEdit *m_ecuNrProd     = nullptr;
    QLineEdit *m_ecuNrEcu      = nullptr;
    QLineEdit *m_ecuSwNum      = nullptr;
    QLineEdit *m_ecuSwVer      = nullptr;
    QLabel    *m_ecuProc       = nullptr;
    QLabel    *m_ecuChecksum   = nullptr;
    QLabel    *m_ecuDiag       = nullptr;  // detector result diagnostic

    // Engine
    QLineEdit *m_engProducer   = nullptr;
    QLineEdit *m_engCode       = nullptr;
    QLineEdit *m_engType       = nullptr;
    QLineEdit *m_displacement  = nullptr;
    QSpinBox  *m_outputPS      = nullptr;
    QSpinBox  *m_outputKW      = nullptr;
    QSpinBox  *m_maxTorque     = nullptr;
    QComboBox *m_emission      = nullptr;
    QComboBox *m_transmission  = nullptr;

    // Project
    QComboBox *m_projectType   = nullptr;
    QComboBox *m_mapLanguage   = nullptr;

    // User defined
    QLineEdit *m_user[6]       = {};

    // Notes
    QPlainTextEdit *m_notes    = nullptr;
};
