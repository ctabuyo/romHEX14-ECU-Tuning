/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "aiprovider.h"
// Community build: no string obfuscation needed
#define OBF(s) std::string(s)
#define OBFQ(s) QStringLiteral(s)

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QUrl>
#include <QByteArray>
#include <QStringList>

// ── AIProvider base ───────────────────────────────────────────────────────────

AIProvider::AIProvider(QObject *parent) : QObject(parent) {}

// ── Helpers ───────────────────────────────────────────────────────────────────

static QJsonArray messagesToClaude(const QVector<AIMessage> &messages)
{
    QJsonArray arr;
    for (const AIMessage &msg : messages) {
        switch (msg.role) {
        case AIMessage::User: {
            QJsonObject m;
            m["role"] = "user";
            m["content"] = msg.content;
            arr.append(m);
            break;
        }
        case AIMessage::Assistant: {
            QJsonObject m;
            m["role"] = "assistant";
            m["content"] = msg.content;
            arr.append(m);
            break;
        }
        case AIMessage::ToolUse: {
            // Emitted by the assistant — wrap in assistant content block
            QJsonObject block;
            block["type"] = "tool_use";
            block["id"]   = msg.toolCallId;
            block["name"] = msg.toolName;
            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(msg.toolInputJson.toUtf8(), &err);
            block["input"] = (err.error == QJsonParseError::NoError && doc.isObject())
                             ? doc.object() : QJsonObject();
            QJsonArray content;
            content.append(block);
            QJsonObject m;
            m["role"]    = "assistant";
            m["content"] = content;
            arr.append(m);
            break;
        }
        case AIMessage::ToolResult: {
            // User message with tool_result block
            QJsonObject block;
            block["type"]       = "tool_result";
            block["tool_use_id"] = msg.toolResultId;
            block["content"]    = msg.toolResultJson;
            QJsonArray content;
            content.append(block);
            QJsonObject m;
            m["role"]    = "user";
            m["content"] = content;
            arr.append(m);
            break;
        }
        default:
            break;
        }
    }
    return arr;
}

static QJsonArray toolsToClaude(const QVector<AIToolDef> &tools)
{
    QJsonArray arr;
    for (const AIToolDef &t : tools) {
        QJsonObject obj;
        obj["name"]         = t.name;
        obj["description"]  = t.description;
        obj["input_schema"] = t.inputSchema;
        arr.append(obj);
    }
    return arr;
}

static QJsonArray messagesToOpenAI(const QString &systemPrompt, const QVector<AIMessage> &messages)
{
    QJsonArray arr;
    if (!systemPrompt.isEmpty()) {
        QJsonObject sys;
        sys["role"]    = "system";
        sys["content"] = systemPrompt;
        arr.append(sys);
    }
    for (const AIMessage &msg : messages) {
        switch (msg.role) {
        case AIMessage::User: {
            QJsonObject m;
            m["role"]    = "user";
            m["content"] = msg.content;
            arr.append(m);
            break;
        }
        case AIMessage::Assistant: {
            QJsonObject m;
            m["role"]    = "assistant";
            m["content"] = msg.content;
            arr.append(m);
            break;
        }
        case AIMessage::ToolUse: {
            // assistant message with tool_calls array
            QJsonArray tcs;
            QVector<AIToolCall> calls = msg.toolCalls;
            if (calls.isEmpty())
                calls.append({msg.toolCallId, msg.toolName, msg.toolInputJson});
            for (const AIToolCall &call : calls) {
                QJsonObject func;
                func["name"]      = call.name;
                func["arguments"] = call.argumentsJson;
                QJsonObject tc;
                tc["id"]       = call.id;
                tc["type"]     = "function";
                tc["function"] = func;
                tcs.append(tc);
            }
            QJsonObject m;
            m["role"]       = "assistant";
            m["content"]    = msg.content;
            if (!msg.reasoningContent.isEmpty())
                m["reasoning_content"] = msg.reasoningContent;
            m["tool_calls"] = tcs;
            arr.append(m);
            break;
        }
        case AIMessage::ToolResult: {
            QJsonObject m;
            m["role"]         = "tool";
            m["tool_call_id"] = msg.toolResultId;
            m["content"]      = msg.toolResultJson;
            arr.append(m);
            break;
        }
        default:
            break;
        }
    }
    return arr;
}

static QJsonArray toolsToOpenAI(const QVector<AIToolDef> &tools)
{
    QJsonArray arr;
    for (const AIToolDef &t : tools) {
        QJsonObject func;
        func["name"]        = t.name;
        func["description"] = t.description;
        func["parameters"]  = t.inputSchema;
        QJsonObject obj;
        obj["type"]     = "function";
        obj["function"] = func;
        arr.append(obj);
    }
    return arr;
}

