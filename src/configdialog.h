/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#include <QDialog>
#include <QListWidget>
#include <QStackedWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include "appconfig.h"

class QCloseEvent;
class QPushButton;

class ConfigDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConfigDialog(QWidget *parent = nullptr);

signals:
    void settingsApplied();

public slots:
    /// Cancel / Esc / window close — reverts the live preview to the last
    /// applied (or on-disk) state before closing.
    void reject() override;
    void markDirty();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildColorsPage();
    void buildDisplayPage();
    void buildAIPage();
    void loadAIProviderFields(int index);
    void saveAISettings();

    // Live preview: push the dialog's working state into AppConfig and
    // notify all views — called on every control change.
    void previewNow();
    // Restore the revert baseline (state at open, or at last Apply).
    void revertPreview();
    // Repaint all color swatch buttons from the working copy.
    void refreshSwatches();
    void setDirty(bool dirty);

    QWidget *makeColorRow(const QString &label, QColor &colorRef);

    QListWidget    *m_nav   = nullptr;
    QStackedWidget *m_stack = nullptr;

    // Working copy of colors edited in the dialog (previewed live)
    AppColors m_working;

    // Revert baseline — what Cancel restores (updated on Apply)
    AppColors m_original;
    WaveStyle m_origStyle;
    bool      m_origLongNames = true;

    // All swatch buttons with the working-copy color they display
    QVector<QPair<QPushButton*, QColor*>> m_swatches;

    // AI settings widgets
    QComboBox   *m_aiProviderCombo   = nullptr;
    QLineEdit   *m_aiKeyEdit         = nullptr;
    QComboBox   *m_aiModelCombo      = nullptr;
    class QToolButton *m_aiDocsBtn   = nullptr;
    QLineEdit   *m_aiUrlEdit         = nullptr;
    QCheckBox   *m_showLongNamesCheck = nullptr;
    QLabel      *m_supportLabel      = nullptr;

    // Button controls & status
    QPushButton *m_btnApply        = nullptr;
    QPushButton *m_btnCancel       = nullptr;
    QLabel      *m_applyStatusLbl  = nullptr;
    bool         m_isDirty         = false;


    // 2D waveform style widgets
    QComboBox            *m_waveShapeCombo = nullptr;
    class QDoubleSpinBox *m_waveWidthSpin  = nullptr;
    class QSpinBox       *m_waveDotSpin    = nullptr;
    QCheckBox            *m_waveFillCheck  = nullptr;

    // AI provider registry (mirrors AIAssistant)
    struct AIProviderEntry {
        QString     name;
        QString     label;
        QString     baseUrl;
        QString     defaultModel;
        QStringList presetModels;
        QString     docsUrl;
        bool        isClaude = false;
        int         tier     = 2;  // 0 = green/best  1 = amber/good  2 = red/limited
    };
    QVector<AIProviderEntry> m_aiProviders;
};
