/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "debug/DebugRpc.h"

#ifdef RX14_DEBUG_RPC

#include "debug/DebugLog.h"
#include "logger.h"
#include "mainwindow.h"
#include "io/ols/OlsImporter.h"
#include "io/ols/KpImporter.h"
#include "io/winols/SimilarityIndex.h"
#include "io/winols/WinOlsConfig.h"

#include <QByteArrayView>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonValue>
#include <QtConcurrent>

#include <atomic>
#include <memory>

DebugRpc::DebugRpc(MainWindow *mw, QObject *parent)
    : QObject(parent), m_mw(mw)
{
}

DebugRpc::~DebugRpc()
{
    stop();
}

bool DebugRpc::start(quint16 port)
{
    if (m_server) return true;
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection,
            this, &DebugRpc::onNewConnection);
    if (!m_server->listen(QHostAddress::LocalHost, port)) {
        qCWarning(catRpc) << "listen failed on" << port
                          << ":" << m_server->errorString();
        delete m_server;
        m_server = nullptr;
        return false;
    }
    m_port = m_server->serverPort();
    qCInfo(catRpc) << "listening on 127.0.0.1:" << m_port;
    return true;
}

void DebugRpc::stop()
{
    for (auto it = m_buffers.begin(); it != m_buffers.end(); ++it) {
        if (it.key()) it.key()->disconnectFromHost();
    }
    m_buffers.clear();
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    m_port = 0;
}

void DebugRpc::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket *sock = m_server->nextPendingConnection();
        if (!sock) continue;
        m_buffers.insert(sock, QByteArray());
        connect(sock, &QTcpSocket::readyRead,
                this, &DebugRpc::onClientReadyRead);
        connect(sock, &QTcpSocket::disconnected,
                this, &DebugRpc::onClientDisconnected);
        qCDebug(catRpc) << "client connected from"
                        << sock->peerAddress().toString()
                        << ":" << sock->peerPort();
    }
}

void DebugRpc::onClientReadyRead()
{
    auto *sock = qobject_cast<QTcpSocket *>(sender());
    if (!sock) return;

    QByteArray &buf = m_buffers[sock];
    buf.append(sock->readAll());

    // Process all complete lines in the buffer
    int nl;
    while ((nl = buf.indexOf('\n')) != -1) {
        QByteArray line = buf.left(nl);
        buf.remove(0, nl + 1);
        // Trim trailing CR (Windows clients)
        while (!line.isEmpty() && (line.back() == '\r' || line.back() == ' '))
            line.chop(1);
        if (line.isEmpty()) continue;

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        QJsonObject response;

        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            response.insert("ok", false);
            response.insert("error",
                QStringLiteral("invalid JSON: %1").arg(err.errorString()));
        } else {
            response = dispatch(doc.object());
        }

        QByteArray out = QJsonDocument(response).toJson(QJsonDocument::Compact);
        out.append('\n');
        sock->write(out);
        sock->flush();
    }
}

void DebugRpc::onClientDisconnected()
{
    auto *sock = qobject_cast<QTcpSocket *>(sender());
    if (!sock) return;
    m_buffers.remove(sock);
    sock->deleteLater();
    qCDebug(catRpc) << "client disconnected";
}

