/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "OlsMapJson.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <cmath>

namespace olsjson {

namespace {

// ── String <-> number helpers ────────────────────────────────────────────────

// "0,046882" / "0.046882" / "-273,128693" / "1.234,5" -> double.
double parseDecimal(const QString &in, bool *ok = nullptr)
{
    QString s = in.trimmed();
    if (s.contains(QLatin1Char(',')) && s.contains(QLatin1Char('.'))) {
        // Thousands separator + decimal comma ("1.234,5"): drop the dots.
        s.remove(QLatin1Char('.'));
    }
    s.replace(QLatin1Char(','), QLatin1Char('.'));
    bool good = false;
    const double v = s.toDouble(&good);
    if (ok) *ok = good;
    return good ? v : 0.0;
}

// "$39C818" / "0x39C818" / "3770392" / "0" -> unsigned offset.
uint32_t parseAddress(const QString &in, bool *ok = nullptr)
{
    QString s = in.trimmed();
    bool good = false;
    unsigned long long v = 0;
    if (s.startsWith(QLatin1Char('$'))) {
        v = s.mid(1).toULongLong(&good, 16);
    } else if (s.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
        v = s.mid(2).toULongLong(&good, 16);
    } else {
        v = s.toULongLong(&good, 10);
    }
    if (ok) *ok = good;
    return good ? uint32_t(v) : 0u;
}

int parseInt(const QString &in, int def = 0)
{
    bool ok = false;
    const int v = in.trimmed().toInt(&ok, 10);
    return ok ? v : def;
}

bool parseBool(const QString &in)
{
    const QString s = in.trimmed().toLower();
    return s == QLatin1String("1") || s == QLatin1String("true");
}

QString formatDecimal(double v, int prec, bool comma)
{
    QString s = QString::number(v, 'f', prec);
    if (comma) s.replace(QLatin1Char('.'), QLatin1Char(','));
    return s;
}

QString formatAddress(uint32_t addr)
{
    if (addr == 0) return QStringLiteral("0");
    return QStringLiteral("$") + QString::number(addr, 16).toUpper();
}

// ── DataOrg <-> (size, endianness, float) ────────────────────────────────────
//
// cellDataType / ptsDataType enum (see romdata.h):
//   1 = u8; 2 = u16 BE; 3 = u16 LE; 4 = u32 BE; 5 = u32 LE;
//   6 = float BE; 7 = float LE; 12 = double BE; 13 = double LE.

struct DataOrg {
    int      size      = 2;
    bool     bigEndian = true;
    uint32_t dataType  = 2;
};

bool parseDataOrg(const QString &s, DataOrg *out)
{
    static const struct { const char *name; int size; bool be; uint32_t dt; } table[] = {
        { "eByte",       1, false, 1  },
        { "eLoHi",       2, false, 3  },
        { "eHiLo",       2, true,  2  },
        { "eLoHiLoHi",   4, false, 5  },
        { "eHiLoHiLo",   4, true,  4  },
        { "eFloatLoHi",  4, false, 7  },
        { "eFloatHiLo",  4, true,  6  },
        { "eDoubleLoHi", 8, false, 13 },
        { "eDoubleHiLo", 8, true,  12 },
    };
    const QString t = s.trimmed();
    for (const auto &e : table) {
        if (t.compare(QLatin1String(e.name), Qt::CaseInsensitive) == 0) {
            out->size = e.size; out->bigEndian = e.be; out->dataType = e.dt;
            return true;
        }
    }
    return false;
}

QString dataOrgName(int size, bool bigEndian, uint32_t dataType)
{
    const bool isFloat  = (dataType == 6 || dataType == 7);
    const bool isDouble = (dataType == 12 || dataType == 13);
    if (isDouble || size == 8) return bigEndian ? QStringLiteral("eDoubleHiLo") : QStringLiteral("eDoubleLoHi");
    if (isFloat)               return bigEndian ? QStringLiteral("eFloatHiLo")  : QStringLiteral("eFloatLoHi");
    if (size == 1)             return QStringLiteral("eByte");
    if (size == 4)             return bigEndian ? QStringLiteral("eHiLoHiLo")   : QStringLiteral("eLoHiLoHi");
    return bigEndian ? QStringLiteral("eHiLo") : QStringLiteral("eLoHi");
}

// ── Scaling helpers ──────────────────────────────────────────────────────────

// Build a CompuMethod from OLS Factor / Offset / bReciprocal / Unit / Precision.
//   plain:      phys = raw * factor + offset
//   reciprocal: phys = 1 / (raw * factor + offset)
// The reciprocal form maps onto CompuMethod::RationalFunction with A2L
// physical->raw coefficients (a,b,c,d,e,f) = (0, -offset, 1, 0, factor, 0).
void fillScaling(CompuMethod &cm, bool &hasScaling,
                 double factor, double offset, bool reciprocal,
                 const QString &unit, int precision)
{
    cm.unit = unit.trimmed();
    if (cm.unit == QLatin1String("-") || cm.unit == QLatin1String("--"))
        cm.unit.clear();
    cm.format = QStringLiteral("%.") + QString::number(qBound(0, precision, 9)) + QLatin1Char('f');
    if (reciprocal) {
        cm.type = CompuMethod::Type::RationalFunction;
        cm.rfA = 0; cm.rfB = -offset; cm.rfC = 1;
        cm.rfD = 0; cm.rfE = factor;  cm.rfF = 0;
        hasScaling = true;
        return;
    }
    cm.type = CompuMethod::Type::Linear;
    cm.linA = factor;
    cm.linB = offset;
    hasScaling = true;
}

// Inverse of fillScaling: factor / offset / reciprocal from a CompuMethod.
void scalingToOls(const CompuMethod &cm, bool hasScaling,
                  double *factor, double *offset, bool *reciprocal)
{
    *factor = 1.0; *offset = 0.0; *reciprocal = false;
    if (!hasScaling) return;
    switch (cm.type) {
    case CompuMethod::Type::Linear:
        *factor = cm.linA; *offset = cm.linB;
        break;
    case CompuMethod::Type::RationalFunction:
        if (cm.rfA == 0.0 && cm.rfD == 0.0 && cm.rfF == 0.0 && cm.rfC != 0.0 && cm.rfE != 0.0) {
            // raw = (b*phys + c) / (e*phys)  ->  phys = c / (e*raw - b)
            //     = 1 / ((e/c)*raw - b/c)
            *reciprocal = true;
            *factor = cm.rfE / cm.rfC;
            *offset = -cm.rfB / cm.rfC;
        } else if (cm.rfA == 0.0 && cm.rfD == 0.0 && cm.rfE == 0.0 && cm.rfF != 0.0) {
            // raw = (b*phys + c) / f  ->  phys = (f*raw - c) / b
            if (cm.rfB != 0.0) { *factor = cm.rfF / cm.rfB; *offset = -cm.rfC / cm.rfB; }
        }
        break;
    default:
        break;
    }
}

// Decimal places from a printf-style format ("%6.2f" -> 2); -1 if absent.
int precisionFromFormat(const QString &fmt)
{
    if (fmt.isEmpty()) return -1;
    const int dot = fmt.indexOf(QLatin1Char('.'));
    if (dot < 0) return -1;
    int end = dot + 1;
    while (end < fmt.size() && fmt[end].isDigit()) ++end;
    if (end == dot + 1) return -1;
    return fmt.mid(dot + 1, end - dot - 1).toInt();
}

int precisionFor(const CompuMethod &cm, bool hasScaling, int def)
{
    const int p = precisionFromFormat(cm.format);
    if (p >= 0) return qBound(0, p, 9);
    if (hasScaling && cm.type == CompuMethod::Type::Linear && cm.linA != 0.0 && cm.linA != 1.0) {
        const int h = int(std::floor(-std::log10(std::abs(cm.linA))));
        return qBound(0, h, 3);
    }
    return def;
}

// ── Axis values ("0.0 1.0 2.0 ...") ──────────────────────────────────────────

QVector<double> parseValueList(const QString &s)
{
    QVector<double> out;
    static const QRegularExpression ws(QStringLiteral("[\\s;]+"));
    const QStringList toks = s.trimmed().split(ws, Qt::SkipEmptyParts);
    out.reserve(toks.size());
    for (const QString &t : toks) {
        bool ok = false;
        const double v = parseDecimal(t, &ok);
        if (ok) out.append(v);
    }
    return out;
}

QString formatValueList(const QVector<double> &vals, int prec)
{
    QStringList parts;
    parts.reserve(vals.size());
    for (double v : vals) parts.append(QString::number(v, 'f', qBound(0, prec, 9)));
    return parts.join(QLatin1Char(' '));
}

// ── Per-axis import ──────────────────────────────────────────────────────────

// Reads the "AxisX." / "AxisY." property group into `ax`. `count` is the
// number of points along this axis (Columns for X, Rows for Y).
void importAxis(const QJsonObject &o, const QString &prefix, int count,
                AxisInfo &ax, QStringList &warnings, const QString &mapName)
{
    auto str = [&](const char *key) { return o.value(prefix + QLatin1String(key)).toString(); };

    const QString dataSrc  = str("DataSrc").trimmed();
    const QString idName   = str("IdName").trimmed();
    const QString name     = str("Name").trimmed();
    const QString values   = str("Values").trimmed();
    const bool    userDef  = dataSrc.compare(QLatin1String("eUserdef"), Qt::CaseInsensitive) == 0;

    // Display name: IdName wins; the plain Name is only meaningful when it
    // is not just a copy of the value list (which is what OLS writes for
    // user-defined axes).
    if (!idName.isEmpty())              ax.inputName = idName;
    else if (!name.isEmpty() && name != values) ax.inputName = name;

    bool okF = false, okO = false;
    const double factor = parseDecimal(str("Factor"), &okF);
    const double offset = parseDecimal(str("Offset"), &okO);
    const int    prec   = parseInt(str("Precision"), 2);
    fillScaling(ax.scaling, ax.hasScaling,
                okF ? factor : 1.0, okO ? offset : 0.0,
                parseBool(str("bReciprocal")), str("Unit"), prec);

    bool okA = false;
    const uint32_t addr = parseAddress(str("DataAddr"), &okA);
    if (!userDef && okA && addr != 0) {
        DataOrg org;
        if (!parseDataOrg(str("DataOrg"), &org))
            org = DataOrg{};   // u16 BE default
        ax.hasPtsAddress = true;
        ax.ptsAddress    = addr;
        ax.ptsCount      = qMax(1, count);
        ax.ptsDataSize   = org.size;
        ax.ptsDataType   = org.dataType;
        ax.ptsBigEndian  = org.bigEndian;
        ax.ptsSigned     = parseBool(str("bSigned"));
    } else if (!values.isEmpty()) {
        ax.fixedValues = parseValueList(values);
    }

    if (parseBool(str("bBackwards")))
        warnings.append(QStringLiteral("%1: %2bBackwards is not supported (axis imported in stored order)")
                            .arg(mapName, prefix));
    if (parseInt(str("SkipBytes"), 0) != 0)
        warnings.append(QStringLiteral("%1: %2SkipBytes ignored").arg(mapName, prefix));
}

// ── Per-axis export ──────────────────────────────────────────────────────────

void exportAxis(QJsonObject &o, const QString &prefix, const AxisInfo &ax,
                bool relevant, const ExportOptions &opt)
{
    auto put = [&](const char *key, const QString &v) { o.insert(prefix + QLatin1String(key), v); };

    double factor, offset; bool reciprocal;
    scalingToOls(ax.scaling, ax.hasScaling, &factor, &offset, &reciprocal);
    const int prec = precisionFor(ax.scaling, ax.hasScaling, relevant ? 2 : 0);
    const bool inRom = relevant && ax.hasPtsAddress && ax.ptsAddress != 0;

    QString unit = ax.hasScaling ? ax.scaling.unit : QString();
    if (unit.isEmpty()) unit = QStringLiteral("-");

    put("DataHeader",    QStringLiteral("0"));
    put("Factor",        formatDecimal(factor, 6, opt.decimalComma));
    put("IdName",        ax.inputName);
    put("Offset",        formatDecimal(offset, 6, opt.decimalComma));
    put("Precision",     QString::number(prec));
    put("Radix",         QStringLiteral("10"));
    put("SignatureByte", QStringLiteral("0x-1"));
    put("SkipBytes",     QStringLiteral("0"));
    put("Unit",          unit);
    put("bBackwards",    QStringLiteral("0"));
    put("bReciprocal",   reciprocal ? QStringLiteral("1") : QStringLiteral("0"));

    if (inRom) {
        const ByteOrder bo = axisByteOrder(ax, opt.projectByteOrder);
        put("DataAddr", formatAddress(ax.ptsAddress));
        put("DataOrg",  dataOrgName(ax.ptsDataSize > 0 ? ax.ptsDataSize : 2,
                                    bo == ByteOrder::BigEndian, ax.ptsDataType));
        put("DataSrc",  QStringLiteral("eRom"));
        put("Name",     ax.inputName);
        put("bSigned",  ax.ptsSigned ? QStringLiteral("1") : QStringLiteral("0"));
    } else {
        const QString vals = relevant ? formatValueList(ax.fixedValues, prec) : QString();
        put("DataAddr", QStringLiteral("0"));
        put("DataOrg",  QStringLiteral("eLoHi"));
        put("DataSrc",  QStringLiteral("eUserdef"));
        put("Name",     ax.inputName.isEmpty() ? vals : ax.inputName);
        put("Values",   vals);
        put("bSigned",  QStringLiteral("0"));
    }
}

} // namespace

// ── Public API ───────────────────────────────────────────────────────────────

bool looksLikeOlsMapJson(const QByteArray &json)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) return false;
    const QJsonValue maps = doc.object().value(QLatin1String("maps"));
    if (!maps.isArray()) return false;
    const QJsonArray arr = maps.toArray();
    if (arr.isEmpty()) return true;           // an empty pack is still a pack
    const QJsonObject first = arr.first().toObject();
    return first.contains(QLatin1String("Fieldvalues.StartAddr"))
        || first.contains(QLatin1String("DataOrg"))
        || first.contains(QLatin1String("AxisX.DataSrc"));
}

