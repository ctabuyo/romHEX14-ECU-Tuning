/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "Med17BlockChecksum.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace Checksum::MED17 {
namespace {

constexpr uint32_t kAfafafaf = 0xafafafaf;   // -0x50505051
constexpr uint32_t kDeadbeef = 0xdeadbeef;
constexpr uint32_t kCafeaffe = 0xcafeaffe;
constexpr size_t kMaxBlocks = 32;
constexpr size_t kMaxSubBlocks = 0x100;

// ─────────────────────────────────────────────────────────────────────────────
// Mutable ROM view
// ─────────────────────────────────────────────────────────────────────────────

struct RomView {
    uint8_t* data = nullptr;
    size_t size = 0;
};

uint32_t rdDword(const RomView& r, size_t off)
{
    if (!r.data || off + 4 > r.size)
        return 0;
    return uint32_t(r.data[off]) | (uint32_t(r.data[off + 1]) << 8)
        | (uint32_t(r.data[off + 2]) << 16) | (uint32_t(r.data[off + 3]) << 24);
}

uint16_t rdWord(const RomView& r, size_t off)
{
    if (!r.data || off + 2 > r.size)
        return 0;
    return uint16_t(r.data[off]) | (uint16_t(r.data[off + 1]) << 8);
}

uint8_t rdByte(const RomView& r, size_t off)
{
    if (!r.data || off >= r.size)
        return 0;
    return r.data[off];
}

void wrDword(const RomView& r, size_t off, uint32_t v)
{
    if (!r.data || off + 4 > r.size)
        return;
    r.data[off] = uint8_t(v);
    r.data[off + 1] = uint8_t(v >> 8);
    r.data[off + 2] = uint8_t(v >> 16);
    r.data[off + 3] = uint8_t(v >> 24);
}

// ─────────────────────────────────────────────────────────────────────────────
// Reflected CRC-32 (zlib, poly 0xEDB88320) — MED17 CalculateCrc32Range
// ─────────────────────────────────────────────────────────────────────────────

const std::array<uint32_t, 256>& reflectedCrcTable()
{
    static const std::array<uint32_t, 256> t = [] {
        std::array<uint32_t, 256> a{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
            a[i] = c;
        }
        return a;
    }();
    return t;
}

// FUN_10001870 — CalculateCrc32Range over [start, endInclusive].
uint32_t calculateCrc32Range(const uint8_t* data, size_t start, size_t endInclusive)
{
    const auto& table = reflectedCrcTable();
    uint32_t crc = 0xffffffff;
    if (data) {
        for (size_t i = start; i <= endInclusive; ++i)
            crc = (crc >> 8) ^ table[(data[i] ^ crc) & 0xff];
    }
    return ~crc;
}

// ─────────────────────────────────────────────────────────────────────────────
// Non-reflected CRC-32 context (poly 0x04C11DB7, bit-reversed table) — the
// streaming/reverse CRC used by SolveAndWriteCrc32Patch.
// ─────────────────────────────────────────────────────────────────────────────

uint32_t reverseBits(uint32_t v, uint32_t count)
{
    uint32_t r = 0;
    for (uint32_t i = 0; i < count; ++i) {
        r = (r << 1) | (v & 1);
        v >>= 1;
    }
    return r;
}

// FUN_10040100 — 256-entry non-reflected table.
const std::array<uint32_t, 256>& reverseCrcTable()
{
    static const std::array<uint32_t, 256> t = [] {
        std::array<uint32_t, 256> a{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = reverseBits(i, 8) << 24;
            for (int k = 0; k < 8; ++k)
                c = (c << 1) ^ ((c & 0x80000000u) ? 0x04c11db7u : 0u);
            a[i] = reverseBits(c, 32);
        }
        return a;
    }();
    return t;
}

struct CrcContext {
    const uint8_t* data = nullptr;
    size_t size = 0;
    bool initialized = false;
    uint32_t state = 0xffffffff;   // +0xc
};

void crcContextInit(CrcContext& ctx, const uint8_t* data, size_t size)
{
    ctx.data = data;
    ctx.size = size;
    ctx.initialized = true;
    ctx.state = 0xffffffff;
}

// FUN_10040280 — update over [start, endInclusive].
void crcContextUpdateRange(CrcContext& ctx, size_t start, size_t endInclusive)
{
    if (!ctx.initialized)
        crcContextInit(ctx, ctx.data, ctx.size);
    const auto& table = reverseCrcTable();
    for (size_t i = start; i <= endInclusive; ++i)
        ctx.state = (ctx.state >> 8) ^ table[(ctx.data[i] ^ ctx.state) & 0xff];
}

// FUN_10040190 — finalize (~state).
uint32_t crcContextFinalize(CrcContext& ctx)
{
    return ~ctx.state;
}

// FUN_100401a0 — reverse step (search table under mask).
bool crcReverseStep(uint32_t& crc, uint32_t mask, uint8_t* byte)
{
    const auto& table = reverseCrcTable();
    for (uint32_t i = 0; i < 256; ++i) {
        if ((table[i] & mask) == (crc & mask)) {
            crc = table[i];
            *byte = static_cast<uint8_t>(i);
            return true;
        }
    }
    return false;
}

// FUN_100401f0 — table lookup.
uint32_t crcTableLookup(uint8_t byte)
{
    return reverseCrcTable()[byte];
}

// ─────────────────────────────────────────────────────────────────────────────
// Block bounds / index helpers
// ─────────────────────────────────────────────────────────────────────────────

// FUN_10036ee0 — GetBlockBounds: start = headerOffset, end = block size.
bool getBlockBounds(const RomView& rom, const Descriptor& d, uint32_t& start, uint32_t& end)
{
    start = d.headerOffset;
    const uint32_t sizeField = rdDword(rom, d.headerOffset + 4);
    if (rdDword(rom, d.headerOffset + sizeField - 4) != kDeadbeef) {
        if ((sizeField & 1) && rdDword(rom, d.headerOffset + (sizeField & 0xfffffffe) - 4) == kDeadbeef
            && (rdWord(rom, d.headerOffset + 0x3c) & 0xffff) + 4
                   == ((sizeField & 0xfffe) - 1) + d.headerOffset) {
            end = sizeField & 0xfffffffe;
            return true;
        }
        const uint32_t a = rdDword(rom, d.headerOffset + 0x38);
        const uint32_t b = rdDword(rom, d.headerOffset + 0x3c);
        end = b + (1 - a);
        if (rdDword(rom, d.headerOffset + end) != kDeadbeef)
            return false;
    } else {
        end = sizeField;
    }
    return true;
}

// FUN_100371c0 — FindBlockIndexForAddress (flash address).
int findBlockIndexForAddress(const RomView& rom, const std::vector<Descriptor>& descriptors,
                             uint32_t address)
{
    for (size_t i = 0; i < descriptors.size(); ++i) {
        const Descriptor& d = descriptors[i];
        const uint32_t size = rdDword(rom, d.headerOffset + 4);
        if (d.crcStartRaw <= address && address < d.crcStartRaw + size)
            return static_cast<int>(i);
    }
    return -1;
}

// FUN_10033520 — FindBlockByStartAddress.
int findBlockByStartAddress(const RomView& rom, const std::vector<Descriptor>& descriptors,
                            int blockIdx)
{
    const uint32_t target = rdDword(rom, descriptors[blockIdx].headerOffset + 8);
    if (target == 0)
        return -1;
    for (size_t i = 0; i < descriptors.size(); ++i)
        if (descriptors[i].crcStartRaw == target)
            return static_cast<int>(i);
    return -1;
}

// FUN_10036fe0 — IsValidBlockHeader.
bool isValidBlockHeader(const RomView& rom, const Descriptor& d)
{
    const size_t base = d.crcStartRaw + d.crcEndInclusive - 0x90;
    if (base + 0x90 > rom.size)
        return false;
    const auto af = [&](size_t o) { return rdDword(rom, base + o) == kAfafafaf; };
    if ((af(0) || af(4)) && !af(8) && !af(0xc) && !af(0x20) && !af(0x21) && !af(0x22)) {
        if (rdDword(rom, d.headerOffset + 8) != kAfafafaf
            && rdDword(rom, d.headerOffset + 0x30) != kAfafafaf)
            return true;
    }
    return false;
}

// FUN_10037080 — IsBlockHeaderAt.
bool isBlockHeaderAt(const RomView& rom, size_t off)
{
    if (off + 0x20 > rom.size)
        return false;
    if (rdByte(rom, off) != 0xe0)
        return false;
    const uint8_t b2 = rdByte(rom, off + 2);
    const uint8_t b4 = rdByte(rom, off + 4);
    if (b2 != 0x1d && b2 != 0x6d && b2 != 0x82 && b4 != 0x1d && b4 != 0x6d)
        return false;
    for (size_t i = 0; i < 0x10; ++i)
        if (rdDword(rom, off + 0x10 + i * 4) != 0)
            return false;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Sub-block table
// ─────────────────────────────────────────────────────────────────────────────

struct SubBlock {
    uint32_t start = 0xffffffff;
    uint32_t end = 0xffffffff;
    uint8_t type = 0xff;
};

struct SubBlockTable {
    std::array<uint16_t, kMaxBlocks> count{};
    std::array<std::array<SubBlock, kMaxSubBlocks>, kMaxBlocks> entries{};
};

// FUN_10037280 — AddSubBlockEntry.
void addSubBlockEntry(SubBlockTable& t, uint8_t block, uint32_t start, uint32_t end, uint8_t type)
{
    if (block >= kMaxBlocks || type >= kMaxBlocks)
        return;
    uint16_t& n = t.count[block];
    if (n >= kMaxSubBlocks)
        return;
    for (uint16_t i = 0; i < n; ++i) {
        if (t.entries[block][i].end == start - 1 && t.entries[block][i].type == type) {
            t.entries[block][i].end = end;
            return;
        }
    }
    t.entries[block][n] = {start, end, type};
    ++n;
}

// FUN_10037350 — BuildSubBlockTable.
void buildSubBlockTable(const RomView& rom, const std::vector<Descriptor>& descriptors,
                        SubBlockTable& t)
{
    t.count.fill(0);
    for (auto& row : t.entries)
        for (auto& sb : row)
            sb = SubBlock{};

    for (size_t bi = 0; bi < descriptors.size(); ++bi) {
        const Descriptor& d = descriptors[bi];
        if ((d.type == 0 && d.subtype == 0) || d.subTableCount == 0)
            continue;
        for (uint32_t si = 0; si < d.subTableCount; ++si) {
            const size_t base = d.subTableOffset + static_cast<size_t>(si) * 0x10;
            const uint32_t value = rdDword(rom, base + 8);
            if (value == kCafeaffe || value == 0xffffffff || rdDword(rom, base + 0xc) == 0)
                continue;
            const uint32_t start = rdDword(rom, base);
            const uint32_t end = rdDword(rom, base + 4);
            const int idx = findBlockIndexForAddress(rom, descriptors, start);
            if (idx == -1 || findBlockIndexForAddress(rom, descriptors, end) != idx)
                continue;
            addSubBlockEntry(t, static_cast<uint8_t>(idx), start, end, static_cast<uint8_t>(bi));
        }
    }
    for (auto& row : t.entries)
        for (size_t i = 0; i + 1 < row.size(); ++i)
            if (row[i].start != 0xffffffff && row[i + 1].start != 0xffffffff
                && row[i].start > row[i + 1].start)
                std::swap(row[i], row[i + 1]);
}

// FUN_100376d0 — IsOffsetInForeignBlock.
bool isOffsetInForeignBlock(const SubBlockTable& t, uint8_t block, uint32_t offset)
{
    for (uint16_t i = 0; i < t.count[block]; ++i) {
        const SubBlock& sb = t.entries[block][i];
        if (sb.start < offset && offset <= sb.end && sb.type != block)
            return true;
    }
    return false;
}

// FUN_10037640 — IsOffsetInOwnBlock.
bool isOffsetInOwnBlock(const SubBlockTable& t, uint8_t block, uint32_t offset)
{
    for (uint16_t i = 0; i < t.count[block]; ++i) {
        const SubBlock& sb = t.entries[block][i];
        if (sb.start < offset && offset <= sb.end && sb.type == block)
            return true;
    }
    return false;
}

// FUN_10037580 — GetFirstSubBlockStart.
uint32_t getFirstSubBlockStart(const SubBlockTable& t, uint8_t block)
{
    uint32_t result = 0xffffffff;
    for (uint16_t i = 0; i < t.count[block]; ++i)
        if (t.entries[block][i].type == block) {
            result = t.entries[block][i].start;
            break;
        }
    return result;
}

// FUN_100375e0 — GetLastSubBlockEnd.
uint32_t getLastSubBlockEnd(const SubBlockTable& t, uint8_t block)
{
    uint32_t result = 0xffffffff;
    for (uint16_t i = 0; i < t.count[block]; ++i)
        if (t.entries[block][i].type == block)
            result = t.entries[block][i].end;
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Checksum field / range
// ─────────────────────────────────────────────────────────────────────────────

// FUN_10037b80 — GetChecksumFieldOffset (returns a FLASH address + 0x14 length).
bool getChecksumFieldOffset(const RomView& rom, const Descriptor& d, uint32_t& offset, uint32_t& length)
{
    uint32_t start, end;
    if (!getBlockBounds(rom, d, start, end))
        return false;
    offset = d.addressBias + start - 0xa8 + end;
    length = 0x14;
    return true;
}

// FUN_10037bf0 — ComputeChecksumRange.
bool computeChecksumRange(const RomView& rom, const std::vector<Descriptor>& descriptors,
                          const SubBlockTable& subBlocks, int blockIdx,
                          uint32_t& offset, uint32_t& length)
{
    const Descriptor& d = descriptors[blockIdx];
    uint32_t fieldOffset, fieldLength;
    if (!getChecksumFieldOffset(rom, d, fieldOffset, fieldLength))
        return false;
    const uint32_t u3 = fieldOffset;
    if (!isOffsetInForeignBlock(subBlocks, static_cast<uint8_t>(blockIdx), fieldOffset)) {
        uint32_t start, end;
        if (getBlockBounds(rom, d, start, end)) {
            if (getLastSubBlockEnd(subBlocks, static_cast<uint8_t>(blockIdx)) == 0xffffffff) {
                const uint32_t addr = rdDword(rom, start - 0x94 + end);
                findBlockIndexForAddress(rom, descriptors, addr);
            }
            offset = u3;
            length = fieldLength;
            return true;
        }
        return false;
    }
    uint32_t start, end;
    if (!getBlockBounds(rom, d, start, end))
        return false;
    uint32_t firstStart = getFirstSubBlockStart(subBlocks, static_cast<uint8_t>(blockIdx));
    if (firstStart == 0xffffffff)
        return false;
    uint32_t u3b = rdDword(rom, (start - 0x94) + end);
    if (findBlockIndexForAddress(rom, descriptors, u3b) != blockIdx || firstStart < u3b) {
        u3b = rdDword(rom, (firstStart - d.addressBias) - 4);
        if (findBlockIndexForAddress(rom, descriptors, u3b) != blockIdx)
            return false;
        firstStart -= 4;
    }
    if (d.crcStartRaw < u3b) {
        if (u3b < firstStart - 0x14) {
            length = 0x14;
            u3b = firstStart - 0x14;
        } else {
            if (u3b <= d.crcStartRaw)
                return false;
            if (firstStart <= u3b)
                return false;
            length = firstStart - u3b;
            if (length < 4)
                return false;
        }
        offset = u3b;
        return true;
    }
    return false;
}

// FUN_10037980 — CompareBlockAgainstReference.
bool compareBlockAgainstReference(const RomView& rom, const uint8_t* reference, size_t refSize,
                                  const Descriptor& d, uint32_t offset, uint32_t length)
{
    const size_t i = static_cast<size_t>(offset - d.addressBias);
    const uint8_t c = rdByte(rom, i);
    if (c == 0x00 || c == 0xff || c == 0xc3) {
        size_t n = 1;
        if (length >= 2) {
            while (rdByte(rom, i + n) == c) {
                ++n;
                if (length <= n)
                    return true;
            }
        }
    }
    // SHA1 of the block [headerOffset, i) compared against the stored 20 bytes at i.
    const auto blockSha1 = calculateDescriptorSha1(rom.data, d.headerOffset, i - 1);
    if (std::memcmp(blockSha1.data(), rom.data + i, length) == 0)
        return true;
    if (reference == nullptr)
        return false;
    const size_t refStart = d.headerOffset;
    if (refStart >= refSize || i >= refSize)
        return false;
    const auto refSha1 = calculateDescriptorSha1(reference, refStart, i - 1);
    if (std::memcmp(refSha1.data(), reference + i, length) == 0)
        return true;
    const uint8_t b = reference[i];
    if (b == 0x00 || b == 0xff || b == 0xc3) {
        size_t n = 1;
        if (length >= 2) {
            while (reference[i + n] == b) {
                ++n;
                if (length <= n)
                    return true;
            }
        }
    }
    return false;
}

// FUN_1003bd30 — FindFreeBlockSlot.
bool findFreeBlockSlot(const RomView& rom, const std::vector<Descriptor>& descriptors,
                       int blockIdx, uint32_t& offset, uint32_t& length)
{
    const Descriptor& d = descriptors[blockIdx];
    if (d.type != 0x50) {
        uint32_t start, end;
        if (!getBlockBounds(rom, d, start, end))
            return false;
        const uint32_t addr = rdDword(rom, (start - 0x94) + end);
        if (findBlockIndexForAddress(rom, descriptors, addr) != blockIdx)
            return false;
        const uint32_t lo = (d.crcStartRaw - 0x94) + end;
        if (addr <= d.crcStartRaw || lo <= addr || lo - addr < 4)
            return false;
        offset = addr;
        length = lo - addr;
        return true;
    }
    uint32_t start, end;
    if (!getBlockBounds(rom, d, start, end))
        return false;
    uint32_t slot = (end + (start - 0xb0)) & 0xffffc0;
    if (slot <= start)
        return false;
    while (!isBlockHeaderAt(rom, slot))
        slot -= 0x20;
    while (isBlockHeaderAt(rom, slot))
        slot -= 0x20;
    if (slot <= start)
        return false;
    offset = slot;
    length = 0x14;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Checksum recompute / adjustment
// ─────────────────────────────────────────────────────────────────────────────

uint32_t add16Range(const RomView& rom, uint32_t start, uint32_t end)
{
    uint32_t sum = 0;
    for (uint32_t i = start; i < end; i += 2)
        sum += rdWord(rom, i);
    return sum;
}

uint32_t add32Range(const RomView& rom, uint32_t start, uint32_t end)
{
    uint32_t sum = 0;
    for (uint32_t i = start; i < end; i += 4)
        sum += rdDword(rom, i);
    return sum;
}

// FUN_10001b90 — RecomputeBlockChecksumTable.
void recomputeBlockChecksumTable(const RomView& rom, uint32_t tableOffset, uint32_t bias, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        const size_t base = tableOffset + static_cast<size_t>(i) * 0x10;
        const uint32_t start = rdDword(rom, base);
        if (bias <= start) {
            const uint32_t end = rdDword(rom, base + 4);
            if (end <= rom.size + bias) {
                const uint32_t value = rdDword(rom, base + 8);
                if (value != kCafeaffe && value != 0xffffffff && rdDword(rom, base + 0xc) != 0) {
                    const uint32_t cs = add16Range(rom, start - bias, end - bias);
                    wrDword(rom, base + 8, cs);
                    wrDword(rom, base + 0xc, ~cs);
                }
            }
        }
    }
}

// FUN_10037780 — AdjustChecksumEndBoundary.
void adjustChecksumEndBoundary(const RomView& rom, const Descriptor& d, uint32_t offset,
                               uint32_t length, bool write)
{
    for (uint32_t i = 0; i < d.subTableCount; ++i) {
        const size_t base = d.subTableOffset + static_cast<size_t>(i) * 0x10;
        const uint32_t start = rdDword(rom, base);
        const uint32_t end = rdDword(rom, base + 4);
        const uint32_t value = rdDword(rom, base + 8);
        if (value == kCafeaffe || value == 0xffffffff || rdDword(rom, base + 0xc) == 0)
            continue;
        if (start < offset && offset <= end) {
            if (write)
                wrDword(rom, base + 4, offset - 1);
        } else if (start <= offset + length && offset + length <= end) {
            return;
        }
    }
}

// FUN_100378c0 — PropagateChecksumAdjustment.
void propagateChecksumAdjustment(const RomView& rom, const SubBlockTable& t,
                                 const std::vector<Descriptor>& descriptors,
                                 uint8_t block, uint32_t offset, uint32_t length, bool write)
{
    for (uint16_t i = 0; i < t.count[block]; ++i) {
        const SubBlock& sb = t.entries[block][i];
        if (sb.start < offset && offset <= sb.end && sb.type != block)
            adjustChecksumEndBoundary(rom, descriptors[sb.type], offset, length, write);
    }
}

// FUN_10001640 — SolveAndWriteCrc32Patch.
void solveAndWriteCrc32Patch(const RomView& rom, uint32_t start, uint32_t end, uint32_t targetCrc)
{
    CrcContext ctx;
    crcContextInit(ctx, rom.data, rom.size);
    crcContextUpdateRange(ctx, start, end - 4);
    const uint32_t crc = ctx.state;
    const uint32_t uVar7 = ~targetCrc;
    uint32_t local10 = uVar7;
    std::array<uint8_t, 4> bytes{};
    for (int i = 3; i >= 0; --i) {
        if (!crcReverseStep(local10, 0xff000000, &bytes[i]))
            return;
        local10 = (local10 ^ uVar7) << 8;
    }
    uint32_t patch = 0;
    uint32_t uVar2 = crc;
    for (int i = 0; i < 4; ++i) {
        patch |= ((bytes[i] ^ uVar2) & 0xff) << (i * 8);
        uVar2 = (uVar2 >> 8) ^ crcTableLookup(bytes[i]);
    }
    wrDword(rom, end - 3, patch);
}

// ─────────────────────────────────────────────────────────────────────────────
// CVN / 0x82 helpers
// ─────────────────────────────────────────────────────────────────────────────

void wrWord(const RomView& r, size_t off, uint16_t v)
{
    if (!r.data || off + 2 > r.size)
        return;
    r.data[off] = uint8_t(v);
    r.data[off + 1] = uint8_t(v >> 8);
}

void wrByte(const RomView& r, size_t off, uint8_t v)
{
    if (!r.data || off >= r.size)
        return;
    r.data[off] = v;
}

// FUN_1002d580 — FindBlockByType.
int findBlockByType(const std::vector<Descriptor>& descriptors, uint16_t type)
{
    for (size_t i = 0; i < descriptors.size(); ++i)
        if (descriptors[i].type == type)
            return static_cast<int>(i);
    return -1;
}

// FUN_10040b20 — byte-swap 16.
uint16_t byteSwap16(uint16_t x)
{
    return uint16_t((x >> 8) | (x << 8));
}

// FUN_10040d90 — byte-swap 32.
uint32_t byteSwap32(uint32_t x)
{
    return (uint32_t(byteSwap16(uint16_t(x & 0xffff))) << 16)
        | uint32_t(byteSwap16(uint16_t(x >> 16)));
}

// FUN_100391f0 — ReadRomWordBigEndian.
uint16_t readRomWordBigEndian(const RomView& r, size_t off)
{
    if (!r.data || off + 2 > r.size)
        return 0;
    return uint16_t(r.data[off]) << 8 | uint16_t(r.data[off + 1]);
}

} // namespace

uint32_t calculateReflectedCrc32(const uint8_t* data, size_t start, size_t endInclusive)
{
    return calculateCrc32Range(data, start, endInclusive);
}

uint32_t calculateAdd16(const uint8_t* data, size_t start, size_t endExclusive)
{
    uint32_t sum = 0;
    if (data) {
        for (size_t i = start; i + 1 < endExclusive; i += 2)
            sum += uint16_t(data[i]) | (uint16_t(data[i + 1]) << 8);
    }
    return sum;
}

uint32_t calculateAdd32(const uint8_t* data, size_t start, size_t endExclusive)
{
    uint32_t sum = 0;
    if (data) {
        for (size_t i = start; i + 3 < endExclusive; i += 4)
            sum += uint32_t(data[i]) | (uint32_t(data[i + 1]) << 8)
                | (uint32_t(data[i + 2]) << 16) | (uint32_t(data[i + 3]) << 24);
    }
    return sum;
}

bool processBlockChecksums(QByteArray& rom, const std::vector<Descriptor>& descriptors,
                           bool hasChecksums, const QByteArray* originalRom)
{
    RomView view{reinterpret_cast<uint8_t*>(rom.data()), static_cast<size_t>(rom.size())};
    if (!view.data || view.size == 0)
        return false;

    const QByteArray reference = (originalRom && !originalRom->isEmpty()) ? *originalRom : rom;   // DAT_10058208 snapshot
    const uint8_t* ref = reinterpret_cast<const uint8_t*>(reference.constData());
    const size_t refSize = static_cast<size_t>(reference.size());

    SubBlockTable subBlocks;
    buildSubBlockTable(view, descriptors, subBlocks);

    std::array<bool, kMaxBlocks> valid{};       // aiStack_280
    std::array<bool, kMaxBlocks> flag2{};       // aiStack_180
    std::array<uint32_t, kMaxBlocks> offsets{}; // auStack_100
    std::array<uint32_t, kMaxBlocks> lengths{}; // auStack_200

    // Pass mask decomposition: 0x1 -> 0x10 -> 0x100 (FUN_1003c210).
    uint32_t mask = 0x111;
    uint32_t bit = 1;
    while (mask != 0 && bit != 0) {
        const uint32_t pass = bit & mask;
        if (pass == 1) {
            if (!hasChecksums)
                return false;
        } else if (pass == 0x10) {
            for (size_t bi = 0; bi < descriptors.size() && bi < kMaxBlocks; ++bi) {
                const Descriptor& d = descriptors[bi];
                valid[bi] = false;
                flag2[bi] = false;
                if (d.type == 0 && d.subtype == 0)
                    continue;
                uint32_t start, end;
                if (!getBlockBounds(view, d, start, end))
                    return false;
                const size_t hdr = start - 0x90 + end;
                const uint32_t w0 = rdDword(view, hdr);
                const uint32_t w1 = rdDword(view, hdr + 4);

                bool bVar15 = false;
                bool notValid = false;   // goto LAB_1003c4d8
                switch (d.type) {
                case 0x10:
                case 0x20:
                    if (w0 != kAfafafaf)
                        return false;
                    bVar15 = (w1 == kAfafafaf);
                    break;
                case 0x30:
                    bVar15 = (w0 == kAfafafaf);
                    break;
                case 0x40:
                    if (w1 == kAfafafaf)
                        return false;
                    notValid = true;
                    break;
                case 0x50:
                case 0x60:
                    if (w0 == kAfafafaf || w1 == kAfafafaf)
                        return false;
                    notValid = true;
                    break;
                default:
                    if (!isValidBlockHeader(view, d)) {
                        notValid = true;
                        break;
                    }
                    {
                        const int other =
                            findBlockByStartAddress(view, descriptors, static_cast<int>(bi));
                        if (other != -1) {
                            bVar15 = (descriptors[other].type == 0x30);
                            break;
                        }
                    }
                    return false;
                }

                if (!notValid) {
                    if (!bVar15)
                        return false;
                    valid[bi] = true;
                }

                // LAB_1003c4d8: checksum computation for non-valid blocks.
                if (!valid[bi]) {
                    if (ref != nullptr && std::memcmp(view.data + start, ref + start, 0x3c) != 0)
                        return false;

                    uint32_t offset, length;
                    if (!computeChecksumRange(view, descriptors, subBlocks, static_cast<int>(bi),
                                              offset, length)) {
                        if (!getChecksumFieldOffset(view, d, offset, length))
                            return false;
                        if (isOffsetInForeignBlock(subBlocks, static_cast<uint8_t>(bi), offset)
                            && d.type == 0x60)
                            return false;
                    }
                    if (!compareBlockAgainstReference(view, ref, refSize, d, offset, length)) {
                        if (d.type == 0x60)
                            return false;
                        if (findFreeBlockSlot(view, descriptors, static_cast<int>(bi), offset, length))
                            compareBlockAgainstReference(view, ref, refSize, d, offset, length);
                    }
                    if (isOffsetInOwnBlock(subBlocks, static_cast<uint8_t>(bi), offset))
                        adjustChecksumEndBoundary(view, d, offset, length, false);
                    if (isOffsetInForeignBlock(subBlocks, static_cast<uint8_t>(bi), offset))
                        propagateChecksumAdjustment(view, subBlocks, descriptors,
                                                    static_cast<uint8_t>(bi), offset, length, false);
                    offsets[bi] = offset;
                    lengths[bi] = length;
                }
            }
        } else if (pass == 0x100) {
            if (ref == nullptr)
                return false;
            for (size_t bi = 0; bi < descriptors.size() && bi < kMaxBlocks; ++bi) {
                const Descriptor& d = descriptors[bi];
                if (valid[bi] || (d.type == 0 && d.subtype == 0))
                    continue;
                uint32_t start, end;
                if (!getBlockBounds(view, d, start, end))
                    return false;
                const size_t crcEnd = (end - 0x91) + start;
                const uint32_t refCrc = calculateCrc32Range(ref, start, crcEnd);
                const uint32_t romCrc = calculateCrc32Range(view.data, start, crcEnd);
                if (refCrc != romCrc) {
                    if (flag2[bi])
                        return false;
                    const uint32_t offset = offsets[bi];
                    if (isOffsetInOwnBlock(subBlocks, static_cast<uint8_t>(bi), offset))
                        adjustChecksumEndBoundary(view, d, offset, lengths[bi], true);
                    if (isOffsetInForeignBlock(subBlocks, static_cast<uint8_t>(bi), offset))
                        propagateChecksumAdjustment(view, subBlocks, descriptors,
                                                    static_cast<uint8_t>(bi), offset, lengths[bi], true);
                    const uint32_t fileOff = offset - d.addressBias;
                    const auto sha1 = calculateDescriptorSha1(view.data, start, fileOff - 1);
                    std::memcpy(view.data + fileOff, sha1.data(), lengths[bi]);
                    const size_t patchEnd = (lengths[bi] - 1) + fileOff;
                    const uint32_t target = calculateCrc32Range(ref, start, patchEnd);
                    solveAndWriteCrc32Patch(view, start, patchEnd, target);
                }
            }
        }
        mask &= ~bit;
        bit *= 2;
    }

    // Unconditionally recompute 16-bit additive sub-block checksum tables (Opcode 0x80)
    for (const auto& d : descriptors) {
        if (d.subTableCount > 0 && d.subTableOffset != 0) {
            recomputeBlockChecksumTable(view, d.subTableOffset, d.addressBias, d.subTableCount);
        }
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// CVN (LocateCvnConfig / ComputeOrCorrectCvn)
// ─────────────────────────────────────────────────────────────────────────────

bool locateCvnConfig(const QByteArray& rom, const std::vector<Descriptor>& descriptors,
                     CvnConfig& cfg)
{
    RomView view{reinterpret_cast<uint8_t*>(const_cast<char*>(rom.constData())),
                 static_cast<size_t>(rom.size())};
    const int dsIdx = findBlockByType(descriptors, 0x60);
    if (dsIdx == -1)
        return false;
    const Descriptor& ds = descriptors[dsIdx];

    if (!ds.hasSubTable) {
        uint32_t local4 = ds.headerOffset;
        uint32_t u6 = local4 + 4;
        const uint32_t bound = rdDword(view, ds.headerOffset + 4) + ds.headerOffset;
        while (local4 < rdDword(view, u6) + ds.headerOffset) {
            if ((int16_t)rdWord(view, u6 - 6) == 0 && (int16_t)rdWord(view, u6 + 0x14) == 0) {
                const int appIdx = findBlockByType(descriptors, 0x40);
                const int ds0Idx = findBlockByType(descriptors, 0x60);
                if (findBlockIndexForAddress(view, descriptors, rdDword(view, local4)) == appIdx
                    && findBlockIndexForAddress(view, descriptors, rdDword(view, u6)) == appIdx
                    && rdDword(view, u6) < rdDword(view, local4)
                    && findBlockIndexForAddress(view, descriptors, rdDword(view, u6 + 4)) == ds0Idx
                    && findBlockIndexForAddress(view, descriptors, rdDword(view, u6 + 8)) == ds0Idx
                    && rdDword(view, u6 + 8) < rdDword(view, u6 + 4)
                    && findBlockIndexForAddress(view, descriptors, rdDword(view, u6 + 0xc)) == ds0Idx
                    && findBlockIndexForAddress(view, descriptors, rdDword(view, u6 + 0x10)) == ds0Idx
                    && rdDword(view, u6 + 0x10) < rdDword(view, u6 + 0xc)) {
                    cfg.regionTable = local4;
                    cfg.storedCrc = rdDword(view, local4 + 8) + (1 - ds.addressBias);
                    if (rdDword(view, cfg.storedCrc - 4) != 0)
                        return false;
                    if (rdDword(view, cfg.storedCrc + 0xc) == 0) {
                        cfg.valid = true;
                        return true;
                    }
                    return false;
                }
            }
            local4 += 4;
            u6 += 4;
            if (bound <= local4)
                return false;
        }
        return false;
    }

    if (ds.subTableOffset < ds.headerOffset + 0x100)
        return true;
    if (rdDword(view, ds.subTableOffset - 4) == 0) {
        cfg.storedCrc = ds.subTableOffset - 0x10;
        uint32_t u6 = ds.headerOffset;
        const uint32_t bound = rdDword(view, ds.headerOffset + 4) + ds.headerOffset;
        while (u6 < bound) {
            if (rdDword(view, u6) == ds.addressBias - 1 + cfg.storedCrc) {
                cfg.regionTable = u6 - 0x10;
                cfg.valid = true;
                return true;
            }
            u6 += 4;
        }
    }
    return false;
}

uint32_t computeOrCorrectCvn(QByteArray& rom, const std::vector<Descriptor>& descriptors,
                             const CvnConfig& cfg, bool write)
{
    RomView view{reinterpret_cast<uint8_t*>(rom.data()), static_cast<size_t>(rom.size())};
    const int dsIdx = findBlockByType(descriptors, 0x60);
    if (dsIdx == -1)
        return 0;
    const Descriptor& ds = descriptors[dsIdx];
    if (ds.hasSubTable && ds.subTableOffset < ds.headerOffset + 0x100)
        return 0xf;

    uint32_t local_c = 0;
    bool bVar1 = false;
    uint32_t result = 0;
    for (uint32_t r = 0; r < 3; ++r) {
        const uint32_t start = rdDword(view, cfg.regionTable + r * 8);
        const uint32_t end = rdDword(view, cfg.regionTable + 4 + r * 8);
        const size_t storedOff = cfg.storedCrc + r * 4;
        if (end < start && (end & 0xff000000) == 0x80000000
            && (start & 0xff000000) == 0x80000000) {
            ++local_c;
            int bi = -1;
            for (size_t i = 0; i < descriptors.size(); ++i) {
                const Descriptor& d = descriptors[i];
                if (d.crcStartRaw <= end
                    && start <= rdDword(view, d.headerOffset + 4) + d.crcStartRaw) {
                    bi = static_cast<int>(i);
                    break;
                }
            }
            if (bi != -1) {
                if (r == 2)
                    bVar1 = true;
                const uint32_t bias = descriptors[bi].addressBias;
                const uint32_t cs = add32Range(view, end - bias, start - bias);
                const uint32_t stored = rdDword(view, storedOff);
                if (cs == stored)
                    result |= 1u << r;
                else if (write)
                    wrDword(view, storedOff, cs);
            }
        }
    }
    if (local_c == 3 && bVar1)
        result |= 8;
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// 0x82 signature marker
// ─────────────────────────────────────────────────────────────────────────────

void findSignatureMarker(const QByteArray& rom, uint16_t ecuType, SignatureMarker& marker)
{
    RomView view{reinterpret_cast<uint8_t*>(const_cast<char*>(rom.constData())),
                 static_cast<size_t>(rom.size())};
    static const uint8_t patA[22] = {0xd9, 0x44, 0xd0, 0xa3, 0x99, 0xff, 0x0c, 0x10,
                                     0x00, 0x00, 0x2d, 0x0f, 0x00, 0x00, 0x76, 0x24,
                                     0xdf, 0x22, 0xd1, 0x01, 0x3c, 0x2b};
    static const uint8_t patB[22] = {0x99, 0xff, 0x0c, 0x10, 0xd9, 0x44, 0xd0, 0xe2,
                                     0x00, 0x00, 0x2d, 0x0f, 0x00, 0x00, 0x76, 0x24,
                                     0xdf, 0x22, 0xcb, 0x01, 0x3c, 0x2b};
    marker = {};

    auto scan = [&](const uint8_t* pat, const bool* wild) {
        int found = 0;
        for (size_t u7 = 0x14; u7 + 0x16 <= view.size; ++u7) {
            bool ok = true;
            for (int i = 0; i < 22; ++i) {
                if (!wild[i] && rdByte(view, u7 - 0x14 + i) != pat[i]) {
                    ok = false;
                    break;
                }
            }
            if (ok) {
                marker.offset = static_cast<uint32_t>(u7);
                marker.value = uint16_t(rdByte(view, u7) << 8) | rdByte(view, u7 + 1);
                marker.found = true;
                if (++found > 1)
                    return found;
            }
        }
        return found;
    };

    static const bool wildA[22] = {false, true,  true,  true,  false, false, false, false,
                                   false, false, false, false, false, false, false, false,
                                   false, false, true,  true,  true,  true};
    static const bool wildB[22] = {false, false, false, false, false, true,  true,  true,
                                   false, false, false, false, false, false, false, false,
                                   false, false, true,  true,  true,  true};

    int foundA = scan(patA, wildA);

    if (ecuType == 0xfa) {
        if (0xffff < marker.offset && marker.offset < 0x20001)
            return;
        marker = {};
        scan(patB, wildB);
        return;
    }
    if (0xffff < marker.offset) {
        marker = {};
        scan(patB, wildB);
        return;
    }
    if (foundA == 0) {
        marker = {};
        scan(patB, wildB);
    }
}

void clearSignatureMarker(QByteArray& rom, const SignatureMarker& marker)
{
    if (!marker.found)
        return;
    RomView view{reinterpret_cast<uint8_t*>(rom.data()), static_cast<size_t>(rom.size())};
    wrByte(view, marker.offset, 0);
    wrByte(view, marker.offset + 1, 0);
}

bool hasEdc17CpVariant(bool cp48, bool cp68, bool cp22)
{
    return cp48 || cp68 || cp22;
}

} // namespace Checksum::MED17