QJsonObject DebugRpc::dispatch(const QJsonObject &request)
{
    const QString cmd = request.value("cmd").toString();
    const QJsonObject args = request.value("args").toObject();
    const QJsonValue id = request.value("id");

    qCDebug(catRpc) << "cmd=" << cmd << "args=" << args;

    QJsonObject resp;
    if      (cmd == "ping")        resp = cmdPing(args);
    else if (cmd == "state")       resp = cmdState(args);
    else if (cmd == "screenshot")  resp = cmdScreenshot(args);
    else if (cmd == "action")      resp = cmdAction(args);
    else if (cmd == "scroll")      resp = cmdScroll(args);
    else if (cmd == "switch_view") resp = cmdSwitchView(args);
    else if (cmd == "tail")        resp = cmdTail(args);
    else if (cmd == "help")        resp = cmdHelp(args);
    else if (cmd == "load_rom")    resp = cmdLoadRom(args);
    else if (cmd == "apply_edit")  resp = cmdApplyEdit(args);
    else if (cmd == "add_anno")    resp = cmdAddAnnotation(args);
    else if (cmd == "list_anno")   resp = cmdListAnnotations(args);
    else if (cmd == "export_maps") resp = cmdExportMapList(args);
    else if (cmd == "find_similar") resp = cmdFindSimilar(args);
    else if (cmd == "open_project") resp = cmdOpenProject(args);
    else if (cmd == "map_list")     resp = cmdMapList(args);
    else if (cmd == "read_bytes")   resp = cmdReadBytes(args);
    else if (cmd == "bulk_edit")    resp = cmdBulkEdit(args);
    else if (cmd == "undo")         resp = cmdUndo(args);
    else if (cmd == "remove_anno")  resp = cmdRemoveAnno(args);
    else if (cmd == "dump_ols_rom") resp = cmdDumpOlsRom(args);
    else if (cmd == "lua_run")      resp = cmdRunLua(args);
    else if (cmd == "rebuild_index") resp = cmdRebuildIndex(args);
    else if (cmd == "find_files")    resp = cmdFindFiles(args);
    else if (cmd == "import_kp")     resp = cmdImportKp(args);
    else {
        resp.insert("ok", false);
        resp.insert("error", QStringLiteral("unknown cmd: %1").arg(cmd));
    }

    if (!id.isUndefined()) resp.insert("id", id);
    return resp;
}

// ── Handlers ────────────────────────────────────────────────────────────────

QJsonObject DebugRpc::cmdPing(const QJsonObject &)
{
    QJsonObject r;
    r.insert("ok", true);
    r.insert("result", QStringLiteral("pong"));
    return r;
}

QJsonObject DebugRpc::cmdHelp(const QJsonObject &)
{
    QJsonObject r;
    r.insert("ok", true);
    QJsonArray cmds;
    cmds.append("ping                       — connectivity check");
    cmds.append("state                      — full UI snapshot (projects/MDI/sync/scroll)");
    cmds.append("screenshot {target?}       — PNG to D:/rx14-debug/screenshots; target ∈ {main,active,subwindow:N}");
    cmds.append("action {trigger}           — trigger named QAction (m_actSyncCursors, m_actCompare, ...)");
    cmds.append("scroll {target,sub,value}  — set scroll; target ∈ {hex,wave}; sub = subwindow index; value = row|byteOffset");
    cmds.append("switch_view {sub,index}    — set ProjectView stack index (0=Text/Hex, 1=2D/Wave, 2=3D, ...)");
    cmds.append("tail {lines?=200}          — last N lines from rx14.log");
    cmds.append("help                       — this list");
    r.insert("result", cmds);
    return r;
}

QJsonObject DebugRpc::cmdState(const QJsonObject &)
{
    QJsonObject r;
    if (!m_mw) {
        r.insert("ok", false);
        r.insert("error", "no MainWindow");
        return r;
    }
    r.insert("ok", true);
    r.insert("result", m_mw->debugStateSnapshot());
    return r;
}

QJsonObject DebugRpc::cmdScreenshot(const QJsonObject &args)
{
    QJsonObject r;
    if (!m_mw) {
        r.insert("ok", false);
        r.insert("error", "no MainWindow");
        return r;
    }
    QString target = args.value("target").toString("main");
    QString path;
    QString err;
    if (m_mw->debugTakeScreenshot(target, &path, &err)) {
        r.insert("ok", true);
        QJsonObject result;
        result.insert("path", path);
        r.insert("result", result);
    } else {
        r.insert("ok", false);
        r.insert("error", err);
    }
    return r;
}

