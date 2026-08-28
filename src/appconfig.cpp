/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "appconfig.h"
#include "appconstants.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <algorithm>

AppConfig::AppConfig() { applyDefaults(colors); }

AppConfig &AppConfig::instance()
{
    static AppConfig s;
    return s;
}

void AppConfig::applyDefaults(AppColors &c)
{
    c.mapBand[0] = QColor(255, 58,  58);
    c.mapBand[1] = QColor(58,  150, 255);
    c.mapBand[2] = QColor(58,  255, 130);
    c.mapBand[3] = QColor(255, 200, 58);
    c.mapBand[4] = QColor(180, 58,  255);

    c.waveRow[0] = QColor(58,  180, 255);
    c.waveRow[1] = QColor(255, 160, 50);
    c.waveRow[2] = QColor(90,  210, 90);
    c.waveRow[3] = QColor(220, 80,  80);
    c.waveRow[4] = QColor(170, 100, 255);
    c.waveRow[5] = QColor(60,  210, 190);
    c.waveRow[6] = QColor(255, 220, 60);
    c.waveRow[7] = QColor(200, 130, 80);

    c.hexBg         = QColor(7,   11,  20);
    c.hexText       = QColor(231, 238, 252);
    c.hexModified   = QColor(245, 158, 11);
    c.hexSelected   = QColor(58,  145, 208);
    c.hexOffset     = QColor(107, 127, 163);
    c.hexHeaderBg   = QColor(15,  22,  41);
    c.hexHeaderText = QColor(58,  145, 208);
    c.hexBarDefault = QColor(115, 125, 140);

    // Map overlay (no-heat / classic terminal style)
    c.mapCellBg       = QColor(0,   0,   160);  // solid blue (classic)
    c.mapCellText     = QColor(255, 255, 255);  // white text
    c.mapCellModified = QColor(255, 255, 0);    // yellow for modified
    c.mapGridLine     = QColor(0,   0,   120);  // darker blue grid
    c.mapAxisXBg      = QColor(12,  32,  48);   // deep ocean teal
    c.mapAxisXText    = QColor(100, 210, 240);  // bright cyan
    c.mapAxisYBg      = QColor(38,  24,  12);   // warm bronze/brown
    c.mapAxisYText    = QColor(240, 180, 100);  // warm amber/gold

    c.waveBg         = QColor(8,  14,  26);
    c.waveGridMajor  = QColor(38, 62,  100);
    c.waveGridMinor  = QColor(22, 38,  62);
    c.waveLine       = QColor(80, 160, 255);
    c.waveOverviewBg = QColor(5,  10,  22);

    c.uiBg       = QColor(8,   11,  16);
    c.uiPanel    = QColor(22,  27,  34);
    c.uiBorder   = QColor(48,  54,  61);
    c.uiText     = QColor(201, 209, 217);
    c.uiTextDim  = QColor(139, 148, 158);
    c.uiAccent   = QColor(58,  145, 208);

    c.topBarBg     = QColor(13,  17,  23);
    c.toolbarBg    = QColor(22,  27,  34);
    c.statusBarBg  = QColor(13,  17,  23);
    c.treeBg       = QColor(13,  17,  23);
    c.treeSelected = QColor(31,  111, 235, 40);
    c.buttonBg     = QColor(33,  38,  45);
    c.buttonText   = QColor(139, 148, 158);
    c.inputBg      = QColor(22,  27,  34);
    c.inputBorder  = QColor(48,  54,  61);
}

// ── Theme Presets ────────────────────────────────────────────────────────────

static AppColors midnight()
{
    AppColors c;
    AppConfig::applyDefaults(c);

    c.topBarBg = QColor(13, 17, 23); c.toolbarBg = QColor(22, 27, 34);
    c.statusBarBg = QColor(13, 17, 23); c.treeBg = QColor(13, 17, 23);
    c.treeSelected = QColor(31, 111, 235, 40); c.buttonBg = QColor(33, 38, 45);
    c.buttonText = QColor(139, 148, 158); c.inputBg = QColor(22, 27, 34);
    c.inputBorder = QColor(48, 54, 61);
    return c;
}

static AppColors ocean()
{
    AppColors c;
    AppConfig::applyDefaults(c);
    c.mapBand[0] = QColor(0,  100, 200); c.mapBand[1] = QColor(0,  180, 180);
    c.mapBand[2] = QColor(0,  140, 255); c.mapBand[3] = QColor(80, 200, 255);
    c.mapBand[4] = QColor(0,  80,  160);
    c.waveRow[0] = QColor(0,  150, 255); c.waveRow[1] = QColor(0,  200, 200);
    c.waveRow[2] = QColor(60, 180, 255); c.waveRow[3] = QColor(0,  220, 160);
    c.hexBg = QColor(2, 8, 22); c.hexText = QColor(160, 210, 255);
    c.hexModified = QColor(0, 255, 200); c.hexSelected = QColor(0, 60, 140);
    c.hexOffset = QColor(40, 100, 180); c.hexHeaderBg = QColor(4, 14, 35);
    c.hexHeaderText = QColor(0, 160, 255); c.hexBarDefault = QColor(40, 100, 160);
    c.mapCellBg = QColor(0, 20, 60); c.mapCellText = QColor(180, 230, 255);
    c.mapCellModified = QColor(0, 255, 180); c.mapGridLine = QColor(0, 15, 45);
    c.mapAxisXBg = QColor(0, 30, 70); c.mapAxisXText = QColor(60, 200, 255);
    c.mapAxisYBg = QColor(0, 25, 55); c.mapAxisYText = QColor(0, 220, 200);
    c.waveBg = QColor(2, 6, 18); c.waveLine = QColor(0, 160, 255);
    c.waveGridMajor = QColor(10, 40, 80); c.waveGridMinor = QColor(5, 20, 45);
    c.waveOverviewBg = QColor(2, 5, 15);
    c.uiBg = QColor(3, 10, 22); c.uiPanel = QColor(6, 18, 38);
    c.uiBorder = QColor(15, 45, 85); c.uiText = QColor(170, 215, 255);
    c.uiTextDim = QColor(60, 120, 180); c.uiAccent = QColor(0, 140, 255);

    c.topBarBg = QColor(2, 6, 18); c.toolbarBg = QColor(6, 18, 38);
    c.statusBarBg = QColor(2, 6, 18); c.treeBg = QColor(3, 10, 22);
    c.treeSelected = QColor(0, 100, 200, 40); c.buttonBg = QColor(10, 35, 65);
    c.buttonText = QColor(100, 180, 255); c.inputBg = QColor(6, 18, 38);
    c.inputBorder = QColor(15, 45, 85);
    return c;
}

