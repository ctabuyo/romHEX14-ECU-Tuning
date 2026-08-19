/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "Med17Variant.h"
#include "Med17BlockChecksum.h"
#include "Med17CustomerBlock.h"

#include <cstdint>
#include <cstring>

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

uint32_t rdDword(const uint8_t* data, uint32_t off)
{
    return uint32_t(data[off]) | (uint32_t(data[off + 1]) << 8) | (uint32_t(data[off + 2]) << 16)
        | (uint32_t(data[off + 3]) << 24);
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

void wrWord(const RomView& r, uint32_t off, uint16_t v)
{
    if (!r.data || off + 2 > r.size)
        return;
    r.data[off] = uint8_t(v);
    r.data[off + 1] = uint8_t(v >> 8);
}

void wrByte(const RomView& r, uint32_t off, uint8_t v)
{
    if (!r.data || off >= r.size)
        return;
    r.data[off] = v;
}

// FUN_10040b20 / FUN_10040d90 — byte-swap 16 / 32.
uint16_t byteSwap16(uint16_t x)
{
    return uint16_t((x >> 8) | (x << 8));
}

uint32_t byteSwap32(uint32_t x)
{
    return (uint32_t(byteSwap16(uint16_t(x & 0xffff))) << 16)
        | uint32_t(byteSwap16(uint16_t(x >> 16)));
}

// FUN_100391f0 — ReadRomWordBigEndian.
uint16_t readRomWordBigEndian(const RomView& r, uint32_t off)
{
    if (!r.data || off + 2 > r.size)
        return 0;
    return uint16_t(r.data[off]) << 8 | uint16_t(r.data[off + 1]);
}

uint16_t readRomWordBigEndian(const uint8_t* data, uint32_t off)
{
    return uint16_t(data[off]) << 8 | uint16_t(data[off + 1]);
}

// FUN_1002d4a0 — TranslateFlashAddress (single-region form used by 0x82).
uint32_t translateFlashAddress(uint32_t addr, size_t size)
{
    if ((addr & 0xff000000) == 0xa0000000)
        addr += 0xe0000000;
    if (addr >= 0x80000000 && addr < size + 0x7fffffff)
        return addr - 0x80000000;
    return 0xffffffff;
}

// FUN_10001e20 — exact bounded substring search.
bool findPattern(const uint8_t* data, size_t size, size_t start, size_t end,
                 const uint8_t* needle, size_t needleLen)
{
    if (!data || start > end || end > size || needleLen == 0)
        return false;
    if (needleLen > end - start)
        return false;
    for (size_t i = start; i + needleLen <= end; ++i)
        if (std::memcmp(data + i, needle, needleLen) == 0)
            return true;
    return false;
}

// FUN_10038420 — bounded search with a wildcard byte.
bool findPatternMasked(const uint8_t* data, size_t size, size_t start, size_t end,
                       const uint8_t* pat, size_t patLen, uint16_t wild)
{
    if (!data || start > end || end > size || patLen == 0)
        return false;
    for (size_t i = start; i + patLen <= end; ++i) {
        size_t k = 0;
        while (k < patLen) {
            const uint8_t b = data[i + k];
            if (wild < 0x100) {
                if (b != pat[k] && wild != pat[k])
                    break;
            } else if (b != pat[k]) {
                break;
            }
            ++k;
        }
        if (k == patLen)
            return true;
    }
    return false;
}

bool contains(const uint8_t* data, size_t size, const char* s)
{
    return findPattern(data, size, 0, size, reinterpret_cast<const uint8_t*>(s), std::strlen(s));
}

// FUN_10026020 — IsEdc17Cv41Tail.
bool isEdc17Cv41Tail(const uint8_t* data)
{
    uint32_t u1 = rdDword(data, 0x3c) - rdDword(data, 0x38);
    if (u1 < 0x87 || 0x40000 < u1) {
        const uint32_t u2 = rdDword(data, 0x1c0038);
        u1 = rdDword(data, 0x1c003c) + (0x1c0000 - u2);
        if (0x3ff79 < u1 - 0x1c0087)
            return false;
    }
    return rdDword(data, u1 - 0x87) == 0xafafafaf;
}

// FUN_10038510 — DetectEdc17C49.
bool detectEdc17C49(const uint8_t* data, size_t size)
{
    static const uint8_t head[0x1c] = {
        0x00, 0x00, 0x00, 0x00, 0x3f, 0x3f, 0x28, 0x80, 0x3f, 0x3f, 0x3f, 0x80,
        0x3f, 0x3f, 0x3f, 0x80, 0x3f, 0x3f, 0x3f, 0x80, 0x3f, 0x3f, 0x2f, 0x80,
        0xff, 0x8f, 0x37, 0x80,
    };
    static const char tailStr[] = "EDC17 BOSCH P8_26_EDC17_671.hex";  // 0x1f
    static const uint8_t pat30[0x2c] = {
        0x00, 0x00, 0x3f, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x01, 0x3f, 0x01, 0x00,
        0x3f, 0x00, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x20, 0x80,
        0x3f, 0x3f, 0x3f, 0x80, 0x3f, 0x3f, 0x3f, 0x80, 0x3f, 0x3f, 0x3f, 0x80,
        0x3f, 0x3f, 0x3f, 0x80, 0xff, 0xff, 0x2e, 0x80,
    };
    static const char edc17c49[] = "EDC17C49";

    if (findPatternMasked(data, size, 0x54000, 0x55000,
                          reinterpret_cast<const uint8_t*>(tailStr), 0x1f, 0x100)) {
        if (findPatternMasked(data, size, 0x200000, 0x200100,
                              reinterpret_cast<const uint8_t*>(edc17c49), 8, 0x100)
            && findPatternMasked(data, size, 0x20c000, 0x20d000, pat30, 0x2c, 0x3f))
            return true;
    }
    if (findPatternMasked(data, size, 0x280000, 0x2f0000,
                          reinterpret_cast<const uint8_t*>(edc17c49), 8, 0x100)
        && findPatternMasked(data, size, 0x280000, 0x290000, head, 0x1c, 0x3f))
        return true;
    return false;
}

// FUN_100382e0 — DetectEdc17Cv41Layout.
bool detectEdc17Cv41LayoutImpl(const uint8_t* data, uint32_t start)
{
    if (start < 0x300001) {
        for (uint32_t off = start; off < 0x300001; ++off) {
            if (rdDword(data, off + 8 + 0x1c) == 0x802effff) {
                if (off + 0x10 < 0x300001)
                    return true;
                return false;
            }
            const uint32_t v0 = rdDword(data, off);
            if (v0 == 0x10001 || v0 == 0x100) {
                const uint32_t v1 = rdDword(data, off + 4);
                if (v1 == 1 || v1 == 0x1000000) {
                    const uint32_t v2 = rdDword(data, off + 8);
                    if (v2 == 0x10000 || v2 == 0x1010100 || v2 == 0x1010101 || v2 == 0x10100
                        || v2 == 0x100 || v2 == 0x10101 || v2 == 0x1010000) {
                        if (rdDword(data, off + 8 + 0x1c) == 0x8032ffff) {
                            if (off + 0x10 < 0x300001)
                                return true;
                            return false;
                        }
                    }
                }
            }
        }
    }
    return false;
}

// DetectBmsMe172 / DetectEdc17Cp02 / DetectEdc17C06 — dataset-block string scans.
void detectSubVariants(const QByteArray& rom, const std::vector<Descriptor>& descriptors,
                       VariantFlags& flags)
{
    const auto* data = reinterpret_cast<const uint8_t*>(rom.constData());
    const size_t size = static_cast<size_t>(rom.size());
    int dsIdx = -1;
    for (size_t i = 0; i < descriptors.size(); ++i)
        if (descriptors[i].type == 0x60) {
            dsIdx = static_cast<int>(i);
            break;
        }
    if (dsIdx == -1)
        return;
    const Descriptor& ds = descriptors[dsIdx];
    const uint32_t blockStart = ds.headerOffset;
    const uint32_t blockEnd = rdDword(data, blockStart + 4) + blockStart;
    if (findPattern(data, size, blockStart, blockEnd,
                    reinterpret_cast<const uint8_t*>("/BMS_X_ME172/"), 13)
        || findPattern(data, size, blockStart, blockEnd,
                       reinterpret_cast<const uint8_t*>("/EDC17_CP02/"), 12)
        || findPattern(data, size, blockStart, blockEnd,
                       reinterpret_cast<const uint8_t*>("/EDC17_C06/"), 11)) {
        // Sub-variant detection records; the flags they set are consumed by the
        // customer-block/CP paths, so mark them via f582f4 for now.
        flags.f582f4 = true;
    }
}

void detectMed17EcuLayout(const QByteArray& rom, VariantFlags& flags)
{
    const auto* data = reinterpret_cast<const uint8_t*>(rom.constData());
    const size_t size = static_cast<size_t>(rom.size());
    if (contains(data, size, "MED17/22/MEDECM")
        || contains(data, size, "MED17/22/CYCLONEME1788"))
        flags.med1722 = true;
    if (contains(data, size, "EDC17CV41"))
        flags.edc17cv41 = true;
    if (contains(data, size, "EDC17CV44"))
        flags.edc17cv44 = true;
}

// FUN_1003c770 — DetectVariantFromKey.
void detectVariantFromKey(const QByteArray& rom, const std::vector<Descriptor>& descriptors,
                          VariantFlags& flags)
{
    const auto* data = reinterpret_cast<const uint8_t*>(rom.constData());
    const size_t size = static_cast<size_t>(rom.size());

    for (const auto& d : descriptors) {
        if (d.type == 0x60)
            flags.f582f8 = true;
        if (d.type == 0x70)
            flags.f582fc = true;
        if (d.type == 0x80)
            flags.f58300 = true;

        const uint32_t key = d.signatureKeyIndex;
        switch (key) {
        case 0:
        case 3:
        case 0xb:
        case 0x1a:
        case 0x6a:
            flags.hasChecksums = true;
            break;
        case 1:
            flags.f58210 = true;
            break;
        case 2:
            if (findPattern(data, size, 0x283000, 0x290000,
                            reinterpret_cast<const uint8_t*>("EDC17C64"), 8))
                flags.f582b0 = true;
            else if (findPattern(data, size, 0x20000, 0x21000,
                                 reinterpret_cast<const uint8_t*>("EDC17_CP20"), 10))
                flags.f58294 = true;
            else if (findPattern(data, size, 0x300000, 0x310000,
                                 reinterpret_cast<const uint8_t*>("EDC17C74"), 8))
                flags.f58298 = true;
            else if (findPattern(data, size, 0x20000, 0x30000,
                                 reinterpret_cast<const uint8_t*>("/EDC17_C46/"), 11))
                flags.f5829c = true;
            else if (findPattern(data, size, 0x30000, 0x40000,
                                 reinterpret_cast<const uint8_t*>("EDC17_CP14"), 10))
                flags.f582a0 = true;
            flags.hasChecksums = true;
            break;
        case 4:
            if (findPattern(data, size, 0x50000, 0x60000,
                            reinterpret_cast<const uint8_t*>("MED17.4.2"), 9))
                flags.f58214 = true;
            else
                flags.f582d4 = true;
            break;
        case 5:
            flags.f581e0 = true;
            break;
        case 6:
            flags.f582f4 = true;
            break;
        case 8:
            flags.f58228 = true;
            break;
        case 9:
            if (findPattern(data, size, 0x300000, size,
                            reinterpret_cast<const uint8_t*>("EDC17_C41"), 9))
                flags.f5821c = true;
            else if (findPattern(data, size, 0x10000, 0x20000,
                                 reinterpret_cast<const uint8_t*>("EDC17CP49"), 9))
                flags.f58248 = true;
            flags.f581f4 = true;
            break;
        case 0xd:
            if (findPattern(data, size, 0x340000, size,
                            reinterpret_cast<const uint8_t*>("/EDC17_CP48/"), 12))
                flags.edc17cp48 = true;
            else if (findPattern(data, size, 0, 0x200000,
                                 reinterpret_cast<const uint8_t*>("/EDC17_CP68/"), 12))
                flags.edc17cp68 = true;
            else if (findPattern(data, size, 0, 0x200000,
                                 reinterpret_cast<const uint8_t*>("/EDC17_CP22/"), 12))
                flags.edc17cp22 = true;
            break;
        case 0x12:
            flags.f58260 = true;
            break;
        case 0x13:
            flags.f582ec = true;
            break;
        case 0x14:
            flags.f5825c = true;
            break;
        case 0x16:
            flags.f582b4 = true;
            break;
        case 0x19:
            flags.f58264 = true;
            flags.f581f4 = true;
            break;
        case 0x1b:
            flags.f58268 = true;
            flags.f581f4 = true;
            break;
        case 0x1e:
            flags.f58258 = true;
            break;
        case 0x21:
            flags.f581ec = true;
            break;
        case 0x26:
            flags.f582c4 = true;
            flags.hasChecksums = true;
            break;
        case 0x27:
            flags.f582e4 = true;
            break;
        case 0x28:
            flags.f5822c = true;
            break;
        case 0x29:
            flags.f581e4 = true;
            break;
        case 0x30:
            flags.f581e8 = true;
            break;
        case 0x3c:
            if (size != 0x100000)
                flags.f582d8 = true;
            break;
        case 0x42:
            flags.f581f0 = true;
            if (findPattern(data, size, 0x200200, 0x200400,
                            reinterpret_cast<const uint8_t*>("EDC17C79_N"), 10)) {
                flags.f581f0 = false;
                flags.f58224 = true;
            }
            if (findPattern(data, size, 0x200200, 0x200400,
                            reinterpret_cast<const uint8_t*>("EDC17_C49"), 9)) {
                flags.f581f0 = true;
                flags.f58280 = true;
            }
            break;
        case 0x4a:
            flags.f58234 = true;
            break;
        case 0x4b:
            flags.f58238 = true;
            break;
        case 0x52:
            flags.f58250 = true;
            break;
        case 0x53:
            flags.f58254 = true;
            flags.f581f4 = true;
            break;
        case 0x56:
            flags.f5826c = true;
            break;
        case 0x59:
            flags.f58270 = true;
            break;
        case 0x5a:
            flags.f58274 = true;
            break;
        case 0x5e:
            flags.f582ac = true;
            break;
        case 0x60:
            flags.f582bc = true;
            break;
        case 99:
            flags.f58290 = true;
            break;
        case 100:
            flags.f58284 = true;
            break;
        case 0x68:
            flags.f58288 = true;
            break;
        case 0x69:
            flags.f5828c = true;
            break;
        case 0x72:
            flags.f582d0 = true;
            break;
        case 0x73:
            flags.f582c8 = true;
            break;
        case 0x76:
            flags.f582c0 = true;
            break;
        case 0x78:
            if (findPattern(data, size, 0xe000, 0x10000,
                            reinterpret_cast<const uint8_t*>("ME177_9700D00_EDC.1.9.0_SERIES;0"),
                            32))
                flags.f582dc = true;
            else if (findPattern(data, size, 0xe000, 0x10000,
                                 reinterpret_cast<const uint8_t*>("PCFG:CB/248_1793_2.0.0"), 22))
                flags.f582e0 = true;
            break;
        case 0x7a:
            flags.f582cc = true;
            break;
        default:
            break;
        }
    }
}

} // namespace

