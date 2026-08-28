/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#include <QObject>
#include <QColor>
#include <QString>
#include <QVector>
#include <functional>

struct AppColors {
    // ── Map highlight bands ─────────────────────────────────────────────────
    // Applied to map regions in the hex editor, 2D waveform, and map overlay
    QColor mapBand[5];

    // ── Waveform curve row colors (2D view) ─────────────────────────────────
    QColor waveRow[8];

    // ── Hex editor ──────────────────────────────────────────────────────────
    QColor hexBg;          // cell area background
    QColor hexText;        // normal byte text
    QColor hexModified;    // modified byte text / bar highlight
    QColor hexSelected;    // selected cell fill
    QColor hexOffset;      // offset column + ASCII/bar sidebar
    QColor hexHeaderBg;    // column header strip background
    QColor hexHeaderText;  // column header labels
    QColor hexBarDefault;  // bar-view default bar color (no region)

    // ── 2D Waveform view ────────────────────────────────────────────────────
    QColor waveBg;         // plot area background
    QColor waveGridMajor;  // major grid lines
    QColor waveGridMinor;  // minor grid lines
    QColor waveLine;       // ROM waveform line color
    QColor waveOverviewBg; // minimap strip background

    // ── Map Overlay ──────────────────────────────────────────────────────────
    QColor mapCellBg;       // cell background when heat map is off
    QColor mapCellText;     // cell text when heat map is off
    QColor mapCellModified; // modified cell text when heat map is off
    QColor mapGridLine;     // grid line color when heat map is off
    QColor mapAxisXBg;      // X axis (column) header background
    QColor mapAxisXText;    // X axis header text
    QColor mapAxisYBg;      // Y axis (row) header background
    QColor mapAxisYText;    // Y axis header text

    // ── General UI ──────────────────────────────────────────────────────────
    QColor uiBg;        // main window / MDI area
    QColor uiPanel;     // sidebars, toolbars, panel headers
    QColor uiBorder;    // dividers and borders
    QColor uiText;      // primary text
    QColor uiTextDim;   // secondary / dimmed labels
    QColor uiAccent;    // highlight color (links, selection, active)

    // ── Structural UI (bars, toolbars, tree) ────────────────────────────────
    QColor topBarBg;     // top welcome bar / hero area
    QColor toolbarBg;    // format/project toolbar strip
    QColor statusBarBg;  // bottom status bar
    QColor treeBg;       // project tree / left panel background
    QColor treeSelected; // selected item in project tree
    QColor buttonBg;     // default button background
    QColor buttonText;   // default button text
    QColor inputBg;      // text input / search field background
    QColor inputBorder;  // text input border
};

// ── 2D waveform draw style ───────────────────────────────────────────────────
// OLS-style display options for the 2D view: how the curve is drawn, how
// thick, whether points are marked, and whether the area below is filled.
struct WaveStyle {
    enum class Shape {
        Line     = 0,   ///< plain connected line
        LineDots = 1,   ///< line with point markers (default)
        Dots     = 2,   ///< point markers only
        Bars     = 3,   ///< vertical bars from the baseline
        Filled   = 4,   ///< line with gradient fill underneath
    };
    Shape  shape          = Shape::LineDots;
    double lineWidth      = 1.5;    ///< curve pen width in px (0.5 – 6.0)
    int    dotSize        = 0;      ///< point marker radius in px; 0 = automatic
    bool   fillUnderCurve = false;  ///< gradient fill below the curve (any shape)
};

struct ColorTheme {
    const char *id;
    const char *nameKey;
    AppColors colors;
};

namespace ColorThemes {
    const QVector<ColorTheme> &all();
}

class AppConfig : public QObject {
    Q_OBJECT
public:
    static AppConfig &instance();

    // ── AI assistant write permission mode ──────────────────────────────────
    // Mirrors Claude Code's permission-mode concept: the user picks once per
    // session how proposed changes are handled, instead of per-call popups.
    enum class PermissionMode {
        Ask         = 0,   ///< Inline confirmation card before every write (default)
        AutoAccept  = 1,   ///< Apply writes immediately, log a card for visibility
        Plan        = 2,   ///< Reject writes; the AI must explain instead
    };

    AppColors      colors;
    WaveStyle      waveStyle;                               ///< 2D waveform draw style
    bool           showLongMapNames = true;                 ///< Map list: append the description to the short name
    PermissionMode aiPermissionMode = PermissionMode::Ask;  ///< AI assistant write-tool gate

    void load();
    void save();
    void resetToDefaults();

    // Language-driven default for showLongMapNames: Chinese UI → long names.
    // Applies only until the user pins a choice in Preferences (issue #56).
    static bool languageDefaultsToLongNames();
    void reapplyLanguageDefaults();   ///< Call after a UI language switch

    // ── Theme skins (.rx14theme, JSON) ──────────────────────────────────────
    // Enumerates every themable color as (key, member ref). The QSettings
    // store, skin export, and skin import all share this one field list, so
    // adding a color to AppColors + the table wires it everywhere at once.
    static void forEachColor(AppColors &c,
        const std::function<void(const QString &key, QColor &color)> &fn);

    // Writes colors + 2D wave style as a versioned JSON skin file.
    static bool exportTheme(const QString &filePath, const AppColors &colors,
                            const WaveStyle &style, const QString &name,
                            QString *errorOut = nullptr);

    // Reads a skin file. Colors start from Midnight defaults and the file's
    // entries overlay them, so partial skins stay valid; unknown keys are
    // ignored (older app reading a newer skin degrades gracefully).
    static bool importTheme(const QString &filePath, AppColors &colorsOut,
                            WaveStyle &styleOut, QString *nameOut = nullptr,
                            QString *errorOut = nullptr);

    // ── User theme library ──────────────────────────────────────────────────
    // User-created themes live as .rx14theme files under AppData/themes and
    // appear in the Settings preset combo below the built-ins. Built-in
    // themes are code (ColorThemes::all()) and can never be renamed/deleted.
    struct UserTheme {
        QString   name;
        QString   filePath;
        AppColors colors;
        WaveStyle waveStyle;
    };
    static QString userThemesDir();
    static QVector<UserTheme> userThemes();      // sorted by name
    // Creates or overwrites (same name, case-insensitive) a user theme.
    static bool saveUserTheme(const QString &name, const AppColors &colors,
                              const WaveStyle &style,
                              QString *pathOut = nullptr,
                              QString *errorOut = nullptr);
    static bool renameUserTheme(const QString &filePath,
                                const QString &newName,
                                QString *newPathOut = nullptr,
                                QString *errorOut = nullptr);
    static bool deleteUserTheme(const QString &filePath,
                                QString *errorOut = nullptr);

signals:
    void colorsChanged();
    void displaySettingsChanged();
    void aiPermissionModeChanged(PermissionMode mode);

public:
    static void applyDefaults(AppColors &c);

private:
    AppConfig();
};