QJsonObject DebugRpc::cmdAction(const QJsonObject &args)
{
    QJsonObject r;
    if (!m_mw) {
        r.insert("ok", false);
        r.insert("error", "no MainWindow");
        return r;
    }
    const QString trigger = args.value("trigger").toString();
    if (trigger.isEmpty()) {
        r.insert("ok", false);
        r.insert("error", "missing 'trigger'");
        return r;
    }
    QString err;
    bool checked = false;
    if (m_mw->debugTriggerAction(trigger, &checked, &err)) {
        r.insert("ok", true);
        QJsonObject result;
        result.insert("trigger", trigger);
        result.insert("checked", checked);
        r.insert("result", result);
    } else {
        r.insert("ok", false);
        r.insert("error", err);
    }
    return r;
}

QJsonObject DebugRpc::cmdScroll(const QJsonObject &args)
{
    QJsonObject r;
    if (!m_mw) {
        r.insert("ok", false);
        r.insert("error", "no MainWindow");
        return r;
    }
    const QString target = args.value("target").toString();
    const int sub = args.value("sub").toInt(-1);
    const int value = args.value("value").toInt(-1);
    if (target.isEmpty() || sub < 0 || value < 0) {
        r.insert("ok", false);
        r.insert("error", "need target ∈ {hex,wave}, sub≥0, value≥0");
        return r;
    }
    QString err;
    if (m_mw->debugSetScroll(target, sub, value, &err)) {
        r.insert("ok", true);
    } else {
        r.insert("ok", false);
        r.insert("error", err);
    }
    return r;
}

QJsonObject DebugRpc::cmdSwitchView(const QJsonObject &args)
{
    QJsonObject r;
    if (!m_mw) {
        r.insert("ok", false);
        r.insert("error", "no MainWindow");
        return r;
    }
    const int sub = args.value("sub").toInt(-1);
    const int idx = args.value("index").toInt(-1);
    if (sub < 0 || idx < 0) {
        r.insert("ok", false);
        r.insert("error", "need sub≥0, index≥0");
        return r;
    }
    QString err;
    if (m_mw->debugSwitchView(sub, idx, &err)) {
        r.insert("ok", true);
    } else {
        r.insert("ok", false);
        r.insert("error", err);
    }
    return r;
}

QJsonObject DebugRpc::cmdLoadRom(const QJsonObject &args)
{
    QJsonObject r;
    if (!m_mw) {
        r.insert("ok", false);
        r.insert("error", "no MainWindow");
        return r;
    }
    const QString path = args.value("path").toString();
    if (path.isEmpty()) {
        r.insert("ok", false);
        r.insert("error", "missing 'path'");
        return r;
    }
    QString err;
    if (m_mw->debugLoadRom(path, &err)) {
        r.insert("ok", true);
    } else {
        r.insert("ok", false);
        r.insert("error", err);
    }
    return r;
}

QJsonObject DebugRpc::cmdApplyEdit(const QJsonObject &args)
{
    QJsonObject r;
    if (!m_mw) { r.insert("ok", false); r.insert("error", "no MainWindow"); return r; }
    int sub   = args.value("sub").toInt(-1);
    int op    = args.value("op").toInt(-1);
    int start = args.value("start").toInt(-1);
    int end   = args.value("end").toInt(-1);
    double v  = args.value("value").toDouble(0.0);
    if (sub < 0 || op < 0 || start < 0 || end <= start) {
        r.insert("ok", false);
        r.insert("error", "need sub>=0, op>=0, start>=0, end>start");
        return r;
    }
    QString err;
    if (m_mw->debugApplyEdit(sub, op, start, end, v, &err)) {
        r.insert("ok", true);
    } else {
        r.insert("ok", false);
        r.insert("error", err);
    }
    return r;
}