uint16_t detectEcuType(const QByteArray& rom)
{
    const auto* data = reinterpret_cast<const uint8_t*>(rom.constData());
    const size_t size = static_cast<size_t>(rom.size());
    if (!data || size == 0)
        return 0;

    static constexpr uint8_t kMagic[8] = {0xfe, 0xca, 0xde, 0xfa, 0xfe, 0xaf, 0xfe, 0xca};
    static constexpr uint8_t kCbMx174[8] = {'C', 0x42, ' ', 'M', 'X', '1', '7', '4'};

    if (!findPattern(data, size, 0, size, kMagic, 8))
        return 0;

    uint16_t type;
    if (contains(data, size, "MED17.5.1")) {
        type = 0xa0;
    } else if (contains(data, size, "MEDC17") && size != 0x178000 && size != 0x180000) {
        type = 0xa1;
    } else if (contains(data, size, "TFSI") || contains(data, size, "MED17.5.5")
               || contains(data, size, "MED17") || contains(data, size, "MED17.5.20")) {
        type = 0xa2;
    } else if (contains(data, size, "EDC17_CP04")) {
        type = 0xd1;
    } else if (contains(data, size, "EDC17_CP14")) {
        type = 0xd2;
    } else if (contains(data, size, "EDC17_U01")) {
        type = 0xd3;
    } else if (contains(data, size, "EDC17_CP02")) {
        type = 0xc0;
    } else if (contains(data, size, "EDC17_C06")) {
        type = 0xc0;
    } else if (contains(data, size, "EDC17_C19")) {
        type = 0xe1;
    } else if (findPattern(data, size, 0, size, kCbMx174, 8)) {
        type = 0xb1;
    } else if (isEdc17Cv41Tail(data)) {
        type = 0xb0;
    } else if (contains(data, size, "ME1730")) {
        type = 0xfa;
    } else if (contains(data, size, "EDC17")) {
        type = 0xff;
    } else {
        type = 0xfe;
    }
    return type;
}

