/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Sprint L — stub-only project / window / version functions.
 *
 * Each is tagged in spec §2a as STUB-OK / STUB-FAIL / STUB-MISSING.
 * They exist so that ported WinOLS scripts that mention them resolve to
 * a callable instead of `nil`, and the test suite (§7 gate 3 expected_funcs)
 * passes.  Real implementations come in later iters or sprints.
 */

#include "LuaEngine.h"
#include "LuaPropertyIds.h"
#include "project.h"
#include "mainwindow.h"
#include "romdata.h"
#include "io/MapListExporter.h"
#include "io/ols/OlsImporter.h"
#include "io/ols/KpImporter.h"
#include "io/ols/EcuAutoDetect.h"
#include "io/winols/SimilarityIndex.h"
#include "io/winols/RomFingerprint.h"
#include "io/legion/Legion.h"
#include "romparser.h"

#include "sol/sol.hpp"

#include <QString>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QCryptographicHash>

namespace lua {

namespace {

QString toQ(const std::string &s) { return QString::fromUtf8(s.c_str(), int(s.size())); }
std::string toS(const QString &q) { auto a = q.toUtf8(); return std::string(a.constData(), size_t(a.size())); }

Project *active(LuaEngine *engine)
{
    if (!engine || !engine->mainWindow()) return nullptr;
    return engine->mainWindow()->luaActiveProject();
}

} // namespace

void bindStubApi(sol::state &L, LuaEngine *engine)
{
    using namespace lua_ids;

    // ── §5.2 checksum group — STUB-FAIL with honest GetLastError ──
    //
    // Iter 8.1: pre-Iter-8 these returned true/true/0 silently, which made EVC
    // samples 04/05 believe checksums were corrected when nothing happened.
    // Implementing the WinOLS checksum plugin DSL is a ~1500 LOC effort
    // (see lua.md OOS); until then the contract is "no engine available" and
    // scripts must check the return value.
    L.set_function("projectSearchChecksums", [engine]() -> bool {
        if (engine) engine->setLastError("checksum engine not implemented");
        return false;
    });
    L.set_function("projectApplyChecksums",  [engine]() -> bool {
        if (engine) engine->setLastError("checksum engine not implemented");
        return false;
    });
    L.set_function("projectStatChecksums",   [engine]() -> int {
        if (engine) engine->setLastError("checksum engine not implemented");
        return -1;
    });
    L.set_function("projectAddChecksum",     [engine](sol::variadic_args) -> bool {
        if (engine) engine->setLastError("checksum engine not implemented");
        return false;
    });
    L.set_function("projectGetChecksumName", [](sol::variadic_args) -> std::string { return std::string(); });
    L.set_function("projectGetChecksumOptionStatus", [](sol::variadic_args) -> int { return 0; });
    L.set_function("projectGetChecksumOptionText",   [](sol::variadic_args) -> std::string { return std::string(); });
    L.set_function("projectGetChecksumOptionType",   [](sol::variadic_args) -> int { return 0; });   // eCOTNone
    L.set_function("projectSetChecksumOptionStatus", [engine](sol::variadic_args) -> bool {
        if (engine) engine->setLastError("checksum engine not implemented");
        return false;
    });

    // ── §5.2 export — Iter 9: Binary / IntelHex / S-record REAL;
    //    BdmToGo / WinOLS native / IntelKp / pluginowe → honest false ──
    L.set_function("projectExport", [engine](sol::variadic_args va) -> bool {
        return engine->callOnGui([&]() -> bool {
        Project *p = active(engine);
        if (!p) { engine->setLastError("no project open"); return false; }
        if (va.size() < 1) { engine->setLastError("projectExport: missing path argument"); return false; }
        QString path = toQ(va[0].as<std::string>());
        int type = va.size() >= 2 ? va[1].as<int>() : eFiletypeBinary;
        // eFiletypeAuto = pick by file extension.
        if (type == eFiletypeAuto) {
            const QString lower = path.toLower();
            if      (lower.endsWith(QStringLiteral(".hex"))) type = eFiletypeIntelHex;
            else if (lower.endsWith(QStringLiteral(".s19")) ||
                     lower.endsWith(QStringLiteral(".s28")) ||
                     lower.endsWith(QStringLiteral(".s37")) ||
                     lower.endsWith(QStringLiteral(".srec"))) type = eFiletypeMotorolaHex;
            else type = eFiletypeBinary;
        }
        QByteArray payload;
        if (type == eFiletypeBinary) {
            payload = p->currentData;
        } else if (type == eFiletypeIntelHex) {
            payload = writeIntelHex(p->currentData, p->baseAddress);
        } else if (type == eFiletypeMotorolaHex) {
            payload = writeSRecord(p->currentData, p->baseAddress);
        } else {
            engine->setLastError(QStringLiteral("export type %1 not implemented "
                "(supported: eFiletypeBinary, eFiletypeIntelHex, eFiletypeMotorolaHex)")
                .arg(type));
            return false;
        }
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) {
            engine->setLastError(f.errorString());
            return false;
        }
        f.write(payload);
        f.close();
        return true;
        });
    });

