/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "Med17CustomerBlock.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace Checksum::MED17 {
namespace {

struct RomView {
    uint8_t* data = nullptr;
    size_t size = 0;
};

uint16_t rdWord(const RomView& r, uint32_t off)
{
    if (!r.data || off + 2 > r.size)
        return 0;
    return uint16_t(r.data[off]) | (uint16_t(r.data[off + 1]) << 8);
}

uint32_t rdDword(const RomView& r, uint32_t off)
{
    if (!r.data || off + 4 > r.size)
        return 0;
    return uint32_t(r.data[off]) | (uint32_t(r.data[off + 1]) << 8)
        | (uint32_t(r.data[off + 2]) << 16) | (uint32_t(r.data[off + 3]) << 24);
}

void wrDword(const RomView& r, uint32_t off, uint32_t v)
{
    if (!r.data || off + 4 > r.size)
        return;
    r.data[off] = uint8_t(v);
    r.data[off + 1] = uint8_t(v >> 8);
    r.data[off + 2] = uint8_t(v >> 16);
    r.data[off + 3] = uint8_t(v >> 24);
}

int findBlockByType(const std::vector<Descriptor>& descriptors, uint16_t type)
{
    for (size_t i = 0; i < descriptors.size(); ++i)
        if (descriptors[i].type == type)
            return static_cast<int>(i);
    return -1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Record field readers (FUN_10030xxx cluster).  Each reads a word/dword at the
// cursor and, on a magic match, advances it and returns the field (0..15);
// otherwise returns 0xff (word/dword readers) or 0 (boolean readers).
// ─────────────────────────────────────────────────────────────────────────────

// FUN_100305b0 — record tag (word, low byte 0x82).
int readTag(const RomView& r, uint32_t& off)
{
    const uint16_t w = rdWord(r, off);
    if (uint8_t(w) == 0x82) {
        off += 2;
        return (w >> 8) & 0x0f;
    }
    return 0xff;
}

// FUN_10030e30 — offset computation from a dword whose low byte is 0x1d.
int readOffset1d(const RomView& r, uint32_t& off)
{
    const uint32_t d = rdDword(r, off);
    if (uint8_t(d) != 0x1d)
        return -1;
    const uint32_t base = off;
    const uint32_t u3 = (d & 0xffffff00) << 8;
    if ((u3 & 0x800000) == 0) {
        off = base + 4;
        return base + ((u3 & 0xffffff) | (d >> 16)) * 2;
    }
    off = base + 4;
    return base + (-((u3 | (d >> 16)) & 0xffffff)) * -2;
}

// FUN_100305f0 — dword, (d & 0xfffffff) == (p << 8 | 0x20208f).
int readField_305f0(const RomView& r, uint32_t& off, uint8_t p)
{
    const uint32_t d = rdDword(r, off);
    if ((d & 0xfffffff) == ((uint32_t(p) << 8) | 0x20208f)) {
        off += 4;
        return d >> 28;
    }
    return 0xff;
}

// FUN_100307d0 — word, (w & 0xf0ff) == ((p3 << 6 | p4) << 6 | 0x10).
int readField_307d0(const RomView& r, uint32_t& off, uint8_t p3, uint8_t p4)
{
    const uint16_t w = rdWord(r, off);
    if ((w & 0xf0ff) == ((((uint32_t(p3) << 6) | p4) << 6) | 0x10)) {
        off += 2;
        return (w >> 8) & 0x0f;
    }
    return 0xff;
}

// FUN_10030640 — dword, (d & 0xfffff0ff) == (p << 12 | 0x400d9).
int readField_30640(const RomView& r, uint32_t& off, uint8_t p)
{
    const uint32_t d = rdDword(r, off);
    if ((d & 0xfffff0ff) == ((uint32_t(p) << 12) | 0x400d9)) {
        off += 4;
        return (d >> 8) & 0xffff0f;
    }
    return 0xff;
}

// FUN_10030690 — dword, (d & 0xfffffff) == ((p5 << 4 | p3) << 4 | p4) << 8 | 0x6000001.
int readField_30690(const RomView& r, uint32_t& off, uint8_t p3, uint8_t p4, uint8_t p5)
{
    const uint32_t d = rdDword(r, off);
    if ((d & 0xfffffff) == ((((uint32_t(p5) << 4 | p3) << 4 | p4) << 8) | 0x6000001)) {
        off += 4;
        return d >> 28;
    }
    return 0xff;
}

// FUN_100306f0 — word, (w & 0xffff) == (p << 8 | 0x16).
bool readField_306f0(const RomView& r, uint32_t& off, uint8_t p)
{
    const uint16_t w = rdWord(r, off);
    if ((w & 0xffff) == ((uint32_t(p) << 8) | 0x16)) {
        off += 2;
        return true;
    }
    return false;
}

// FUN_10030780 — word, (w & 0xf0ff) == ((p & 0xfc) << 10 | 0x48).
int readField_30780(const RomView& r, uint32_t& off, uint8_t p)
{
    const uint16_t w = rdWord(r, off);
    if ((w & 0xf0ff) == (((uint32_t(p) & 0xfc) << 10) | 0x48)) {
        off += 2;
        return (w >> 8) & 0x0f;
    }
    return 0xff;
}

// FUN_10030ea0 — dword, (d & 0xfffffff) == (((p5 << 7 | p6) << 4 | p4) << 4 | p3) << 8 | 0x37.
int readField_30ea0(const RomView& r, uint32_t& off, uint8_t p3, uint8_t p4, uint8_t p5, uint8_t p6)
{
    const uint32_t d = rdDword(r, off);
    if ((d & 0xfffffff)
        == ((((((uint32_t(p5) << 7) | p6) << 4 | p4) << 4 | p3) << 8) | 0x37)) {
        off += 4;
        return d >> 28;
    }
    return 0xff;
}

// FUN_10030830 — dword, (d & 0xfffffff) == (p << 8 | 0x680037).
int readField_30830(const RomView& r, uint32_t& off, uint8_t p)
{
    const uint32_t d = rdDword(r, off);
    if ((d & 0xfffffff) == ((uint32_t(p) << 8) | 0x680037)) {
        off += 4;
        return d >> 28;
    }
    return 0xff;
}

// FUN_10030880 — word, (w & 0xf0ff) == (p << 12 | 0xc2).
int readField_30880(const RomView& r, uint32_t& off, uint8_t p)
{
    const uint16_t w = rdWord(r, off);
    if ((w & 0xf0ff) == ((uint32_t(p) << 12) | 0xc2)) {
        off += 2;
        return (w >> 8) & 0x0f;
    }
    return 0xff;
}

// FUN_100309c0 — word, (w & 0xf0ff) == 0x8006.
int readField_309c0(const RomView& r, uint32_t& off)
{
    const uint16_t w = rdWord(r, off);
    if ((w & 0xf0ff) == 0x8006) {
        off += 2;
        return (w >> 8) & 0x0f;
    }
    return 0xff;
}

// FUN_10030a10 — word, (w & 0xffff) == (p << 12 | 0xf08) || (p << 8 | 0xf00c).
bool readField_30a10(const RomView& r, uint32_t& off, uint8_t p)
{
    const uint16_t w = rdWord(r, off);
    if ((w & 0xffff) == ((uint32_t(p) << 12 | 0xf08))
        || (w & 0xffff) == ((uint32_t(p) << 8 | 0xf00c))) {
        off += 2;
        return true;
    }
    return false;
}

// FUN_10030af0 — word, (w & 0xffff) == ((p4 << 4 | p3) << 8 | 0x3a).
bool readField_30af0(const RomView& r, uint32_t& off, uint8_t p3, uint8_t p4)
{
    const uint16_t w = rdWord(r, off);
    if ((w & 0xffff) == (((uint32_t(p4) << 4 | p3) << 8) | 0x3a)) {
        off += 2;
        return true;
    }
    return false;
}

// FUN_10030b40 — word, (w & 0xf0ff) == (p << 12 | 0xea).
int readField_30b40(const RomView& r, uint32_t& off, uint8_t p)
{
    const uint16_t w = rdWord(r, off);
    if ((w & 0xf0ff) == ((uint32_t(p) << 12) | 0xea)) {
        off += 2;
        return (w >> 8) & 0x0f;
    }
    return 0xff;
}

// FUN_10030b90 — dword, (d & 0xfffffff) == ((p4 << 4 | p3) << 8 | 0x220008b).
int readField_30b90(const RomView& r, uint32_t& off, uint8_t p3, uint8_t p4)
{
    const uint32_t d = rdDword(r, off);
    if ((d & 0xfffffff) == (((uint32_t(p4) << 4 | p3) << 8) | 0x220008b)) {
        off += 4;
        return d >> 28;
    }
    return 0xff;
}

// FUN_10030bf0 — dword, (d & 0xfffffff) == ((p4 << 4 | p3) << 8 | 0x200008b).
int readField_30bf0(const RomView& r, uint32_t& off, uint8_t p3, uint8_t p4)
{
    const uint32_t d = rdDword(r, off);
    if ((d & 0xfffffff) == (((uint32_t(p4) << 4 | p3) << 8) | 0x200008b)) {
        off += 4;
        return d >> 28;
    }
    return 0xff;
}

// FUN_10030730 — word, (w & 0xf0ff) == (p << 12 | 0x26).
int readField_30730(const RomView& r, uint32_t& off, uint8_t p)
{
    const uint16_t w = rdWord(r, off);
    if ((w & 0xf0ff) == ((uint32_t(p) << 12) | 0x26)) {
        off += 2;
        return (w >> 8) & 0x0f;
    }
    return 0xff;
}

// FUN_10030d60 — word, low byte 0xee -> jump computation.
int readJumpEe(const RomView& r, uint32_t& off)
{
    const uint16_t w = rdWord(r, off);
    if (uint8_t(w) != 0xee)
        return 0xff;
    const uint32_t base = off;
    if ((uint8_t(w >> 8) & 0x80) == 0x80) {
        off = base + 2;
        return base + (-(w >> 8) & 0xff) * -2;
    }
    off = base + 2;
    return base + (w >> 8) * 2;
}

// FUN_10030d10 — word, (w & 0xf0ff) == (p << 12 | 2).
int readField_30d10(const RomView& r, uint32_t& off, uint8_t p)
{
    const uint16_t w = rdWord(r, off);
    if ((w & 0xf0ff) == ((uint32_t(p) << 12) | 2)) {
        off += 2;
        return (w >> 8) & 0x0f;
    }
    return 0xff;
}

// FUN_100308d0 — word, (w & 0xffff) == ((p4 << 4 | p3) << 8 | 0x9a).
bool readField_308d0(const RomView& r, uint32_t& off, uint8_t p3, uint8_t p4)
{
    const uint16_t w = rdWord(r, off);
    if ((w & 0xffff) == (((uint32_t(p4) << 4 | p3) << 8) | 0x9a)) {
        off += 2;
        return true;
    }
    return false;
}

// FUN_10030a70 — word, two cases.
int readField_30a70(const RomView& r, uint32_t& off, uint8_t p)
{
    const uint16_t w = rdWord(r, off);
    if ((w & 0xf0ff) == ((uint32_t(p) << 12) | 8)) {
        off += 2;
        return (w >> 8) & 0x0f;
    }
    if ((w & 0xfff) == ((uint32_t(p) << 8) | 0xc) && (w & 0xf000) == 0xf000) {
        off += 2;
        return 0xf;
    }
    return 0xff;
}

// FUN_10030c50 — dword, (d & 0xfffffff) == ((p4 << 4 | p3) << 8 | 0x100000b).
int readField_30c50(const RomView& r, uint32_t& off, uint8_t p3, uint8_t p4)
{
    const uint32_t d = rdDword(r, off);
    if ((d & 0xfffffff) == (((uint32_t(p4) << 4 | p3) << 8) | 0x100000b)) {
        off += 4;
        return d >> 28;
    }
    return 0xff;
}

// FUN_10030cb0 — dword, (d & 0xfffffff) == ((p3 << 12 | p5) << 4 | p4) << 8 | 0x8000ab.
int readField_30cb0(const RomView& r, uint32_t& off, uint8_t p3, uint8_t p4, uint8_t p5)
{
    const uint32_t d = rdDword(r, off);
    if ((d & 0xfffffff) == ((((uint32_t(p3) << 12 | p5) << 4 | p4) << 8) | 0x8000ab)) {
        off += 4;
        return d >> 28;
    }
    return 0xff;
}

// FUN_10030dd0 — dword, (d & 0xfffffff) == ((p4 << 4 | p3) << 8 | 0x8b).
int readField_30dd0(const RomView& r, uint32_t& off, uint8_t p3, uint8_t p4)
{
    const uint32_t d = rdDword(r, off);
    if ((d & 0xfffffff) == (((uint32_t(p4) << 4 | p3) << 8) | 0x8b)) {
        off += 4;
        return d >> 28;
    }
    return 0xff;
}

// FUN_10030920 — word, (w & 0xffff) == ((p4 << 4 | p3) << 8 | 0x1a).
bool readField_30920(const RomView& r, uint32_t& off, uint8_t p3, uint8_t p4)
{
    const uint16_t w = rdWord(r, off);
    if ((w & 0xffff) == (((uint32_t(p4) << 4 | p3) << 8) | 0x1a)) {
        off += 2;
        return true;
    }
    return false;
}

// FUN_10030970 — word, (w & 0xfff) == (p << 8 | 0x1a) -> (w >> 12).
int readField_30970(const RomView& r, uint32_t& off, uint8_t p)
{
    const uint16_t w = rdWord(r, off);
    if ((w & 0xfff) == ((uint32_t(p) << 8) | 0x1a)) {
        off += 2;
        return (w & 0xffff) >> 12;
    }
    return 0xff;
}

} // namespace

void processCustomerBlock(QByteArray& rom, const std::vector<Descriptor>& descriptors,
                          bool correct, int* flagOut)
{
    RomView view{reinterpret_cast<uint8_t*>(rom.data()), static_cast<size_t>(rom.size())};
    const int b = findBlockByType(descriptors, 0x30);
    if (b == -1) {
        if (flagOut)
            *flagOut = 0;
        return;
    }
    const Descriptor& d = descriptors[b];
    const uint32_t start = d.headerOffset;
    const uint32_t end = rdDword(view, start + 4) + start;

    // All locals mirror the decompiler's single-scope layout.
    uint32_t cur = 0;        // local_5c
    int local58 = 0, local54 = 0, local4c = 0, local48 = 0, local38 = 0, local30 = 0;
    int local44 = 0, local24 = 0, local1c = 0;
    int local34 = 0;
    uint32_t savedCur = 0;   // local_40
    uint32_t jumpOffset = 0; // local_2c
    int local50 = 0;
    bool bVar2 = false, bVar15 = false;
    int uVar8 = 0, uVar9 = 0, uVar13 = 0, uVar17 = 0, bVar3 = 0, bVar6 = 0;
    int cVar4 = 0, cVar5 = 0;
    int bVar16 = 0;

    uint32_t scanOffset = start;   // local_28

    if (start < end) {
        while (true) {
            bVar15 = false;
            cur = scanOffset;
            local44 = readTag(view, cur);
            if (local44 != 0xff) {
                local54 = readTag(view, cur);
                if (local54 != 0xff) {
                    bVar2 = false;
                    savedCur = cur;
                    local50 = 0;
                    jumpOffset = readOffset1d(view, cur);
                    if (jumpOffset == 0xffffffff) {
                        bVar6 = readField_305f0(view, cur, uint8_t(local54));
                        local4c = bVar6;
                        if (bVar6 == 0xff || bVar6 != 0xf) {
                            cVar4 = readTag(view, cur);
                            local4c = cVar4;
                            if (cVar4 == 0xff || cVar4 != 0x0f)
                                goto nextRecord;
                            bVar15 = true;
                            bVar6 = 0xf;
                        }
                    via31023:
                        local58 = readTag(view, cur);
                        if (local58 != 0xff) {
                            uVar8 = readField_307d0(view, cur, 4, 0);
                            if (uVar8 == 0x0f)
                                goto via3105f;
                        }
                        if ((bVar2 && (bVar3 = readField_30690(view, cur, 0xf, uint8_t(local54), 2),
                                      bVar3 == 0xf))
                            || (!bVar15 && (uVar8 = readField_30640(view, cur, 4), uVar8 != 0xff))) {
                            local34 = 1;
                            if (bVar2) {
                                local4c = 0xf;
                            } else {
                                bVar6 = readField_30690(view, cur, 0xf, uint8_t(local54), 2);
                                if (bVar6 == 0xff || bVar6 != 0xf)
                                    goto nextRecord;
                            }
                            local58 = readTag(view, cur);
                            if (local58 == 0xff || !readField_306f0(view, cur, 0xff))
                                goto nextRecord;
                            cVar4 = readField_30780(view, cur, 0);
                            local38 = cVar4;
                        joined:
                            if (cVar4 != 0xff) {
                                cVar5 = readField_307d0(view, cur, 5, 0);
                                local1c = cVar5;
                                goto validate;
                            }
                        } else {
                            if (bVar2 && (cur += 2, cur != 0)) {
                                local58 = readTag(view, cur);
                                if (local58 == 0xff)
                                    goto via31202;
                            } else {
                            via31202:
                                if (!bVar15 || (uVar8 = readField_30640(view, cur, 4), uVar8 == 0xff))
                                    goto nextRecord;
                            }
                            local34 = 1;
                            if (bVar2) {
                                bVar6 = 0xf;
                                local4c = 0xf;
                            via31277:
                                bVar3 = readField_30ea0(view, cur, uint8_t(local4c),
                                                       uint8_t(local54), 2, 6);
                                if (bVar3 == bVar6) {
                                    cVar4 = readField_30780(view, cur, 0);
                                    local38 = cVar4;
                                    goto joined;
                                }
                            } else {
                                bVar3 = readField_30690(view, cur, 0xf, uint8_t(local54), 2);
                                if (bVar3 != 0xff && bVar3 == 0xf) {
                                    local58 = readTag(view, cur);
                                    if (local58 != 0xff)
                                        goto via31277;
                                }
                            }
                        }
                    } else {
                        cur += 4;
                        bVar2 = true;
                        bVar6 = 0xf;
                        local50 = 1;
                        local4c = 0xf;
                        bVar3 = readField_30830(view, cur, 0xf);
                        local24 = bVar3;
                        if (bVar3 == 0xff)
                            goto via31023;
                    via3105f:
                        local34 = 2;
                        if (bVar2) {
                            local4c = 0xf;
                        } else {
                            bVar6 = readField_30830(view, cur, uint8_t(local4c));
                            local24 = bVar6;
                            if (bVar6 == 0xff)
                                goto nextRecord;
                        }
                        cVar5 = readField_30780(view, cur, 4);
                        local38 = cVar5;
                        cVar4 = cVar5;
                    validate:
                        uVar8 = int(cur);
                        if (cVar5 != 0xff) {
                            if (local34 == 1) {
                                bVar6 = readField_30690(view, cur, uint8_t(local1c),
                                                        uint8_t(local58), 0);
                                if (bVar6 != 0xff && bVar6 == 0xf) {
                                    bVar6 = readField_30830(view, cur, uint8_t(local38));
                                    local30 = bVar6;
                                    if (bVar6 != 0xff) {
                                        uVar9 = readField_30880(view, cur, 1);
                                        if (uVar9 == local58 && readField_309c0(view, cur) == cVar4
                                            && readField_30a10(view, cur, 0)
                                            && readField_30af0(view, cur, uint8_t(local4c),
                                                              uint8_t(local30))) {
                                            bVar6 = readField_30830(view, cur, uint8_t(local58));
                                            if (bVar6 == local58) {
                                                uVar9 = readField_30b40(view, cur, 4);
                                                bVar15 = (uVar9 == local44);
                                            via3152c:
                                                if (bVar15) {
                                                via31532:
                                                    bVar6 = readField_30b90(view, cur,
                                                                            uint8_t(local58), 4);
                                                    if (bVar6 == local4c) {
                                                        bVar6 = readField_30bf0(view, cur,
                                                                                uint8_t(local44), 0);
                                                        if (bVar6 == local30) {
                                                            uVar13 = local30;
                                                            uVar9 = readField_30730(view, cur,
                                                                                    uint8_t(uVar13));
                                                            if (uVar9 != 0xff && uVar9 == 0x0f
                                                                && readJumpEe(view, cur) == uVar8) {
                                                                if (local34 == 1) {
                                                                    uVar8 = readField_30880(view,
                                                                                            cur, 1);
                                                                    if (uVar8 == local54) {
                                                                        uVar17 = local54;
                                                                    via31625:
                                                                        bVar6 = readField_30830(
                                                                            view, cur,
                                                                            uint8_t(uVar17));
                                                                        if (bVar6 == local54
                                                                            && readField_30b90(
                                                                                   view, cur,
                                                                                   uint8_t(local54),
                                                                                   4)
                                                                                   == 0xf) {
                                                                            uVar8 =
                                                                                readField_30730(
                                                                                    view, cur,
                                                                                    uint8_t(uVar13));
                                                                            if (uVar8 != 0xff
                                                                                && uVar8 == 0x0f
                                                                                && readJumpEe(
                                                                                       view, cur)
                                                                                       == int(savedCur)) {
                                                                                if (local50 == 0)
                                                                                    jumpOffset = cur;
                                                                                else if (jumpOffset
                                                                                         != cur)
                                                                                    goto nextRecord;
                                                                                uVar8 =
                                                                                    readField_30d10(
                                                                                        view, cur,
                                                                                        uint8_t(local44));
                                                                                if ((uVar8 == 0x02
                                                                                     || uint16_t(rdWord(view, cur))
                                                                                            == 0x9000)
                                                                                    && (jumpOffset
                                                                                            - savedCur
                                                                                        == 0x44
                                                                                        || jumpOffset
                                                                                               - savedCur
                                                                                           == 0x3e)) {
                                                                                    goto matchFound;
                                                                                }
                                                                            }
                                                                        }
                                                                    }
                                                                } else if (local34 == 2) {
                                                                    bVar3 = local54;
                                                                    bVar6 = readField_30dd0(
                                                                        view, cur, uint8_t(bVar3), 1);
                                                                    if ((bVar6 == local48
                                                                         || local48 != 0xf)
                                                                        || readField_308d0(
                                                                               view, cur,
                                                                               uint8_t(bVar3), 1)) {
                                                                        uVar17 = local48;
                                                                        goto via31625;
                                                                    }
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            } else if (local34 == 2) {
                                if (local50 == 0) {
                                    bVar15 = readField_30920(view, cur, uint8_t(local24),
                                                            uint8_t(local58));
                                } else {
                                    uVar9 = readField_30970(view, cur, uint8_t(local24));
                                    local58 = uVar9;
                                    bVar15 = (uVar9 == 0xff);
                                }
                                if (!bVar15) {
                                    bVar6 = readField_30830(view, cur, uint8_t(local38));
                                    local30 = bVar6;
                                    if (bVar6 != 0xff) {
                                        uVar9 = readField_307d0(view, cur, 5, 0);
                                        if (uVar9 != 0xff && uVar9 == 0x0f) {
                                            uVar9 = readField_30880(view, cur, 1);
                                            if (uVar9 == local58
                                                && readField_309c0(view, cur) == cVar4) {
                                                uVar13 = local30;
                                                bVar6 = readField_30a70(view, cur, 0);
                                                local48 = bVar6;
                                                if (bVar6 != 0xff) {
                                                    bVar16 = local30;
                                                    bVar3 = readField_30c50(view, cur,
                                                                            uint8_t(bVar6),
                                                                            uint8_t(bVar16));
                                                    if (bVar3 == bVar6
                                                        || readField_30af0(view, cur,
                                                                           uint8_t(local48),
                                                                           uint8_t(bVar16))) {
                                                        bVar6 = readField_30830(view, cur,
                                                                                uint8_t(local58));
                                                        if (bVar6 == local58) {
                                                            bVar3 = readField_30cb0(
                                                                view, cur, uint8_t(local48),
                                                                uint8_t(local44), 4);
                                                            bVar6 = local44;
                                                            if (bVar3 != local44) {
                                                                uVar9 = readField_30b40(view, cur,
                                                                                        4);
                                                                bVar15 = (uVar9 == bVar6);
                                                                goto via3152c;
                                                            }
                                                            goto via31532;
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        goto nextRecord;
                    }
                }
            }
        nextRecord:
            scanOffset += 2;
            if (scanOffset >= end)
                break;
        }
    }

matchFound:
    if (flagOut)
        *flagOut = local50;
    if (!correct)
        return;

    // Correct mode: patch the block's descriptor magic.
    if (jumpOffset - savedCur == 0x44) {
        wrDword(view, savedCur, 0x22001d);
        wrDword(view, savedCur + 4, 0xa2cd0ed1);
        static const uint8_t magic[5] = {0x23, 0x42, 0x54, 0x4c, 0x23};
        uint32_t found = 0xffffffff;
        for (uint32_t i = 0xfd00; i + 5 <= 0xff00 && i + 5 <= view.size; ++i) {
            if (std::memcmp(view.data + i, magic, 5) == 0) {
                found = i;
                break;
            }
        }
        if (found == 0xffffffff) {
            if (flagOut)
                *flagOut = 1;
            return;
        }
        const uint32_t uVar14 = uint8_t(view.data[found + 0x188]);
        static const uint8_t dmb2[4] = {0x44, 0x4d, 0x42, 0x32};
        if (std::memcmp(view.data + found + uVar14 + 0x185, dmb2, 4) != 0)
            wrDword(view, found + uVar14 + 0x18a, 0x32424d44);
        if (found + 0x188 < view.size)
            view.data[found + 0x188] += 5;
    } else if (jumpOffset - savedCur == 0x3e) {
        wrDword(view, savedCur, 0x1f001d);
        wrDword(view, savedCur + 4, 0x827e1eb1);
    }
    if (flagOut)
        *flagOut = 1;
}

} // namespace Checksum::MED17