QJsonObject DebugRpc::cmdAddAnnotation(const QJsonObject &args)
{
    QJsonObject r;
    if (!m_mw) { r.insert("ok", false); r.insert("error", "no MainWindow"); return r; }
    int sub      = args.value("sub").toInt(0);
    qint64 addr  = static_cast<qint64>(args.value("addr").toDouble(-1));
    qint64 len   = static_cast<qint64>(args.value("length").toDouble(1));
    QString text = args.value("text").toString();
    if (addr < 0) {
        r.insert("ok", false);
        r.insert("error", "need addr >= 0");
        return r;
    }
    QString err;
    if (m_mw->debugAddAnnotation(sub, addr, text, len, &err)) {
        r.insert("ok", true);
    } else {
        r.insert("ok", false);
        r.insert("error", err);
    }
    return r;
}

QJsonObject DebugRpc::cmdListAnnotations(const QJsonObject &args)
{
    QJsonObject r;
    if (!m_mw) { r.insert("ok", false); r.insert("error", "no MainWindow"); return r; }
    int sub = args.value("sub").toInt(0);
    QJsonObject result;
    result.insert("items", m_mw->debugAnnotationList(sub));
    r.insert("ok", true);
    r.insert("result", result);
    return r;
}

QJsonObject DebugRpc::cmdExportMapList(const QJsonObject &args)
{
    QJsonObject r;
    if (!m_mw) { r.insert("ok", false); r.insert("error", "no MainWindow"); return r; }
    int sub          = args.value("sub").toInt(0);
    bool csv         = args.value("format").toString("csv") == "csv";
    QString path     = args.value("path").toString();
    if (path.isEmpty()) {
        r.insert("ok", false);
        r.insert("error", "missing 'path'");
        return r;
    }
    QString err;
    if (m_mw->debugExportMapList(sub, csv, path, &err)) {
        r.insert("ok", true);
        QJsonObject result;
        result.insert("path", path);
        r.insert("result", result);
    } else {
        r.insert("ok", false);
        r.insert("error", err);
    }
    return r;
}

QJsonObject DebugRpc::cmdReadBytes(const QJsonObject &args)
{
    QJsonObject r;
    if (!m_mw) { r.insert("ok", false); r.insert("error", "no MainWindow"); return r; }
    int sub      = args.value("sub").toInt(0);
    qint64 addr  = static_cast<qint64>(args.value("addr").toDouble(-1));
    int len      = args.value("len").toInt(16);
    if (addr < 0) {
        r.insert("ok", false); r.insert("error", "need addr >= 0"); return r;
    }
    QString hex = m_mw->debugReadBytes(sub, addr, len);
    if (hex.isEmpty()) {
        r.insert("ok", false); r.insert("error", "out of range or no project");
        return r;
    }
    QJsonObject result;
    result.insert("hex", hex);
    r.insert("ok", true);
    r.insert("result", result);
    return r;
}

QJsonObject DebugRpc::cmdBulkEdit(const QJsonObject &args)
{
    QJsonObject r;
    if (!m_mw) { r.insert("ok", false); r.insert("error", "no MainWindow"); return r; }
    int sub      = args.value("sub").toInt(0);
    int op       = args.value("op").toInt(-1);
    double v     = args.value("value").toDouble(0.0);
    QStringList names;
    for (const auto &x : args.value("maps").toArray())
        names.append(x.toString());
    if (op < 0 || names.isEmpty()) {
        r.insert("ok", false); r.insert("error", "need op>=0 and non-empty maps[]");
        return r;
    }
    QString err;
    if (m_mw->debugBulkEdit(sub, names, op, v, &err)) {
        r.insert("ok", true);
    } else {
        r.insert("ok", false); r.insert("error", err);
    }
    return r;
}

QJsonObject DebugRpc::cmdUndo(const QJsonObject &args)
{
    QJsonObject r;
    if (!m_mw) { r.insert("ok", false); r.insert("error", "no MainWindow"); return r; }
    int sub = args.value("sub").toInt(0);
    QString err;
    if (m_mw->debugUndo(sub, &err)) r.insert("ok", true);
    else { r.insert("ok", false); r.insert("error", err); }
    return r;
}