static AppColors forest()
{
    AppColors c;
    AppConfig::applyDefaults(c);
    c.mapBand[0] = QColor(40, 160, 60); c.mapBand[1] = QColor(120, 180, 40);
    c.mapBand[2] = QColor(0,  200, 120); c.mapBand[3] = QColor(180, 200, 60);
    c.mapBand[4] = QColor(60, 130, 80);
    c.waveRow[0] = QColor(80, 220, 80); c.waveRow[1] = QColor(160, 200, 40);
    c.waveRow[2] = QColor(0, 200, 140); c.waveRow[3] = QColor(200, 220, 80);
    c.hexBg = QColor(5, 16, 5); c.hexText = QColor(180, 240, 180);
    c.hexModified = QColor(255, 220, 0); c.hexSelected = QColor(20, 80, 30);
    c.hexOffset = QColor(60, 140, 60); c.hexHeaderBg = QColor(8, 24, 10);
    c.hexHeaderText = QColor(40, 200, 80); c.hexBarDefault = QColor(50, 120, 50);
    c.mapCellBg = QColor(0, 50, 20); c.mapCellText = QColor(200, 255, 200);
    c.mapCellModified = QColor(255, 255, 0); c.mapGridLine = QColor(0, 35, 12);
    c.mapAxisXBg = QColor(5, 40, 15); c.mapAxisXText = QColor(80, 255, 120);
    c.mapAxisYBg = QColor(20, 35, 5); c.mapAxisYText = QColor(220, 220, 80);
    c.waveBg = QColor(3, 12, 4); c.waveLine = QColor(50, 220, 80);
    c.waveGridMajor = QColor(15, 50, 20); c.waveGridMinor = QColor(8, 30, 10);
    c.waveOverviewBg = QColor(2, 8, 3);
    c.uiBg = QColor(5, 12, 6); c.uiPanel = QColor(10, 25, 14);
    c.uiBorder = QColor(25, 55, 30); c.uiText = QColor(180, 230, 180);
    c.uiTextDim = QColor(80, 140, 80); c.uiAccent = QColor(40, 200, 80);

    c.topBarBg = QColor(4, 10, 5); c.toolbarBg = QColor(10, 25, 14);
    c.statusBarBg = QColor(4, 10, 5); c.treeBg = QColor(5, 12, 6);
    c.treeSelected = QColor(40, 160, 60, 40); c.buttonBg = QColor(15, 40, 20);
    c.buttonText = QColor(100, 200, 120); c.inputBg = QColor(10, 25, 14);
    c.inputBorder = QColor(25, 55, 30);
    return c;
}

static AppColors volcanic()
{
    AppColors c;
    AppConfig::applyDefaults(c);
    c.mapBand[0] = QColor(255, 80, 20); c.mapBand[1] = QColor(255, 160, 0);
    c.mapBand[2] = QColor(200, 40, 0);  c.mapBand[3] = QColor(255, 200, 40);
    c.mapBand[4] = QColor(180, 60, 20);
    c.waveRow[0] = QColor(255, 100, 30); c.waveRow[1] = QColor(255, 180, 0);
    c.waveRow[2] = QColor(220, 50, 0);   c.waveRow[3] = QColor(255, 220, 60);
    c.hexBg = QColor(18, 6, 2); c.hexText = QColor(255, 210, 180);
    c.hexModified = QColor(255, 255, 0); c.hexSelected = QColor(120, 40, 10);
    c.hexOffset = QColor(180, 100, 50); c.hexHeaderBg = QColor(28, 10, 4);
    c.hexHeaderText = QColor(255, 140, 40); c.hexBarDefault = QColor(140, 70, 30);
    c.mapCellBg = QColor(120, 15, 0); c.mapCellText = QColor(255, 230, 200);
    c.mapCellModified = QColor(255, 255, 80); c.mapGridLine = QColor(80, 10, 0);
    c.mapAxisXBg = QColor(60, 15, 0); c.mapAxisXText = QColor(255, 180, 60);
    c.mapAxisYBg = QColor(50, 25, 0); c.mapAxisYText = QColor(255, 220, 80);
    c.waveBg = QColor(14, 4, 2); c.waveLine = QColor(255, 100, 20);
    c.waveGridMajor = QColor(50, 18, 8); c.waveGridMinor = QColor(30, 10, 4);
    c.waveOverviewBg = QColor(10, 3, 1);
    c.uiBg = QColor(14, 6, 3); c.uiPanel = QColor(28, 12, 6);
    c.uiBorder = QColor(65, 28, 12); c.uiText = QColor(255, 220, 190);
    c.uiTextDim = QColor(160, 90, 50); c.uiAccent = QColor(255, 120, 20);

    c.topBarBg = QColor(12, 4, 2); c.toolbarBg = QColor(28, 12, 6);
    c.statusBarBg = QColor(12, 4, 2); c.treeBg = QColor(14, 6, 3);
    c.treeSelected = QColor(200, 60, 10, 40); c.buttonBg = QColor(50, 20, 8);
    c.buttonText = QColor(255, 160, 80); c.inputBg = QColor(28, 12, 6);
    c.inputBorder = QColor(65, 28, 12);
    return c;
}

