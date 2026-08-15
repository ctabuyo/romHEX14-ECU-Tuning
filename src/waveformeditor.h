/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QObject>
#include <QByteArray>
#include <QVector>
#include "romdata.h"   // for ByteOrder

class WaveformEditor : public QObject {
    Q_OBJECT
public:
    explicit WaveformEditor(QObject *parent = nullptr);

    // ── Data binding ────────────────────────────────────────────────────
    // Bind to live buffers. Neither is owned. originalData may be null
    // (undo still works; "restore original" becomes a no-op).
    void setData(QByteArray *data, const QByteArray *originalData);
    void setCellSize(int bytes);      // 1, 2, or 4
    void setByteOrder(ByteOrder bo);
    void setSigned(bool isSigned);

    // ── Value access ────────────────────────────────────────────────────
    // Read/write a single cell at `offset` respecting cellSize + byteOrder.
    // Values are doubles to accommodate float cells and large integers.
    double readValue(int offset) const;
    void   writeValue(int offset, double value);

    // ── Batch editing operations ────────────────────────────────────────
    // All ops work on the byte range [start, end) rounded to cellSize.
    // Each op snapshots the before-state, applies the change, pushes undo,
    // and emits dataModified.
    void setAbsolute(int start, int end, double value);
    void addDelta(int start, int end, double delta);
    void scale(int start, int end, double factor);
    void interpolate(int start, int end);
    void smooth(int start, int end, int windowCells = 3);
    void flatten(int start, int end);        // set all to average
    void restoreOriginal(int start, int end);
    void increment(int start, int end, int delta); // +1, -1, +N

    /// Round each cell in [start,end) to the nearest @p multiple, then
    /// clamp into [@p minVal, @p maxVal].  multiple <= 0 disables the
    /// rounding step (clamp still runs).  One combined undo entry —
    /// matches the convention of every other batch op above.
    void roundLimit(int start, int end, int multiple,
                    double minVal, double maxVal);

    // ── Free-form drawing ───────────────────────────────────────────────
    // Drawing = user drags on the waveform and "paints" values at each
    // horizontal cell position. beginDraw snapshots the affected range,
    // continueDraw writes values live, endDraw commits the undo entry.
    void beginDraw(int offset, double value);
    void continueDraw(int offset, double value);
    void endDraw();
    bool isDrawing() const { return m_drawing; }

    // ── External batch ──────────────────────────────────────────────────
    // Caller mutated `m_data[offset..offset+after.size())` already; this
    // records a single undo entry with the supplied before/after spans
    // and emits dataModified.  Used by LEGION submit to register a whole
    // batch as ONE Ctrl-Z step.  `before` and `after` must have the same
    // size and represent the buffer state at @p offset.
    void submitExternal(int offset,
                        const QByteArray &before,
                        const QByteArray &after);

    // ── Apply + record a raw byte span ──────────────────────────────────
    // Writes @p after into the buffer at @p offset, snapshotting the
    // previous bytes itself, and records the change as ONE undo entry.
    // Unlike submitExternal (caller pre-mutates the buffer), this performs
    // the write. Used by MapEditorView so its map-aware cell edits share this
    // editor's single undo stack. No-op if the span is out of range or the
    // bytes are unchanged.
    void applyRawBatch(int offset, const QByteArray &after);

    // ── Undo / redo ─────────────────────────────────────────────────────
    bool canUndo() const;
    bool canRedo() const;
    void undo();
    void redo();

signals:
    // Emitted after every modification (batch op, draw stroke, undo, redo).
    // Widget should repaint; ProjectView should mirror bytes into Project.
    void dataModified(int startOffset, int endOffset);

    // Emitted when canUndo/canRedo changes (for menu/toolbar enable state).
    void undoStateChanged();

private:
    // ── Helpers ─────────────────────────────────────────────────────────
    int  cellCount(int start, int end) const;         // number of cells in range
    int  alignDown(int offset) const;                 // snap to cell boundary
    int  alignUp(int offset) const;
    QByteArray snapshot(int offset, int length) const; // copy bytes from m_data
    void       restore(int offset, const QByteArray &bytes); // write bytes back
    void       pushUndo(int offset, const QByteArray &before, const QByteArray &after);
    void       emitModified(int start, int end);
    double     clampValue(double value) const;

    // ── State ───────────────────────────────────────────────────────────
    QByteArray       *m_data         = nullptr;
    const QByteArray *m_originalData = nullptr;
    int               m_cellSize     = 2;
    ByteOrder         m_byteOrder    = ByteOrder::BigEndian;
    bool              m_signed       = false;

    struct UndoEntry {
        int        offset;
        QByteArray before;
        QByteArray after;
    };
    QVector<UndoEntry> m_undoStack;
    int                m_undoIndex = -1;   // points at the last applied entry
    static constexpr int kMaxUndo = 200;

    // Drawing state
    bool       m_drawing      = false;
    int        m_drawStart    = 0;
    int        m_drawEnd      = 0;
    QByteArray m_drawSnapshot;   // pre-draw bytes for the undo entry
};
