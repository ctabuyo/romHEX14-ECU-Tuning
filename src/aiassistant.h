/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#include <QWidget>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QTextBrowser>
#include <QVector>
#include <QEvent>
#include <QTimer>
#include <QAction>
#include <QPointer>
#include "aiprovider.h"
#include "aitoolexecutor.h"
#include "appconfig.h"

class BubbleWidget;   // forward-declare

class Project;

class AIAssistant : public QWidget {
    Q_OBJECT
public:
    enum class AssistantState { IDLE, WORKING, AWAITING_CONFIRMATION };

    explicit AIAssistant(QWidget *parent = nullptr);

    void setProject(Project *project);
    void setAllProjects(const QVector<Project*> &projects); // all open projects (for finding linked ROM)
    void setSelectedMap(const MapInfo &map);  // called when user selects a map
    void retranslateUi();
    void loadSettings();

    /// Create a one-shot AI provider from saved settings (caller takes ownership)
    static AIProvider *createOneShotProvider(QObject *parent = nullptr);

    bool eventFilter(QObject *obj, QEvent *event) override;

signals:
    void projectModified();

private slots:
    void onSend();
    void onCancel();
    void onProviderChanged(int index);
    void onSettingsClicked();
    void onClearChat();

private:
    void appendMessage(const QString &role, const QString &text);
    void appendChunk(const QString &chunk);    // adds to last assistant bubble
    void startAssistantBubble();
    void buildSystemPrompt();
    void doSend();                             // one async round-trip; continues via timer if tool calls pending
    void handleToolCall(const QString &callId, const QString &name, const QJsonObject &input,
                        bool recordToolUse = true);
    void saveSettings();
    AIProvider *createProvider(int index);
    void transitionTo(AssistantState newState);
    void manageContext();
    QString classifyIntent(const QString &userMessage);
    QStringList toolsForCategory(const QString &category);
    QString buildMapPreflight(const QString &userMessage);

    // UI
    QWidget        *m_header        = nullptr;
    QComboBox      *m_providerCombo = nullptr;
    QComboBox      *m_projectCombo  = nullptr;
    QStackedWidget *m_stack         = nullptr;   // 0=welcome, 1=chat
    QWidget        *m_welcomeWidget = nullptr;
    QScrollArea    *m_chatScroll    = nullptr;
    QWidget        *m_chatContainer = nullptr;
    QVBoxLayout    *m_chatLayout    = nullptr;
    QWidget        *m_typingRow     = nullptr;   // animated thinking indicator
    QTimer         *m_typingTimer   = nullptr;
    int             m_typingPhase   = 0;
    QLabel         *m_typingDots    = nullptr;
    QWidget        *m_orbWidget     = nullptr;   // header logo (animated when thinking)
    QPlainTextEdit *m_input         = nullptr;
    QPushButton    *m_sendBtn       = nullptr;
    QPushButton    *m_cancelBtn     = nullptr;
    // ── Header v2 ────────────────────────────────────────────────────────────
    // Header carries only three controls:
    //   • the permission-mode pill (primary — always visible, always legible)
    //   • a single overflow menu (⋯) that holds every secondary action
    //   • a slim banner below the header that surfaces setup problems (e.g.
    //     missing API key) without fighting the icons for space
    QPushButton    *m_permissionBtn = nullptr;   // mode pill: Ask / Auto / Plan
    QPushButton    *m_menuBtn       = nullptr;   // ⋯ overflow menu
    QWidget        *m_setupBanner   = nullptr;   // "Set API key" banner
    QAction        *m_actLogbook    = nullptr;
    QAction        *m_actVerbose    = nullptr;   // checkable
    QAction        *m_actSettings   = nullptr;
    QAction        *m_actClear      = nullptr;
    QLabel         *m_statusLabel   = nullptr;
    QLabel         *m_headerTitleLbl = nullptr;
    QLabel         *m_welcomeTitleLbl = nullptr;

    // Last assistant bubble for streaming (markdown)
    BubbleWidget   *m_streamingBubble = nullptr;

    void updateHeaderProviderInfo();
    void showTyping(bool on);
    void checkWelcome();
    void abortWithError(const QString &msg);
    void scrollToBottom();
    void refreshProjectCombo();