static AppColors arctic()
{
    AppColors c;
    AppConfig::applyDefaults(c);
    c.mapBand[0] = QColor(40, 100, 180); c.mapBand[1] = QColor(0, 130, 140);
    c.mapBand[2] = QColor(50, 140, 50);  c.mapBand[3] = QColor(160, 100, 20);
    c.mapBand[4] = QColor(120, 60, 160);
    c.waveRow[0] = QColor(20, 80, 180); c.waveRow[1] = QColor(0, 120, 120);
    c.waveRow[2] = QColor(40, 120, 40);  c.waveRow[3] = QColor(140, 80, 10);
    c.hexBg = QColor(248, 250, 252); c.hexText = QColor(25, 35, 55);
    c.hexModified = QColor(190, 50, 0); c.hexSelected = QColor(180, 205, 235);
    c.hexOffset = QColor(70, 95, 135); c.hexHeaderBg = QColor(230, 236, 245);
    c.hexHeaderText = QColor(20, 60, 130); c.hexBarDefault = QColor(120, 145, 175);
    c.mapCellBg = QColor(45, 80, 140); c.mapCellText = QColor(245, 250, 255);
    c.mapCellModified = QColor(255, 210, 0); c.mapGridLine = QColor(35, 65, 115);
    c.mapAxisXBg = QColor(230, 238, 248); c.mapAxisXText = QColor(15, 55, 120);
    c.mapAxisYBg = QColor(235, 240, 248); c.mapAxisYText = QColor(30, 80, 150);
    c.waveBg = QColor(248, 250, 253); c.waveLine = QColor(20, 70, 160);
    c.waveGridMajor = QColor(218, 226, 238); c.waveGridMinor = QColor(232, 238, 246);
    c.waveOverviewBg = QColor(240, 244, 250);
    c.uiBg = QColor(244, 247, 251); c.uiPanel = QColor(232, 238, 246);
    c.uiBorder = QColor(200, 212, 230); c.uiText = QColor(20, 35, 55);
    c.uiTextDim = QColor(80, 105, 140); c.uiAccent = QColor(20, 85, 185);

    c.topBarBg = QColor(52, 72, 100); c.toolbarBg = QColor(62, 82, 112);
    c.statusBarBg = QColor(48, 68, 96); c.treeBg = QColor(240, 244, 250);
    c.treeSelected = QColor(20, 85, 185, 35); c.buttonBg = QColor(222, 230, 242);
    c.buttonText = QColor(30, 50, 80); c.inputBg = QColor(250, 252, 254);
    c.inputBorder = QColor(200, 212, 230);
    return c;
}

static AppColors cyberpunk()
{
    AppColors c;
    AppConfig::applyDefaults(c);
    c.mapBand[0] = QColor(255, 0, 200); c.mapBand[1] = QColor(0, 255, 255);
    c.mapBand[2] = QColor(180, 0, 255); c.mapBand[3] = QColor(255, 255, 0);
    c.mapBand[4] = QColor(0, 200, 255);
    c.waveRow[0] = QColor(255, 0, 180); c.waveRow[1] = QColor(0, 255, 255);
    c.waveRow[2] = QColor(200, 0, 255); c.waveRow[3] = QColor(255, 255, 0);
    c.hexBg = QColor(10, 0, 18); c.hexText = QColor(0, 255, 255);
    c.hexModified = QColor(255, 0, 200); c.hexSelected = QColor(80, 0, 120);
    c.hexOffset = QColor(140, 0, 200); c.hexHeaderBg = QColor(18, 0, 30);
    c.hexHeaderText = QColor(255, 0, 255); c.hexBarDefault = QColor(100, 0, 160);
    c.mapCellBg = QColor(30, 0, 60); c.mapCellText = QColor(0, 255, 255);
    c.mapCellModified = QColor(255, 0, 200); c.mapGridLine = QColor(20, 0, 40);
    c.mapAxisXBg = QColor(40, 0, 70); c.mapAxisXText = QColor(255, 0, 255);
    c.mapAxisYBg = QColor(50, 0, 40); c.mapAxisYText = QColor(0, 255, 200);
    c.waveBg = QColor(8, 0, 14); c.waveLine = QColor(0, 255, 255);
    c.waveGridMajor = QColor(30, 0, 50); c.waveGridMinor = QColor(18, 0, 30);
    c.waveOverviewBg = QColor(6, 0, 10);
    c.uiBg = QColor(8, 0, 16); c.uiPanel = QColor(18, 0, 30);
    c.uiBorder = QColor(50, 0, 80); c.uiText = QColor(0, 255, 255);
    c.uiTextDim = QColor(100, 0, 160); c.uiAccent = QColor(255, 0, 200);

    c.topBarBg = QColor(6, 0, 12); c.toolbarBg = QColor(18, 0, 30);
    c.statusBarBg = QColor(6, 0, 12); c.treeBg = QColor(8, 0, 16);
    c.treeSelected = QColor(255, 0, 200, 30); c.buttonBg = QColor(30, 0, 50);
    c.buttonText = QColor(0, 255, 255); c.inputBg = QColor(18, 0, 30);
    c.inputBorder = QColor(50, 0, 80);
    return c;
}

static AppColors solarized()
{
    AppColors c;
    AppConfig::applyDefaults(c);
    c.mapBand[0] = QColor(220, 50, 47); c.mapBand[1] = QColor(38, 139, 210);
    c.mapBand[2] = QColor(133, 153, 0); c.mapBand[3] = QColor(181, 137, 0);
    c.mapBand[4] = QColor(108, 113, 196);
    c.waveRow[0] = QColor(38, 139, 210); c.waveRow[1] = QColor(133, 153, 0);
    c.waveRow[2] = QColor(203, 75, 22);  c.waveRow[3] = QColor(42, 161, 152);
    c.hexBg = QColor(0, 43, 54); c.hexText = QColor(131, 148, 150);
    c.hexModified = QColor(181, 137, 0); c.hexSelected = QColor(7, 54, 66);
    c.hexOffset = QColor(88, 110, 117); c.hexHeaderBg = QColor(7, 54, 66);
    c.hexHeaderText = QColor(38, 139, 210); c.hexBarDefault = QColor(88, 110, 117);
    c.mapCellBg = QColor(7, 54, 66); c.mapCellText = QColor(238, 232, 213);
    c.mapCellModified = QColor(181, 137, 0); c.mapGridLine = QColor(0, 43, 54);
    c.mapAxisXBg = QColor(0, 43, 54); c.mapAxisXText = QColor(42, 161, 152);
    c.mapAxisYBg = QColor(7, 54, 66); c.mapAxisYText = QColor(203, 75, 22);
    c.waveBg = QColor(0, 43, 54); c.waveLine = QColor(38, 139, 210);
    c.waveGridMajor = QColor(7, 54, 66); c.waveGridMinor = QColor(0, 43, 54);
    c.waveOverviewBg = QColor(0, 36, 46);
    c.uiBg = QColor(0, 43, 54); c.uiPanel = QColor(7, 54, 66);
    c.uiBorder = QColor(88, 110, 117); c.uiText = QColor(147, 161, 161);
    c.uiTextDim = QColor(88, 110, 117); c.uiAccent = QColor(38, 139, 210);

    c.topBarBg = QColor(0, 36, 46); c.toolbarBg = QColor(7, 54, 66);
    c.statusBarBg = QColor(0, 36, 46); c.treeBg = QColor(0, 43, 54);
    c.treeSelected = QColor(38, 139, 210, 30); c.buttonBg = QColor(7, 54, 66);
    c.buttonText = QColor(131, 148, 150); c.inputBg = QColor(7, 54, 66);
    c.inputBorder = QColor(88, 110, 117);
    return c;
}