ImportResult importFromJson(const QByteArray &json)
{
    ImportResult res;
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError) {
        res.error = QStringLiteral("JSON parse error at offset %1: %2")
                        .arg(pe.offset).arg(pe.errorString());
        return res;
    }
    if (!doc.isObject() || !doc.object().value(QLatin1String("maps")).isArray()) {
        res.error = QStringLiteral("Not an OLS map pack: expected a top-level \"maps\" array");
        return res;
    }

    const QJsonArray arr = doc.object().value(QLatin1String("maps")).toArray();
    int idx = 0;
    for (const QJsonValue &v : arr) {
        ++idx;
        if (!v.isObject()) {
            res.warnings.append(QStringLiteral("Entry %1 is not an object, skipped").arg(idx));
            continue;
        }
        const QJsonObject o = v.toObject();
        auto str = [&](const char *key) { return o.value(QLatin1String(key)).toString(); };

        MapInfo m;
        m.name        = str("Name").trimmed();
        m.id          = str("IdName").trimmed();
        m.userNotes   = str("Comment");
        m.folderPath  = str("FolderName").trimmed();
        if (m.name.isEmpty()) m.name = m.id;
        if (m.name.isEmpty()) m.name = QStringLiteral("Map %1").arg(idx);
        m.description = m.name;
        m.linkConfidence = 100;
        m.columnMajor    = false;

        bool okAddr = false;
        const uint32_t addr = parseAddress(str("Fieldvalues.StartAddr"), &okAddr);
        if (!okAddr) {
            res.warnings.append(QStringLiteral("Skipped '%1': bad Fieldvalues.StartAddr \"%2\"")
                                    .arg(m.name, str("Fieldvalues.StartAddr")));
            continue;
        }
        m.address    = addr;
        m.rawAddress = addr;

        // Geometry
        const QString type = str("Type").trimmed();
        int cols = qMax(1, parseInt(str("Columns"), 1));
        int rows = qMax(1, parseInt(str("Rows"), 1));
        if (type.compare(QLatin1String("eEinzel"), Qt::CaseInsensitive) == 0) { cols = 1; rows = 1; }
        if (cols > 10000 || rows > 10000 || qint64(cols) * rows > 1000000) {
            res.warnings.append(QStringLiteral("Skipped '%1': implausible size %2x%3")
                                    .arg(m.name).arg(cols).arg(rows));
            continue;
        }
        m.dimensions = { cols, rows };
        if (cols == 1 && rows == 1)      m.type = QStringLiteral("VALUE");
        else if (cols > 1 && rows > 1)   m.type = QStringLiteral("MAP");
        else                             m.type = QStringLiteral("CURVE");

        // Cell layout
        DataOrg org;
        if (!parseDataOrg(str("DataOrg"), &org)) {
            res.warnings.append(QStringLiteral("'%1': unknown DataOrg \"%2\", assuming 16-bit big-endian")
                                    .arg(m.name, str("DataOrg")));
            org = DataOrg{};
        }
        m.dataSize      = org.size;
        m.cellDataType  = org.dataType;
        m.cellBigEndian = org.bigEndian;
        m.dataSigned    = parseBool(str("bSigned"));
        m.mapDataOffset = 0;
        m.length        = cols * rows * m.dataSize;

        // Scaling
        bool okF = false, okO = false;
        const double factor = parseDecimal(str("Fieldvalues.Factor"), &okF);
        const double offset = parseDecimal(str("Fieldvalues.Offset"), &okO);
        fillScaling(m.scaling, m.hasScaling,
                    okF ? factor : 1.0, okO ? offset : 0.0,
                    parseBool(str("bReciprocal")), str("Fieldvalues.Unit"),
                    parseInt(str("Precision"), 2));

        // Axes — only the ones that span more than one point matter.
        if (cols > 1)
            importAxis(o, QStringLiteral("AxisX."), cols, m.xAxis, res.warnings, m.name);
        if (rows > 1)
            importAxis(o, QStringLiteral("AxisY."), rows, m.yAxis, res.warnings, m.name);

        if (parseInt(str("SkipBytes"), 0) != 0 || parseInt(str("LineSkipBytes"), 0) != 0)
            res.warnings.append(QStringLiteral("'%1': SkipBytes/LineSkipBytes ignored (cells imported contiguously)")
                                    .arg(m.name));

        res.maps.append(m);
    }
    return res;
}

