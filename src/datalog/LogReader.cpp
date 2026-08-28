#include "LogReader.h"

#include <QFile>
#include <QHash>
#include <QTextStream>
#include <QStringConverter>
#include <QStringList>
#include <QRegularExpression>
#include <algorithm>
#include <utility>

namespace datalog {

namespace {

QString detectEncoding(const QByteArray &raw)
{
    if (raw.startsWith("\xEF\xBB\xBF")) return QStringLiteral("utf-8-sig");
    auto dec = QStringDecoder(QStringConverter::Utf8, QStringDecoder::Flag::Stateless);
    QString s = dec.decode(raw);
    if (dec.hasError()) return QStringLiteral("cp1252");
    return QStringLiteral("utf-8");
}

QChar detectDelimiter(const QString &firstLine)
{
    int nComma = firstLine.count(QLatin1Char(','));
    int nTab   = firstLine.count(QLatin1Char('\t'));
    int nSemi  = firstLine.count(QLatin1Char(';'));
    if (nTab > nComma && nTab > nSemi)  return QLatin1Char('\t');
    if (nSemi > nComma)                  return QLatin1Char(';');
    return QLatin1Char(',');
}

// RFC-4180 lite: handle quoted fields with comma inside descriptions.
QStringList splitRow(const QString &line, QChar delim)
{
    QStringList out;
    QString cur;
    cur.reserve(line.size());
    bool inQuote = false;
    for (int i = 0; i < line.size(); ++i) {
        QChar c = line[i];
        if (inQuote) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < line.size() && line[i+1] == QLatin1Char('"')) {
                    cur.append(QLatin1Char('"')); ++i;
                } else {
                    inQuote = false;
                }
            } else {
                cur.append(c);
            }
        } else {
            if (c == QLatin1Char('"')) {
                inQuote = true;
            } else if (c == delim) {
                out.push_back(cur);
                cur.clear();
            } else {
                cur.append(c);
            }
        }
    }
    out.push_back(cur);
    return out;
}

QString decodeText(const QByteArray &raw, const QString &encoding)
{
    if (encoding == QLatin1String("cp1252")) return QString::fromLatin1(raw);
    QByteArray body = raw;
    if (body.startsWith("\xEF\xBB\xBF")) body.remove(0, 3);
    auto dec = QStringDecoder(QStringConverter::Utf8);
    return dec.decode(body);
}

enum class SourceFormat { Vehical, Autotuner };

// Tolerant numeric parse: VCDS from European locales can emit ',' decimals.
double parseNum(QString s, bool *ok)
{
    s = s.trimmed();
    double v = s.toDouble(ok);
    if (!*ok && s.contains(QLatin1Char(','))) {
        s.replace(QLatin1Char(','), QLatin1Char('.'));
        v = s.toDouble(ok);
    }
    return v;
}

// ── VCDS (Ross-Tech) group logs ──────────────────────────────────────────
//
//   row 0: Sunday,23,August,2026,10:44:27:...,VCDS Version: Release 25.3.2,...
//   row 1: 8P0 907 115 P,,2.0l R4/4V TFSI 0010,
//   row 2: (blank)
//   row 3: ,Group A:,'020,,,,Group B:,'031,,,,Group C:,'230
//   row 4: ,,Timing Retardation,...,,Lambda Control,...        (locations)
//   row 5: ,TIME,Cylinder 1,...,TIME,Bank 1 (actual),...       (labels)
//   row 6: Marker,STAMP,°KW,...,STAMP,,...                     (units)
//   row 7+: ,0.06,0.0,...,0.11,0.96,...                        (data)
//
// Every group carries its own TIME column with stamps in seconds; the
// groups sample independently, so channels are merged onto the union of
// all stamps by linear interpolation (clamped at both ends).

// Returns the index of the units row (the one holding "STAMP" cells, with
// "TIME" cells directly above), or -1 when the file is not a VCDS log.
int sniffVcdsStampRow(const QStringList &lines, QChar delim)
{
    const int scan = qMin(lines.size(), 12);
    for (int r = 1; r < scan; ++r) {
        const QStringList cells = splitRow(lines[r], delim);
        bool hasStamp = false;
        for (const QString &c : cells) {
            if (c.trimmed().compare(QStringLiteral("STAMP"), Qt::CaseInsensitive) == 0) {
                hasStamp = true;
                break;
            }
        }
        if (!hasStamp) continue;
        const QStringList above = splitRow(lines[r - 1], delim);
        for (const QString &c : above) {
            if (c.trimmed().compare(QStringLiteral("TIME"), Qt::CaseInsensitive) == 0)
                return r;
        }
    }
    return -1;
}

