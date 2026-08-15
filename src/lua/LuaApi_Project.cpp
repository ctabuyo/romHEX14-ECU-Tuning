/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Sprint L §5.2 — Project context bindings.
 *
 * Iter 4: properties (get/set), close, save, the byte access trio
 *         (getAt/setAt/setOrg), and the simple counters / address
 *         converters that don't need MapInfo.
 * Iter 5: find/replace bytes, comments via AnnotationStore.
 * Iter 6: maps, export/import, similarity, etc.
 *
 * "Active project" = MainWindow::activeProject().  Returns nullptr ⇒
 * function sets GetLastError and returns false/0/"".
 */

#include "LuaEngine.h"
#include "LuaPropertyIds.h"
#include "project.h"
#include "mainwindow.h"
#include "annotations/AnnotationStore.h"

#include "sol/sol.hpp"

#include <QFileInfo>
#include <QDateTime>
#include <QByteArray>
#include <QString>

namespace lua {

namespace {

QString toQ(const std::string &s) { return QString::fromUtf8(s.c_str(), int(s.size())); }
std::string toS(const QString &q) { auto a = q.toUtf8(); return std::string(a.constData(), size_t(a.size())); }

Project *active(LuaEngine *engine)
{
    if (!engine || !engine->mainWindow()) return nullptr;
    return engine->mainWindow()->luaActiveProject();
}

// Read a value of the given WinOLS datatype enum from project->currentData
// at the given offset. Returns lua_ids::eEmptyvalue on out-of-range.
double readAt(const Project *p, qint64 addr, int dtype)
{
    using namespace lua_ids;
    if (!p || addr < 0 || addr >= p->currentData.size()) return eEmptyvalue;
    const uint8_t *d = reinterpret_cast<const uint8_t *>(p->currentData.constData());
    int n = p->currentData.size();
    switch (dtype) {
    case eByte:
        return double(d[addr]);
    case eLoHi:
        if (addr + 1 >= n) return eEmptyvalue;
        return double(d[addr] | (uint32_t(d[addr + 1]) << 8));
    case eHiLo:
        if (addr + 1 >= n) return eEmptyvalue;
        return double((uint32_t(d[addr]) << 8) | d[addr + 1]);
    case eLoHiLoHi:
        if (addr + 3 >= n) return eEmptyvalue;
        return double(uint32_t(d[addr]) | (uint32_t(d[addr+1]) << 8)
                     | (uint32_t(d[addr+2]) << 16) | (uint32_t(d[addr+3]) << 24));
    case eHiLoHiLo:
        if (addr + 3 >= n) return eEmptyvalue;
        return double((uint32_t(d[addr]) << 24) | (uint32_t(d[addr+1]) << 16)
                     | (uint32_t(d[addr+2]) << 8)  | d[addr+3]);
    case eFloatLoHi: {
        if (addr + 3 >= n) return eEmptyvalue;
        uint32_t v = uint32_t(d[addr]) | (uint32_t(d[addr+1]) << 8)
                   | (uint32_t(d[addr+2]) << 16) | (uint32_t(d[addr+3]) << 24);
        float f;
        std::memcpy(&f, &v, sizeof(f));
        return double(f);
    }
    case eFloatHiLo: {
        if (addr + 3 >= n) return eEmptyvalue;
        uint32_t v = (uint32_t(d[addr]) << 24) | (uint32_t(d[addr+1]) << 16)
                   | (uint32_t(d[addr+2]) << 8) | d[addr+3];
        float f;
        std::memcpy(&f, &v, sizeof(f));
        return double(f);
    }
    case eDoubleLoHi: {
        // Iter 10.5: 8-byte IEEE754 double, little-endian.
        if (addr + 7 >= n) return eEmptyvalue;
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= uint64_t(d[addr+i]) << (8*i);
        double dv;
        std::memcpy(&dv, &v, sizeof(dv));
        return dv;
    }
    case eDoubleHiLo: {
        // Iter 10.5: 8-byte IEEE754 double, big-endian.
        if (addr + 7 >= n) return eEmptyvalue;
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= uint64_t(d[addr+i]) << (8*(7-i));
        double dv;
        std::memcpy(&dv, &v, sizeof(dv));
        return dv;
    }
    default:
        return eEmptyvalue;
    }
}

// Write a value of the given datatype at the given offset.
// Returns false on out-of-range or unsupported dtype.
int datatypeSize(int dtype);

bool writeAt(Project *p, qint64 addr, double value, int dtype)
{
    using namespace lua_ids;
    if (!p || addr < 0) return false;
    int n = p->currentData.size();
    const int sz = datatypeSize(dtype);
    if (addr + sz > n) return false;

    QByteArray bytes(sz, 0);
    uint8_t *d = reinterpret_cast<uint8_t *>(bytes.data());
    auto v32 = uint32_t(value);
    switch (dtype) {
    case eByte:
        d[0] = uint8_t(v32);
        break;
    case eLoHi:
        d[0] = uint8_t(v32 & 0xFF);
        d[1] = uint8_t((v32 >> 8) & 0xFF);
        break;
    case eHiLo:
        d[0] = uint8_t((v32 >> 8) & 0xFF);
        d[1] = uint8_t(v32 & 0xFF);
        break;
    case eLoHiLoHi:
        d[0] = uint8_t( v32        & 0xFF);
        d[1] = uint8_t((v32 >> 8)  & 0xFF);
        d[2] = uint8_t((v32 >> 16) & 0xFF);
        d[3] = uint8_t((v32 >> 24) & 0xFF);
        break;
    case eHiLoHiLo:
        d[0] = uint8_t((v32 >> 24) & 0xFF);
        d[1] = uint8_t((v32 >> 16) & 0xFF);
        d[2] = uint8_t((v32 >> 8)  & 0xFF);
        d[3] = uint8_t( v32        & 0xFF);
        break;
    case eFloatLoHi: {
        float f = float(value);
        uint32_t bits;
        std::memcpy(&bits, &f, sizeof(bits));
        d[0] = uint8_t( bits        & 0xFF);
        d[1] = uint8_t((bits >> 8)  & 0xFF);
        d[2] = uint8_t((bits >> 16) & 0xFF);
        d[3] = uint8_t((bits >> 24) & 0xFF);
        break;
    }
    case eFloatHiLo: {
        float f = float(value);
        uint32_t bits;
        std::memcpy(&bits, &f, sizeof(bits));
        d[0] = uint8_t((bits >> 24) & 0xFF);
        d[1] = uint8_t((bits >> 16) & 0xFF);
        d[2] = uint8_t((bits >> 8)  & 0xFF);
        d[3] = uint8_t( bits        & 0xFF);
        break;
    }
    case eDoubleLoHi: {
        uint64_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        for (int i = 0; i < 8; ++i) d[i] = uint8_t((bits >> (8*i)) & 0xFF);
        break;
    }
    case eDoubleHiLo: {
        uint64_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        for (int i = 0; i < 8; ++i) d[i] = uint8_t((bits >> (8*(7-i))) & 0xFF);
        break;
    }
    default:
        return false;
    }
    p->applyRomPatches({{int(addr), bytes}}, QObject::tr("Lua script write"));
    return true;
}

// Bytes per cell for a given WinOLS datatype enum.
// Iter 10.5: previously inlined as `step = byte ? 1 : 2` which mis-stepped
// 32-bit / float / double values.
int datatypeSize(int dtype)
{
    using namespace lua_ids;
    switch (dtype) {
    case eByte:                                   return 1;
    case eLoHi:     case eHiLo:                   return 2;
    case eLoHiLoHi: case eHiLoHiLo:               return 4;
    case eFloatLoHi: case eFloatHiLo:             return 4;
    case eDoubleLoHi: case eDoubleHiLo:           return 8;
    default:                                      return 1;
    }
}

} // namespace

void bindProjectApi(sol::state &L, LuaEngine *engine)
{
    using namespace lua_ids;

    // 2.4.1  projectGetProperty(id, iOrgVer=0) → string
    L.set_function("projectGetProperty", [engine](int id, sol::optional<int> ver) -> std::string {
        return engine->callOnGui([&]() -> std::string {
            Project *p = active(engine);
            if (!p) { engine->setLastError("no project open"); return std::string(); }
            return toS(p->propertyById(id, ver ? *ver : 0));
        });
    });

    // 2.4.2  projectSetProperty(id, value) → bool
    L.set_function("projectSetProperty", [engine](int id, const std::string &value) -> bool {
        const QString qvalue = toQ(value);
        return engine->callOnGui([&]() -> bool {
            Project *p = active(engine);
            if (!p) { engine->setLastError("no project open"); return false; }
            bool ok = p->setPropertyById(id, qvalue);
            if (!ok) engine->setLastError(p->lastError());
            return ok;
        });
    });

    // 2.4.3  projectClose(bDeleteFile=false) → bool
    //
    // Iter 8.2 v2: delegates to MainWindow::luaCloseActiveProject(), which is
    // itself a thin wrapper over sw->close() — the actual model cleanup
    // (m_projects.removeAll, deleteLater, refreshProjectTree, etc.) happens
    // deferred via the existing eventFilter + finalizeClosedProject() path.
    // This avoids the access-violation race that bit Iter 8 v1 when both
    // the binding and eventFilter tried to mutate m_projects.
    L.set_function("projectClose", [engine](sol::optional<bool> del) -> bool {
        const bool deleteFile = del.value_or(false);
        return engine->callOnGui([&]() -> bool {
            if (!engine || !engine->mainWindow()) {
                if (engine) engine->setLastError("no MainWindow");
                return false;
            }
            if (!engine->mainWindow()->luaActiveProject()) {
                engine->setLastError("no project open");
                return false;
            }
            return engine->mainWindow()->luaCloseActiveProject(deleteFile);
        });
    });

    // 2.4.4  projectSave(newVersion, name="CreatedByLua", desc="") → bool
    //
    // Iter 7: honour newVersion + name args via Project::snapshotVersion.
    // Description arg ignored (ProjectVersion has no field — Sprint M).
    //
    // Iter 8.3: a project without filePath cannot be persisted.  Pre-Iter-8
    // we silently returned true after zeroing the modified flag, which made
    // EVC sample 06 "Import Server" appear to save when it did not.  Now we
    // return false + setLastError; scripts must route through
    // projectSetProperty(ePrjPropFilename, ...) first.
    L.set_function("projectSave", [engine](sol::optional<bool> newVer,
                                           sol::optional<std::string> name,
                                           sol::optional<std::string> desc) -> bool {
        const bool createVer = newVer.value_or(false);
        const QString vname = name ? QString::fromUtf8(name->c_str())
                                   : QStringLiteral("CreatedByLua");
        return engine->callOnGui([&]() -> bool {
            Project *p = active(engine);
            if (!p) { engine->setLastError("no project open"); return false; }
            if (createVer) {
                p->snapshotVersion(vname);
                (void)desc;
            }
            if (p->filePath.isEmpty()) {
                engine->setLastError("no file path; set ePrjPropFilename before projectSave()");
                return false;
            }
            return p->save();
        });
    });

    // 2.4.18  projectGetElementOffset → number
    L.set_function("projectGetElementOffset", []() -> int { return 0; });

    // 2.4.19  projectGetElement → number  (always eElementEprom)
    L.set_function("projectGetElement", []() -> int { return eElementEprom; });

    // 2.4.20  projectSetElement(id) → bool (no-op accept)
    L.set_function("projectSetElement", [](int) -> bool { return true; });

    // 2.4.21  projectSetElementRanges(s) → bool (no-op accept)
    L.set_function("projectSetElementRanges", [](const std::string &) -> bool { return true; });

    // 2.4.25  projectGetAt(addr, dtype=eByte, n=1) → number / table
    L.set_function("projectGetAt", [engine, &L](sol::variadic_args va) -> sol::object {
        qint64 addr = va.size() >= 1 ? qint64(va[0].as<double>()) : 0;
        int dtype = va.size() >= 2 ? va[1].as<int>() : eByte;
        int n     = va.size() >= 3 ? va[2].as<int>() : 1;
        QVector<double> values = engine->callOnGui([&]() -> QVector<double> {
            QVector<double> out;
            Project *p = active(engine);
            if (!p) {
                engine->setLastError("no project open");
                out.push_back(eEmptyvalue);
                return out;
            }
            const int count = qMax(1, n);
            out.reserve(count);
            const int step = datatypeSize(dtype);
            for (int i = 0; i < count; ++i)
                out.push_back(readAt(p, addr + qint64(i) * step, dtype));
            return out;
        });
        if (n <= 1) {
            return sol::make_object(L, values.isEmpty() ? double(eEmptyvalue) : values[0]);
        }
        sol::table arr = L.create_table();
        for (int i = 0; i < values.size(); ++i)
            arr[i + 1] = values[i];
        return arr;
    });

    // 2.4.26  projectSetAt(addr, value, dtype=eByte, mode=eSetAbsolute) → bool
    // Handles:
    //   numeric value with mode = absolute / relative (delta) / percent
    //   string value: "AA BB ?? DD" hex, "10M" decimal-suffix, eEmptyvalue → restore from original
    L.set_function("projectSetAt", [engine](sol::variadic_args va) -> bool {
        if (va.size() < 2) return false;
        qint64 addr = qint64(va[0].as<double>());
        int dtype = va.size() >= 3 ? va[2].as<int>() : eByte;
        int mode  = va.size() >= 4 ? va[3].as<int>() : eSetAbsolute;

        sol::object val = va[1];
        if (val.get_type() == sol::type::string) {
            // String form: hex bytes with ?? skip and 'M' decimal suffix.
            QString s = toQ(val.as<std::string>());
            return engine->callOnGui([&]() -> bool {
                Project *p = active(engine);
                if (!p) { engine->setLastError("no project open"); return false; }
                QStringList toks = s.split(QRegularExpression(QStringLiteral("[\\s,:]+")), Qt::SkipEmptyParts);
                qint64 cur = addr;
                const int step = datatypeSize(dtype);   // Iter 10.5: was hard-coded 1/2
                for (const QString &raw : toks) {
                    QString t = raw;
                    if (t.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) t = t.mid(2);
                    if (t == QStringLiteral("??")) { cur += step; continue; }
                    bool decM = t.endsWith(QLatin1Char('M'));
                    if (decM) t.chop(1);
                    bool ok = false;
                    qulonglong v = decM ? t.toULongLong(&ok, 10)
                                        : t.toULongLong(&ok, 16);
                    if (!ok) { cur += step; continue; }
                    writeAt(p, cur, double(v), dtype);
                    cur += step;
                }
                p->modified = true;
                emit p->dataChanged();
                return true;
            });
        }

        double v = val.as<double>();
        // Special sentinel — restore from originalData.
        // Iter 10.5: restore all bytes of the datatype cell, not just byte 0.
        return engine->callOnGui([&]() -> bool {
            Project *p = active(engine);
            if (!p) { engine->setLastError("no project open"); return false; }
            double out = v;
            if (qint64(out) == eEmptyvalue) {
                const int size = datatypeSize(dtype);
                if (addr < 0 || addr + size > p->originalData.size()) return false;
                for (int i = 0; i < size; ++i)
                    p->currentData[addr + i] = p->originalData.at(addr + i);
                p->modified = true;
                emit p->dataChanged();
                return true;
            }
            if (mode == eSetRelative) {
                out = readAt(p, addr, dtype) + out;
            } else if (mode == eSetPercent) {
                out = readAt(p, addr, dtype) * (1.0 + out / 100.0);
            }
            return writeAt(p, addr, out, dtype);
        });
    });

    // 2.4.27  projectSetOrg(start=nil, end=nil) → bool
    L.set_function("projectSetOrg", [engine](sol::optional<int> start, sol::optional<int> end_) -> bool {
        int s = start ? *start : 0;
        return engine->callOnGui([&]() -> bool {
            Project *p = active(engine);
            if (!p) { engine->setLastError("no project open"); return false; }
            int e = end_ ? *end_ : p->originalData.size();
            if (p->currentData.size() < p->originalData.size())
                p->currentData = p->originalData;
            if (s < 0) s = 0;
            if (e > p->currentData.size()) e = p->currentData.size();
            for (int i = s; i < e; ++i)
                p->currentData[i] = p->originalData.at(i);
            p->modified = true;
            emit p->dataChanged();
            return true;
        });
    });

    // 2.4.40  projectCountDifferentBytes(reallyDiff=false) → int
    // Default returns IDENTICAL bytes per manual.
    L.set_function("projectCountDifferentBytes", [engine](sol::optional<bool> really) -> qint64 {
        return engine->callOnGui([&]() -> qint64 {
            Project *p = active(engine);
            if (!p) { engine->setLastError("no project open"); return 0; }
            qint64 diff = 0;
            qint64 n = qMin(p->currentData.size(), p->originalData.size());
            for (qint64 i = 0; i < n; ++i) {
                if (p->currentData.at(i) != p->originalData.at(i)) ++diff;
            }
            if (really && *really) return diff;
            return n - diff;   // identical count (counterintuitive WinOLS default)
        });
    });

    // 2.4.43 / 2.4.44  projectAddrCpuToRaw / projectAddrRawToCpu → number
    // We're a flat 1:1 model, so the conversion is identity (success for any
    // in-range address, 0xFFFFFFFF for out-of-range).
    L.set_function("projectAddrCpuToRaw", [engine](qint64 a) -> qint64 {
        return engine->callOnGui([&]() -> qint64 {
            Project *p = active(engine);
            if (!p) return 0xFFFFFFFFLL;
            if (a < 0 || a >= p->currentData.size()) return 0xFFFFFFFFLL;
            return a;
        });
    });
    L.set_function("projectAddrRawToCpu", [engine](qint64 a) -> qint64 {
        return engine->callOnGui([&]() -> qint64 {
            Project *p = active(engine);
            if (!p) return 0xFFFFFFFFLL;
            if (a < 0 || a >= p->currentData.size()) return 0xFFFFFFFFLL;
            return a;
        });
    });

    // 2.4.22  projectAddCommentAt(from, to, text, frame=-1, back=-1) → bool
    L.set_function("projectAddCommentAt", [engine](sol::variadic_args va) -> bool {
        if (va.size() < 3) return false;
        qint64 from = qint64(va[0].as<double>());
        qint64 to   = qint64(va[1].as<double>());
        QString text = toQ(va[2].as<std::string>());
        return engine->callOnGui([&]() -> bool {
            Project *p = active(engine);
            if (!p) { engine->setLastError("no project open"); return false; }
            if (auto *a = p->annotations()) {
                a->add(from, text, /*length=*/to - from + 1);
                p->modified = true;
                return true;
            }
            return false;
        });
    });

    // 2.4.23  projectGetCommentAt(addr) → string
    L.set_function("projectGetCommentAt", [engine](qint64 addr) -> std::string {
        return engine->callOnGui([&]() -> std::string {
            Project *p = active(engine);
            if (!p) return std::string();
            if (auto *a = p->annotations()) {
                const auto hits = a->at(addr);
                if (!hits.isEmpty()) return toS(hits.first().text);
            }
            return std::string();
        });
    });

    // 2.4.24  projectDelCommentAt(addr) → bool
    L.set_function("projectDelCommentAt", [engine](qint64 addr) -> bool {
        return engine->callOnGui([&]() -> bool {
            Project *p = active(engine);
            if (!p) return false;
            if (auto *a = p->annotations()) {
                bool ok = a->removeAt(addr);
                if (ok) p->modified = true;
                return ok;
            }
            return false;
        });
    });
}

} // namespace lua