// ── ClaudeProvider ─────────────────────────────────────────────────────────────

ClaudeProvider::ClaudeProvider(QObject *parent) : AIProvider(parent)
{
    m_nam = new QNetworkAccessManager(this);
}

void ClaudeProvider::abort()
{
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

void ClaudeProvider::send(const QString &systemPrompt,
                          const QVector<AIMessage> &messages,
                          const QVector<AIToolDef> &tools,
                          ChunkFn onChunk, ReasoningFn onReasoning, ToolCallFn onToolCall,
                          DoneFn onDone, ErrorFn onError)
{
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    m_onChunk     = onChunk;
    Q_UNUSED(onReasoning);
    m_onToolCall  = onToolCall;
    m_onDone      = onDone;
    m_onError     = onError;
    m_sseBuffer.clear();
    m_pendingCallId.clear();
    m_pendingName.clear();
    m_pendingInputJson.clear();
    m_inToolUse   = false;
    m_errorFired  = false;

    // Build request body
    QJsonObject body;
    body["model"]      = m_model;
    body["max_tokens"] = 4096;
    body["stream"]     = true;
    if (!systemPrompt.isEmpty())
        body["system"] = systemPrompt;
    body["messages"] = messagesToClaude(messages);
    if (!tools.isEmpty())
        body["tools"] = toolsToClaude(tools);

    QUrl endpoint(m_baseUrl.isEmpty()
                  ? OBFQ("https://api.anthropic.com/v1/messages")
                  : m_baseUrl + "/v1/messages");
    QNetworkRequest req(endpoint);
    req.setHeader(QNetworkRequest::ContentTypeHeader, OBF("application/json").c_str());
    req.setRawHeader(OBF("x-api-key").c_str(),          m_apiKey.toUtf8());
    req.setRawHeader(OBF("anthropic-version").c_str(),  "2023-06-01");
    req.setTransferTimeout(0);

    m_reply = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));

    connect(m_reply, &QNetworkReply::readyRead, this, &ClaudeProvider::onReadyRead);

    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        if (!m_reply) return;
        QNetworkReply::NetworkError netErr = m_reply->error();
        int statusCode = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray remaining = m_reply->readAll();  // any bytes not yet consumed by readyRead
        m_reply->deleteLater();
        m_reply = nullptr;

        if (netErr != QNetworkReply::NoError && netErr != QNetworkReply::OperationCanceledError) {
            if (!m_errorFired) {
                m_errorFired = true;
                // Build a human-readable error: prefer HTTP status + body over raw error code
                QString msg;
                if (statusCode > 0) {
                    // Try to extract "error.message" from the JSON body
                    QJsonParseError pe;
                    QJsonDocument doc = QJsonDocument::fromJson(remaining, &pe);
                    if (pe.error == QJsonParseError::NoError && doc.isObject()) {
                        QString apiMsg = doc.object()["error"].toObject()["message"].toString();
                        msg = apiMsg.isEmpty()
                            ? QString("HTTP %1").arg(statusCode)
                            : QString("HTTP %1: %2").arg(statusCode).arg(apiMsg);
                    } else {
                        msg = QString("HTTP %1: %2")
                            .arg(statusCode)
                            .arg(QString::fromUtf8(remaining.left(200)));
                    }
                } else {
                    msg = QString("Network error: %1 (code %2)")
                        .arg(m_reply ? m_reply->errorString() : "connection failed")
                        .arg((int)netErr);
                }
                if (m_onError) m_onError(msg);
            }
            return;
        }

        if (m_errorFired) return;  // SSE-level error already reported; don't also call onDone

        // If we were still in a tool use when the stream ended, fire the callback now
        if (m_inToolUse && !m_pendingCallId.isEmpty() && m_onToolCall) {
            QJsonParseError pe;
            QJsonDocument doc = QJsonDocument::fromJson(m_pendingInputJson.toUtf8(), &pe);
            QJsonObject inputObj = (pe.error == QJsonParseError::NoError && doc.isObject())
                                   ? doc.object() : QJsonObject();
            m_onToolCall(m_pendingCallId, m_pendingName, inputObj);
            m_inToolUse = false;
        }
        if (m_onDone) m_onDone();
    });

    emit stateChanged(State::Streaming);
}

void ClaudeProvider::onReadyRead()
{
    if (!m_reply) return;
    m_sseBuffer += QString::fromUtf8(m_reply->readAll());

    // Split on double newline (SSE event separator)
    while (true) {
        int sep = m_sseBuffer.indexOf("\n\n");
        if (sep < 0) break;
        QString block = m_sseBuffer.left(sep);
        m_sseBuffer   = m_sseBuffer.mid(sep + 2);

        // Parse event + data from block
        QString eventType;
        QString dataStr;
        for (const QString &line : block.split('\n')) {
            if (line.startsWith("event:"))
                eventType = line.mid(6).trimmed();
            else if (line.startsWith("data:"))
                dataStr = line.mid(5).trimmed();
        }
        if (!dataStr.isEmpty())
            processEvent(eventType, dataStr);
    }
}