QByteArray exportToJson(const QVector<MapInfo> &maps, const ExportOptions &opt)
{
    QJsonArray arr;
    for (const MapInfo &m : maps) {
        const int cols = qMax(1, m.dimensions.x);
        const int rows = qMax(1, m.dimensions.y);

        QString type;
        if (cols == 1 && rows == 1)     type = QStringLiteral("eEinzel");
        else if (cols > 1 && rows > 1)  type = QStringLiteral("eZweidim");
        else if (rows > 1)              type = QStringLiteral("eZweiInv");
        else                            type = QStringLiteral("eEindim");

        double factor, offset; bool reciprocal;
        scalingToOls(m.scaling, m.hasScaling, &factor, &offset, &reciprocal);
        const int prec = precisionFor(m.scaling, m.hasScaling, 2);
        const ByteOrder bo = cellByteOrder(m, opt.projectByteOrder);

        QJsonObject o;
        exportAxis(o, QStringLiteral("AxisX."), m.xAxis, cols > 1, opt);
        exportAxis(o, QStringLiteral("AxisY."), m.yAxis, rows > 1, opt);

        o.insert(QLatin1String("Columns"),              QString::number(cols));
        o.insert(QLatin1String("Comment"),              m.userNotes);
        o.insert(QLatin1String("DataOrg"),              dataOrgName(m.dataSize, bo == ByteOrder::BigEndian, m.cellDataType));
        o.insert(QLatin1String("Fieldvalues.Factor"),   formatDecimal(factor, 6, opt.decimalComma));
        o.insert(QLatin1String("Fieldvalues.Name"),     QString());
        o.insert(QLatin1String("Fieldvalues.Offset"),   formatDecimal(offset, 6, opt.decimalComma));
        o.insert(QLatin1String("Fieldvalues.StartAddr"), formatAddress(m.cellDataStart()));
        o.insert(QLatin1String("Fieldvalues.Unit"),     m.hasScaling ? m.scaling.unit : QString());
        o.insert(QLatin1String("FolderName"),           m.folderPath);
        o.insert(QLatin1String("IdName"),               m.id);
        o.insert(QLatin1String("LineSkipBytes"),        QStringLiteral("0"));
        o.insert(QLatin1String("Name"),                 m.name);
        o.insert(QLatin1String("Precision"),            QString::number(prec));
        o.insert(QLatin1String("RWin"),                 QStringLiteral("eBars"));
        o.insert(QLatin1String("Radix"),                QStringLiteral("10"));
        o.insert(QLatin1String("Rows"),                 QString::number(rows));
        o.insert(QLatin1String("SkipBytes"),            QStringLiteral("0"));
        o.insert(QLatin1String("Type"),                 type);
        o.insert(QLatin1String("ViewMode"),             QStringLiteral("eViewText"));
        o.insert(QLatin1String("bDelta"),               QStringLiteral("0"));
        o.insert(QLatin1String("bOriginal"),            QStringLiteral("0"));
        o.insert(QLatin1String("bOriginalValues"),      QStringLiteral("0"));
        o.insert(QLatin1String("bPercent"),             QStringLiteral("0"));
        o.insert(QLatin1String("bReciprocal"),          reciprocal ? QStringLiteral("1") : QStringLiteral("0"));
        o.insert(QLatin1String("bSigned"),              m.dataSigned ? QStringLiteral("1") : QStringLiteral("0"));
        arr.append(o);
    }

    QJsonObject root;
    root.insert(QLatin1String("maps"), arr);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

} // namespace olsjson