QJsonObject DebugRpc::cmdRemoveAnno(const QJsonObject &args)
{
    QJsonObject r;
    if (!m_mw) { r.insert("ok", false); r.insert("error", "no MainWindow"); return r; }
    int sub     = args.value("sub").toInt(0);
    qint64 addr = static_cast<qint64>(args.value("addr").toDouble(-1));
    if (addr < 0) {
        r.insert("ok", false); r.insert("error", "need addr"); return r;
    }
    QString err;
    if (m_mw->debugRemoveAnnotation(sub, addr, &err)) r.insert("ok", true);
    else { r.insert("ok", false); r.insert("error", err.isEmpty() ? QString("no anno at addr") : err); }
    return r;
}

QJsonObject DebugRpc::cmdOpenProject(const QJsonObject &args)
{
    QJsonObject r;
    if (!m_mw) { r.insert("ok", false); r.insert("error", "no MainWindow"); return r; }
    QString path = args.value("path").toString();
    if (path.isEmpty()) {
        r.insert("ok", false); r.insert("error", "missing 'path'"); return r;
    }
    QString err;
    if (m_mw->debugOpenProject(path, &err)) {
        r.insert("ok", true);
    } else {
        r.insert("ok", false); r.insert("error", err);
    }
    return r;
}

QJsonObject DebugRpc::cmdMapList(const QJsonObject &args)
{
    QJsonObject r;
    if (!m_mw) { r.insert("ok", false); r.insert("error", "no MainWindow"); return r; }
    int sub = args.value("sub").toInt(0);
    int limit = args.value("limit").toInt(0);
    QJsonObject result;
    result.insert("maps", m_mw->debugMapList(sub, limit));
    r.insert("ok", true);
    r.insert("result", result);
    return r;
}


QJsonObject DebugRpc::cmdFindSimilar(const QJsonObject &args)
{
    QJsonObject r;
    if (!m_mw) { r.insert("ok", false); r.insert("error", "no MainWindow"); return r; }
    int sub          = args.value("sub").toInt(0);
    QString refName  = args.value("ref").toString();
    double threshold = args.value("threshold").toDouble(0.8);
    if (refName.isEmpty()) {
        r.insert("ok", false);
        r.insert("error", "missing 'ref' map name");
        return r;
    }
    QJsonObject result;
    result.insert("matches", m_mw->debugFindSimilar(sub, refName, threshold));
    r.insert("ok", true);
    r.insert("result", result);
    return r;
}

QJsonObject DebugRpc::cmdTail(const QJsonObject &args)
{
    const int n = args.value("lines").toInt(200);
    QStringList lines = Logger::instance().tail(n);
    QJsonArray arr;
    for (const auto &l : lines) arr.append(l);
    QJsonObject r;
    r.insert("ok", true);
    QJsonObject result;
    result.insert("lines", arr);
    r.insert("result", result);
    return r;
}

