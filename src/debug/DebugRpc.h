/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * In-process TCP RPC server for development & automated UI inspection.
 *
 * Listens on 127.0.0.1:<port> (default 48714).  Each request is a single
 * UTF-8 JSON object terminated by '\n'; each response likewise.  The server
 * is single-threaded — all handlers run on the Qt main thread, so they can
 * touch UI state safely.
 *
 * Built only when RX14_DEBUG_RPC is defined (CMake option, on by default
 * for non-release builds).
 */

#pragma once

#ifdef RX14_DEBUG_RPC

#include <QObject>
#include <QHash>
#include <QByteArray>
#include <QJsonObject>

class MainWindow;
class QTcpServer;
class QTcpSocket;

class DebugRpc : public QObject {
    Q_OBJECT
public:
    explicit DebugRpc(MainWindow *mw, QObject *parent = nullptr);
    ~DebugRpc() override;

    /// Start listening.  Returns true on success.  Logs (catRpc) on failure.
    bool start(quint16 port = 48714);
    void stop();

    quint16 port() const { return m_port; }

private slots:
    void onNewConnection();
    void onClientReadyRead();
    void onClientDisconnected();

private:
    QJsonObject dispatch(const QJsonObject &request);

    QJsonObject cmdPing(const QJsonObject &args);
    QJsonObject cmdState(const QJsonObject &args);
    QJsonObject cmdScreenshot(const QJsonObject &args);
    QJsonObject cmdAction(const QJsonObject &args);
    QJsonObject cmdScroll(const QJsonObject &args);
    QJsonObject cmdSwitchView(const QJsonObject &args);
    QJsonObject cmdTail(const QJsonObject &args);
    QJsonObject cmdHelp(const QJsonObject &args);
    QJsonObject cmdLoadRom(const QJsonObject &args);
    QJsonObject cmdApplyEdit(const QJsonObject &args);
    QJsonObject cmdAddAnnotation(const QJsonObject &args);
    QJsonObject cmdListAnnotations(const QJsonObject &args);
    QJsonObject cmdExportMapList(const QJsonObject &args);
    QJsonObject cmdFindSimilar(const QJsonObject &args);
    QJsonObject cmdOpenProject(const QJsonObject &args);
    QJsonObject cmdMapList(const QJsonObject &args);
    QJsonObject cmdReadBytes(const QJsonObject &args);
    QJsonObject cmdBulkEdit(const QJsonObject &args);
    QJsonObject cmdUndo(const QJsonObject &args);
    QJsonObject cmdRemoveAnno(const QJsonObject &args);
    QJsonObject cmdDumpOlsRom(const QJsonObject &args);
    QJsonObject cmdRunLua(const QJsonObject &args);     // Sprint L §5.0.5
    QJsonObject cmdRebuildIndex(const QJsonObject &args); // scoped MinHash rebuild test
    QJsonObject cmdFindFiles(const QJsonObject &args);    // MinHash findSimilar test

    MainWindow *m_mw = nullptr;
    QTcpServer *m_server = nullptr;
    quint16     m_port   = 0;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};
#endif // RX14_DEBUG_RPC