bool detectEdc17Cv41Layout(const QByteArray& rom, uint32_t start)
{
    return detectEdc17Cv41LayoutImpl(reinterpret_cast<const uint8_t*>(rom.constData()), start);
}

VariantResult detectVariant(const QByteArray& rom, const std::vector<Descriptor>& descriptors)
{
    VariantResult result;
    result.ecuType = detectEcuType(rom);
    const auto* data = reinterpret_cast<const uint8_t*>(rom.constData());
    const size_t size = static_cast<size_t>(rom.size());

    detectMed17EcuLayout(rom, result.flags);
    detectSubVariants(rom, descriptors, result.flags);
    detectVariantFromKey(rom, descriptors, result.flags);

    // DetectEcuType: the Ford check and DDE73 flag.
    if (contains(data, size, "MED17") && contains(data, size, "Copyright Ford Motor Co"))
        result.flags.ford = true;
    result.flags.isDde73 = contains(data, size, "DDE73");
    return result;
}

bool validateDescriptorSetForKey(const QByteArray& rom,
                                 const std::vector<Descriptor>& descriptors,
                                 uint32_t keyIndex)
{
    if (keyIndex != 9 && keyIndex != 0x19 && keyIndex != 0x1b && keyIndex != 0x1c
        && keyIndex != 0x3e && keyIndex != 0x53 && keyIndex != 0x7a)
        return false;

    QByteArray romCopy = rom;
    int flag = 0;
    processCustomerBlock(romCopy, descriptors, false, &flag);
    return flag != 0;
}