void ClaudeProvider::processEvent(const QString &event, const QString &data)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return;
    QJsonObject obj = doc.object();

    if (event == "content_block_start") {
        QJsonObject block = obj["content_block"].toObject();
        if (block["type"].toString() == "tool_use") {
            m_inToolUse        = true;
            m_pendingCallId    = block["id"].toString();
            m_pendingName      = block["name"].toString();
            m_pendingInputJson = "";
        }
    } else if (event == "content_block_delta") {
        QJsonObject delta = obj["delta"].toObject();
        QString dtype = delta["type"].toString();
        if (dtype == "text_delta") {
            QString text = delta["text"].toString();
            if (m_onChunk) m_onChunk(text);
        } else if (dtype == "input_json_delta") {
            m_pendingInputJson += delta["partial_json"].toString();
        }
    } else if (event == "content_block_stop") {
        if (m_inToolUse && !m_pendingCallId.isEmpty() && m_onToolCall) {
            QJsonParseError pe;
            QJsonDocument inputDoc = QJsonDocument::fromJson(m_pendingInputJson.toUtf8(), &pe);
            QJsonObject inputObj = (pe.error == QJsonParseError::NoError && inputDoc.isObject())
                                   ? inputDoc.object() : QJsonObject();
            m_onToolCall(m_pendingCallId, m_pendingName, inputObj);
            m_inToolUse        = false;
            m_pendingCallId.clear();
            m_pendingName.clear();
            m_pendingInputJson.clear();
        }
    } else if (event == "message_stop") {
        // onDone fired from finished() signal
    } else if (event == "error") {
        QJsonObject errObj = obj["error"].toObject();
        QString msg = errObj["message"].toString();
        if (msg.isEmpty()) msg = data;
        if (!m_errorFired) {
            m_errorFired = true;
            if (m_onError) m_onError(msg);
        }
    }
}

// ── OpenAICompatProvider ────────────────────────────────────────────────────────

OpenAICompatProvider::OpenAICompatProvider(QObject *parent) : AIProvider(parent)
{
    m_nam = new QNetworkAccessManager(this);
}

void OpenAICompatProvider::abort()
{
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

void OpenAICompatProvider::send(const QString &systemPrompt,
                                const QVector<AIMessage> &messages,
                                const QVector<AIToolDef> &tools,
                                ChunkFn onChunk, ReasoningFn onReasoning, ToolCallFn onToolCall,
                                DoneFn onDone, ErrorFn onError)
{
    if (m_reply) {
        m_reply->abort();
        m_reply = nullptr;
    }

    m_onChunk    = onChunk;
    m_onReasoning = onReasoning;
    m_onToolCall = onToolCall;
    m_onDone     = onDone;
    m_onError    = onError;
    m_sseBuffer.clear();
    m_pendingCalls.clear();
    m_errorFired = false;
    m_reasoningContent.clear();

    QJsonObject body;
    body["model"]  = m_model;
    body["stream"] = true;
    body["messages"] = messagesToOpenAI(systemPrompt, messages);
    if (!tools.isEmpty())
        body["tools"] = toolsToOpenAI(tools);
    // DeepSeek thinking traces are retained and replayed with tool-call turns.
    if (m_isDeepSeek)
        body["thinking"] = QJsonObject{{"type", "enabled"}};

    QString url = m_baseUrl;
    if (!url.endsWith('/')) url += '/';
    url += "chat/completions";

    QUrl endpoint(url);
    QNetworkRequest req(endpoint);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!m_apiKey.isEmpty())
        req.setRawHeader(OBF("Authorization").c_str(), (OBF("Bearer ") + m_apiKey.toStdString()).c_str());
    req.setTransferTimeout(0);

    m_reply = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));

    connect(m_reply, &QNetworkReply::readyRead, this, &OpenAICompatProvider::onReadyRead);

    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        if (!m_reply) return;
        QNetworkReply::NetworkError netErr = m_reply->error();
        int statusCode = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray remaining = m_reply->readAll();
        m_reply->deleteLater();
        m_reply = nullptr;

        if (netErr != QNetworkReply::NoError && netErr != QNetworkReply::OperationCanceledError) {
            if (!m_errorFired) {
                m_errorFired = true;
                QString msg;
                if (statusCode > 0) {
                    QJsonParseError pe;
                    QJsonDocument doc = QJsonDocument::fromJson(remaining, &pe);
                    if (pe.error == QJsonParseError::NoError && doc.isObject()) {
                        QString apiMsg = doc.object()["error"].toObject()["message"].toString();
                        msg = apiMsg.isEmpty()
                            ? QString("HTTP %1").arg(statusCode)
                            : QString("HTTP %1: %2").arg(statusCode).arg(apiMsg);
                    } else {
                        msg = QString("HTTP %1: %2")
                            .arg(statusCode)
                            .arg(QString::fromUtf8(remaining.left(200)));
                    }
                } else {
                    msg = QString("Network error (code %1)").arg((int)netErr);
                }
                if (m_onError) m_onError(msg);
            }
            return;
        }

        if (m_errorFired) return;
        if (m_onDone) m_onDone();
    });

    emit stateChanged(State::Streaming);
}