LogTable parseVcds(const QStringList &lines, QChar delim, const QString &enc,
                   const QString &path, int stampRow, QString *err)
{
    LogTable t;
    t.sourcePath = path;
    t.encoding   = enc;
    t.delimiter  = delim;

    if (stampRow + 1 >= lines.size()) {
        if (err) *err = QStringLiteral("VCDS log has no data rows after the header");
        return t;
    }

    const QStringList unitsRow = splitRow(lines[stampRow], delim);
    const QStringList labelRow = splitRow(lines[stampRow - 1], delim);
    QStringList locRow = (stampRow >= 2) ? splitRow(lines[stampRow - 2], delim)
                                         : QStringList();
    // Guard against a missing location row (the cell pattern "Group X:"
    // means we grabbed the group-number row instead).
    static const QRegularExpression rxGroup(QStringLiteral("^\\s*Group\\s+\\S+:"));
    for (const QString &c : std::as_const(locRow)) {
        if (rxGroup.match(c).hasMatch()) { locRow.clear(); break; }
    }

    QVector<int> timeCols;
    for (int c = 0; c < unitsRow.size(); ++c) {
        if (unitsRow[c].trimmed().compare(QStringLiteral("STAMP"), Qt::CaseInsensitive) == 0)
            timeCols.push_back(c);
    }
    if (timeCols.isEmpty()) {
        if (err) *err = QStringLiteral("VCDS log has no TIME/STAMP columns");
        return t;
    }

    // Channel columns: everything right of the first TIME column that is not
    // itself a TIME column and carries a label. Owner group = nearest TIME
    // column to the left.
    struct Chan {
        int      col;
        int      group;      // index into timeCols
        QString  name;
        QString  desc;
        QString  unit;
        QVector<double> ts;  // seconds
        QVector<double> vs;
    };
    QVector<Chan> chans;
    const int nCols = qMax(labelRow.size(), unitsRow.size());
    for (int c = timeCols.first() + 1; c < nCols; ++c) {
        if (timeCols.contains(c)) continue;
        int group = 0;
        for (int g = 0; g < timeCols.size(); ++g)
            if (timeCols[g] < c) group = g;
        const QString loc   = (c < locRow.size())   ? locRow[c].trimmed()   : QString();
        const QString label = (c < labelRow.size()) ? labelRow[c].trimmed() : QString();
        QString name = loc.isEmpty() ? label
                     : label.isEmpty() ? loc
                     : loc + QLatin1Char(' ') + label;
        if (name.isEmpty()) continue;                 // filler column
        Chan ch;
        ch.col   = c;
        ch.group = group;
        ch.name  = name;
        ch.desc  = loc.isEmpty() ? label : loc;
        ch.unit  = (c < unitsRow.size()) ? unitsRow[c].trimmed() : QString();
        chans.push_back(ch);
    }
    if (chans.isEmpty()) {
        if (err) *err = QStringLiteral("VCDS log has no labelled channels");
        return t;
    }

    // Duplicate names across groups get the group letter appended.
    {
        QHash<QString, int> seen;
        for (const Chan &ch : std::as_const(chans)) seen[ch.name]++;
        for (Chan &ch : chans) {
            if (seen.value(ch.name) > 1)
                ch.name += QStringLiteral(" [%1]").arg(QChar('A' + ch.group));
        }
    }

    // Collect each group's samples on its own time base.
    QVector<double> axis;                  // union of all stamps, seconds
    for (int r = stampRow + 1; r < lines.size(); ++r) {
        const QString &line = lines[r];
        if (line.trimmed().isEmpty()) continue;
        const QStringList vals = splitRow(line, delim);
        QVector<double> groupTime(timeCols.size(), -1.0);
        for (int g = 0; g < timeCols.size(); ++g) {
            const int tc = timeCols[g];
            if (tc >= vals.size()) continue;
            bool ok = false;
            const double tv = parseNum(vals[tc], &ok);
            if (ok) { groupTime[g] = tv; axis.push_back(tv); }
        }
        for (Chan &ch : chans) {
            const double tv = groupTime[ch.group];
            if (tv < 0.0 || ch.col >= vals.size()) continue;
            bool ok = false;
            const double v = parseNum(vals[ch.col], &ok);
            if (!ok) continue;                        // gap — leave for interp
            ch.ts.push_back(tv);
            ch.vs.push_back(v);
        }
    }

    std::sort(axis.begin(), axis.end());
    axis.erase(std::unique(axis.begin(), axis.end()), axis.end());
    if (axis.isEmpty()) {
        if (err) *err = QStringLiteral("VCDS log has no parsable data rows");
        return t;
    }

    // Column 0: normalized Time in milliseconds (stamps are seconds).
    LogColumn timeCol;
    timeCol.name        = QStringLiteral("Time");
    timeCol.description = QStringLiteral("Elapsed time");
    timeCol.unitRaw     = QStringLiteral("ms");
    timeCol.index       = 0;
    t.columns.push_back(timeCol);

    t.data.resize(1 + chans.size());
    t.data[0].reserve(axis.size());
    t.timeMs.reserve(axis.size());
    for (double sec : std::as_const(axis)) {
        t.data[0].push_back(sec * 1000.0);
        t.timeMs.push_back(sec * 1000.0);
    }

    // Merge each channel onto the union axis: linear interpolation between
    // its own samples, clamped to first/last outside its range.
    for (int i = 0; i < chans.size(); ++i) {
        const Chan &ch = chans[i];
        LogColumn c;
        c.name        = ch.name;
        c.description = ch.desc;
        c.unitRaw     = ch.unit;
        c.index       = 1 + i;
        t.columns.push_back(c);

        QVector<double> &out = t.data[1 + i];
        out.reserve(axis.size());
        int k = 0;                          // ch.ts is naturally sorted
        for (double sec : std::as_const(axis)) {
            double v = 0.0;
            if (ch.ts.isEmpty()) {
                v = 0.0;
            } else if (sec <= ch.ts.first()) {
                v = ch.vs.first();
            } else if (sec >= ch.ts.last()) {
                v = ch.vs.last();
            } else {
                while (k + 1 < ch.ts.size() && ch.ts[k + 1] < sec) ++k;
                const double t0 = ch.ts[k],  t1 = ch.ts[k + 1];
                const double v0 = ch.vs[k],  v1 = ch.vs[k + 1];
                v = (t1 > t0) ? v0 + (v1 - v0) * (sec - t0) / (t1 - t0) : v0;
            }
            out.push_back(v);
        }
    }
    return t;
}