static AppColors monokai()
{
    AppColors c;
    AppConfig::applyDefaults(c);
    c.mapBand[0] = QColor(249, 38, 114); c.mapBand[1] = QColor(102, 217, 239);
    c.mapBand[2] = QColor(166, 226, 46); c.mapBand[3] = QColor(253, 151, 31);
    c.mapBand[4] = QColor(174, 129, 255);
    c.waveRow[0] = QColor(166, 226, 46); c.waveRow[1] = QColor(102, 217, 239);
    c.waveRow[2] = QColor(249, 38, 114); c.waveRow[3] = QColor(253, 151, 31);
    c.hexBg = QColor(39, 40, 34); c.hexText = QColor(248, 248, 242);
    c.hexModified = QColor(230, 219, 116); c.hexSelected = QColor(73, 72, 62);
    c.hexOffset = QColor(117, 113, 94); c.hexHeaderBg = QColor(49, 50, 44);
    c.hexHeaderText = QColor(102, 217, 239); c.hexBarDefault = QColor(100, 100, 80);
    c.mapCellBg = QColor(49, 50, 44); c.mapCellText = QColor(248, 248, 242);
    c.mapCellModified = QColor(249, 38, 114); c.mapGridLine = QColor(39, 40, 34);
    c.mapAxisXBg = QColor(39, 40, 34); c.mapAxisXText = QColor(166, 226, 46);
    c.mapAxisYBg = QColor(45, 46, 38); c.mapAxisYText = QColor(253, 151, 31);
    c.waveBg = QColor(39, 40, 34); c.waveLine = QColor(166, 226, 46);
    c.waveGridMajor = QColor(58, 59, 50); c.waveGridMinor = QColor(49, 50, 42);
    c.waveOverviewBg = QColor(34, 35, 30);
    c.uiBg = QColor(39, 40, 34); c.uiPanel = QColor(49, 50, 44);
    c.uiBorder = QColor(73, 72, 62); c.uiText = QColor(248, 248, 242);
    c.uiTextDim = QColor(117, 113, 94); c.uiAccent = QColor(102, 217, 239);

    c.topBarBg = QColor(34, 35, 30); c.toolbarBg = QColor(49, 50, 44);
    c.statusBarBg = QColor(34, 35, 30); c.treeBg = QColor(39, 40, 34);
    c.treeSelected = QColor(102, 217, 239, 30); c.buttonBg = QColor(58, 59, 50);
    c.buttonText = QColor(248, 248, 242); c.inputBg = QColor(49, 50, 44);
    c.inputBorder = QColor(73, 72, 62);
    return c;
}

static AppColors dracula()
{
    AppColors c;
    AppConfig::applyDefaults(c);
    c.mapBand[0] = QColor(255, 85, 85);  c.mapBand[1] = QColor(139, 233, 253);
    c.mapBand[2] = QColor(80, 250, 123); c.mapBand[3] = QColor(255, 184, 108);
    c.mapBand[4] = QColor(189, 147, 249);
    c.waveRow[0] = QColor(189, 147, 249); c.waveRow[1] = QColor(80, 250, 123);
    c.waveRow[2] = QColor(255, 121, 198); c.waveRow[3] = QColor(139, 233, 253);
    c.hexBg = QColor(40, 42, 54); c.hexText = QColor(248, 248, 242);
    c.hexModified = QColor(241, 250, 140); c.hexSelected = QColor(68, 71, 90);
    c.hexOffset = QColor(98, 114, 164); c.hexHeaderBg = QColor(33, 34, 44);
    c.hexHeaderText = QColor(189, 147, 249); c.hexBarDefault = QColor(98, 114, 164);
    c.mapCellBg = QColor(68, 71, 90); c.mapCellText = QColor(248, 248, 242);
    c.mapCellModified = QColor(255, 121, 198); c.mapGridLine = QColor(40, 42, 54);
    c.mapAxisXBg = QColor(33, 34, 44); c.mapAxisXText = QColor(139, 233, 253);
    c.mapAxisYBg = QColor(33, 34, 44); c.mapAxisYText = QColor(255, 184, 108);
    c.waveBg = QColor(33, 34, 44); c.waveLine = QColor(189, 147, 249);
    c.waveGridMajor = QColor(68, 71, 90); c.waveGridMinor = QColor(52, 55, 70);
    c.waveOverviewBg = QColor(25, 26, 33);
    c.uiBg = QColor(33, 34, 44); c.uiPanel = QColor(40, 42, 54);
    c.uiBorder = QColor(68, 71, 90); c.uiText = QColor(248, 248, 242);
    c.uiTextDim = QColor(98, 114, 164); c.uiAccent = QColor(189, 147, 249);

    c.topBarBg = QColor(25, 26, 33); c.toolbarBg = QColor(40, 42, 54);
    c.statusBarBg = QColor(25, 26, 33); c.treeBg = QColor(33, 34, 44);
    c.treeSelected = QColor(189, 147, 249, 35); c.buttonBg = QColor(68, 71, 90);
    c.buttonText = QColor(248, 248, 242); c.inputBg = QColor(40, 42, 54);
    c.inputBorder = QColor(98, 114, 164);
    return c;
}

