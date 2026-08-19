/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#include <QObject>
#include <QByteArray>
#include <QString>
#include <QVector>
#include <QMap>

// ── DLL registry entry ────────────────────────────────────────────────────────
struct ChecksumDllInfo {
    int     devNum      = 0;     // DEV001 → 1
    QString description;         // "BOSCH EDC15 V3.1 PLCC 2x27C010 - ALL BRAND"
    QString filename;            // "DEV001.dll"
    bool    hasNative   = false; // native C++ implementation available (cross-platform)
};

// ── Result type ───────────────────────────────────────────────────────────────
enum class ChecksumResult {
    OK,           // checksum verified / corrected successfully
    Mismatch,     // checksum is wrong (verify mode)
    Unsupported,  // no DLL and no native implementation for this ECU
    Error         // I/O error, DLL load failure, etc.
};

namespace Checksum { class IChecksumPlugin; }

// ── ChecksumManager ───────────────────────────────────────────────────────────
class ChecksumManager : public QObject {
    Q_OBJECT
public:
    static ChecksumManager* instance();

    // All known DLL entries
    const QVector<ChecksumDllInfo>& allDlls() const { return m_dlls; }

    // Find best matching DLL for an ECU type string ("MED17.1.6", "EDC15", etc.)
    ChecksumDllInfo findDllForEcu(const QString& ecuType) const;

    // Auto-detect best algorithm by scanning ROM binary for embedded ECU identifiers,
    // then fall back to ecuType string. Preferred entry point for UI.
    ChecksumDllInfo autoDetect(const QByteArray& rom, const QString& ecuType) const;

    // Verify checksum using best available method (plugin, native, or DLL bridge)
    ChecksumResult verify(const QByteArray& rom, const ChecksumDllInfo& dll, QString& errorMsg);

    // Correct checksum in-place using best available method
    ChecksumResult correct(QByteArray& rom, const ChecksumDllInfo& dll, QString& errorMsg);

    // Direct verify/correct by ECU type string (combines find + verify/correct)
    ChecksumResult verifyForEcu(const QByteArray& rom, const QString& ecuType, QString& errorMsg);
    ChecksumResult correctForEcu(QByteArray& rom, const QString& ecuType, QString& errorMsg);

    // Path where DEVxxx.dll files are expected (next to exe / in app dir)
    QString dllFolder() const;

    // Path to checksumhelper.exe (32-bit bridge, Windows only)
    QString helperPath() const;

    bool isDllAvailable(const ChecksumDllInfo& dll) const;
    bool isHelperAvailable() const;

    // Dynamic plugin search directories
    QStringList pluginSearchPaths() const;

    // Reload all dynamic plugins
    void loadPlugins();

    /// Returns a platform warning string if DLL bridge is unavailable
    static QString platformNote();

private:
    explicit ChecksumManager(QObject* parent = nullptr);

    // 32-bit DLL bridge via subprocess (Windows only)
    ChecksumResult bridgeVerify(const QByteArray& rom, const ChecksumDllInfo& dll, QString& errorMsg);
    ChecksumResult bridgeCorrect(QByteArray& rom, const ChecksumDllInfo& dll, QString& errorMsg);
    ChecksumResult runHelper(const QByteArray& romIn, const ChecksumDllInfo& dll,
                             bool correctMode, QByteArray& romOut, QString& errorMsg);

    void buildRegistry();
    Checksum::IChecksumPlugin* pluginForDev(uint32_t devNum) const;

    static ChecksumManager* s_instance;
    QVector<ChecksumDllInfo> m_dlls;
    QMap<uint32_t, Checksum::IChecksumPlugin*> m_plugins;
    QMap<QString, class QPluginLoader*> m_loaders;
    class QFileSystemWatcher* m_watcher = nullptr;
};