// Sniff whether the CSV is Autotuner (single header, "timestamp" first col,
// units embedded as "(unit)" at end of name) or Vehical (3-row header with
// separate description and unit rows).
SourceFormat sniff(const QStringList &headerLine0, const QStringList &maybeRow1)
{
    if (headerLine0.isEmpty()) return SourceFormat::Vehical;
    QString first = headerLine0.first().trimmed();
    // Strip surrounding quotes if any
    if (first.startsWith(QLatin1Char('"')) && first.endsWith(QLatin1Char('"')))
        first = first.mid(1, first.size() - 2);
    if (first.compare(QStringLiteral("timestamp"), Qt::CaseInsensitive) == 0)
        return SourceFormat::Autotuner;

    // If columns embed "(unit)" pattern broadly, also call it Autotuner.
    int withUnits = 0;
    static const QRegularExpression rxUnit(QStringLiteral("\\([^()]*\\)\\s*$"));
    for (const QString &c : headerLine0) {
        if (rxUnit.match(c).hasMatch()) ++withUnits;
    }
    if (headerLine0.size() >= 3 && withUnits * 2 >= headerLine0.size())
        return SourceFormat::Autotuner;

    // Heuristic: if row[1] contains mostly numeric tokens, this is single-header
    // (no description row); fall back to Autotuner-style parser.
    if (!maybeRow1.isEmpty()) {
        int numeric = 0;
        for (const QString &v : maybeRow1) {
            bool ok = false;
            v.toDouble(&ok);
            if (ok) ++numeric;
        }
        if (numeric >= maybeRow1.size() - 1 && numeric > 0)
            return SourceFormat::Autotuner;
    }

    return SourceFormat::Vehical;
}

LogTable parseVehical(const QStringList &lines, QChar delim, const QString &enc,
                     const QString &path, QString *err)
{
    LogTable t;
    t.sourcePath = path;
    t.encoding   = enc;
    t.delimiter  = delim;

    if (lines.size() < 4) {
        if (err) *err = QStringLiteral("file has fewer than 4 lines (need 3-row header + 1 data row)");
        return t;
    }

    QStringList names = splitRow(lines[0], delim);
    QStringList descs = splitRow(lines[1], delim);
    QStringList units = splitRow(lines[2], delim);

    int nCols = names.size();
    if (nCols < 2) {
        if (err) *err = QStringLiteral("header has %1 columns; need at least 2 (Time + 1 channel)").arg(nCols);
        return t;
    }
    if (names.first().trimmed().compare(QStringLiteral("Time"), Qt::CaseInsensitive) != 0) {
        if (err) *err = QStringLiteral("first column is '%1'; expected 'Time'").arg(names.first());
        return t;
    }

    t.columns.reserve(nCols);
    for (int i = 0; i < nCols; ++i) {
        LogColumn c;
        c.name        = names[i].trimmed();
        c.description = (i < descs.size()) ? descs[i].trimmed() : QString();
        c.unitRaw     = (i < units.size()) ? units[i].trimmed() : QString();
        c.index       = i;
        t.columns.push_back(c);
    }

    t.data.resize(nCols);
    int rows = lines.size() - 3;
    for (auto &col : t.data) col.reserve(rows);
    t.timeMs.reserve(rows);

    for (int r = 3; r < lines.size(); ++r) {
        const QString &line = lines[r];
        if (line.trimmed().isEmpty()) continue;
        QStringList vals = splitRow(line, delim);
        for (int c = 0; c < nCols; ++c) {
            double v = 0.0;
            if (c < vals.size()) {
                bool ok = false;
                v = vals[c].toDouble(&ok);
                if (!ok) v = 0.0;
            }
            t.data[c].push_back(v);
        }
        t.timeMs.push_back(t.data[0].back());
    }
    return t;
}