static AppColors nord()
{
    AppColors c;
    AppConfig::applyDefaults(c);
    c.mapBand[0] = QColor(191, 97, 106);  c.mapBand[1] = QColor(136, 192, 208);
    c.mapBand[2] = QColor(163, 190, 140); c.mapBand[3] = QColor(235, 203, 139);
    c.mapBand[4] = QColor(180, 142, 173);
    c.waveRow[0] = QColor(136, 192, 208); c.waveRow[1] = QColor(163, 190, 140);
    c.waveRow[2] = QColor(208, 135, 112); c.waveRow[3] = QColor(129, 161, 193);
    c.hexBg = QColor(46, 52, 64); c.hexText = QColor(216, 222, 233);
    c.hexModified = QColor(235, 203, 139); c.hexSelected = QColor(67, 76, 94);
    c.hexOffset = QColor(97, 110, 136); c.hexHeaderBg = QColor(59, 66, 82);
    c.hexHeaderText = QColor(136, 192, 208); c.hexBarDefault = QColor(76, 86, 106);
    c.mapCellBg = QColor(59, 66, 82); c.mapCellText = QColor(236, 239, 244);
    c.mapCellModified = QColor(235, 203, 139); c.mapGridLine = QColor(46, 52, 64);
    c.mapAxisXBg = QColor(67, 76, 94); c.mapAxisXText = QColor(136, 192, 208);
    c.mapAxisYBg = QColor(67, 76, 94); c.mapAxisYText = QColor(208, 135, 112);
    c.waveBg = QColor(41, 46, 57); c.waveLine = QColor(136, 192, 208);
    c.waveGridMajor = QColor(67, 76, 94); c.waveGridMinor = QColor(55, 62, 76);
    c.waveOverviewBg = QColor(36, 41, 51);
    c.uiBg = QColor(46, 52, 64); c.uiPanel = QColor(59, 66, 82);
    c.uiBorder = QColor(76, 86, 106); c.uiText = QColor(216, 222, 233);
    c.uiTextDim = QColor(124, 135, 156); c.uiAccent = QColor(136, 192, 208);

    c.topBarBg = QColor(36, 41, 51); c.toolbarBg = QColor(59, 66, 82);
    c.statusBarBg = QColor(36, 41, 51); c.treeBg = QColor(46, 52, 64);
    c.treeSelected = QColor(136, 192, 208, 35); c.buttonBg = QColor(67, 76, 94);
    c.buttonText = QColor(216, 222, 233); c.inputBg = QColor(59, 66, 82);
    c.inputBorder = QColor(76, 86, 106);
    return c;
}

static AppColors gruvbox()
{
    AppColors c;
    AppConfig::applyDefaults(c);
    c.mapBand[0] = QColor(251, 73, 52);   c.mapBand[1] = QColor(142, 192, 124);
    c.mapBand[2] = QColor(184, 187, 38);  c.mapBand[3] = QColor(250, 189, 47);
    c.mapBand[4] = QColor(211, 134, 155);
    c.waveRow[0] = QColor(184, 187, 38);  c.waveRow[1] = QColor(131, 165, 152);
    c.waveRow[2] = QColor(254, 128, 25);  c.waveRow[3] = QColor(211, 134, 155);
    c.hexBg = QColor(29, 32, 33); c.hexText = QColor(235, 219, 178);
    c.hexModified = QColor(250, 189, 47); c.hexSelected = QColor(80, 73, 69);
    c.hexOffset = QColor(146, 131, 116); c.hexHeaderBg = QColor(40, 40, 40);
    c.hexHeaderText = QColor(254, 128, 25); c.hexBarDefault = QColor(124, 111, 100);
    c.mapCellBg = QColor(60, 56, 54); c.mapCellText = QColor(235, 219, 178);
    c.mapCellModified = QColor(254, 128, 25); c.mapGridLine = QColor(40, 40, 40);
    c.mapAxisXBg = QColor(50, 48, 47); c.mapAxisXText = QColor(184, 187, 38);
    c.mapAxisYBg = QColor(50, 48, 47); c.mapAxisYText = QColor(250, 189, 47);
    c.waveBg = QColor(29, 32, 33); c.waveLine = QColor(254, 128, 25);
    c.waveGridMajor = QColor(60, 56, 54); c.waveGridMinor = QColor(45, 43, 42);
    c.waveOverviewBg = QColor(24, 26, 27);
    c.uiBg = QColor(29, 32, 33); c.uiPanel = QColor(40, 40, 40);
    c.uiBorder = QColor(80, 73, 69); c.uiText = QColor(235, 219, 178);
    c.uiTextDim = QColor(146, 131, 116); c.uiAccent = QColor(254, 128, 25);

    c.topBarBg = QColor(24, 26, 27); c.toolbarBg = QColor(40, 40, 40);
    c.statusBarBg = QColor(24, 26, 27); c.treeBg = QColor(29, 32, 33);
    c.treeSelected = QColor(254, 128, 25, 35); c.buttonBg = QColor(60, 56, 54);
    c.buttonText = QColor(235, 219, 178); c.inputBg = QColor(40, 40, 40);
    c.inputBorder = QColor(80, 73, 69);
    return c;
}