    // §5.2 projectExportMaps — REAL via existing MapListExporter
    L.set_function("projectExportMaps", [engine](const std::string &path) -> bool {
        return engine->callOnGui([&]() -> bool {
        Project *p = active(engine);
        if (!p) { engine->setLastError("no project open"); return false; }
        QString qpath = toQ(path);
        QString err;
        bool ok = qpath.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive)
                    ? MapListExporter::toCsv(*p, qpath, &err)
                    : MapListExporter::toJson(*p, qpath, &err);
        if (!ok) engine->setLastError(err);
        return ok;
        });
    });

    // §5.2 projectImport — Iter 9: Binary / IntelHex / S-record REAL via
    //                     parseROMData() (auto-detects from content).
    L.set_function("projectImport", [engine](sol::variadic_args va) -> bool {
        return engine->callOnGui([&]() -> bool {
        Project *p = active(engine);
        if (!p) { engine->setLastError("no project open"); return false; }
        if (va.size() < 1) { engine->setLastError("projectImport: missing path argument"); return false; }
        QString file = toQ(va[0].as<std::string>());
        int type = va.size() >= 2 ? va[1].as<int>() : eFiletypeAuto;
        // §5.7 — .winolsskript stub
        if (file.endsWith(QStringLiteral(".winolsskript"), Qt::CaseInsensitive)
         || type == eFiletypeIni) {
            engine->setLastError("winolsskript not supported in community build");
            return false;
        }
        if (type != eFiletypeBinary
         && type != eFiletypeAuto
         && type != eFiletypeIntelHex
         && type != eFiletypeMotorolaHex) {
            engine->setLastError(QStringLiteral("import type %1 not implemented "
                "(supported: eFiletypeBinary, eFiletypeIntelHex, eFiletypeMotorolaHex)")
                .arg(type));
            return false;
        }
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly)) {
            engine->setLastError(QStringLiteral("cannot open %1").arg(file));
            return false;
        }
        QByteArray raw = f.readAll();
        f.close();
        QByteArray data;
        if (type == eFiletypeBinary) {
            data = raw;
        } else {
            // IntelHex / S-record / Auto: dispatch via parseROMData which
            // detects the format from content.
            ParsedROM pr = parseROMData(raw);
            if (!pr.ok) {
                engine->setLastError(pr.error);
                return false;
            }
            // If caller asked explicitly for IntelHex/MotorolaHex, sanity-check.
            if (type == eFiletypeIntelHex && pr.format != QStringLiteral("Intel HEX")) {
                engine->setLastError(QStringLiteral("file is not Intel HEX (detected %1)").arg(pr.format));
                return false;
            }
            if (type == eFiletypeMotorolaHex && pr.format != QStringLiteral("Motorola S-record")) {
                engine->setLastError(QStringLiteral("file is not S-record (detected %1)").arg(pr.format));
                return false;
            }
            data = pr.data;
            // Inherit baseAddress on first import (only when project is fresh).
            if (p->currentData.isEmpty()) p->baseAddress = pr.baseAddress;
        }
        // Same-size: overwrite currentData; else fail (no rebase logic in MVP).
        if (data.size() != p->currentData.size() && !p->currentData.isEmpty()) {
            engine->setLastError(QStringLiteral("size mismatch: %1 vs %2")
                .arg(data.size()).arg(p->currentData.size()));
            return false;
        }
        p->currentData = data;
        if (p->originalData.isEmpty()) p->originalData = data;
        p->modified = true;
        emit p->dataChanged();
        return true;
        });
    });

    // §5.2 projectImportFromOls(path, version=0, flags=0) — REAL via OlsImporter
    //
    // Iter 9.6: honors eIFOls* flags from LuaConstants so callers can keep
    // parts of the active project intact (maps, data, byteOrder, baseAddress).
    L.set_function("projectImportFromOls", [engine](sol::variadic_args va) -> bool {
        return engine->callOnGui([&]() -> bool {
        Project *p = active(engine);
        if (!p) { engine->setLastError("no project open"); return false; }
        if (va.size() < 1) { engine->setLastError("projectImportFromOls: missing path argument"); return false; }
        QString path = toQ(va[0].as<std::string>());
        int version = va.size() >= 2 ? va[1].as<int>() : 0;
        int flags   = va.size() >= 3 ? va[2].as<int>() : 0;
        constexpr int kSkipMaps            = 0x01;
        constexpr int kSkipData            = 0x02;
        constexpr int kPreserveByteOrder   = 0x04;
        constexpr int kPreserveBaseAddress = 0x08;

        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            engine->setLastError(f.errorString());
            return false;
        }
        auto r = ols::OlsImporter::importFromBytes(f.readAll());
        f.close();
        if (!r.error.isEmpty() || r.versions.isEmpty()) {
            engine->setLastError(r.error.isEmpty() ? QStringLiteral("no versions in .ols") : r.error);
            return false;
        }
        int v = qBound(0, version, r.versions.size() - 1);
        if (!(flags & kSkipData))            p->currentData = r.versions[v].romData;
        if (!(flags & kSkipMaps)) {
            p->maps = r.versions[v].maps;
            engine->mainWindow()->luaClearLastCreatedMap(p);
        }
        if (!(flags & kPreserveByteOrder))   p->byteOrder   = r.versions[v].byteOrder;
        if (!(flags & kPreserveBaseAddress)) p->baseAddress = r.versions[v].baseAddress;
        p->modified = true;
        emit p->dataChanged();
        return true;
        });
    });

    L.set_function("projectImportCsvJson",  [engine](sol::variadic_args) -> bool {
        if (engine) engine->setLastError("not yet implemented"); return false;
    });

    // §5.2 projectImportMapPack — REAL via existing KpImporter
    L.set_function("projectImportMapPack", [engine](sol::variadic_args va) -> bool {
        return engine->callOnGui([&]() -> bool {
        Project *p = active(engine);
        if (!p) { engine->setLastError("no project open"); return false; }
        if (va.size() < 1) { engine->setLastError("projectImportMapPack: missing path argument"); return false; }
        QString path = toQ(va[0].as<std::string>());
        uint32_t baseAddr = va.size() >= 2 ? uint32_t(va[1].as<double>()) : p->baseAddress;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            engine->setLastError(f.errorString());
            return false;
        }
        auto r = ols::KpImporter::importFromBytes(
            f.readAll(), baseAddr,
            static_cast<uint32_t>(qMax(0, p->currentData.size())));
        f.close();
        if (!r.error.isEmpty()) {
            engine->setLastError(r.error);
            return false;
        }
        p->maps.append(r.maps);
        p->modified = true;
        emit p->dataChanged();
        return true;
        });
    });

    // P0-7: honest false + GetLastError instead of silent failure.
    L.set_function("projectMail", [engine](sol::variadic_args) -> bool {
        if (engine) engine->setLastError("projectMail not implemented in community build");
        return false;
    });

    // ── §5.2 find/replace bytes — REAL ──
    // Parse a hex-string pattern with ?? wildcards into (bytes, mask).
    auto parsePattern = [](const QString &s, QByteArray &bytes, QByteArray &mask) {
        bytes.clear(); mask.clear();
        const QStringList toks = s.split(QRegularExpression(QStringLiteral("[\\s,:]+")), Qt::SkipEmptyParts);
        for (const QString &raw : toks) {
            QString t = raw;
            if (t.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) t = t.mid(2);
            if (t == QStringLiteral("??")) { bytes.append(char(0)); mask.append(char(0)); continue; }
            bool ok = false;
            uint v = t.toUInt(&ok, 16);
            if (!ok) continue;
            bytes.append(char(v & 0xFF));
            mask.append(char(0xFF));
        }
    };
    // Search buf from startAt for needle; ret offset or -1.
    auto findMasked = [](const QByteArray &buf, qint64 startAt,
                         const QByteArray &needle, const QByteArray &mask) -> qint64 {
        if (needle.isEmpty() || buf.isEmpty()) return -1;
        qint64 limit = buf.size() - needle.size();
        for (qint64 i = qMax<qint64>(0, startAt); i <= limit; ++i) {
            bool match = true;
            for (qint64 k = 0; k < needle.size(); ++k) {
                if (mask.at(k) == 0) continue;
                if (buf.at(i + k) != needle.at(k)) { match = false; break; }
            }
            if (match) return i;
        }
        return -1;
    };
    // Iter 10.6: encode each numeric pattern value as N bytes per datatype's
    // endianness.  Returns true if it consumed all input as a numeric
    // pattern; false if non-numeric tokens are present (caller falls back
    // to byte-string parsing).
    auto encodeNumeric = [](const QByteArray &valueBytes, int dtype) -> QByteArray {
        // valueBytes already encoded as a 1-byte value (caller passed raw uint8).
        // Re-encode by repacking the original uint to N bytes per dtype.
        // For multi-byte dtypes we accept the value as a single integer.
        (void)valueBytes;
        return {};
    };
    (void)encodeNumeric;   // silence unused — encoding happens inline below

    // Helper: encode a uint64 value into N bytes per WinOLS datatype.
    auto packValue = [](quint64 v, int dtype) -> QByteArray {
        using namespace lua_ids;
        QByteArray out;
        switch (dtype) {
        case eByte:     out.append(char(v & 0xFF)); break;
        case eLoHi:
            out.append(char(v & 0xFF));
            out.append(char((v >> 8) & 0xFF));
            break;
        case eHiLo:
            out.append(char((v >> 8) & 0xFF));
            out.append(char(v & 0xFF));
            break;
        case eLoHiLoHi:
            for (int k = 0; k < 4; ++k) out.append(char((v >> (8*k)) & 0xFF));
            break;
        case eHiLoHiLo:
            for (int k = 3; k >= 0; --k) out.append(char((v >> (8*k)) & 0xFF));
            break;
        default:
            out.append(char(v & 0xFF));
            break;
        }
        return out;
    };

    L.set_function("projectFindBytes", [engine, &L, parsePattern, findMasked, packValue]
                   (sol::variadic_args va) -> sol::object {
        if (va.size() < 3) return sol::make_object(L, -1);
        qint64 start = qint64(va[0].as<double>());
        // 4-arg overload: (start, end, orgVer, …)
        qint64 endAddr = -1;
        int orgVer = 0;
        size_t firstByteArg = 0;
        // Manual: if 2nd arg is 0 or 1 it's orgVer; otherwise end.
        int second = va[1].as<int>();
        if (second == 0 || second == 1) {
            orgVer = second;
            firstByteArg = 2;
        } else {
            endAddr = second;
            orgVer = va[2].as<int>();
            firstByteArg = 3;
        }
        QByteArray buf = engine->callOnGui([&]() -> QByteArray {
            Project *p = active(engine);
            if (!p) { engine->setLastError("no project open"); return QByteArray(); }
            return orgVer ? p->currentData : p->originalData;
        });
        if (buf.isEmpty()) return sol::make_object(L, -1);

        // Iter 10.6: peel a trailing datatype enum (eByte..eHiLoHiLo, 1..5).
        // We only treat the *last* arg as datatype when (a) all args after
        // firstByteArg are numeric and (b) its value is in range 1..5.
        // Otherwise leave args intact and default to eByte.
        int dtype = lua_ids::eByte;
        size_t lastByteArg = va.size();
        if (lastByteArg > firstByteArg + 1
            && va[lastByteArg - 1].get_type() == sol::type::number) {
            int v = va[lastByteArg - 1].as<int>();
            if (v >= 1 && v <= 5) {
                dtype = v;
                lastByteArg = lastByteArg - 1;
            }
        }

        // Build pattern from args [firstByteArg, lastByteArg).
        QByteArray needle, mask;
        if (lastByteArg > firstByteArg
            && va[firstByteArg].get_type() == sol::type::string) {
            // String form — raw hex bytes, datatype ignored (matches WinOLS).
            parsePattern(toQ(va[firstByteArg].as<std::string>()), needle, mask);
        } else {
            // Numeric byte-list form — each value packed per datatype.
            for (size_t i = firstByteArg; i < lastByteArg; ++i) {
                quint64 v = quint64(va[i].as<double>());
                QByteArray packed = packValue(v, dtype);
                needle.append(packed);
                for (int k = 0; k < packed.size(); ++k) mask.append(char(0xFF));
            }
        }
        // -1 startAddress → array of all hits
        if (start == -1) {
            sol::table arr = L.create_table();
            qint64 at = 0;
            int idx = 1;
            while (true) {
                qint64 hit = findMasked(buf, at, needle, mask);
                if (hit < 0) break;
                if (endAddr > 0 && hit > endAddr) break;
                arr[idx++] = hit;
                at = hit + 1;
            }
            return arr;
        }
        // Single search
        qint64 hit = findMasked(buf, start, needle, mask);
        if (endAddr > 0 && hit > endAddr) hit = -1;
        return sol::make_object(L, hit);
    });
    // Iter 10.6: extended signature.  Manual:
    //   projectReplaceBytes(start, end, search, replace, orgVer=0,
    //                       count=1, datatype=eByte, mode=0)
    // mode is accepted but only Mode=0 (raw byte search/replace) is wired —
    // Mode=1 (datatype-aware numeric encode/decode) is honored via the
    // `datatype` arg already, so Mode field acts as an explicit no-op here.
    L.set_function("projectReplaceBytes", [engine, parsePattern, findMasked, packValue]
                   (sol::variadic_args va) -> int {
        if (va.size() < 4) return 0;
        qint64 start = qint64(va[0].as<double>());
        qint64 end   = qint64(va[1].as<double>());
        // search/replace can be either string ("AA BB ?? DD") or numeric scalar.
        const bool searchIsStr  = va[2].get_type() == sol::type::string;
        const bool replaceIsStr = va[3].get_type() == sol::type::string;
        QString searchStr  = searchIsStr  ? toQ(va[2].as<std::string>()) : QString();
        QString replaceStr = replaceIsStr ? toQ(va[3].as<std::string>()) : QString();
        int orgVer  = va.size() >= 5 ? va[4].as<int>() : 0;
        int limit   = va.size() >= 6 ? va[5].as<int>() : 1;
        int dtype   = va.size() >= 7 ? va[6].as<int>() : lua_ids::eByte;
        // mode arg consumed but ignored (Mode=0 raw is the only wired mode).
        (void)(va.size() >= 8 ? va[7].as<int>() : 0);

        QByteArray needle, nmask, replBytes, rmask;
        if (searchIsStr) {
            parsePattern(searchStr, needle, nmask);
        } else {
            QByteArray packed = packValue(quint64(va[2].as<double>()), dtype);
            needle = packed;
            for (int k = 0; k < packed.size(); ++k) nmask.append(char(0xFF));
        }
        if (replaceIsStr) {
            parsePattern(replaceStr, replBytes, rmask);
        } else {
            QByteArray packed = packValue(quint64(va[3].as<double>()), dtype);
            replBytes = packed;
            for (int k = 0; k < packed.size(); ++k) rmask.append(char(0xFF));
        }
        if (needle.isEmpty()) return 0;
        return engine->callOnGui([&]() -> int {
        Project *p = active(engine);
        if (!p) { engine->setLastError("no project open"); return 0; }
        const QByteArray &src = orgVer ? p->currentData : p->originalData;
        int count = 0;
        qint64 at = qMax<qint64>(0, start);
        while (count < limit) {
            qint64 hit = findMasked(src, at, needle, nmask);
            if (hit < 0 || (end > 0 && hit > end)) break;
            for (int k = 0; k < replBytes.size() && hit + k < p->currentData.size(); ++k) {
                if (rmask.size() > k && rmask.at(k) == 0) continue;   // ?? — leave byte
                p->currentData[hit + k] = replBytes.at(k);
            }
            ++count;
            at = hit + needle.size();
        }
        if (count > 0) {
            p->modified = true;
            emit p->dataChanged();
        }
        return count;
        });
    });

    // ── §5.2 maps — REAL ──
    L.set_function("projectFindMap", [engine, &L](sol::variadic_args va) -> sol::object {
        if (va.size() < 2) return sol::make_object(L, -1);
        QString crit = toQ(va[0].as<std::string>());
        QString val  = toQ(va[1].as<std::string>());
        qint64 startAddr = va.size() >= 3 ? qint64(va[2].as<double>()) : 0;
        bool wantAll = (startAddr == -1);
        QVector<qint64> hits = engine->callOnGui([&]() -> QVector<qint64> {
            QVector<qint64> out;
            Project *p = active(engine);
            if (!p) { engine->setLastError("no project open"); return out; }
            for (const MapInfo &m : p->maps) {
                QString candidate;
                if (crit == QStringLiteral("Name"))   candidate = m.name;
                else if (crit == QStringLiteral("IdName")) candidate = m.getSideProp("IdName").toString();
                if (candidate != val) continue;
                if (!wantAll) {
                    if (qint64(m.address) < startAddr) continue;
                    out.push_back(qint64(m.address));
                    return out;
                }
                out.push_back(qint64(m.address));
            }
            return out;
        });
        if (!wantAll)
            return sol::make_object(L, hits.isEmpty() ? qint64(-1) : hits.first());
        sol::table arr = L.create_table();
        for (int i = 0; i < hits.size(); ++i)
            arr[i + 1] = hits[i];
        return arr;
    });
    L.set_function("projectAddMap", [engine]() -> bool {
        return engine->callOnGui([&]() -> bool {
        Project *p = active(engine);
        if (!p) { engine->setLastError("no project open"); return false; }
        MapInfo m;
        m.name = QStringLiteral("LuaCreated");
        m.type = QStringLiteral("MAP");
        const int newMapIndex = int(p->maps.size());
        p->maps.push_back(m);
        engine->mainWindow()->luaRememberLastCreatedMap(p, newMapIndex);
        // Iter 10.4: mark project as modified + tell UI so Save Project /
        // diff overlays react to the new map.
        p->modified = true;
        emit p->dataChanged();
        return true;
        });
    });
    L.set_function("projectDelMap", [engine](sol::object arg) -> int {
        const bool isNum = arg.get_type() == sol::type::number;
        const bool isStr = arg.get_type() == sol::type::string;
        const uint32_t addr = isNum ? uint32_t(arg.as<double>()) : 0;
        const QString pat = isStr ? toQ(arg.as<std::string>()) : QString();
        return engine->callOnGui([&]() -> int {
        Project *p = active(engine);
        if (!p) return 0;
        int before = p->maps.size();
        if (isNum) {
            auto it = std::remove_if(p->maps.begin(), p->maps.end(),
                [addr](const MapInfo &m) { return m.address == addr; });
            p->maps.erase(it, p->maps.end());
        } else if (isStr) {
            QRegularExpression rx(QRegularExpression::wildcardToRegularExpression(pat));
            auto it = std::remove_if(p->maps.begin(), p->maps.end(),
                [&rx](const MapInfo &m) { return rx.match(m.name).hasMatch(); });
            p->maps.erase(it, p->maps.end());
        }
        int removed = before - p->maps.size();
        if (removed > 0) {
            engine->mainWindow()->luaClearLastCreatedMap(p);
            p->modified = true;
            emit p->dataChanged();
        }
        return removed;
        });
    });
    L.set_function("projectDelFolder", [engine](const std::string &name) -> bool {
        return engine->callOnGui([&]() -> bool {
        Project *p = active(engine);
        if (!p) { engine->setLastError("no project open"); return false; }
        QString folder = toQ(name);
        int before = p->maps.size();
        auto it = std::remove_if(p->maps.begin(), p->maps.end(),
            [&folder](const MapInfo &m) {
                return m.getSideProp("FolderName").toString() == folder;
            });
        p->maps.erase(it, p->maps.end());
        if (p->maps.size() != before) {
            engine->mainWindow()->luaClearLastCreatedMap(p);
            p->modified = true;
            emit p->dataChanged();
        }
        return true;
        });
    });
    L.set_function("projectDelDuplicateMaps", [engine](bool byAddrOnly, bool sameFolder) -> int {
        return engine->callOnGui([&]() -> int {
        Project *p = active(engine);
        if (!p) return -1;
        int before = p->maps.size();
        QSet<QString> seen;
        QVector<MapInfo> kept;
        kept.reserve(p->maps.size());
        for (const MapInfo &m : p->maps) {
            QString key;
            if (byAddrOnly) {
                key = QStringLiteral("%1:%2").arg(m.address).arg(m.length);
            } else {
                key = QStringLiteral("%1@%2").arg(m.name).arg(m.address);
            }
            if (sameFolder) {
                key += QStringLiteral("|") + m.getSideProp("FolderName").toString();
            }
            if (seen.contains(key)) continue;
            seen.insert(key);
            kept.push_back(m);
        }
        p->maps = kept;
        int removed = before - p->maps.size();
        if (removed > 0) {
            engine->mainWindow()->luaClearLastCreatedMap(p);
            p->modified = true;
            emit p->dataChanged();
        }
        return removed;
        });
    });

    // ── §5.2 similarity — REAL via winols::SimilarityIndex ──
    //
    // Manual §2.4.31: projectFindSimilarProjectsSql(MinPercent, MaxResults,
    //                  [flags...], idCol1, idCol2, ...)
    //
    // Returns flat array: [overall_rel, ..desired_cols..,  overall_rel,
    //                       ..desired_cols.., ...]
    //
    // With eFSPTrippleRelevance flag (5.52+): first 3 cells per record are
    // overall / project / data-area relevance, then desired columns.
    //
    // We use the existing SimilarityIndex DB built by Sprint I.  Property
    // columns are resolved via Project::propertyById on each match (one
    // open/close per match — adequate for tens of hits; can cache later).
    auto findSimilarImpl = [engine, &L](sol::variadic_args va) -> sol::table {
        sol::table arr = L.create_table();
        if (va.size() < 2) return arr;
        int minPct = int(va[0].as<double>());
        int maxRes = int(va[1].as<double>());
        // Iter 10.8: flags are now in the high bits (eFSP* = 0x10000+) so
        // they never collide with property IDs in the trailing column list.
        // Any arg with bits set in 0xF0000 is a flag word; everything else
        // is a column ID.
        constexpr int kFlagMask    = 0xF0000;
        constexpr int kFlagTripple = 0x20000;
        bool trippleRelevance = false;
        QVector<int> propCols;
        for (size_t i = 2; i < va.size(); ++i) {
            int v = va[i].as<int>();
            if (v & kFlagMask) {
                if (v & kFlagTripple) trippleRelevance = true;
            } else {
                propCols.push_back(v);
            }
        }
        int relCols = trippleRelevance ? 3 : 1;
        int nCols   = relCols + propCols.size();

        struct ActiveSnapshot {
            QByteArray originalData;
            QString filePath;
        };
        ActiveSnapshot activeSnapshot = engine->callOnGui([&]() -> ActiveSnapshot {
            ActiveSnapshot snap;
            Project *p = active(engine);
            if (!p || p->originalData.isEmpty()) {
                if (engine) engine->setLastError("no project or empty ROM");
                return snap;
            }
            snap.originalData = p->originalData;
            snap.filePath = p->filePath;
            return snap;
        });
        if (activeSnapshot.originalData.isEmpty())
            return arr;

        // Build fingerprint over our originalData and query the index.
        winols::RomFingerprint needle = winols::fingerprint(activeSnapshot.originalData);
        winols::SimilarityIndex idx;
        QString err;
        if (!idx.open(&err)) {
            if (engine) engine->setLastError(err);
            return arr;
        }
        // Negative minPercent → data-area mode (manual 2.4.31 / WinOLS 5.11+).
        // SimilarityIndex internally ranks by data-area regardless; we honour
        // the sign by switching the relevance column we report.
        bool dataAreaMode = (minPct < 0);
        const int absMin = std::abs(minPct);
        auto hits = idx.findSimilar(needle, absMin, maxRes);

        // Iter 10.8: exclude the active project's own .rx14proj from results
        // per manual.  Match by absolute path, case-insensitive on Windows.
        const QString selfPath = activeSnapshot.filePath;
        int writeIdx = 1;
        for (const auto &h : hits) {
            if (!selfPath.isEmpty()
                && QFileInfo(h.path).canonicalFilePath()
                        .compare(QFileInfo(selfPath).canonicalFilePath(),
                                 Qt::CaseInsensitive) == 0) {
                continue;
            }
            int overall = h.score.wholePct();
            int prjSim  = h.score.wholePct();
            int dataSim = h.score.dataPct();
            int reported = dataAreaMode ? -dataSim : overall;
            if (std::abs(reported) < absMin) continue;
            arr[writeIdx++] = reported;
            if (trippleRelevance) {
                arr[writeIdx++] = prjSim;
                arr[writeIdx++] = dataAreaMode ? -dataSim : dataSim;
            }
            // Resolve property columns by opening the matched project file.
            // For .ols/.kp/.bin files (raw ROMs) we can only fill ePrjFilename;
            // for .rx14proj we can open it and pull real properties.
            QScopedPointer<Project> sub;
            if (h.path.endsWith(QStringLiteral(".rx14proj"), Qt::CaseInsensitive)) {
                sub.reset(Project::open(h.path));
            }
            for (int colId : propCols) {
                QString val;
                if (colId == lua_ids::ePrjFilename) {
                    val = h.path;
                } else if (sub) {
                    val = sub->propertyById(colId);
                }
                arr[writeIdx++] = toS(val);
            }
        }
        (void)nCols;
        return arr;
    };
    L.set_function("projectFindSimilarProjectsSql", findSimilarImpl);
    L.set_function("projectFindSimilarProjects",    findSimilarImpl);  // obsolete alias

    // ── §5.2 rights — OUT OF SCOPE (P0-7: honest false + GetLastError) ──
    L.set_function("projectSetRight",      [engine](sol::variadic_args) -> bool {
        if (engine) engine->setLastError("projectSetRight not implemented in community build");
        return false;
    });
    L.set_function("projectSetRightsOwner",[engine](sol::variadic_args) -> bool {
        if (engine) engine->setLastError("projectSetRightsOwner not implemented in community build");
        return false;
    });

    // ── §5.2 projectImportChanges — REAL ──
    //
    // Manual §2.4.45: import maps/values from a source project's version
    // into the active project.  Returns 0=source missing, 1=partial,
    // 2=axes missing, 3=full ok, 4=full+single-offset (with eICAllowReturn4).
    //
    // Iter 6 implementation strategy (good enough for portal_apply_selection):
    //   1. Open source project from disk (Project::open on .rx14proj)
    //   2. Resolve the requested version: number→index, string→match by name
    //      ("Original"/"Oryginal"/0 = source originalData; otherwise look up
    //       p->versions[idx] or by name)
    //   3. Compute byte diff between source-version and source-original
    //   4. If flags include eICImportDataAreas: write each differing byte
    //      from source-version into our currentData (absolute mode; relative
    //      mode adds the delta; percent multiplies)
    //   5. If flags include eICImportMapContents: ALSO write bytes within
    //      map ranges (in our flat model, "data areas" and "map contents"
    //      overlap heavily — we just write all diffing bytes)
    //   6. Sizes must match; otherwise return 0 + lastError
    //
    // Returns 3 (full ok) on success, 0 on any failure.
    L.set_function("projectImportChanges", [engine](sol::variadic_args va) -> int {
        return engine->callOnGui([&]() -> int {
        Project *p = active(engine);
        if (!p) { if (engine) engine->setLastError("no project open"); return 0; }
        if (va.size() < 2) return 0;
        QString srcPath = toQ(va[0].as<std::string>());
        sol::object verArg = va[1];

        // Iter 11+: handle both .rx14proj (via Project::open) and .ols
        // (via OlsImporter) sources.  Portal flow normally references
        // .ols entries from the WinOLS catalog DB.
        QByteArray srcOrig;                  // source's Original / version 0
        QByteArray srcVerBytes;              // chosen tuned version

        const bool isOls = srcPath.endsWith(QStringLiteral(".ols"), Qt::CaseInsensitive);
        if (isOls) {
            QFile f(srcPath);
            if (!f.open(QIODevice::ReadOnly)) {
                if (engine) engine->setLastError(QStringLiteral("cannot open %1").arg(srcPath));
                return 0;
            }
            auto r = ols::OlsImporter::importFromBytes(f.readAll());
            f.close();
            if (!r.error.isEmpty() || r.versions.isEmpty()) {
                if (engine) engine->setLastError(r.error.isEmpty()
                    ? QStringLiteral("no versions in .ols") : r.error);
                return 0;
            }
            // .ols Version 0 is the Original; subsequent versions are user mods.
            srcOrig = r.versions.first().romData;
            int idx = 0;
            if (verArg.get_type() == sol::type::number) {
                idx = int(verArg.as<double>());
            } else if (verArg.get_type() == sol::type::string) {
                const QString name = toQ(verArg.as<std::string>());
                if (name == QStringLiteral("Original") || name == QStringLiteral("Oryginal")) {
                    idx = 0;
                } else {
                    for (int i = 0; i < r.versions.size(); ++i) {
                        if (r.versions[i].name == name) { idx = i; break; }
                    }
                }
            }
            if (idx < 0 || idx >= r.versions.size()) {
                if (engine) engine->setLastError("version index out of range");
                return 0;
            }
            srcVerBytes = r.versions[idx].romData;
        } else {
            QScopedPointer<Project> src(Project::open(srcPath));
            if (!src) {
                if (engine) engine->setLastError(QStringLiteral("cannot open %1").arg(srcPath));
                return 0;
            }
            srcOrig = src->originalData;
            if (verArg.get_type() == sol::type::number) {
                int idx = int(verArg.as<double>());
                if (idx <= 0) {
                    srcVerBytes = srcOrig;
                } else if (idx - 1 < src->versions.size()) {
                    srcVerBytes = src->versions[idx - 1].data;
                } else {
                    if (engine) engine->setLastError("version index out of range");
                    return 0;
                }
            } else if (verArg.get_type() == sol::type::string) {
                QString name = toQ(verArg.as<std::string>());
                if (name == QStringLiteral("Original") || name == QStringLiteral("Oryginal")) {
                    srcVerBytes = srcOrig;
                } else {
                    bool found = false;
                    for (const ProjectVersion &v : src->versions) {
                        if (v.name == name) { srcVerBytes = v.data; found = true; break; }
                    }
                    if (!found) {
                        if (engine) engine->setLastError(QStringLiteral("version '%1' not found").arg(name));
                        return 0;
                    }
                }
            } else {
                return 0;
            }
        }

        if (srcVerBytes.isEmpty() || srcOrig.isEmpty()) {
            if (engine) engine->setLastError("source has no data");
            return 0;
        }
        if (srcVerBytes.size() != srcOrig.size()) {
            if (engine) engine->setLastError("source version/original size mismatch");
            return 0;
        }
        if (p->currentData.size() < srcVerBytes.size()) {
            if (engine) engine->setLastError(QStringLiteral("size mismatch: %1 vs %2 (target smaller)")
                .arg(p->currentData.size()).arg(srcVerBytes.size()));
            return 0;
        }

        // Real-world .ols files store just the "data area" subset of full
        // ECU flash (e.g. EDC17C46: 76 B header + 2097072 B data + 4 B
        // trailer = 2097152 B).  Find the target offset where source's
        // Original aligns — scan the first 4 KB of target for the source's
        // first 32-byte signature.  If no alignment is found (different SW
        // version, or sizes already equal), fall back to offset 0.
        qint64 alignOffset = 0;
        if (p->currentData.size() > srcVerBytes.size()
            && srcOrig.size() >= 32 && p->currentData.size() >= 32) {
            const qint64 maxScan = qMin<qint64>(4096,
                p->currentData.size() - srcVerBytes.size()) + 1;
            const char *needle = srcOrig.constData();
            const char *hay    = p->currentData.constData();
            for (qint64 off = 0; off < maxScan; ++off) {
                bool match = true;
                for (int k = 0; k < 32; ++k) {
                    if (hay[off + k] != needle[k]) { match = false; break; }
                }
                if (match) { alignOffset = off; break; }
            }
        }

        int flags = va.size() >= 3 ? va[2].as<int>() : (1 | 2);   // structure + contents
        int xmode = va.size() >= 4 ? va[3].as<int>() : 0;         // eICTMAbsolute
        // Iter 6 honours eICImportDataAreas + eICImportMapContents implicitly
        // (we copy all differing bytes regardless of whether they're inside
        // a map range — our flat model can't distinguish efficiently).
        (void)flags;

        int changedBytes = 0;
        const char *srcV = srcVerBytes.constData();
        const char *srcO = srcOrig.constData();
        char       *curC = p->currentData.data() + alignOffset;
        const qint64 n = qMin<qint64>(p->currentData.size() - alignOffset,
                                       srcVerBytes.size());
        for (qint64 i = 0; i < n; ++i) {
            unsigned vV = static_cast<unsigned char>(srcV[i]);
            unsigned vO = static_cast<unsigned char>(srcO[i]);
            unsigned vC = static_cast<unsigned char>(curC[i]);
            if (vV == vO) continue;  // byte unchanged in source
            unsigned newVal = vV;
            if (xmode == 1 /* eICTMRelative */) {
                int delta = int(vV) - int(vO);
                newVal = unsigned((int(vC) + delta) & 0xFF);
            } else if (xmode == 2 /* eICTMPercent */) {
                double ratio = double(vV) / (vO != 0 ? double(vO) : 1.0);
                newVal = unsigned(int(double(vC) * ratio) & 0xFF);
            }
            // xmode==3 (eICTMAllFromOrg) / 4 (AllFromVer) handled outside loop
            curC[i] = static_cast<char>(newVal);
            ++changedBytes;
        }
        if (xmode == 3) {            // eICTMAllFromOrg
            // Overwrite the aligned subset of target; leave any extra
            // bytes (header/trailer) intact.
            memcpy(p->currentData.data() + alignOffset, srcOrig.constData(), size_t(n));
        } else if (xmode == 4) {     // eICTMAllFromVer
            memcpy(p->currentData.data() + alignOffset, srcVerBytes.constData(), size_t(n));
        }
        if (changedBytes > 0 || xmode == 3 || xmode == 4) {
            p->modified = true;
            emit p->dataChanged();
        }
        return 3;   // full ok
        });
    });
    // P0-7: honest false + GetLastError.
    L.set_function("projectAutoUpdate",    [engine](sol::variadic_args) -> bool {
        if (engine) engine->setLastError("projectAutoUpdate not implemented in community build");
        return false;
    });
    L.set_function("projectAutoImport",    [engine](sol::variadic_args) -> bool {
        if (engine) engine->setLastError("projectAutoImport not implemented in community build");
        return false;
    });

    // ── §5.2 vehicle data — REAL via EcuAutoDetect ──
    L.set_function("projectSearchVehicleData", [engine]() -> bool {
        return engine->callOnGui([&]() -> bool {
        Project *p = active(engine);
        if (!p || p->originalData.isEmpty()) {
            if (engine) engine->setLastError("no project / empty ROM");
            return false;
        }
        auto r = ols::EcuAutoDetect::detect(p->originalData);
        if (!r.ok) {
            if (engine) engine->setLastError("ECU not detected");
            return false;
        }
        ols::EcuMetadataFields f;
        f.producer    = &p->ecuProducer;
        f.ecuName     = &p->ecuType;
        f.hwNumber    = &p->ecuNrEcu;
        f.swNumber    = &p->ecuNrProd;
        f.swVersion   = &p->ecuSwVersion;
        f.productionNo = &p->ecuSwNumber;
        f.engineCode  = &p->engineCode;
        ols::EcuAutoDetect::applyToFields(r, f, /*overwrite=*/false);
        p->modified = true;
        return true;
        });
    });
    // P0-7: honest false + GetLastError.
    L.set_function("projectCloneVehicleData",  [engine](sol::variadic_args) -> bool {
        if (engine) engine->setLastError("projectCloneVehicleData not implemented in community build");
        return false;
    });

    // ── §5.2 QuickFix — STUB-MISSING per manual §2.4.50-52 ──
    L.set_function("projectGetQuickFixState", [](sol::variadic_args) -> int { return -2; });
    L.set_function("projectSetQuickFixState", [](sol::variadic_args) -> int { return -2; });
    L.set_function("projectGetQuickFixes",    [&L]() -> sol::table { return L.create_table(); });

    // ── §5.3 Version context ──
    //
    // Iter 7: extended to cover eVerPropChecksum (MD5 of currentData),
    // eVerPropState (mapped from Project::projectType string), and
    // eVerPropCredits / eVerPropCVN (STUB-MISSING return "").
    L.set_function("versionGetProperty", [engine](int id) -> std::string {
        return engine->callOnGui([&]() -> std::string {
        Project *p = active(engine);
        if (!p) return std::string();
        switch (id) {
        case eVerPropName:      return toS(p->name);
        case eVerPropComment:   return toS(p->notes);
        case eVerPropCreatedOn: return toS(p->createdAt.toString(Qt::ISODate));
        case eVerPropChangedOn: return toS(p->changedAt.toString(Qt::ISODate));
        case eVerPropOutput:    return p->outputPS > 0 ? toS(QString::number(p->outputPS)) : std::string();
        case eVerPropTorque:    return p->maxTorque > 0 ? toS(QString::number(p->maxTorque)) : std::string();
        case eVerPropChecksum: {
            if (p->currentData.isEmpty()) return std::string();
            return toS(QString::fromLatin1(
                QCryptographicHash::hash(p->currentData, QCryptographicHash::Md5).toHex()));
        }
        case eVerPropState: {
            // Map our string projectType to eVerStat* int.
            const QString t = p->projectType.toLower();
            int v = eVerStatNone;
            if      (t.contains(QStringLiteral("master")))     v = eVerStatMaster;
            else if (t.contains(QStringLiteral("finished")))   v = eVerStatFinished;
            else if (t.contains(QStringLiteral("test")))       v = eVerStatTestable;
            else if (t.contains(QStringLiteral("error")))      v = eVerStatErrors;
            else if (t.contains(QStringLiteral("experiment"))) v = eVerStatExperiment;
            else if (t.contains(QStringLiteral("todo")))       v = eVerStatToDo;
            else if (t.contains(QStringLiteral("dev")))        v = eVerStatDev;
            return toS(QString::number(v));
        }
        case eVerPropCVN:
        case eVerPropCredits:
            return std::string();  // STUB-MISSING — no romHEX14 backing field
        default: return std::string();
        }
        });
    });
    L.set_function("versionSetProperty", [engine](int id, sol::object value) -> bool {
        const bool valueIsNumber = value.get_type() == sol::type::number;
        QString s = (value.get_type() == sol::type::string)
                        ? toQ(value.as<std::string>())
                        : QString::number(value.as<double>());
        const int numericValue = valueIsNumber ? value.as<int>() : s.toInt();
        return engine->callOnGui([&]() -> bool {
        Project *p = active(engine);
        if (!p) { engine->setLastError("no project open"); return false; }
        switch (id) {
        case eVerPropName:    p->name = s; break;
        case eVerPropComment: p->notes = s; break;
        case eVerPropOutput:  p->outputPS = s.toInt(); break;
        case eVerPropTorque:  p->maxTorque = s.toInt(); break;
        case eVerPropState: {
            // Accept either int eVerStat* or string.  Reverse-map to projectType.
            int v = numericValue;
            switch (v) {
            case eVerStatMaster:     p->projectType = QStringLiteral("Master"); break;
            case eVerStatFinished:   p->projectType = QStringLiteral("Finished"); break;
            case eVerStatTestable:   p->projectType = QStringLiteral("Testable"); break;
            case eVerStatErrors:     p->projectType = QStringLiteral("Errors"); break;
            case eVerStatExperiment: p->projectType = QStringLiteral("Experiment"); break;
            case eVerStatToDo:       p->projectType = QStringLiteral("ToDo"); break;
            case eVerStatDev:        p->projectType = QStringLiteral("Developing"); break;
            default:                 p->projectType = QStringLiteral(""); break;
            }
            break;
        }
        default:
            engine->setLastError(QStringLiteral(
                "versionSetProperty: unsupported property id %1").arg(id));
            return false;
        }
        p->modified = true;
        return true;
        });
    });

    // ── §5.4 Window context — REAL via QMdiArea ──
    L.set_function("windowGetActive", [engine]() -> qint64 {
        return engine->callOnGui([&]() -> qint64 {
        if (!engine || !engine->mainWindow()) return 0;
        // Walk subWindowList for a stable session-local index (1-based).
        // QMdiArea::activeSubWindow() returns the focused child.
        QMdiArea *mdi = engine->mainWindow()->findChild<QMdiArea *>();
        if (!mdi) return 0;
        auto *active = mdi->activeSubWindow();
        if (!active) return 0;
        const auto list = mdi->subWindowList();
        for (int i = 0; i < list.size(); ++i)
            if (list[i] == active) return qint64(i + 1);
        return 0;
        });
    });
    L.set_function("windowSetActive", [engine](qint64 id) -> bool {
        return engine->callOnGui([&]() -> bool {
        if (!engine || !engine->mainWindow()) {
            if (engine) engine->setLastError("no main window");
            return false;
        }
        QMdiArea *mdi = engine->mainWindow()->findChild<QMdiArea *>();
        if (!mdi) { engine->setLastError("no MDI area"); return false; }
        const auto list = mdi->subWindowList();
        int idx = int(id) - 1;
        if (idx < 0 || idx >= list.size()) {
            engine->setLastError(QStringLiteral(
                "windowSetActive: index %1 out of range").arg(id));
            return false;
        }
        mdi->setActiveSubWindow(list[idx]);
        return true;
        });
    });
    // Iter 10.7: map selector helper used by both window*MapProperties
    // overloads.  Returns nullptr if no map matches; the caller falls back
    // to the last map for backwards-compat callers without a selector arg.
    //
    // sel semantics (mirrors WinOLS manual):
    //   sol::lua_nil              → last map
    //   bool true / number==1 → last map created by projectAddMap (else last)
    //   number==0             → last map (bLastNew=false)
    //   number>1              → find by start address (m.address == sel)
    //   string                → find by MapId (sideProp "IdName") or by name
    auto pickMap = [engine](Project *p, sol::object sel) -> MapInfo * {
        if (!p || p->maps.isEmpty()) return nullptr;
        if (!sel.valid() || sel.is<sol::lua_nil_t>())
            return &p->maps.last();
        if (sel.get_type() == sol::type::string) {
            const QString id = toQ(sel.as<std::string>());
            for (auto &m : p->maps) {
                if (m.getSideProp("IdName").toString() == id) return &m;
                if (m.name == id) return &m;
            }
            return nullptr;
        }
        if (sel.get_type() == sol::type::number
         || sel.get_type() == sol::type::boolean) {
            // Distinguish bLastNew (0/1) from startAddr (>1) using value.
            const double dv = (sel.get_type() == sol::type::boolean)
                ? (sel.as<bool>() ? 1.0 : 0.0) : sel.as<double>();
            const qint64 v = qint64(dv);
            if (v == 1) {
                if (MapInfo *lastCreated = engine->mainWindow()->luaLastCreatedMap(p))
                    return lastCreated;
            }
            if (v == 0 || v == 1) return &p->maps.last();
            // v > 1 → treat as start address.
            const uint32_t addr = uint32_t(v);
            for (auto &m : p->maps) if (m.address == addr) return &m;
            return nullptr;
        }
        return &p->maps.last();
    };

    L.set_function("windowGetMapProperties", [engine, pickMap](sol::variadic_args va) -> std::string {
        return engine->callOnGui([&]() -> std::string {
        if (va.size() < 1) return std::string();
        QString name = toQ(va[0].as<std::string>());
        Project *p = active(engine);
        sol::object sel = va.size() >= 2 ? sol::object(va[1]) : sol::object();
        const MapInfo *mp = pickMap(p, sel);
        if (!mp) return std::string();
        const MapInfo &m = *mp;
        // Map a handful of common props to real fields, rest come from side-map.
        // Direct-field reads must mirror the write path in windowSetMapProperties.
        if (name == QStringLiteral("Name"))      return toS(m.name);
        if (name == QStringLiteral("Spalten"))   return toS(QString::number(m.dimensions.x));
        if (name == QStringLiteral("Zeilen"))    return toS(QString::number(m.dimensions.y));
        if (name == QStringLiteral("Kommentar")) return toS(m.userNotes);
        if (name == QStringLiteral("Typ"))       return toS(QString::number(mapTypeToWinOlsEnum(m.type)));
        if (name == QStringLiteral("bVorzeichen")) return toS(QString::number(m.dataSigned ? 1 : 0));
        if (name == QStringLiteral("Feldwerte.StartAddr")) return toS(QString::number(m.address));
        if (name == QStringLiteral("Feldwerte.Faktor"))    return toS(QString::number(m.scaling.linA, 'g', 12));
        if (name == QStringLiteral("Feldwerte.Offset"))    return toS(QString::number(m.scaling.linB, 'g', 12));
        if (name == QStringLiteral("Feldwerte.Einheit"))   return toS(m.scaling.unit);
        if (name == QStringLiteral("StuetzX.Name"))        return toS(m.xAxis.inputName);
        if (name == QStringLiteral("StuetzX.DataAddr"))    return toS(QString::number(m.xAxis.ptsAddress));
        if (name == QStringLiteral("StuetzY.Name"))        return toS(m.yAxis.inputName);
        if (name == QStringLiteral("StuetzY.DataAddr"))    return toS(QString::number(m.yAxis.ptsAddress));
        if (name == QStringLiteral("IdName"))    return toS(m.getSideProp("IdName").toString());
        if (name == QStringLiteral("FolderName")) return toS(m.getSideProp("FolderName").toString());
        QVariant v = m.getSideProp(name);
        return toS(v.toString());
        });
    });
    L.set_function("windowSetMapProperties", [engine, pickMap](sol::variadic_args va) -> bool {
        return engine->callOnGui([&]() -> bool {
        if (va.size() < 2) {
            if (engine) engine->setLastError(
                "windowSetMapProperties: needs property name + value");
            return false;
        }
        QString name = toQ(va[0].as<std::string>());
        // Iter 10.7: third arg now goes through pickMap so script can target
        // a map by start address (number>1) or MapId (string), not just
        // bLastNew TRUE/FALSE.
        Project *p = active(engine);
        sol::object sel = va.size() >= 3 ? sol::object(va[2]) : sol::object();
        MapInfo *m = pickMap(p, sel);
        if (!m) { if (engine) engine->setLastError("map not found"); return false; }
        sol::object val = va[1];
        QString sv = (val.get_type() == sol::type::string)
                        ? toQ(val.as<std::string>())
                        : QString::number(val.as<double>());
        // Direct-field mappings (single exit point so Iter 10.4 dirty-flag
        // bookkeeping below applies regardless of which branch wrote).
        if      (name == QStringLiteral("Name"))      { m->name = sv; }
        else if (name == QStringLiteral("IdName"))    { m->setSideProp(name, sv); }
        else if (name == QStringLiteral("Kommentar")) { m->userNotes = sv; }
        else if (name == QStringLiteral("Typ"))       { m->type = mapTypeFromWinOlsEnum(int(val.as<double>())); }
        else if (name == QStringLiteral("Spalten"))     { m->dimensions.x = int(val.as<double>()); }
        else if (name == QStringLiteral("Zeilen"))      { m->dimensions.y = int(val.as<double>()); }
        else if (name == QStringLiteral("bVorzeichen")) { m->dataSigned = int(val.as<double>()) != 0; }
        else if (name == QStringLiteral("Feldwerte.StartAddr")) { m->address = uint32_t(val.as<double>()); }
        else if (name == QStringLiteral("Feldwerte.Faktor"))    { m->scaling.linA = val.as<double>(); m->hasScaling = true; }
        else if (name == QStringLiteral("Feldwerte.Offset"))    { m->scaling.linB = val.as<double>(); m->hasScaling = true; }
        else if (name == QStringLiteral("Feldwerte.Einheit"))   { m->scaling.unit = sv; }
        else if (name == QStringLiteral("StuetzX.Name"))     { m->xAxis.inputName = sv; }
        else if (name == QStringLiteral("StuetzX.DataAddr")) { m->xAxis.ptsAddress = uint32_t(val.as<double>()); m->xAxis.hasPtsAddress = true; }
        else if (name == QStringLiteral("StuetzY.Name"))     { m->yAxis.inputName = sv; }
        else if (name == QStringLiteral("StuetzY.DataAddr")) { m->yAxis.ptsAddress = uint32_t(val.as<double>()); m->yAxis.hasPtsAddress = true; }
        else {
            // Anything else lands in the side-map (accepted-no-op storage).
            m->setSideProp(name, sv);
        }
        // Iter 10.4: any successful write dirties the project + notifies UI.
        p->modified = true;
        emit p->dataChanged();
        return true;
        });
    });

    // ── LEGION internal test bindings (M.1+) ──────────────────────────────
    //
    // These expose pieces of the legion:: pipeline so the Lua test harness
    // can verify each stage in isolation.  They take/return Lua strings as
    // byte buffers (sol2 maps QByteArray<->std::string cleanly).
    //
    // _legion_detectRegions(originalStr, stage1Str, kAdjacency)
    //   → array of {startAddr, endAddr, size} tables
    L.set_function("_legion_detectRegions",
        [&L](const std::string &origStr, const std::string &stage1Str, int k) -> sol::table {
            QByteArray orig  = QByteArray::fromStdString(origStr);
            QByteArray stage = QByteArray::fromStdString(stage1Str);
            auto regions = legion::detectRegions(orig, stage, k);
            sol::table out = L.create_table();
            for (int i = 0; i < regions.size(); ++i) {
                sol::table row = L.create_table();
                row["startAddr"] = double(regions[i].startAddr);
                row["endAddr"]   = double(regions[i].endAddr);
                row["size"]      = double(regions[i].endAddr - regions[i].startAddr + 1);
                out[i + 1] = row;
            }
            return out;
        });

    // _legion_inferStructure(originalStr) → {cellSize, bigEndian, rows, cols, kind}
    L.set_function("_legion_inferStructure",
        [&L](const std::string &origStr) -> sol::table {
            QByteArray bytes = QByteArray::fromStdString(origStr);
            legion::LegionRegion region;
            region.startAddr     = 0;
            region.endAddr       = bytes.isEmpty() ? 0 : uint32_t(bytes.size() - 1);
            region.originalBytes = bytes;
            region.modifiedBytes = bytes;
            const auto h = legion::inferStructure(region);
            sol::table out = L.create_table();
            out["cellSize"]  = h.cellSize;
            out["bigEndian"] = h.bigEndian;
            out["rows"]      = h.rows;
            out["cols"]      = h.cols;
            const char *kind = "Scalar";
            switch (h.kind) {
            case legion::VerdictKind::Scalar:   kind = "Scalar";   break;
            case legion::VerdictKind::Curve:    kind = "Curve";    break;
            case legion::VerdictKind::SmallMap: kind = "SmallMap"; break;
            case legion::VerdictKind::LargeMap: kind = "LargeMap"; break;
            }
            out["kind"] = std::string(kind);
            return out;
        });

    // _legion_clusterVoices(voicesTbl, jaccardMin?)
    //   voicesTbl = {{sourcePath="...", addrs={a1, a2, ...}}, ...}
    //   → array of {voiceIndices={1-based...}, label, addrRangeMin,
    //               addrRangeMax, consensusAddrCount, memberCount}
    L.set_function("_legion_clusterVoices",
        [&L](sol::table voicesTbl, sol::optional<double> jaccardOpt) -> sol::table {
            const double jacc = jaccardOpt.value_or(0.50);
            QVector<legion::LegionVoice> voices;
            voices.reserve(int(voicesTbl.size()));
            for (std::size_t i = 1; i <= voicesTbl.size(); ++i) {
                sol::table row = voicesTbl[i];
                legion::LegionVoice v;
                sol::optional<std::string> sp = row["sourcePath"];
                if (sp) v.sourcePath = QString::fromStdString(*sp);
                sol::optional<sol::table> addrs = row["addrs"];
                if (addrs) {
                    sol::table at = *addrs;
                    for (std::size_t k = 1; k <= at.size(); ++k) {
                        v.addressSet.insert(uint32_t(double(at[k])));
                    }
                }
                voices.append(std::move(v));
            }
            const auto clusters = legion::clusterVoices(voices, jacc);
            sol::table out = L.create_table();
            for (int ci = 0; ci < clusters.size(); ++ci) {
                const auto &c = clusters[ci];
                sol::table row = L.create_table();
                sol::table idx = L.create_table();
                for (int k = 0; k < c.voiceIndices.size(); ++k) {
                    idx[k + 1] = c.voiceIndices[k] + 1;     // 1-based for Lua
                }
                row["voiceIndices"]       = idx;
                row["memberCount"]        = c.voiceIndices.size();
                row["addrRangeMin"]       = double(c.addrRangeMin);
                row["addrRangeMax"]       = double(c.addrRangeMax);
                row["consensusAddrCount"] = c.consensusAddrCount;
                row["label"]              = c.label.toStdString();
                out[ci + 1] = row;
            }
            return out;
        });

    // _legion_aggregate(voicesTbl, baselineStr, clusterIndices, localSimMin?)
    //   voicesTbl[i]   = {sourcePath="...", regions={{startAddr=, originalBytes="", modifiedBytes=""}, ...}}
    //   clusterIndices = array of 1-based voice indices
    //   → array of verdicts:
    //     {startAddr, endAddr, cellSize, rows, cols, kind, maxSampleCount,
    //      cells={{meanDelta, stdDevDelta, sampleCount}, ...},
    //      contributingVoices={...}}
    L.set_function("_legion_aggregate",
        [&L](sol::table voicesTbl, const std::string &baselineStr,
             sol::table clusterIdxTbl, sol::optional<double> simOpt) -> sol::table {
            const QByteArray baseline = QByteArray::fromStdString(baselineStr);
            const double sim = simOpt.value_or(0.90);

            QVector<legion::LegionVoice> voices;
            voices.reserve(int(voicesTbl.size()));
            for (std::size_t i = 1; i <= voicesTbl.size(); ++i) {
                sol::table row = voicesTbl[i];
                legion::LegionVoice v;
                sol::optional<std::string> sp = row["sourcePath"];
                if (sp) v.sourcePath = QString::fromStdString(*sp);

                sol::optional<sol::table> regs = row["regions"];
                if (regs) {
                    sol::table rt = *regs;
                    for (std::size_t k = 1; k <= rt.size(); ++k) {
                        sol::table rr = rt[k];
                        legion::LegionRegion reg;
                        reg.startAddr = uint32_t(double(rr["startAddr"]));
                        sol::optional<std::string> ob = rr["originalBytes"];
                        sol::optional<std::string> mb = rr["modifiedBytes"];
                        if (ob) reg.originalBytes = QByteArray::fromStdString(*ob);
                        if (mb) reg.modifiedBytes = QByteArray::fromStdString(*mb);
                        reg.endAddr = reg.startAddr +
                                      uint32_t(qMax(0, reg.originalBytes.size() - 1));
                        // Populate addressSet for any byte where orig != modified.
                        for (int b = 0; b < reg.originalBytes.size() &&
                                        b < reg.modifiedBytes.size(); ++b) {
                            if (reg.originalBytes[b] != reg.modifiedBytes[b]) {
                                v.addressSet.insert(reg.startAddr + uint32_t(b));
                            }
                        }
                        v.regions.append(std::move(reg));
                    }
                }
                voices.append(std::move(v));
            }

            legion::VoiceCluster cluster;
            for (std::size_t i = 1; i <= clusterIdxTbl.size(); ++i) {
                cluster.voiceIndices.append(int(double(clusterIdxTbl[i])) - 1);
            }

            const auto verdicts = legion::aggregate(voices, baseline, cluster, sim);

            sol::table out = L.create_table();
            for (int vi = 0; vi < verdicts.size(); ++vi) {
                const auto &v = verdicts[vi];
                sol::table row = L.create_table();
                row["startAddr"]      = double(v.startAddr);
                row["endAddr"]        = double(v.endAddr);
                row["cellSize"]       = v.cellSize;
                row["rows"]           = v.rows;
                row["cols"]           = v.cols;
                row["maxSampleCount"] = v.maxSampleCount;
                const char *kind = "Scalar";
                switch (v.kind) {
                case legion::VerdictKind::Scalar:   kind = "Scalar";   break;
                case legion::VerdictKind::Curve:    kind = "Curve";    break;
                case legion::VerdictKind::SmallMap: kind = "SmallMap"; break;
                case legion::VerdictKind::LargeMap: kind = "LargeMap"; break;
                }
                row["kind"] = std::string(kind);

                sol::table cellsTbl = L.create_table();
                for (int ci = 0; ci < v.cells.size(); ++ci) {
                    sol::table cell = L.create_table();
                    cell["meanDelta"]   = v.cells[ci].meanDelta;
                    cell["stdDevDelta"] = v.cells[ci].stdDevDelta;
                    cell["sampleCount"] = v.cells[ci].sampleCount;
                    cellsTbl[ci + 1] = cell;
                }
                row["cells"] = cellsTbl;

                sol::table cvTbl = L.create_table();
                for (int k = 0; k < v.contributingVoices.size(); ++k) {
                    cvTbl[k + 1] = v.contributingVoices[k] + 1;   // 1-based
                }
                row["contributingVoices"] = cvTbl;
                out[vi + 1] = row;
            }
            return out;
        });

    // _legion_applyVerdict(dataStr, verdictTbl)
    //   verdictTbl: {startAddr, cellSize, bigEndian, cells={{meanDelta, sampleCount}, ...}}
    //   Returns the modified data string (Lua strings are immutable, so we
    //   copy through a temporary QByteArray).
    L.set_function("_legion_applyVerdict",
        [](const std::string &dataStr, sol::table vt) -> std::string {
            QByteArray data = QByteArray::fromStdString(dataStr);
            legion::LegionVerdict v;
            v.startAddr = uint32_t(double(vt["startAddr"]));
            v.cellSize  = int(double(vt["cellSize"]));
            sol::optional<bool> be = vt["bigEndian"];
            v.bigEndian = be.value_or(false);
            sol::table cellsTbl = vt["cells"];
            v.cells.resize(int(cellsTbl.size()));
            for (std::size_t k = 1; k <= cellsTbl.size(); ++k) {
                sol::table cell = cellsTbl[k];
                v.cells[int(k) - 1].meanDelta   = double(cell["meanDelta"]);
                v.cells[int(k) - 1].sampleCount = int(double(cell["sampleCount"]));
            }
            v.endAddr = v.startAddr +
                        uint32_t(qMax(0, v.cells.size() * v.cellSize - 1));
            (void)legion::applyVerdict(data, v);
            return data.toStdString();
        });

    // _legion_classify(verdictsTbl, totalVoicesInCluster)
    //   verdictsTbl: array of verdicts as returned by _legion_aggregate
    //                (uses .cells[].sampleCount/.meanDelta/.stdDevDelta).
    //   Returns a NEW array of verdicts with tag (string) + consensusStrength
    //   added/overwritten, sorted by consensusStrength descending.
    L.set_function("_legion_classify",
        [&L](sol::table verdictsTbl, int totalVoices) -> sol::table {
            QVector<legion::LegionVerdict> verdicts;
            verdicts.reserve(int(verdictsTbl.size()));
            for (std::size_t i = 1; i <= verdictsTbl.size(); ++i) {
                sol::table row = verdictsTbl[i];
                legion::LegionVerdict v;
                v.startAddr      = uint32_t(double(row["startAddr"]));
                v.endAddr        = uint32_t(double(row["endAddr"]));
                v.cellSize       = int(double(row["cellSize"]));
                v.rows           = int(double(row["rows"]));
                v.cols           = int(double(row["cols"]));
                v.maxSampleCount = int(double(row["maxSampleCount"]));
                sol::table cellsTbl = row["cells"];
                v.cells.resize(int(cellsTbl.size()));
                for (std::size_t k = 1; k <= cellsTbl.size(); ++k) {
                    sol::table cell = cellsTbl[k];
                    v.cells[int(k) - 1].meanDelta   = double(cell["meanDelta"]);
                    v.cells[int(k) - 1].stdDevDelta = double(cell["stdDevDelta"]);
                    v.cells[int(k) - 1].sampleCount = int(double(cell["sampleCount"]));
                }
                verdicts.append(std::move(v));
            }

            legion::classify(verdicts, totalVoices);

            sol::table out = L.create_table();
            for (int vi = 0; vi < verdicts.size(); ++vi) {
                const auto &v = verdicts[vi];
                sol::table row = L.create_table();
                row["startAddr"]         = double(v.startAddr);
                row["endAddr"]           = double(v.endAddr);
                row["cellSize"]          = v.cellSize;
                row["rows"]              = v.rows;
                row["cols"]              = v.cols;
                row["maxSampleCount"]    = v.maxSampleCount;
                row["consensusStrength"] = v.consensusStrength;
                const char *tag = "Heretic";
                switch (v.tag) {
                case legion::VerdictTag::Unanimous:       tag = "Unanimous";       break;
                case legion::VerdictTag::StrongConsensus: tag = "StrongConsensus"; break;
                case legion::VerdictTag::Majority:        tag = "Majority";        break;
                case legion::VerdictTag::Contested:       tag = "Contested";       break;
                case legion::VerdictTag::Heretic:         tag = "Heretic";         break;
                case legion::VerdictTag::Checksum:        tag = "Checksum";        break;
                case legion::VerdictTag::KillRegion:      tag = "KillRegion";      break;
                }
                row["tag"] = std::string(tag);
                sol::table cellsTbl = L.create_table();
                for (int k = 0; k < v.cells.size(); ++k) {
                    sol::table cell = L.create_table();
                    cell["meanDelta"]   = v.cells[k].meanDelta;
                    cell["stdDevDelta"] = v.cells[k].stdDevDelta;
                    cell["sampleCount"] = v.cells[k].sampleCount;
                    cellsTbl[k + 1]     = cell;
                }
                row["cells"] = cellsTbl;
                out[vi + 1] = row;
            }
            return out;
        });

    // ── LEGION.9 ─────────────────────────────────────────────────────────
    //
    // legionInvoke(opts) — one-shot end-to-end pipeline:
    //   detectRegions → clusterVoices → for each cluster: aggregate +
    //   classify.  Returns the full forest of clusters + classified verdicts.
    //
    // opts = {
    //   baseline = "<bytes>",                 -- user ROM (Version 0)
    //   voices   = {                          -- voice corpus
    //       { sourcePath="...",
    //         originalBytes="...",            -- voice's Version 0
    //         modifiedBytes="..." },          -- voice's Version 1
    //       ...
    //   },
    //   jaccardMin  = 0.50,                   -- voice clustering threshold
    //   localSimMin = 0.90,                   -- per-region hamming gate
    //   detectK     = 16,                     -- region adjacency window
    // }
    //
    // returns = {
    //   voices = N,                           -- voices accepted (non-empty diff)
    //   clusters = {
    //     { voiceIndices, memberCount, label, addrRangeMin, addrRangeMax,
    //       consensusAddrCount,
    //       verdicts = { ...one-shot aggregate+classify per cluster... } },
    //     ...
    //   }
    // }
    L.set_function("legionInvoke",
        [&L](sol::table opts) -> sol::table {
            sol::optional<std::string> bsOpt = opts["baseline"];
            const QByteArray baseline = bsOpt
                ? QByteArray::fromStdString(*bsOpt) : QByteArray();
            const double jacc = sol::optional<double>(opts["jaccardMin"])
                                    .value_or(0.50);
            const double sim  = sol::optional<double>(opts["localSimMin"])
                                    .value_or(0.90);
            const int kAdj    = int(sol::optional<double>(opts["detectK"])
                                    .value_or(16.0));

            // 1) Ingest voices.
            QVector<legion::LegionVoice> voices;
            sol::optional<sol::table> voicesTbl = opts["voices"];
            if (voicesTbl) {
                sol::table vt = *voicesTbl;
                voices.reserve(int(vt.size()));
                for (std::size_t i = 1; i <= vt.size(); ++i) {
                    sol::table row = vt[i];
                    legion::LegionVoice v;
                    sol::optional<std::string> sp = row["sourcePath"];
                    if (sp) v.sourcePath = QString::fromStdString(*sp);
                    sol::optional<std::string> ob = row["originalBytes"];
                    sol::optional<std::string> mb = row["modifiedBytes"];
                    if (!ob || !mb) { voices.append(std::move(v)); continue; }
                    QByteArray orig = QByteArray::fromStdString(*ob);
                    QByteArray mod  = QByteArray::fromStdString(*mb);
                    v.regions = legion::detectRegions(orig, mod, kAdj);
                    for (const auto &r : v.regions) {
                        for (uint32_t a = r.startAddr; a <= r.endAddr; ++a) {
                            if (a < uint32_t(orig.size()) &&
                                a < uint32_t(mod.size()) &&
                                orig[int(a)] != mod[int(a)]) {
                                v.addressSet.insert(a);
                            }
                        }
                    }
                    if (!v.addressSet.isEmpty())
                        voices.append(std::move(v));
                }
            }

            // 2) Cluster.
            const auto clusters = legion::clusterVoices(voices, jacc);

            // 3) Per cluster: aggregate + classify (only multi-member).
            sol::table out = L.create_table();
            out["voices"] = voices.size();
            sol::table clustersTbl = L.create_table();
            for (int ci = 0; ci < clusters.size(); ++ci) {
                const auto &c = clusters[ci];
                sol::table row = L.create_table();
                sol::table idxTbl = L.create_table();
                for (int k = 0; k < c.voiceIndices.size(); ++k)
                    idxTbl[k + 1] = c.voiceIndices[k] + 1;          // 1-based
                row["voiceIndices"]       = idxTbl;
                row["memberCount"]        = c.voiceIndices.size();
                row["label"]              = c.label.toStdString();
                row["addrRangeMin"]       = double(c.addrRangeMin);
                row["addrRangeMax"]       = double(c.addrRangeMax);
                row["consensusAddrCount"] = c.consensusAddrCount;

                // Aggregate + classify (no-op for singletons but cheap).
                QVector<legion::LegionVerdict> verdicts;
                if (c.voiceIndices.size() >= 2 && !baseline.isEmpty()) {
                    verdicts = legion::aggregate(voices, baseline, c, sim);
                    legion::classify(verdicts, c.voiceIndices.size());
                }
                sol::table vsTbl = L.create_table();
                for (int vi = 0; vi < verdicts.size(); ++vi) {
                    const auto &v = verdicts[vi];
                    sol::table vrow = L.create_table();
                    vrow["startAddr"]         = double(v.startAddr);
                    vrow["endAddr"]           = double(v.endAddr);
                    vrow["cellSize"]          = v.cellSize;
                    vrow["bigEndian"]         = v.bigEndian;
                    vrow["rows"]              = v.rows;
                    vrow["cols"]              = v.cols;
                    vrow["maxSampleCount"]    = v.maxSampleCount;
                    vrow["consensusStrength"] = v.consensusStrength;
                    const char *kind = "Scalar";
                    switch (v.kind) {
                    case legion::VerdictKind::Scalar:   kind = "Scalar";   break;
                    case legion::VerdictKind::Curve:    kind = "Curve";    break;
                    case legion::VerdictKind::SmallMap: kind = "SmallMap"; break;
                    case legion::VerdictKind::LargeMap: kind = "LargeMap"; break;
                    }
                    vrow["kind"] = std::string(kind);
                    const char *tag = "Heretic";
                    switch (v.tag) {
                    case legion::VerdictTag::Unanimous:       tag = "Unanimous";       break;
                    case legion::VerdictTag::StrongConsensus: tag = "StrongConsensus"; break;
                    case legion::VerdictTag::Majority:        tag = "Majority";        break;
                    case legion::VerdictTag::Contested:       tag = "Contested";       break;
                    case legion::VerdictTag::Heretic:         tag = "Heretic";         break;
                    case legion::VerdictTag::Checksum:        tag = "Checksum";        break;
                    case legion::VerdictTag::KillRegion:      tag = "KillRegion";      break;
                    }
                    vrow["tag"] = std::string(tag);
                    sol::table cellsTbl = L.create_table();
                    for (int k = 0; k < v.cells.size(); ++k) {
                        sol::table cell = L.create_table();
                        cell["meanDelta"]   = v.cells[k].meanDelta;
                        cell["stdDevDelta"] = v.cells[k].stdDevDelta;
                        cell["sampleCount"] = v.cells[k].sampleCount;
                        cellsTbl[k + 1] = cell;
                    }
                    vrow["cells"] = cellsTbl;
                    sol::table cvTbl = L.create_table();
                    for (int k = 0; k < v.contributingVoices.size(); ++k)
                        cvTbl[k + 1] = v.contributingVoices[k] + 1;   // 1-based
                    vrow["contributingVoices"] = cvTbl;
                    vsTbl[vi + 1] = vrow;
                }
                row["verdicts"] = vsTbl;
                clustersTbl[ci + 1] = row;
            }
            out["clusters"] = clustersTbl;
            return out;
        });
}

} // namespace lua
