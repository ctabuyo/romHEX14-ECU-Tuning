/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QStandardPaths>
#include <QMessageBox>
#include <QDateTime>
#include <QSharedMemory>
#include <QElapsedTimer>
#include <QThread>
#include <QRegularExpression>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include "appconstants.h"
#include "mainwindow.h"
#include "uiscale.h"
#include "logger.h"
#include "version.h"
#include "appconfig.h"
#include "uiwidgets.h"

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <dbghelp.h>
#endif

// ── Qt message handler ───────────────────────────────────────────────────────
static void qtMessageHandler(QtMsgType type, const QMessageLogContext &ctx,
                              const QString &msg)
{
    Logger::Level level;
    switch (type) {
    case QtDebugMsg:    level = Logger::Debug;    break;
    case QtInfoMsg:     level = Logger::Info;     break;
    case QtWarningMsg:  level = Logger::Warning;  break;
    case QtCriticalMsg: level = Logger::Error;    break;
    case QtFatalMsg:    level = Logger::Critical; break;
    default:            level = Logger::Info;     break;
    }
    QString cat = ctx.category ? QString::fromLatin1(ctx.category) : QString();
    Logger::instance().log(level, cat, msg, ctx.file, ctx.line);

    if (type == QtFatalMsg) {
        Logger::instance().writeCrashLine("QtFatalMsg received — aborting");
        std::abort();
    }
}

