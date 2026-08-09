/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#include <QObject>
#include <QString>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>

struct AIToolCall {
    QString id;
    QString name;
    QString argumentsJson;
};

struct AIMessage {
    enum Role { System, User, Assistant, ToolUse, ToolResult };
    Role    role    = User;
    QString content;
    QString reasoningContent;
    // For ToolUse (assistant emitting a tool call)
    QString toolCallId;
    QString toolName;
    QString toolInputJson;   // raw JSON string
    QVector<AIToolCall> toolCalls; // all calls emitted in one assistant turn
    // For ToolResult
    QString toolResultId;    // matches toolCallId
    QString toolResultName;
    QString toolResultJson;  // JSON result string
};

struct AIToolDef {
    QString     name;
    QString     description;
    QJsonObject inputSchema;  // JSON Schema {"type":"object","properties":{...},"required":[...]}
};

// Abstract base — one concrete impl per API dialect
class AIProvider : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Streaming, Error };
    using ChunkFn    = std::function<void(const QString &text)>;
    using ReasoningFn = std::function<void(const QString &text)>;
    using ToolCallFn = std::function<void(const QString &callId, const QString &name, const QJsonObject &input)>;
    using DoneFn     = std::function<void()>;
    using ErrorFn    = std::function<void(const QString &msg)>;

    explicit AIProvider(QObject *parent = nullptr);
    ~AIProvider() override = default;

    virtual QString providerName() const = 0;
    virtual void setApiKey(const QString &key)     = 0;
    virtual void setModel(const QString &model)    = 0;
    virtual void setBaseUrl(const QString &url)    = 0;  // for OpenAI-compat

    // Send a conversation turn; callbacks fire on the main thread.
    // The provider streams text via onChunk. When a tool call is complete it fires onToolCall.
    // onDone fires when the model stops. onError fires on network/API error.
    virtual void send(const QString &systemPrompt,
                      const QVector<AIMessage> &messages,
                      const QVector<AIToolDef> &tools,
                      ChunkFn    onChunk,
                      ReasoningFn onReasoning,
                      ToolCallFn onToolCall,
                      DoneFn     onDone,
                      ErrorFn    onError) = 0;
    virtual void abort() = 0;

signals:
    void stateChanged(State state);
};

// ── Claude (Anthropic) ────────────────────────────────────────────────────────
class ClaudeProvider : public AIProvider {
    Q_OBJECT
public:
    explicit ClaudeProvider(QObject *parent = nullptr);
    QString providerName() const override { return "Claude (Anthropic)"; }
    void setApiKey(const QString &k) override  { m_apiKey = k; }
    void setModel(const QString &m) override   { m_model = m; }
    void setBaseUrl(const QString &u) override  { m_baseUrl = u; }

    void send(const QString &systemPrompt,
              const QVector<AIMessage> &messages,
              const QVector<AIToolDef> &tools,
              ChunkFn onChunk, ReasoningFn onReasoning, ToolCallFn onToolCall,
              DoneFn onDone, ErrorFn onError) override;
    void abort() override;

private:
    QString m_apiKey;
    QString m_model = "claude-sonnet-4-6";
    QString m_baseUrl;  // empty = default Anthropic endpoint
    class QNetworkAccessManager *m_nam   = nullptr;
    class QNetworkReply          *m_reply = nullptr;
    // SSE parse state
    QString m_sseBuffer;
    // Tool call accumulation (one at a time for simplicity)
    QString m_pendingCallId;
    QString m_pendingName;
    QString m_pendingInputJson;
    bool    m_inToolUse = false;
    // Callbacks
    ChunkFn    m_onChunk;
    ToolCallFn m_onToolCall;
    DoneFn     m_onDone;
    ErrorFn    m_onError;

    void onReadyRead();
    void processEvent(const QString &event, const QString &data);

    bool m_errorFired = false;  // guard against double-fire (SSE error + finished)
};

// ── OpenAI-compatible (OpenAI, Qwen, DeepSeek, Gemini, Groq, Ollama, etc.) ──
class OpenAICompatProvider : public AIProvider {
    Q_OBJECT
public:
    explicit OpenAICompatProvider(QObject *parent = nullptr);
    QString providerName() const override { return m_providerLabel; }
    void setApiKey(const QString &k) override   { m_apiKey = k; }
    void setModel(const QString &m) override    { m_model = m; }
    void setBaseUrl(const QString &u) override  { m_baseUrl = u; }
    void setProviderLabel(const QString &l)     { m_providerLabel = l; }
    void setDeepSeek(bool on)                   { m_isDeepSeek = on; }

    void send(const QString &systemPrompt,
              const QVector<AIMessage> &messages,
              const QVector<AIToolDef> &tools,
              ChunkFn onChunk, ReasoningFn onReasoning, ToolCallFn onToolCall,
              DoneFn onDone, ErrorFn onError) override;
    void abort() override;

private:
    QString m_apiKey;
    QString m_model = "gpt-4o";
    QString m_baseUrl = "https://api.openai.com/v1";
    QString m_providerLabel = "OpenAI";
    bool    m_isDeepSeek = false;
    class QNetworkAccessManager *m_nam   = nullptr;
    class QNetworkReply          *m_reply = nullptr;
    QString m_sseBuffer;
    // Tool call accumulation — OpenAI streams tool calls incrementally
    struct PendingCall { QString id; QString name; QString argsJson; int index = 0; };
    QVector<PendingCall> m_pendingCalls;
    ChunkFn    m_onChunk;
    ReasoningFn m_onReasoning;
    ToolCallFn m_onToolCall;
    DoneFn     m_onDone;
    ErrorFn    m_onError;

    void onReadyRead();
    void processDelta(const QJsonObject &delta);

    bool m_errorFired = false;
    QString m_reasoningContent;
};