static AppColors amberCrt()
{
    AppColors c;
    AppConfig::applyDefaults(c);
    c.mapBand[0] = QColor(255, 176, 0);  c.mapBand[1] = QColor(255, 140, 0);
    c.mapBand[2] = QColor(255, 200, 60); c.mapBand[3] = QColor(200, 120, 0);
    c.mapBand[4] = QColor(255, 100, 0);
    c.waveRow[0] = QColor(255, 176, 0);  c.waveRow[1] = QColor(255, 210, 80);
    c.waveRow[2] = QColor(220, 130, 0);  c.waveRow[3] = QColor(255, 150, 40);
    c.hexBg = QColor(10, 7, 2); c.hexText = QColor(255, 176, 0);
    c.hexModified = QColor(255, 255, 180); c.hexSelected = QColor(80, 50, 0);
    c.hexOffset = QColor(150, 100, 10); c.hexHeaderBg = QColor(22, 15, 4);
    c.hexHeaderText = QColor(255, 200, 60); c.hexBarDefault = QColor(130, 90, 15);
    c.mapCellBg = QColor(40, 26, 0); c.mapCellText = QColor(255, 200, 80);
    c.mapCellModified = QColor(255, 255, 150); c.mapGridLine = QColor(28, 18, 0);
    c.mapAxisXBg = QColor(35, 24, 2); c.mapAxisXText = QColor(255, 190, 40);
    c.mapAxisYBg = QColor(35, 24, 2); c.mapAxisYText = QColor(255, 160, 20);
    c.waveBg = QColor(8, 6, 2); c.waveLine = QColor(255, 176, 0);
    c.waveGridMajor = QColor(60, 40, 5); c.waveGridMinor = QColor(35, 24, 4);
    c.waveOverviewBg = QColor(6, 4, 1);
    c.uiBg = QColor(10, 7, 2); c.uiPanel = QColor(24, 16, 4);
    c.uiBorder = QColor(70, 48, 10); c.uiText = QColor(255, 190, 60);
    c.uiTextDim = QColor(150, 100, 20); c.uiAccent = QColor(255, 176, 0);

    c.topBarBg = QColor(7, 5, 1); c.toolbarBg = QColor(24, 16, 4);
    c.statusBarBg = QColor(7, 5, 1); c.treeBg = QColor(10, 7, 2);
    c.treeSelected = QColor(255, 176, 0, 35); c.buttonBg = QColor(45, 30, 6);
    c.buttonText = QColor(255, 190, 80); c.inputBg = QColor(24, 16, 4);
    c.inputBorder = QColor(70, 48, 10);
    return c;
}

static AppColors paper()
{
    AppColors c;
    AppConfig::applyDefaults(c);
    c.mapBand[0] = QColor(170, 60, 40);  c.mapBand[1] = QColor(30, 110, 120);
    c.mapBand[2] = QColor(110, 120, 30); c.mapBand[3] = QColor(180, 130, 20);
    c.mapBand[4] = QColor(130, 70, 130);
    c.waveRow[0] = QColor(40, 80, 140);  c.waveRow[1] = QColor(170, 60, 40);
    c.waveRow[2] = QColor(100, 120, 30); c.waveRow[3] = QColor(30, 110, 120);
    c.hexBg = QColor(247, 241, 227); c.hexText = QColor(60, 48, 36);
    c.hexModified = QColor(180, 70, 20); c.hexSelected = QColor(225, 210, 180);
    c.hexOffset = QColor(150, 130, 105); c.hexHeaderBg = QColor(237, 228, 210);
    c.hexHeaderText = QColor(130, 90, 40); c.hexBarDefault = QColor(175, 155, 125);
    c.mapCellBg = QColor(120, 95, 60); c.mapCellText = QColor(250, 245, 232);
    c.mapCellModified = QColor(255, 225, 120); c.mapGridLine = QColor(100, 78, 48);
    c.mapAxisXBg = QColor(235, 225, 205); c.mapAxisXText = QColor(120, 85, 35);
    c.mapAxisYBg = QColor(238, 229, 210); c.mapAxisYText = QColor(150, 95, 30);
    c.waveBg = QColor(249, 243, 230); c.waveLine = QColor(140, 95, 40);
    c.waveGridMajor = QColor(225, 213, 190); c.waveGridMinor = QColor(237, 229, 212);
    c.waveOverviewBg = QColor(240, 233, 217);
    c.uiBg = QColor(245, 238, 222); c.uiPanel = QColor(236, 227, 207);
    c.uiBorder = QColor(210, 196, 170); c.uiText = QColor(62, 50, 38);
    c.uiTextDim = QColor(140, 122, 98); c.uiAccent = QColor(160, 105, 45);

    c.topBarBg = QColor(92, 76, 56); c.toolbarBg = QColor(102, 86, 64);
    c.statusBarBg = QColor(88, 72, 52); c.treeBg = QColor(240, 233, 217);
    c.treeSelected = QColor(160, 105, 45, 35); c.buttonBg = QColor(228, 217, 194);
    c.buttonText = QColor(80, 64, 46); c.inputBg = QColor(250, 245, 233);
    c.inputBorder = QColor(210, 196, 170);
    return c;
}

const QVector<ColorTheme> &ColorThemes::all()
{
    static QVector<ColorTheme> themes = {
        { "midnight",  "Midnight (Default)", midnight() },
        { "ocean",     "Ocean Deep",         ocean() },
        { "forest",    "Dark Forest",        forest() },
        { "volcanic",  "Volcanic",           volcanic() },
        { "arctic",    "Arctic",             arctic() },
        { "cyberpunk", "Cyberpunk",          cyberpunk() },
        { "solarized", "Solarized Dark",     solarized() },
        { "monokai",   "Monokai",            monokai() },
        { "dracula",   "Dracula",            dracula() },
        { "nord",      "Nord",               nord() },
        { "gruvbox",   "Gruvbox Dark",       gruvbox() },
        { "ambercrt",  "Amber CRT",          amberCrt() },
        { "paper",     "Paper Light",        paper() },
    };
    return themes;
}

// One (key, member) row per themable color. mapBand/waveRow arrays are
// enumerated separately in forEachColor.
namespace {
struct ColorField {
    const char *key;
    QColor AppColors::*member;
};
const ColorField kColorFields[] = {
    { "hexBg",           &AppColors::hexBg },
    { "hexText",         &AppColors::hexText },
    { "hexModified",     &AppColors::hexModified },
    { "hexSelected",     &AppColors::hexSelected },
    { "hexOffset",       &AppColors::hexOffset },
    { "hexHeaderBg",     &AppColors::hexHeaderBg },
    { "hexHeaderText",   &AppColors::hexHeaderText },
    { "hexBarDefault",   &AppColors::hexBarDefault },
    { "mapCellBg",       &AppColors::mapCellBg },
    { "mapCellText",     &AppColors::mapCellText },
    { "mapCellModified", &AppColors::mapCellModified },
    { "mapGridLine",     &AppColors::mapGridLine },
    { "mapAxisXBg",      &AppColors::mapAxisXBg },
    { "mapAxisXText",    &AppColors::mapAxisXText },
    { "mapAxisYBg",      &AppColors::mapAxisYBg },
    { "mapAxisYText",    &AppColors::mapAxisYText },
    { "waveBg",          &AppColors::waveBg },
    { "waveGridMajor",   &AppColors::waveGridMajor },
    { "waveGridMinor",   &AppColors::waveGridMinor },
    { "waveLine",        &AppColors::waveLine },
    { "waveOverviewBg",  &AppColors::waveOverviewBg },
    { "uiBg",            &AppColors::uiBg },
    { "uiPanel",         &AppColors::uiPanel },
    { "uiBorder",        &AppColors::uiBorder },
    { "uiText",          &AppColors::uiText },
    { "uiTextDim",       &AppColors::uiTextDim },
    { "uiAccent",        &AppColors::uiAccent },
    { "topBarBg",        &AppColors::topBarBg },
    { "toolbarBg",       &AppColors::toolbarBg },
    { "statusBarBg",     &AppColors::statusBarBg },
    { "treeBg",          &AppColors::treeBg },
    { "treeSelected",    &AppColors::treeSelected },
    { "buttonBg",        &AppColors::buttonBg },
    { "buttonText",      &AppColors::buttonText },
    { "inputBg",         &AppColors::inputBg },
    { "inputBorder",     &AppColors::inputBorder },
};
} // namespace

