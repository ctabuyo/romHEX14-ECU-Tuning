/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * LegionHarvestWorker implementation.
 */

#include "io/legion/LegionHarvestWorker.h"
#include "io/legion/Legion.h"

#include "io/winols/SimilarityIndex.h"
#include "io/winols/RomFingerprint.h"
#include "io/ols/OlsImporter.h"

#include <QFile>
#include <QFileInfo>
#include <QMetaType>

namespace legion {

namespace {
// Auto-register QVector<LegionVoice> the first time the worker compiles in.
void ensureRegistered()
{
    static const int kVoiceVectorMetaId =
        qRegisterMetaType<QVector<legion::LegionVoice>>("QVector<legion::LegionVoice>");
    (void)kVoiceVectorMetaId;
}
}

LegionHarvestWorker::LegionHarvestWorker(QByteArray baseline,
                                         QString    selfPath,
                                         int        minPercent,
                                         int        maxVoices,
                                         QObject   *parent)
    : QObject(parent),
      m_baseline(std::move(baseline)),
      m_selfPath(std::move(selfPath)),
      m_minPercent(minPercent),
      m_maxVoices(maxVoices)
{
    ensureRegistered();
}

void LegionHarvestWorker::run()
{
    QVector<LegionVoice> voices;
    voices.reserve(m_maxVoices);

    if (m_baseline.isEmpty()) {
        emit finished(voices, m_cancel.load());
        return;
    }

    // ── Open the catalog index on the worker thread (each thread gets
    //    its own SQLite connection name; SimilarityIndex handles that).
    winols::SimilarityIndex idx;
    QString err;
    if (!idx.open(&err)) {
        emit finished(voices, m_cancel.load());
        return;
    }

    QVector<winols::SimilarityMatch> matches;
    try {
        winols::RomFingerprint needle = winols::fingerprint(
            QByteArrayView(m_baseline));
        matches = idx.findSimilar(needle, m_minPercent, m_maxVoices * 4);
    } catch (...) {
        emit finished(voices, m_cancel.load());
        return;
    }

    const int total = matches.size();
    int processed = 0;

    for (const auto &m : matches) {
        if (m_cancel.load()) break;
        if (voices.size() >= m_maxVoices) break;

        ++processed;
        emit progress(processed, total, m.path);

        if (!m_selfPath.isEmpty() &&
            QFileInfo(m.path) == QFileInfo(m_selfPath)) continue;

        QFile f(m.path);
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QByteArray bytes = f.readAll();
        f.close();
        if (bytes.isEmpty()) continue;

        ols::OlsImportResult res;
        try { res = ols::OlsImporter::importFromBytes(bytes); }
        catch (...) { continue; }
        // #6: a tune suggestion is a (Version 0 → Version i) diff, so a file
        // needs at least two versions to contribute.  Raw .bin/.rom/.ori (one
        // version, no embedded "original") can match in the index but have no
        // author-made diff to harvest — skipping them here is intentional, not
        // a silent failure.  We deliberately do NOT synthesise a
        // baseline→file diff for them: that side would equal the user's
        // baseline, so its local-similarity gate is always 100%, which would
        // re-admit exactly the noisy off-target files this pass filters out.
        if (!res.error.isEmpty() || res.versions.size() < 2) continue;

        // #1/#2: the file matched because the UNION of all its versions is
        // similar to the needle — we don't know which one did.  Instead of
        // blindly diffing first-vs-last, emit one voice per tuned version
        // (Version 0 → Version i).  aggregate()'s per-voice local-similarity
        // gate then keeps only the versions whose original matches the
        // user's baseline, and versionIndex/Label lets the UI attribute
        // each suggestion to a concrete version.
        const QByteArray &v0 = res.versions.first().romData;
        if (v0.isEmpty()) continue;

        const int nv = res.versions.size();
        for (int vi = 1; vi < nv; ++vi) {
            if (m_cancel.load()) break;
            if (voices.size() >= m_maxVoices) break;

            const QByteArray &vmod = res.versions[vi].romData;
            if (vmod.size() != v0.size()) continue;

            LegionVoice voice;
            voice.sourcePath   = m.path;
            voice.versionIndex = vi;
            voice.versionLabel = res.versions[vi].name;
            voice.similarity   = int(m.score.wholePct());
            voice.regions      = detectRegions(v0, vmod, 16);
            for (const auto &r : voice.regions) {
                for (uint32_t a = r.startAddr; a <= r.endAddr; ++a) {
                    if (a < uint32_t(v0.size()) && a < uint32_t(vmod.size())
                        && v0[int(a)] != vmod[int(a)]) {
                        voice.addressSet.insert(a);
                    }
                }
            }
            if (!voice.addressSet.isEmpty()) voices.append(std::move(voice));
        }
    }

    emit finished(voices, m_cancel.load());
}

}   // namespace legion