// Dumps OlsImporter output for a given .ols/.kp path so we can debug
// what version 0 actually contains.  Returns:
//   - error string if parse failed
//   - n_versions, version[i].name, version[i].rom_size, version[i].sha256_first16
//   - rawHash16: SHA256[..16] of the raw file bytes (for sanity)
QJsonObject DebugRpc::cmdDumpOlsRom(const QJsonObject &args)
{
    QJsonObject r;
    QString path = args.value("path").toString();
    if (path.isEmpty()) {
        r.insert("ok", false);
        r.insert("error", "missing 'path'");
        return r;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        r.insert("ok", false);
        r.insert("error", "cannot open file");
        return r;
    }
    QByteArray fileData = f.readAll();
    f.close();
    QByteArray rawHash = QCryptographicHash::hash(fileData, QCryptographicHash::Sha256);

    QJsonObject result;
    result.insert("file_size", fileData.size());
    result.insert("raw_sha256_16", QString::fromLatin1(rawHash.toHex().left(32)));

    ols::OlsImportResult res = ols::OlsImporter::importFromBytes(fileData);
    result.insert("parse_error", res.error);
    QJsonArray versions;
    for (int i = 0; i < res.versions.size(); ++i) {
        const auto &v = res.versions[i];
        QJsonObject vobj;
        vobj.insert("index", i);
        vobj.insert("name", v.name);
        vobj.insert("rom_size", v.romData.size());
        QByteArray rh = QCryptographicHash::hash(v.romData, QCryptographicHash::Sha256);
        vobj.insert("rom_sha256_16", QString::fromLatin1(rh.toHex().left(32)));
        vobj.insert("rom_first32_hex", QString::fromLatin1(v.romData.left(32).toHex()));
        // Optional: return the full ROM as base64 for offline analysis.
        if (args.value("include_bytes").toBool() && i == 0) {
            vobj.insert("rom_bytes_b64", QString::fromLatin1(v.romData.toBase64()));
        }
        versions.append(vobj);
    }
    result.insert("versions", versions);
    r.insert("ok", true);
    r.insert("result", result);
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Sprint L §5.0.5 / Iter 1 — lua_run RPC handler
// ─────────────────────────────────────────────────────────────────────────────
#include "lua/LuaEngine.h"

QJsonObject DebugRpc::cmdRunLua(const QJsonObject &args)
{
    QJsonObject r;
    QString file = args.value(QStringLiteral("file")).toString();
    if (file.isEmpty()) {
        r.insert("ok", false);
        r.insert("error", QStringLiteral("missing 'file' argument"));
        return r;
    }
    return lua::LuaEngine::instance().runFile(file);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Scoped MinHash index rebuild (dev/test).  Runs the real
//  SimilarityIndex::rebuild on a detached worker (same path the UI uses),
//  so we can verify it at controlled scale without an hours-long full scan.
//  args: { "roots": ["C:/ROMs", ...] }  — defaults to configured scanFallback.
// ─────────────────────────────────────────────────────────────────────────────
QJsonObject DebugRpc::cmdRebuildIndex(const QJsonObject &args)
{
    QJsonObject r;
    QStringList roots;
    for (const QJsonValue &v : args.value(QStringLiteral("roots")).toArray())
        roots << v.toString();
    if (roots.isEmpty()) {
        winols::Config cfg;
        roots = cfg.scanFallback();
    }
    if (roots.isEmpty()) {
        r.insert("ok", false);
        r.insert("error", QStringLiteral("no roots (pass 'roots' or configure scanFallback)"));
        return r;
    }

    static std::atomic<bool> running{false};
    if (running.exchange(true)) {
        r.insert("ok", false);
        r.insert("error", QStringLiteral("rebuild already running"));
        return r;
    }

    auto idx = std::make_shared<winols::SimilarityIndex>();
    QString err;
    if (!idx->open(&err)) {
        running.store(false);
        r.insert("ok", false);
        r.insert("error", QStringLiteral("index open failed: ") + err);
        return r;
    }
    const int before = idx->rowCount();
    const QString dbp = idx->dbPath();

    (void)QtConcurrent::run([idx, roots]() {
        idx->rebuild(roots);
        running.store(false);   // static local — accessible without capture
    });

    r.insert("ok", true);
    QJsonObject result;
    result.insert("started", true);
    result.insert("roots", int(roots.size()));
    result.insert("rowsBefore", before);
    result.insert("dbPath", dbp);
    r.insert("result", result);
    return r;
}

// MinHash findSimilar test: fingerprint a file and query the local index.
// args: { "path": "...", "minPct": 30, "limit": 20 }
QJsonObject DebugRpc::cmdFindFiles(const QJsonObject &args)
{
    QJsonObject r;
    const QString path = args.value(QStringLiteral("path")).toString();
    const int minPct = args.value(QStringLiteral("minPct")).toInt(30);
    const int limit  = args.value(QStringLiteral("limit")).toInt(20);

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        r.insert("ok", false);
        r.insert("error", QStringLiteral("cannot open ") + path);
        return r;
    }
    QByteArray raw = f.read(64 * 1024 * 1024);
    f.close();

    // Needle = inner ROM for .ols/.kp, else raw bytes.
    QByteArray rom = raw;
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == QStringLiteral("ols") || ext == QStringLiteral("kp")) {
        const ols::OlsImportResult res = ols::OlsImporter::importFromBytes(raw);
        if (res.error.isEmpty() && !res.versions.isEmpty())
            rom = res.versions.first().romData;
    }

    const winols::RomFingerprint fp = winols::fingerprint(QByteArrayView(rom));
    winols::SimilarityIndex idx;
    QString e;
    if (!idx.open(&e)) {
        r.insert("ok", false);
        r.insert("error", QStringLiteral("index open: ") + e);
        return r;
    }
    QElapsedTimer t; t.start();
    const auto matches = idx.findSimilar(fp, minPct, limit);
    const qint64 ms = t.elapsed();

    QJsonArray arr;
    for (const auto &m : matches) {
        QJsonObject o;
        o.insert("file", QFileInfo(m.path).fileName());
        o.insert("version", m.versionLabel);
        o.insert("pct", m.score.bestPct());
        o.insert("jaccard", m.score.wholePct());
        o.insert("containment", m.score.containPct());
        arr.append(o);
    }
    QJsonObject res;
    res.insert("needle", QFileInfo(path).fileName());
    res.insert("needleBytes", rom.size());
    res.insert("indexRows", idx.rowCount());
    res.insert("ms", int(ms));
    res.insert("count", arr.size());
    res.insert("matches", arr);
    r.insert("ok", true);
    r.insert("result", res);
    return r;
}