// ── POSIX / Windows signal handler ───────────────────────────────────────────
static void crashHandler(int sig) noexcept
{
    char buf[128];
    const char *name = "UNKNOWN";
    if      (sig == SIGSEGV)  name = "SIGSEGV (segmentation fault)";
    else if (sig == SIGABRT)  name = "SIGABRT (abort)";
    else if (sig == SIGFPE)   name = "SIGFPE (floating-point exception)";
    else if (sig == SIGILL)   name = "SIGILL (illegal instruction)";

#ifdef _WIN32
    snprintf(buf, sizeof(buf), "Signal %d: %s", sig, name);
#else
    snprintf(buf, sizeof(buf), "Signal %d: %s", sig, name);
#endif

    Logger::instance().writeCrashLine(buf);
    Logger::instance().writeCrashLine("Stack trace not available on this platform.");
    Logger::instance().writeCrashLine("Check the log file for the last operations before the crash.");

    // Re-raise to get the default OS crash handler (generates minidump on Windows)
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

#ifdef Q_OS_WIN
// Single guard: VEH and SEH both reach here, but we only want to walk + log
// once.  Without this, a single SIGSEGV can produce duplicate stack traces.
static std::atomic<bool> sStackTraceWritten{false};

// Walk the stack using DbgHelp and write each frame (module + offset, plus
// symbol if PDB is present) to the log.  Called from VEH (runs before SEH
// translation, so we capture the stack pristine on MinGW where SIGSEGV would
// otherwise pre-empt our SEH filter).
static void walkAndLogStack(EXCEPTION_POINTERS *ep, const char *origin)
{
    bool expected = false;
    if (!sStackTraceWritten.compare_exchange_strong(expected, true))
        return;  // already logged from another handler

    char buf[512];
    snprintf(buf, sizeof(buf),
             "[%s] Exception code=0x%08lX at addr=%p",
             origin,
             (unsigned long)ep->ExceptionRecord->ExceptionCode,
             ep->ExceptionRecord->ExceptionAddress);
    Logger::instance().writeCrashLine(buf);

    HANDLE proc = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    SymSetOptions(SymGetOptions() | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS);
    SymInitialize(proc, nullptr, TRUE);

    CONTEXT *ctx = ep->ContextRecord;
    STACKFRAME64 frame{};
    DWORD machineType;
#ifdef _M_X64
    machineType         = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = ctx->Rip;
    frame.AddrFrame.Offset = ctx->Rbp;
    frame.AddrStack.Offset = ctx->Rsp;
#else
    machineType         = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = ctx->Eip;
    frame.AddrFrame.Offset = ctx->Ebp;
    frame.AddrStack.Offset = ctx->Esp;
#endif
    frame.AddrPC.Mode    = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;

    Logger::instance().writeCrashLine("--- Stack trace ---");

    char symBuf[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO *sym = reinterpret_cast<SYMBOL_INFO *>(symBuf);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    IMAGEHLP_LINE64 line{};
    line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

    for (int i = 0; i < 64; ++i) {
        if (!StackWalk64(machineType, proc, thread, &frame, ctx, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64,
                         nullptr))
            break;
        if (frame.AddrPC.Offset == 0) break;

        DWORD64 addr = frame.AddrPC.Offset;
        char modBase[MAX_PATH] = "?";
        DWORD64 modBaseAddr = SymGetModuleBase64(proc, addr);
        if (modBaseAddr) {
            char modPath[MAX_PATH];
            if (GetModuleFileNameA(reinterpret_cast<HMODULE>(modBaseAddr),
                                   modPath, sizeof(modPath))) {
                const char *slash = strrchr(modPath, '\\');
                snprintf(modBase, sizeof(modBase), "%s",
                         slash ? slash + 1 : modPath);
            }
        }

        char symName[256] = "";
        DWORD64 disp = 0;
        if (SymFromAddr(proc, addr, &disp, sym)) {
            snprintf(symName, sizeof(symName), "%s+0x%llx",
                     sym->Name, (unsigned long long)disp);
        } else {
            snprintf(symName, sizeof(symName), "(no symbol)");
        }

        DWORD lineDisp = 0;
        char lineInfo[280] = "";
        if (SymGetLineFromAddr64(proc, addr, &lineDisp, &line)) {
            const char *slash = strrchr(line.FileName, '\\');
            snprintf(lineInfo, sizeof(lineInfo), "  [%s:%lu]",
                     slash ? slash + 1 : line.FileName,
                     (unsigned long)line.LineNumber);
        }

        const DWORD64 rva = modBaseAddr ? addr - modBaseAddr : 0;
        snprintf(buf, sizeof(buf),
                 "  #%2d 0x%016llx %s+0x%llx (base 0x%016llx) %s!%s%s",
                 i,
                 (unsigned long long)addr,
                 modBase,
                 (unsigned long long)rva,
                 (unsigned long long)modBaseAddr,
                 modBase,
                 symName,
                 lineInfo);
        Logger::instance().writeCrashLine(buf);
    }

    Logger::instance().writeCrashLine("--- End stack trace ---");
    SymCleanup(proc);
}

// Vectored exception handler: runs BEFORE the SEH chain and BEFORE MinGW's
// signal translator.  This is the only place we can reliably capture the
// stack on MinGW Windows for access violations / divide-by-zero / etc —
// the C runtime would otherwise convert these to SIGSEGV / SIGFPE before
// SetUnhandledExceptionFilter ever fires.
//
// We only handle truly fatal exception codes; software-thrown SEH (C++
// exceptions, Windows messaging) must continue to propagate normally.
static LONG WINAPI windowsVectoredHandler(EXCEPTION_POINTERS *ep)
{
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_STACK_OVERFLOW:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_NONCONTINUABLE_EXCEPTION:
        walkAndLogStack(ep, "VEH");
        break;
    default:
        break;
    }
    return EXCEPTION_CONTINUE_SEARCH;  // never swallow — let normal flow continue
}

// Windows Structured Exception Filter — fallback in case VEH was somehow
// bypassed.  walkAndLogStack() guards against duplicate output.
static LONG WINAPI windowsExceptionFilter(EXCEPTION_POINTERS *ep)
{
    walkAndLogStack(ep, "SEH");
    Logger::instance().writeCrashLine(
        "The application encountered a fatal error. "
        "A crash report has been written to the log file.");
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

#ifdef RX14_PRO_BUILD
// ── Anti-debug: detect debuggers (release builds only) ───────────────────────
#ifndef QT_DEBUG
static bool isDebuggerAttached() {
#ifdef Q_OS_WIN
    if (IsDebuggerPresent()) return true;
    BOOL remoteDebugger = FALSE;
    CheckRemoteDebuggerPresent(GetCurrentProcess(), &remoteDebugger);
    if (remoteDebugger) return true;
#endif
#ifdef Q_OS_LINUX
    QFile f("/proc/self/status");
    if (f.open(QIODevice::ReadOnly)) {
        QByteArray data = f.readAll();
        int idx = data.indexOf("TracerPid:");
        if (idx >= 0) {
            int pid = data.mid(idx + 10).trimmed().split('\n')[0].toInt();
            if (pid != 0) return true;
        }
    }
#endif
    return false;
}
#endif
#endif // RX14_PRO_BUILD

// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char *argv[])
{
    // ── Logger ─────────────────────────────────────────────────────────────
    // Must be initialised before QApplication so it can catch early Qt messages.
    {
#ifdef RX14_DEBUG_RPC
        // Debug build: log to D:\rx14-debug so the dev workspace doesn't fill
        // up the C: drive.  The directory is created on first write.
        QString logPath = QStringLiteral("D:/rx14-debug/rx14.log");
#else
        QString logDir = QStandardPaths::writableLocation(
                             QStandardPaths::AppDataLocation);
        // QStandardPaths needs org/app set first — set them manually here
        // since QApplication isn't constructed yet.
        QString logPath = logDir + "/CT14/romHEX14/rx14.log";
        // Fallback: write next to exe if AppData not available
        if (logDir.isEmpty()) logPath = "rx14.log";
#endif
        Logger::instance().init(logPath);
    }

    // ── Signal / exception handlers ────────────────────────────────────────
    std::signal(SIGSEGV, crashHandler);
    std::signal(SIGABRT, crashHandler);
    std::signal(SIGFPE,  crashHandler);
    std::signal(SIGILL,  crashHandler);
#ifdef Q_OS_WIN
    // Add VEH first — it runs before SEH, before MinGW's signal translation,
    // so it captures the stack pristine even when SIGSEGV is the apparent
    // cause of death.  Keep SEH as a safety net.
    AddVectoredExceptionHandler(/*FirstHandler*/ 1, windowsVectoredHandler);
    SetUnhandledExceptionFilter(windowsExceptionFilter);
#endif

    // ── Qt message handler ─────────────────────────────────────────────────
    qInstallMessageHandler(qtMessageHandler);
    LOG_INFO(QStringLiteral("main(): qInstallMessageHandler done"));

#ifdef RX14_PRO_BUILD
    // ── Anti-debug check (release builds only) ────────────────────────────
#ifndef QT_DEBUG
    if (isDebuggerAttached()) {
        return 1;
    }
#endif
#endif // RX14_PRO_BUILD

    // ── Interface scale (MUST be applied before QApplication) ──────────────
    // macOS/Qt don't surface the OS "large text" accessibility setting, so we
    // provide our own scale. QT_SCALE_FACTOR zooms the whole UI (text AND
    // layout) uniformly, which stays clean with the app's fixed-size widgets.
    // "auto" mode (default) derives the factor from the physical density of
    // the screen the window last lived on, so a 57" desk monitor and a Retina
    // laptop each get a sensible scale without touching Settings.
    {
        QSettings s(QString::fromUtf8(rx14::kOrgName),
                    QString::fromUtf8(rx14::kAppName));
        const QRect lastScreen =
            s.value(QString::fromUtf8(UiScale::kLastScreenKey)).toRect();
        const double scale = UiScale::startupScale(lastScreen);
        if (scale > 1.001 && scale <= 3.0
            && !qEnvironmentVariableIsSet("QT_SCALE_FACTOR")) {
            qputenv("QT_SCALE_FACTOR", QByteArray::number(scale, 'g', 4));
        }
    }

    // ── Application ────────────────────────────────────────────────────────
    QApplication app(argc, argv);
    app.setApplicationName(QString::fromUtf8(rx14::kAppName));
    app.setOrganizationName(QString::fromUtf8(rx14::kOrgName));
    app.setApplicationVersion(RX14_VERSION_STRING);
    LOG_INFO(QStringLiteral("main(): QApplication constructed"));

    // ── Settings migration ─────────────────────────────────────────────────
    // Pre-2026 builds wrote to CT14/RX14; everything since uses
    // CT14/romHEX14.  Copy old keys into the new store on first boot
    // after the upgrade so users don't lose recent files, geometry,
    // language, autosave preferences, etc.  Idempotent via a flag.
    {
        QSettings dest(QString::fromUtf8(rx14::kOrgName),
                       QString::fromUtf8(rx14::kAppName));
        const QString flag = QStringLiteral("internal/legacyMigrated");
        if (!dest.value(flag, false).toBool()) {
            QSettings src(QString::fromUtf8(rx14::kOrgName),
                          QString::fromUtf8(rx14::kLegacyAppName));
            const QStringList keys = src.allKeys();
            int copied = 0;
            for (const QString &k : keys) {
                if (dest.contains(k)) continue;     // never clobber
                dest.setValue(k, src.value(k));
                ++copied;
            }
            dest.setValue(flag, true);
            dest.sync();
            if (copied > 0)
                Logger::instance().log(Logger::Info,
                    QStringLiteral("Migrated %1 legacy QSettings keys "
                                   "from CT14/%2 to CT14/%3")
                        .arg(copied)
                        .arg(QString::fromUtf8(rx14::kLegacyAppName))
                        .arg(QString::fromUtf8(rx14::kAppName)));
        }
    }

    // ── Single instance check ───────────────────────────────────────────
    QSharedMemory singleLock("romHEX14_SingleInstance_Lock");
    bool lockAcquired = singleLock.create(1);
    if (!lockAcquired && qEnvironmentVariableIsSet("RX14_RESTART")) {
        // Self-relaunch (e.g. UI-scale change): the old instance may still be
        // shutting down and holding the lock — wait for it to release.
        QElapsedTimer t;
        t.start();
        while (!lockAcquired && t.elapsed() < 10000) {
            QThread::msleep(150);
            lockAcquired = singleLock.create(1);
        }
    }
    if (!lockAcquired) {
        // Another instance is running — bring it to front on Windows
#ifdef Q_OS_WIN
        HWND existing = FindWindowW(nullptr, L"romHEX 14");
        if (existing) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
#endif
        return 0;
    }

    // Re-init logger now that QStandardPaths works properly
    {
#ifdef RX14_DEBUG_RPC
        // Debug build keeps the D:\rx14-debug path set above.
        QString logPath = QStringLiteral("D:/rx14-debug/rx14.log");
#else
        QString logPath = QStandardPaths::writableLocation(
                              QStandardPaths::AppDataLocation) + "/rx14.log";
#endif
        Logger::instance().init(logPath);
    }

    LOG_INFO(QString("UI scale: %1% (%2 mode)")
             .arg(qRound(UiScale::appliedScale() * 100))
             .arg(UiScale::mode()));
    LOG_INFO(QString("romHEX14 v%1 starting on %2")
             .arg(app.applicationVersion())
             .arg(QDateTime::currentDateTime().toString(Qt::ISODate)));

    // ── Stylesheet ─────────────────────────────────────────────────────────
    QFile qss(":/style.qss");
    if (qss.open(QFile::ReadOnly | QFile::Text)) {
        QString styleSheet = QString::fromUtf8(qss.readAll());
        qss.close();
#ifdef Q_OS_MAC
        // On macOS, strip the custom QComboBox dark-theme styles so that
        // native Aqua rendering is used instead.  The markers in the QSS
        // delimit the block to remove.
        static const QRegularExpression comboRe(
            QStringLiteral("/\\* BEGIN_COMBOBOX_STYLE.*?END_COMBOBOX_STYLE \\*/"),
            QRegularExpression::DotMatchesEverythingOption);
        styleSheet.remove(comboRe);

        // ── macOS ergonomics pass ───────────────────────────────────────────
        // Qt's default metrics are tuned for Windows, where form controls
        // sit at ~20px tall. On macOS HiDPI that renders tiny, squished
        // inputs next to native chrome. This supplement is appended AFTER
        // the base sheet so it wins the cascade, and only applies on macOS.
        styleSheet += QStringLiteral(R"(
            QLineEdit {
                min-height: 26px;
                padding: 6px 10px;
                font-size: 11pt;
            }
            QPlainTextEdit, QTextEdit, QTextBrowser {
                padding: 6px 10px;
                font-size: 11pt;
            }
            QSpinBox, QDoubleSpinBox, QDateEdit, QDateTimeEdit, QTimeEdit {
                min-height: 26px;
                padding: 4px 8px;
                font-size: 11pt;
            }
            QComboBox {
                min-height: 24px;
                font-size: 11pt;
            }
            QCheckBox, QRadioButton {
                spacing: 8px;
                font-size: 11pt;
            }
            QCheckBox::indicator, QRadioButton::indicator {
                width: 18px;
                height: 18px;
            }
            QLabel { font-size: 11pt; }
            QDialog QLabel { padding: 2px 0; }
            QDialog QFormLayout { spacing: 10px; }
        )");
#endif
        // Substitute color placeholders with defaults before setting stylesheet
        {
            AppColors c;
            AppConfig::applyDefaults(c);
            styleSheet.replace("${uiBg}",          c.uiBg.name());
            styleSheet.replace("${uiPanel}",       c.uiPanel.name());
            styleSheet.replace("${buttonBg}",      c.buttonBg.name());
            styleSheet.replace("${uiBorder}",      c.uiBorder.name());
            styleSheet.replace("${uiAccent}",      c.uiAccent.name());
            styleSheet.replace("${uiAccentHover}", c.uiAccent.lighter(120).name());
            styleSheet.replace("${uiAccentLight}", c.uiAccent.lighter(140).name());
            styleSheet.replace("${uiTextDim}",     c.uiTextDim.name());
            styleSheet.replace("${uiText}",        c.uiText.name());
        }
        // Append the design-system object styles
        styleSheet += Theme::objectStyles();
        app.setStyleSheet(styleSheet);
    }

    app.setWindowIcon(QIcon(":/icon.png"));

    // ── Main window ────────────────────────────────────────────────────────
    int exitCode = 0;
    try {
        MainWindow w;
        w.setWindowIcon(QIcon(":/icon.png"));
#ifdef RX14_PRO_BUILD
        w.setWindowTitle("romHEX 14");
#else
        w.setWindowTitle("romHEX 14 Community");
#endif
        w.resize(1400, 850);
        w.show();

        LOG_INFO("Main window shown — entering event loop");
        exitCode = app.exec();
        LOG_INFO(QString("Event loop exited with code %1").arg(exitCode));

    } catch (const std::exception &ex) {
        LOG_CRITICAL(QString("Unhandled std::exception in main: %1").arg(ex.what()));
        QMessageBox::critical(
            nullptr,
            "Fatal Error",
            QString("romHEX14 encountered an unrecoverable error:\n\n%1\n\n"
                    "A crash report has been saved to:\n%2")
                .arg(ex.what())
                .arg(Logger::instance().logPath()));
        exitCode = 1;
    } catch (...) {
        LOG_CRITICAL("Unhandled unknown exception in main");
        QMessageBox::critical(
            nullptr,
            "Fatal Error",
            "romHEX14 encountered an unknown fatal error.\n\n"
            "A crash report has been saved to:\n" + Logger::instance().logPath());
        exitCode = 1;
    }

    return exitCode;
}