void OpenAICompatProvider::onReadyRead()
{
    if (!m_reply) return;
    m_sseBuffer += QString::fromUtf8(m_reply->readAll());

    while (true) {
        int nl = m_sseBuffer.indexOf('\n');
        if (nl < 0) break;
        QString line = m_sseBuffer.left(nl).trimmed();
        m_sseBuffer  = m_sseBuffer.mid(nl + 1);

        if (!line.startsWith("data:")) continue;
        QString dataStr = line.mid(5).trimmed();
        if (dataStr == "[DONE]") {
            if (m_onReasoning && !m_reasoningContent.isEmpty())
                m_onReasoning(m_reasoningContent);
            // Fire all pending tool calls
            for (const PendingCall &pc : m_pendingCalls) {
                if (m_onToolCall) {
                    QJsonParseError pe;
                    QJsonDocument doc = QJsonDocument::fromJson(pc.argsJson.toUtf8(), &pe);
                    QJsonObject inputObj = (pe.error == QJsonParseError::NoError && doc.isObject())
                                          ? doc.object() : QJsonObject();
                    m_onToolCall(pc.id, pc.name, inputObj);
                }
            }
            m_pendingCalls.clear();
            // onDone fired from finished()
            continue;
        }
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(dataStr.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) continue;
        QJsonObject obj = doc.object();

        // Check for API-level error
        if (obj.contains("error")) {
            if (!m_errorFired) {
                m_errorFired = true;
                QJsonObject errObj = obj["error"].toObject();
                QString msg = errObj["message"].toString();
                if (msg.isEmpty()) msg = dataStr;
                if (m_onError) m_onError(msg);
            }
            continue;
        }

        QJsonArray choices = obj["choices"].toArray();
        if (choices.isEmpty()) continue;
        QJsonObject choice = choices[0].toObject();
        QJsonObject delta  = choice["delta"].toObject();
        QString finishReason = choice["finish_reason"].toString();

        processDelta(delta);

        if (finishReason == "tool_calls") {
            if (m_onReasoning && !m_reasoningContent.isEmpty())
                m_onReasoning(m_reasoningContent);
            for (const PendingCall &pc : m_pendingCalls) {
                if (m_onToolCall) {
                    QJsonParseError pe;
                    QJsonDocument idoc = QJsonDocument::fromJson(pc.argsJson.toUtf8(), &pe);
                    QJsonObject inputObj = (pe.error == QJsonParseError::NoError && idoc.isObject())
                                          ? idoc.object() : QJsonObject();
                    m_onToolCall(pc.id, pc.name, inputObj);
                }
            }
            m_pendingCalls.clear();
        }
    }
}

void OpenAICompatProvider::processDelta(const QJsonObject &delta)
{
    if (delta.contains("reasoning_content") && !delta["reasoning_content"].isNull())
        m_reasoningContent += delta["reasoning_content"].toString();
    // Text content
    if (delta.contains("content") && !delta["content"].isNull()) {
        QString text = delta["content"].toString();
        if (!text.isEmpty() && m_onChunk) m_onChunk(text);
    }

    // Tool calls (streamed incrementally)
    if (delta.contains("tool_calls")) {
        QJsonArray tcs = delta["tool_calls"].toArray();
        for (const QJsonValue &tcv : tcs) {
            QJsonObject tc = tcv.toObject();
            int idx = tc["index"].toInt(0);

            // Grow pending calls array as needed
            while (m_pendingCalls.size() <= idx)
                m_pendingCalls.append(PendingCall{});

            PendingCall &pc = m_pendingCalls[idx];
            pc.index = idx;

            if (tc.contains("id"))
                pc.id = tc["id"].toString();

            QJsonObject func = tc["function"].toObject();
            if (func.contains("name"))
                pc.name = func["name"].toString();
            if (func.contains("arguments"))
                pc.argsJson += func["arguments"].toString();
        }
    }
}