void AppConfig::forEachColor(AppColors &c,
    const std::function<void(const QString &, QColor &)> &fn)
{
    for (int i = 0; i < 5; ++i)
        fn(QString("mapBand%1").arg(i), c.mapBand[i]);
    for (int i = 0; i < 8; ++i)
        fn(QString("waveRow%1").arg(i), c.waveRow[i]);
    for (const auto &f : kColorFields)
        fn(QString::fromUtf8(f.key), c.*(f.member));
}

void AppConfig::load()
{
    QSettings s("CT14", "RX14");
    AppColors def; applyDefaults(def);

    colors = def;
    forEachColor(colors, [&](const QString &key, QColor &target) {
        const QString v = s.value("colors/" + key).toString();
        if (v.isEmpty()) return;                 // keep default
        const QColor c(v);
        if (c.isValid()) target = c;
    });
    showLongMapNames = s.value("display/showLongMapNames",
                               languageDefaultsToLongNames()).toBool();

    int sh = s.value("wave2d/shape",
                     static_cast<int>(WaveStyle::Shape::LineDots)).toInt();
    if (sh < 0 || sh > 4) sh = static_cast<int>(WaveStyle::Shape::LineDots);
    waveStyle.shape        = static_cast<WaveStyle::Shape>(sh);
    waveStyle.lineWidth    = qBound(0.5, s.value("wave2d/lineWidth", 1.5).toDouble(), 6.0);
    waveStyle.dotSize      = qBound(0,   s.value("wave2d/dotSize", 0).toInt(), 8);
    waveStyle.fillUnderCurve = s.value("wave2d/fillUnderCurve", false).toBool();

    int pm = s.value("aiassistant/permissionMode",
                     static_cast<int>(PermissionMode::Ask)).toInt();
    if (pm < 0 || pm > 2) pm = static_cast<int>(PermissionMode::Ask);
    aiPermissionMode = static_cast<PermissionMode>(pm);
}

// Chinese users read maps by their full description; everywhere else the
// short German identifiers (KFMIOP, ...) are the convention — community
// issue #56.  Only the *default* is language-driven: once the user applies
// a choice in Preferences the stored value wins.
bool AppConfig::languageDefaultsToLongNames()
{
    const QString lang = rx14::appSettings()
                             .value("language",
                                    QLocale::system().name().section('_', 0, 0))
                             .toString();
    return lang.startsWith(QLatin1String("zh"));
}

void AppConfig::reapplyLanguageDefaults()
{
    QSettings s("CT14", "RX14");
    if (s.contains("display/showLongMapNames"))
        return;                                  // user made an explicit choice
    const bool def = languageDefaultsToLongNames();
    if (def == showLongMapNames)
        return;
    showLongMapNames = def;
    emit displaySettingsChanged();
}

void AppConfig::save()
{
    QSettings s("CT14", "RX14");
    forEachColor(colors, [&](const QString &key, QColor &c) {
        s.setValue("colors/" + key, c.name(QColor::HexArgb));
    });
    // Persist only an explicit deviation from the language default — while
    // the key is absent the default keeps tracking the UI language.
    if (s.contains("display/showLongMapNames")
        || showLongMapNames != languageDefaultsToLongNames())
        s.setValue("display/showLongMapNames", showLongMapNames);

    s.setValue("wave2d/shape",        static_cast<int>(waveStyle.shape));
    s.setValue("wave2d/lineWidth",    waveStyle.lineWidth);
    s.setValue("wave2d/dotSize",      waveStyle.dotSize);
    s.setValue("wave2d/fillUnderCurve", waveStyle.fillUnderCurve);

    s.setValue("aiassistant/permissionMode", static_cast<int>(aiPermissionMode));

    s.sync();
}

void AppConfig::resetToDefaults()
{
    applyDefaults(colors);
    waveStyle = WaveStyle();
    emit colorsChanged();
}

// ── Theme skin files ─────────────────────────────────────────────────────────
// {
//   "format": "rx14-theme", "version": 1, "name": "My Skin",
//   "colors": { "hexBg": "#ff10141b", ... },        // any subset
//   "wave2d": { "shape": 1, "lineWidth": 1.5, "dotSize": 0,
//               "fillUnderCurve": false }           // optional
// }

static const char *kThemeFormatTag = "rx14-theme";
static constexpr int kThemeVersion = 1;

bool AppConfig::exportTheme(const QString &filePath, const AppColors &colors,
                            const WaveStyle &style, const QString &name,
                            QString *errorOut)
{
    QJsonObject colorsObj;
    // forEachColor takes a mutable ref; we only read from it here.
    forEachColor(const_cast<AppColors &>(colors),
                 [&](const QString &key, QColor &c) {
        colorsObj.insert(key, c.name(QColor::HexArgb));
    });

    QJsonObject waveObj;
    waveObj.insert("shape",          static_cast<int>(style.shape));
    waveObj.insert("lineWidth",      style.lineWidth);
    waveObj.insert("dotSize",        style.dotSize);
    waveObj.insert("fillUnderCurve", style.fillUnderCurve);

    QJsonObject root;
    root.insert("format",  QString::fromUtf8(kThemeFormatTag));
    root.insert("version", kThemeVersion);
    if (!name.trimmed().isEmpty())
        root.insert("name", name.trimmed());
    root.insert("colors", colorsObj);
    root.insert("wave2d", waveObj);

    QSaveFile f(filePath);
    if (!f.open(QIODevice::WriteOnly)) {
        if (errorOut) *errorOut = f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (errorOut) *errorOut = f.errorString();
        return false;
    }
    return true;
}