bool patchVariantMarker(QByteArray& rom, const QByteArray& originalRom,
                        const std::vector<Descriptor>& descriptors,
                        const VariantFlags& flags)
{
    if (originalRom.isEmpty())
        return false;
    const auto* orig = reinterpret_cast<const uint8_t*>(originalRom.constData());
    const size_t origSize = static_cast<size_t>(originalRom.size());
    RomView view{reinterpret_cast<uint8_t*>(rom.data()), static_cast<size_t>(rom.size())};

    int b = -1;
    for (size_t i = 0; i < descriptors.size(); ++i)
        if (descriptors[i].type == 0x30) {
            b = static_cast<int>(i);
            break;
        }
    if (b == -1)
        return false;
    const Descriptor& d = descriptors[b];
    const uint32_t blockStart = d.headerOffset;
    const uint32_t end = rdDword(view, blockStart + 4) + blockStart;

    uint32_t local28 = 0xffffffff;
    uint32_t local24 = 0xffffffff;
    uint32_t local20 = 0;

    for (uint32_t off = blockStart; off + 4 <= end; off += 4) {
        const uint32_t trans = translateFlashAddress(rdDword(view, off), view.size);
        if (trans != 0xffffffff && trans < (end | 0xfff)) {
            if (readRomWordBigEndian(view, off + 4) == 0x62f1) {
                const uint16_t w0 = rdWord(view, off + 2);
                const uint16_t w2 = rdWord(view, off);
                if (w0 == w2 && w2 < 0x20 && w0 < 0x20 && local28 == 0xffffffff) {
                    local20 = w2 & 0xffff;
                    local28 = off;
                    local24 = trans;
                }
            }
        }
    }
    if (local28 == 0xffffffff || local24 == 0xffffffff)
        return false;

    uint8_t local30 = 0xff;
    uint32_t local2c = 0xffffffff;
    for (uint32_t off = 0; off + 0x80 + 0x80 <= origSize; off += 0x80) {
        if (readRomWordBigEndian(orig, off) == 0x1700
            && readRomWordBigEndian(orig, off + 0x80) == 0x1800
            && std::memcmp(orig + off + 8, orig + off + 0x88, 5) == 0) {
            local30 = orig[off + 8];
            local2c = rdDword(orig, off + 9);
        }
    }
    if (local30 == 0xff || local2c == 0xffffffff)
        return false;

    uint16_t markerValue = 0;
    if (flags.edc17cp68)
        markerValue = 0xaaaa;
    else if (flags.edc17cp48)
        markerValue = 0xbbbb;
    else if (flags.edc17cp22)
        markerValue = 0xcccc;

    uint32_t scan = local24 + 1;
    const uint32_t scanEnd = (local20 & 0xffff) + local24;
    for (; scan < scanEnd; ++scan) {
        if (uint16_t(rdWord(view, scan)) == markerValue && view.data[scan + 2] == local30
            && rdDword(view, scan + 3) == byteSwap32(local2c)) {
            return true;
        }
    }
    wrWord(view, scanEnd, markerValue);
    wrByte(view, scanEnd + 2, local30);
    wrDword(view, scanEnd + 3, byteSwap32(local2c));
    wrWord(view, local28 + 4, local20 + 7);
    wrWord(view, local28 + 6, local20 + 7);
    return true;
}

uint32_t computeOverallRomChecksum(const QByteArray& rom)
{
    return calculateReflectedCrc32(reinterpret_cast<const uint8_t*>(rom.constData()), 0,
                                   static_cast<size_t>(rom.size()) - 1);
}

} // namespace Checksum::MED17