    QTimer *m_requestTimeout = nullptr;   // kills request after 60s
    QTimer *m_chunkTimer     = nullptr;   // debounces markdown re-renders

    // State
    AssistantState      m_state         = AssistantState::IDLE;
    Project            *m_project       = nullptr;
    QVector<Project*>   m_allProjects;
    MapInfo             m_selectedMap;
    bool                m_hasSelectedMap = false;
    QString             m_systemPrompt;
    QVector<AIMessage>  m_history;
    QString             m_pendingReasoningContent;

    // Pending tool calls (accumulated per round-trip)
    struct PendingToolCall {
        QString     callId;
        QString     name;
        QJsonObject input;
    };
    QVector<PendingToolCall> m_pendingToolCalls;
    PendingToolCall          m_confirmingCall;   // write tool waiting for user confirmation
    bool m_hadToolCalls = false;   // flag for current round-trip
    int  m_toolRound      = 0;    // iteration counter to prevent infinite loops
    int  m_retryCount     = 0;    // 429 retry counter
    static constexpr int kMaxAgentLoops = 10; // max tool-call round-trips per user message
    static constexpr int kMaxRetries    = 10; // max retries on 429 per call

    // Intent classification & dynamic tool selection
    QStringList m_selectedTools;
    QString     m_activeCategory;

    // Batch recipe system — AI searches, app executes
    struct TuningRecipe {
        QString keyword;        // user trigger word
        QStringList patterns;   // map name search patterns
        QString action;         // "zero", "fill", "scale"
        double  value = 0;      // value for fill/scale
        QString description;    // human-readable description
    };
    static QVector<TuningRecipe> builtinRecipes();
    TuningRecipe *matchRecipe(const QString &userText);
    void executeRecipe(const TuningRecipe &recipe);
    void onRecipeSearchDone(const TuningRecipe &recipe);
    void showTargetSelection(const QStringList &mapNames, const TuningRecipe &recipe);
    void showRecipeConfirmation(const QStringList &mapNames, const TuningRecipe &recipe, int targetRomIndex);
    void applyRecipe(const QStringList &mapNames, const TuningRecipe &recipe, int targetRomIndex);

    TuningRecipe m_activeRecipe;
    QStringList  m_foundMaps;        // maps found during recipe search
    int          m_recipeTargetIndex = -1;  // -1 = main ROM, >=0 = linked ROM index

    // Verbose / thinking-out-loud mode
    bool         m_verboseMode = false;
    void         setVerboseMode(bool on);

    // User-extensible recipe library (loaded from JSON file)
    void         loadUserRecipes();
    static QVector<TuningRecipe> m_userRecipes;  // loaded once at startup

    // Logbook panel
    void         showLogbookPanel();
    void         showWriteConfirmation(const PendingToolCall &ptc);

    // ── Permission mode (Claude-Code-style) ──────────────────────────────────
    // Decides whether write tools require an inline confirmation card.
    void         applyPermissionMode(AppConfig::PermissionMode mode);
    void         cyclePermissionMode();
    void         executeWriteToolImmediately(const PendingToolCall &ptc, bool announce);
    void         rejectWriteToolForPlanMode(const PendingToolCall &ptc);
    void         continueAfterWriteResolution();
    void         refreshSetupBanner();

    // ── Session tracking ─────────────────────────────────────────────────────
    // True once the user has sent at least one message this session — guards
    // checkWelcome() from ever flipping back to the welcome page mid-chat.
    bool         m_sessionStarted = false;

    // Accumulated text for current assistant turn
    QString m_accumulatedText;

    // Provider (lazily created / recreated on provider change)
    QPointer<AIProvider> m_provider;
    AIToolExecutor *m_executor  = nullptr;

    // Provider registry
    struct ProviderConfig {
        QString name;           // settings key
        QString label;          // display label
        QString baseUrl;        // default base URL (empty for Claude)
        QString     defaultModel;
        QStringList presetModels;
        QString     docsUrl;
        bool        isClaude = false;
        // Compatibility tier rendered as a coloured dot in the settings combo:
        //   0 = green  — native API, full tool-calling and streaming
        //   1 = amber  — OpenAI-compatible, tool-calling available
        //   2 = red    — limited compatibility, some features may not work
        int     tier    = 2;
    };
    QVector<ProviderConfig> m_providerConfigs;
    int m_currentProvider = 0;
};