bool AppConfig::importTheme(const QString &filePath, AppColors &colorsOut,
                            WaveStyle &styleOut, QString *nameOut,
                            QString *errorOut)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorOut) *errorOut = f.errorString();
        return false;
    }
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (doc.isNull() || !doc.isObject()) {
        if (errorOut)
            *errorOut = perr.error != QJsonParseError::NoError
                        ? perr.errorString()
                        : QStringLiteral("not a JSON object");
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.value("format").toString() != QLatin1String(kThemeFormatTag)) {
        if (errorOut) *errorOut = QStringLiteral("not a romHEX14 theme file");
        return false;
    }
    if (root.value("version").toInt(1) > kThemeVersion) {
        if (errorOut)
            *errorOut = QStringLiteral("theme was made by a newer romHEX14 "
                                       "version");
        return false;
    }
    const QJsonObject colorsObj = root.value("colors").toObject();
    if (colorsObj.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("theme has no colors");
        return false;
    }

    applyDefaults(colorsOut);                    // partial skins stay valid
    forEachColor(colorsOut, [&](const QString &key, QColor &target) {
        const QJsonValue v = colorsObj.value(key);
        if (!v.isString()) return;
        const QColor c(v.toString());
        if (c.isValid()) target = c;
    });

    styleOut = WaveStyle();
    const QJsonObject waveObj = root.value("wave2d").toObject();
    if (!waveObj.isEmpty()) {
        int sh = waveObj.value("shape")
                     .toInt(static_cast<int>(WaveStyle::Shape::LineDots));
        if (sh < 0 || sh > 4) sh = static_cast<int>(WaveStyle::Shape::LineDots);
        styleOut.shape     = static_cast<WaveStyle::Shape>(sh);
        styleOut.lineWidth = qBound(0.5, waveObj.value("lineWidth")
                                             .toDouble(1.5), 6.0);
        styleOut.dotSize   = qBound(0, waveObj.value("dotSize").toInt(0), 8);
        styleOut.fillUnderCurve = waveObj.value("fillUnderCurve")
                                      .toBool(false);
    }

    if (nameOut)
        *nameOut = root.value("name").toString();
    return true;
}

// ── User theme library ───────────────────────────────────────────────────────

QString AppConfig::userThemesDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/themes");
}

// Filesystem-safe slug for a theme name ("Night Rider!" -> "night-rider").
static QString themeSlug(const QString &name)
{
    QString out;
    for (const QChar ch : name.toLower())
        out += ch.isLetterOrNumber() ? ch : QLatin1Char('-');
    static const QRegularExpression dashes(QStringLiteral("-{2,}"));
    out.replace(dashes, QStringLiteral("-"));
    while (out.startsWith(QLatin1Char('-'))) out.remove(0, 1);
    while (out.endsWith(QLatin1Char('-')))   out.chop(1);
    return out.isEmpty() ? QStringLiteral("theme") : out;
}

QVector<AppConfig::UserTheme> AppConfig::userThemes()
{
    QVector<UserTheme> out;
    const QDir dir(userThemesDir());
    const QStringList files =
        dir.entryList({QStringLiteral("*.rx14theme")}, QDir::Files);
    for (const QString &f : files) {
        UserTheme t;
        t.filePath = dir.filePath(f);
        if (!importTheme(t.filePath, t.colors, t.waveStyle, &t.name))
            continue;                            // unreadable — skip silently
        if (t.name.trimmed().isEmpty())
            t.name = QFileInfo(f).completeBaseName();
        out.append(t);
    }
    std::sort(out.begin(), out.end(),
              [](const UserTheme &a, const UserTheme &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return out;
}

bool AppConfig::saveUserTheme(const QString &name, const AppColors &colors,
                              const WaveStyle &style, QString *pathOut,
                              QString *errorOut)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("theme name is empty");
        return false;
    }
    QDir dir(userThemesDir());
    if (!dir.mkpath(QStringLiteral("."))) {
        if (errorOut) *errorOut = QStringLiteral("cannot create ")
                                  + dir.absolutePath();
        return false;
    }

    // Same name (case-insensitive) overwrites that theme's file; otherwise
    // pick a fresh, unique file name.
    QString path;
    for (const UserTheme &t : userThemes()) {
        if (t.name.compare(trimmed, Qt::CaseInsensitive) == 0) {
            path = t.filePath;
            break;
        }
    }
    if (path.isEmpty()) {
        const QString slug = themeSlug(trimmed);
        path = dir.filePath(slug + QStringLiteral(".rx14theme"));
        for (int n = 2; QFileInfo::exists(path); ++n)
            path = dir.filePath(slug + QStringLiteral("-%1.rx14theme").arg(n));
    }

    if (!exportTheme(path, colors, style, trimmed, errorOut))
        return false;
    if (pathOut) *pathOut = path;
    return true;
}

bool AppConfig::renameUserTheme(const QString &filePath,
                                const QString &newName, QString *newPathOut,
                                QString *errorOut)
{
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("theme name is empty");
        return false;
    }
    AppColors c;
    WaveStyle ws;
    if (!importTheme(filePath, c, ws, nullptr, errorOut))
        return false;
    // Rewrite under the new name, then drop the old file (skip the delete
    // when the slug maps to the same file, e.g. only capitalization changed).
    QString newPath;
    if (!saveUserTheme(trimmed, c, ws, &newPath, errorOut))
        return false;
    if (QFileInfo(newPath) != QFileInfo(filePath))
        QFile::remove(filePath);
    if (newPathOut) *newPathOut = newPath;
    return true;
}

bool AppConfig::deleteUserTheme(const QString &filePath, QString *errorOut)
{
    QFile f(filePath);
    if (!f.exists())
        return true;                             // already gone
    if (!f.remove()) {
        if (errorOut) *errorOut = f.errorString();
        return false;
    }
    return true;
}
