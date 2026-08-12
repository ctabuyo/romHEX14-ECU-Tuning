/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "vehicledb.h"
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QRegularExpression>
#include <algorithm>

VehicleDatabase &VehicleDatabase::instance()
{
    static VehicleDatabase db;
    return db;
}

VehicleDatabase::VehicleDatabase() {}

void VehicleDatabase::load()
{
    if (m_loaded)
        return;
    m_loaded = true;

    // --- ecus.json ---
    {
        QFile f(QStringLiteral(":/data/ecus.json"));
        if (f.open(QIODevice::ReadOnly)) {
            const auto arr = QJsonDocument::fromJson(f.readAll()).array();
            for (const auto &v : arr) {
                auto o = v.toObject();
                EcuEntry e;
                e.use          = o["use"].toString();
                e.manufacturer = o["manufacturer"].toString();
                e.group        = o["group"].toString();
                e.name         = o["name"].toString();
                if (!e.name.isEmpty())
                    m_ecus.append(e);
            }
        }
    }

    // --- models.json ---
    {
        QFile f(QStringLiteral(":/data/models.json"));
        if (f.open(QIODevice::ReadOnly)) {
            const auto arr = QJsonDocument::fromJson(f.readAll()).array();
            for (const auto &v : arr) {
                auto o = v.toObject();
                ModelEntry e;
                e.category = o["category"].toString();
                e.brand    = o["brand"].toString();
                e.model    = o["model"].toString();
                e.engine   = o["engine"].toString();
                if (!e.brand.isEmpty())
                    m_models.append(e);
            }
        }
    }

    // --- gearboxes.json ---
    {
        QFile f(QStringLiteral(":/data/gearboxes.json"));
        if (f.open(QIODevice::ReadOnly)) {
            const auto arr = QJsonDocument::fromJson(f.readAll()).array();
            for (const auto &v : arr) {
                auto o = v.toObject();
                GearboxEntry e;
                e.brand = o["brand"].toString();
                e.name  = o["name"].toString();
                if (!e.name.isEmpty())
                    m_gearboxes.append(e);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// ECU queries
// ---------------------------------------------------------------------------

QStringList VehicleDatabase::ecuManufacturers() const
{
    const_cast<VehicleDatabase *>(this)->load();
    QSet<QString> set;
    for (const auto &e : m_ecus)
        set.insert(e.manufacturer);
    QStringList list = set.values();
    list.sort(Qt::CaseInsensitive);
    return list;
}

QStringList VehicleDatabase::ecuGroups(const QString &manufacturer) const
{
    const_cast<VehicleDatabase *>(this)->load();
    QSet<QString> set;
    for (const auto &e : m_ecus) {
        if (e.manufacturer.compare(manufacturer, Qt::CaseInsensitive) == 0)
            set.insert(e.group);
    }
    QStringList list = set.values();
    list.sort(Qt::CaseInsensitive);
    return list;
}

QStringList VehicleDatabase::ecuNames(const QString &group) const
{
    const_cast<VehicleDatabase *>(this)->load();
    QSet<QString> set;
    for (const auto &e : m_ecus) {
        if (e.group.compare(group, Qt::CaseInsensitive) == 0)
            set.insert(e.name);
    }
    QStringList list = set.values();
    list.sort(Qt::CaseInsensitive);
    return list;
}

QStringList VehicleDatabase::ecuNamesFlat() const
{
    const_cast<VehicleDatabase *>(this)->load();
    QSet<QString> set;
    for (const auto &e : m_ecus)
        set.insert(e.name);
    QStringList list = set.values();
    list.sort(Qt::CaseInsensitive);
    return list;
}

// ---------------------------------------------------------------------------
// Vehicle queries
// ---------------------------------------------------------------------------

QStringList VehicleDatabase::categories() const
{
    const_cast<VehicleDatabase *>(this)->load();
    QSet<QString> set;
    for (const auto &e : m_models)
        set.insert(e.category);
    // Return in a logical order: PKW first, then alphabetical for the rest
    QStringList list = set.values();
    list.sort(Qt::CaseInsensitive);
    // Move PKW to front if present
    if (list.removeOne(QStringLiteral("PKW")))
        list.prepend(QStringLiteral("PKW"));
    return list;
}

QStringList VehicleDatabase::brands(const QString &category) const
{
    const_cast<VehicleDatabase *>(this)->load();
    QSet<QString> set;
    for (const auto &e : m_models) {
        if (category.isEmpty() || e.category.compare(category, Qt::CaseInsensitive) == 0)
            set.insert(e.brand);
    }
    QStringList list = set.values();
    list.sort(Qt::CaseInsensitive);
    return list;
}

QStringList VehicleDatabase::models(const QString &brand) const
{
    const_cast<VehicleDatabase *>(this)->load();
    QSet<QString> set;
    for (const auto &e : m_models) {
        if (e.brand.compare(brand, Qt::CaseInsensitive) == 0 && !e.model.isEmpty())
            set.insert(e.model);
    }
    QStringList list = set.values();
    list.sort(Qt::CaseInsensitive);
    return list;
}

QStringList VehicleDatabase::engines(const QString &brand, const QString &model) const
{
    const_cast<VehicleDatabase *>(this)->load();
    QSet<QString> set;
    for (const auto &e : m_models) {
        if (e.brand.compare(brand, Qt::CaseInsensitive) == 0
            && e.model.compare(model, Qt::CaseInsensitive) == 0
            && !e.engine.isEmpty()) {
            set.insert(e.engine);
        }
    }
    QStringList list = set.values();
    list.sort(Qt::CaseInsensitive);
    return list;
}

// ---------------------------------------------------------------------------
// Gearbox queries
// ---------------------------------------------------------------------------

QStringList VehicleDatabase::gearboxes(const QString &brand) const
{
    const_cast<VehicleDatabase *>(this)->load();
    QSet<QString> set;
    for (const auto &e : m_gearboxes) {
        if (e.brand.compare(brand, Qt::CaseInsensitive) == 0)
            set.insert(e.name);
    }
    QStringList list = set.values();
    list.sort(Qt::CaseInsensitive);
    return list;
}

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

QStringList VehicleDatabase::searchEcu(const QString &query, int maxResults) const
{
    const_cast<VehicleDatabase *>(this)->load();
    if (query.isEmpty())
        return {};

    QStringList results;
    for (const auto &e : m_ecus) {
        if (e.name.contains(query, Qt::CaseInsensitive)
            || e.group.contains(query, Qt::CaseInsensitive)
            || e.manufacturer.contains(query, Qt::CaseInsensitive)) {
            QString entry = e.manufacturer + QStringLiteral(" ") + e.group
                            + QStringLiteral(" - ") + e.name;
            if (!results.contains(entry))
                results.append(entry);
            if (results.size() >= maxResults)
                break;
        }
    }
    return results;
}

QStringList VehicleDatabase::searchModels(const QString &query, int maxResults) const
{
    const_cast<VehicleDatabase *>(this)->load();
    if (query.isEmpty())
        return {};

    QStringList results;
    for (const auto &e : m_models) {
        if (e.brand.contains(query, Qt::CaseInsensitive)
            || e.model.contains(query, Qt::CaseInsensitive)
            || e.engine.contains(query, Qt::CaseInsensitive)) {
            QString entry = e.brand;
            if (!e.model.isEmpty())
                entry += QStringLiteral(" ") + e.model;
            if (!e.engine.isEmpty())
                entry += QStringLiteral(" - ") + e.engine;
            if (!results.contains(entry))
                results.append(entry);
            if (results.size() >= maxResults)
                break;
        }
    }
    return results;
}

// ---------------------------------------------------------------------------
// ROM detection
// ---------------------------------------------------------------------------

QString VehicleDatabase::detectEcuFromRom(const QByteArray &rom) const
{
    const_cast<VehicleDatabase *>(this)->load();

    // Known ECU identifier strings to search for in ROM data, ordered by
    // specificity (most specific first so we return the best match).
    static const struct {
        const char *signature;
        const char *ecuFamily;
    } signatures[] = {
        // Bosch diesel
        {"EDC17C46", "EDC17C46"},
        {"EDC17C74", "EDC17C74"},
        {"EDC17CP14", "EDC17CP14"},
        {"EDC17CP44", "EDC17CP44"},
        {"EDC17C14", "EDC17C14"},
        {"EDC17C11", "EDC17C11"},
        {"EDC17",    "EDC17"},
        {"EDC16C39", "EDC16C39"},
        {"EDC16C34", "EDC16C34"},
        {"EDC16CP31","EDC16CP31"},
        {"EDC16",    "EDC16"},
        {"EDC15C11", "EDC15C11"},
        {"EDC15",    "EDC15"},
        // Bosch gasoline
        {"MED17.1",  "MED17.1"},
        {"MED17.5",  "MED17.5"},
        {"MED17",    "MED17"},
        {"MED9",     "MED9"},
        {"ME7.5",    "ME7.5"},
        {"ME7.1",    "ME7.1"},
        {"ME7.",     "ME7"},
        {"MSA15",    "MSA15"},
        {"MS6.1",    "MS6.1"},
        // Siemens/Continental
        {"SID807",   "SID807"},
        {"SID206",   "SID206"},
        {"SID201",   "SID201"},
        {"SID80",    "SID80"},
        {"SIMOS18",  "SIMOS18"},
        {"SIMOS",    "SIMOS"},
        {"PCR2.1",   "PCR2.1"},
        {"PCR",      "PCR"},
        {"EMS3",     "EMS3"},
        // Delphi
        {"DCM3.5",   "DCM3.5"},
        {"DCM3.7",   "DCM3.7"},
        {"DCM6.2",   "DCM6.2"},
        {"DCM",      "DCM"},
        // Marelli
        {"IAW 5SF",  "IAW 5SF"},
        {"IAW 4SF",  "IAW 4SF"},
        {"IAW",      "IAW"},
        // Generic identifiers (last resort)
        {"Continental", "Continental ECU"},
        {"Siemens",     "Siemens ECU"},
        {"Bosch",       "Bosch ECU"},
    };

    // Search most-specific signatures first
    for (const auto &sig : signatures) {
        if (rom.contains(sig.signature))
            return QString::fromLatin1(sig.ecuFamily);
    }

    return {};
}

// ---------------------------------------------------------------------------
// SW / part number extraction
// ---------------------------------------------------------------------------

QStringList VehicleDatabase::extractSwNumbers(const QByteArray &rom) const
{
    if (rom.isEmpty())
        return {};

    // We only scan the first and last 128 KB of the ROM (most ECU metadata
    // lives in these regions).  For small ROMs we scan everything.
    static constexpr int kScanSize = 128 * 1024;

    QVector<QPair<int, int>> regions; // start, length
    if (rom.size() <= kScanSize * 2) {
        regions.append({0, rom.size()});
    } else {
        regions.append({0, kScanSize});
        regions.append({rom.size() - kScanSize, kScanSize});
    }

    QSet<QString> seen;
    // Each candidate has a score (higher = more likely a real part number).
    struct Candidate {
        QString text;
        int     score = 0;
    };
    QVector<Candidate> candidates;

    auto addCandidate = [&](const QString &s, int score) {
        QString t = s.trimmed();
        if (t.isEmpty() || seen.contains(t))
            return;
        seen.insert(t);
        candidates.append({t, score});
    };

    // Helper: extract a printable ASCII string starting at `pos` in `data`,
    // up to `maxLen` characters.
    auto extractPrintable = [](const QByteArray &data, int pos, int maxLen) -> QString {
        QString out;
        for (int i = pos; i < data.size() && out.size() < maxLen; ++i) {
            char c = data.at(i);
            if (c >= 0x20 && c <= 0x7E)
                out.append(QLatin1Char(c));
            else
                break;
        }
        return out.trimmed();
    };

    // --- Regex patterns (applied to ASCII-decoded region text) ----------------
    // Bosch-style 10-digit: starts with 0261/0281/028x/026x followed by
    // an optional letter then digits, e.g. 0261S04567, 0281014567
    static const QRegularExpression reBosch(
        QStringLiteral("\\b(0[23][0-9]{2}[A-Z]?[0-9]{5,6})\\b"));

    // VW/Audi-style: 3 alphanumeric + "906" + 3-5 alphanumeric
    // e.g. 06E906023A, 03L906018, 8P0906056
    static const QRegularExpression reVwAudi(
        QStringLiteral("\\b([0-9A-Z]{2,3}906[0-9A-Z]{3,5})\\b"));

    // Generic ECU identifier: 8-15 alphanumeric characters containing at
    // least one digit and one letter, not pure hex, not all-same.
    static const QRegularExpression reGenericId(
        QStringLiteral("\\b([A-Z0-9][A-Z0-9._/-]{6,13}[A-Z0-9])\\b"));

    // Marker prefixes and the max length of the value that follows
    static const struct {
        const char *marker;
        int         valueMaxLen;
        int         score;
    } markers[] = {
        {"SSECUHN:", 30, 90},
        {"SSECUSN:", 30, 90},
        {"HW:",       20, 85},
        {"SW:",       20, 85},
        {"EPK",       40, 80},
    };

    for (const auto &region : regions) {
        const QByteArray chunk = rom.mid(region.first, region.second);

        // --- Marker-based extraction -----------------------------------------
        for (const auto &mk : markers) {
            QByteArray tag(mk.marker);
            int pos = 0;
            while ((pos = chunk.indexOf(tag, pos)) != -1) {
                int valStart = pos + tag.size();
                // Skip optional whitespace / null bytes after marker
                while (valStart < chunk.size()
                       && (chunk.at(valStart) == ' '
                           || chunk.at(valStart) == '\0'
                           || chunk.at(valStart) == ':'))
                    ++valStart;
                QString val = extractPrintable(chunk, valStart, mk.valueMaxLen);
                if (val.size() >= 4)
                    addCandidate(val, mk.score);
                pos = valStart + 1;
            }
        }

        // --- Regex-based extraction (on ASCII representation) ----------------
        // Build a QString keeping only printable ASCII; non-printable chars
        // become spaces so word boundaries work correctly.
        QString ascii;
        ascii.reserve(chunk.size());
        for (int i = 0; i < chunk.size(); ++i) {
            char c = chunk.at(i);
            ascii.append((c >= 0x20 && c <= 0x7E) ? QLatin1Char(c)
                                                    : QLatin1Char(' '));
        }

        // Bosch-style (high confidence)
        {
            auto it = reBosch.globalMatch(ascii);
            while (it.hasNext()) {
                auto m = it.next();
                addCandidate(m.captured(1), 80);
            }
        }

        // VW/Audi-style (high confidence)
        {
            auto it = reVwAudi.globalMatch(ascii);
            while (it.hasNext()) {
                auto m = it.next();
                addCandidate(m.captured(1), 75);
            }
        }

        // Generic ECU identifiers (lower confidence, filtered)
        {
            auto it = reGenericId.globalMatch(ascii);
            while (it.hasNext()) {
                auto m = it.next();
                QString cap = m.captured(1);

                // Must contain at least one letter AND one digit
                bool hasLetter = false, hasDigit = false;
                for (const QChar &ch : cap) {
                    if (ch.isLetter()) hasLetter = true;
                    if (ch.isDigit())  hasDigit  = true;
                }
                if (!hasLetter || !hasDigit)
                    continue;

                // Skip strings that look like common false positives
                // (pure hex dumps, fill patterns, generic words)
                static const QRegularExpression reFalsePositive(
                    QStringLiteral("^(0x[0-9A-F]+|[0-9A-F]{8,}|AAAA|FFFF|"
                                   "Copyright|Copyrigh|Reserved|RESERVED|"
                                   "Function|function|unsigned|"
                                   "UNKNOWN|unknown)$"),
                    QRegularExpression::CaseInsensitiveOption);
                if (reFalsePositive.match(cap).hasMatch())
                    continue;

                int score = 30 + cap.size(); // longer = slightly better
                addCandidate(cap, score);
            }
        }
    }

    // Sort by score descending, then alphabetically for equal scores
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate &a, const Candidate &b) {
                  if (a.score != b.score)
                      return a.score > b.score;
                  return a.text < b.text;
              });

    QStringList result;
    result.reserve(candidates.size());
    for (const auto &c : candidates)
        result.append(c.text);
    return result;
}