LogTable parseAutotuner(const QStringList &lines, QChar delim, const QString &enc,
                       const QString &path, QString *err)
{
    LogTable t;
    t.sourcePath = path;
    t.encoding   = enc;
    t.delimiter  = delim;

    if (lines.size() < 2) {
        if (err) *err = QStringLiteral("file has fewer than 2 lines (need header + 1 data row)");
        return t;
    }

    QStringList rawNames = splitRow(lines[0], delim);
    int nCols = rawNames.size();
    if (nCols < 2) {
        if (err) *err = QStringLiteral("header has %1 columns; need at least 2").arg(nCols);
        return t;
    }

    static const QRegularExpression rxUnit(QStringLiteral("^(.*?)\\s*\\(([^()]*)\\)\\s*$"));
    t.columns.reserve(nCols);
    for (int i = 0; i < nCols; ++i) {
        LogColumn c;
        c.index = i;
        QString raw = rawNames[i].trimmed();
        QRegularExpressionMatch m = rxUnit.match(raw);
        if (m.hasMatch()) {
            c.name    = m.captured(1).trimmed();
            c.unitRaw = m.captured(2).trimmed();
        } else {
            c.name    = raw;
            c.unitRaw = QString();
        }
        c.description = c.name;

        // Normalize the time column so the rest of the pipeline finds "Time".
        if (i == 0 && c.name.compare(QStringLiteral("timestamp"), Qt::CaseInsensitive) == 0) {
            c.name        = QStringLiteral("Time");
            c.description = QStringLiteral("Elapsed time");
            if (c.unitRaw.isEmpty()) c.unitRaw = QStringLiteral("ms");
        }
        t.columns.push_back(c);
    }

    t.data.resize(nCols);
    int rows = lines.size() - 1;
    for (auto &col : t.data) col.reserve(rows);
    t.timeMs.reserve(rows);

    for (int r = 1; r < lines.size(); ++r) {
        const QString &line = lines[r];
        if (line.trimmed().isEmpty()) continue;
        QStringList vals = splitRow(line, delim);
        for (int c = 0; c < nCols; ++c) {
            double v = 0.0;
            if (c < vals.size()) {
                bool ok = false;
                v = vals[c].toDouble(&ok);
                if (!ok) v = 0.0;
            }
            t.data[c].push_back(v);
        }
        t.timeMs.push_back(t.data[0].back());
    }
    return t;
}

} // namespace

LogTable LogReader::read(const QString &path, QString *err)
{
    LogTable t;
    t.sourcePath = path;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = QStringLiteral("cannot open: %1").arg(f.errorString());
        return t;
    }
    QByteArray raw = f.readAll();
    f.close();

    QString encoding = detectEncoding(raw.left(4096));
    QString text     = decodeText(raw, encoding);

    QStringList lines = text.split(QRegularExpression(QStringLiteral("\\r?\\n")), Qt::KeepEmptyParts);
    while (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
    if (lines.size() < 2) {
        if (err) *err = QStringLiteral("file has fewer than 2 lines");
        return t;
    }

    QChar delim = detectDelimiter(lines[0]);

    // VCDS group logs have a structure of their own (per-group TIME/STAMP
    // columns) — detect them before the two single-time-column formats.
    const int stampRow = sniffVcdsStampRow(lines, delim);
    if (stampRow > 0)
        return parseVcds(lines, delim, encoding, path, stampRow, err);

    QStringList line0 = splitRow(lines[0], delim);
    QStringList line1 = splitRow(lines[1], delim);
    SourceFormat fmt = sniff(line0, line1);

    if (fmt == SourceFormat::Autotuner)
        return parseAutotuner(lines, delim, encoding, path, err);
    return parseVehical(lines, delim, encoding, path, err);
}

} // namespace datalog