// .kp mappack import test - reports per-map decoded address (issue #18).
// args: { "path": "...", "limit": 30, "romSize": optional }
QJsonObject DebugRpc::cmdImportKp(const QJsonObject &args)
{
    QJsonObject r;
    const QString path = args.value(QStringLiteral("path")).toString();
    const int limit = args.value(QStringLiteral("limit")).toInt(30);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        r.insert("ok", false);
        r.insert("error", QStringLiteral("cannot open ") + path);
        return r;
    }
    const QByteArray data = f.readAll();
    f.close();

    const uint32_t romSize = static_cast<uint32_t>(
        qMax(0, args.value(QStringLiteral("romSize")).toInt(0)));
    const ols::KpImportResult kr =
        ols::KpImporter::importFromBytes(data, 0, romSize);
    QJsonObject res;
    res.insert("fmtVer", int(kr.formatVersion));
    res.insert("mapCount", int(kr.mapCount));
    res.insert("error", kr.error);
    QJsonArray warns;
    for (const QString &w : kr.warnings) warns.append(w);
    res.insert("warnings", warns);

    int zeroAddr = 0;
    for (const MapInfo &m : kr.maps)
        if (m.address == 0) ++zeroAddr;
    res.insert("zeroAddrCount", zeroAddr);

    QJsonArray maps;
    int n = 0;
    for (const MapInfo &m : kr.maps) {
        if (n++ >= limit) break;
        QJsonObject o;
        o.insert("name", m.name);
        o.insert("address", QStringLiteral("0x%1").arg(quint64(m.address), 0, 16));
        o.insert("rawAddress", QStringLiteral("0x%1").arg(quint64(m.rawAddress), 0, 16));
        o.insert("x", int(m.dimensions.x));
        o.insert("y", int(m.dimensions.y));
        o.insert("type", m.type);
        maps.append(o);
    }
    res.insert("maps", maps);
    r.insert("ok", true);
    r.insert("result", res);
    return r;
}

#endif // RX14_DEBUG_RPC
