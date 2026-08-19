/*
 * This file is part of romHEX14.
 * Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "Med17Descriptor.h"
#include "Med17Keys.h"
#include "Med17Rsa.h"

#include <array>
#include <cstddef>

namespace Checksum::MED17 {
namespace {

constexpr uint32_t kHeaderSignature0 = 0xfadecafe;
constexpr uint32_t kHeaderSignature1 = 0xcafeaffe;
constexpr uint32_t kTrailerSignature = 0xdeadbeef;
constexpr size_t kHeaderPrefixSize = 0x40;
constexpr size_t kSignatureSize = 0x80;
constexpr size_t kSignatureTrailerSize = 0x84;
constexpr size_t kMaximumDescriptors = 32;

uint16_t readLe16(const uint8_t* data, size_t offset)
{
    return uint16_t(data[offset]) | (uint16_t(data[offset + 1]) << 8);
}

uint32_t readLe32(const uint8_t* data, size_t offset)
{
    return uint32_t(data[offset])
        | (uint32_t(data[offset + 1]) << 8)
        | (uint32_t(data[offset + 2]) << 16)
        | (uint32_t(data[offset + 3]) << 24);
}

// Reproduce MED17 FindChecksumStructureTable: scan [start, end] for a table of
// 16-byte records {start, end, checksum, ~checksum} where each record's start/end
// are flash addresses (top byte 0x80) with start <= end and [8] == ~[0xc].
// On success the table start, end, and record count are written back.
bool findChecksumStructureTable(const uint8_t* data, size_t size,
                                size_t* startOut, size_t* endOut,
                                uint8_t minCount, uint32_t* countOut)
{
    const size_t start = *startOut;
    const size_t end = *endOut;
    for (size_t candidate = start; candidate + 0x10 <= size && candidate < end; ++candidate) {
        const uint32_t recStart = readLe32(data, candidate);
        const uint32_t recEnd = readLe32(data, candidate + 4);
        if (recStart >= recEnd)
            continue;
        const uint32_t v8 = readLe32(data, candidate + 8);
        const uint32_t vc = readLe32(data, candidate + 0xc);
        if (v8 != ~vc)
            continue;

        uint32_t count = 0;
        size_t pos = candidate;
        while (pos + 0x10 <= size && pos < end) {
            const uint32_t s = readLe32(data, pos);
            const uint32_t e = readLe32(data, pos + 4);
            if (e < s || (s >> 24) != (e >> 24) || (s >> 24) != 0x80)
                break;
            const uint32_t v = readLe32(data, pos + 8);
            const uint32_t w = readLe32(data, pos + 0xc);
            if (v != ~w)
                break;
            ++count;
            pos += 0x10;
        }
        if (count >= minCount) {
            *startOut = candidate;
            *endOut = candidate + count * 0x10;
            *countOut = count;
            return true;
        }
    }
    return false;
}

const std::array<uint32_t, 256>& crcTable()
{
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> result{};
        for (uint32_t index = 0; index < result.size(); ++index) {
            uint32_t value = index << 24;
            for (int bit = 0; bit < 8; ++bit)
                value = (value & 0x80000000u) ? ((value << 1) ^ 0x04c11db7u) : (value << 1);
            result[index] = value;
        }
        return result;
    }();
    return table;
}

uint32_t rotateLeft(uint32_t value, int amount)
{
    return (value << amount) | (value >> (32 - amount));
}

void transformSha1Block(std::array<uint32_t, 5>& state, const std::array<uint32_t, 16>& block)
{
    uint32_t* S = state.data();
    const uint32_t* w = block.data();
  uint32_t iVar1;
  uint32_t iVar2;
  uint32_t iVar3;
  uint32_t iVar4;
  uint32_t iVar5;
  uint32_t iVar6;
  uint32_t iVar7;
  uint32_t iVar8;
  uint32_t iVar9;
  uint32_t iVar10;
  uint32_t iVar11;
  uint32_t iVar12;
  uint32_t iVar13;
  uint32_t iVar14;
  uint32_t uVar15;
  uint32_t uVar16;
  uint32_t uVar17;
  uint32_t uVar18;
  uint32_t uVar19;
  uint32_t uVar20;
  uint32_t uVar21;
  uint32_t uVar22;
  uint32_t uVar23;
  uint32_t uVar24;
  uint32_t uVar25;
  uint32_t uVar26;
  uint32_t uVar27;
  
  uVar21 = S[3];
  uVar16 = S[2];
  uVar17 = S[1];
  iVar1 = *w;
  uVar15 = (uVar21 ^ uVar16 ^ uVar17) + iVar1 + *S;
  uVar15 = (uVar15 * 0x800 | uVar15 >> 0x15) + S[4];
  uVar20 = uVar16 << 10 | uVar16 >> 0x16;
  uVar16 = (uVar20 ^ uVar17 ^ uVar15) + w[1] + S[4];
  iVar2 = w[2];
  uVar16 = (uVar16 * 0x4000 | uVar16 >> 0x12) + uVar21;
  uVar26 = uVar17 << 10 | uVar17 >> 0x16;
  uVar21 = (uVar16 ^ uVar26 ^ uVar15) + iVar2 + uVar21;
  iVar3 = w[3];
  uVar22 = (uVar21 * 0x8000 | uVar21 >> 0x11) + uVar20;
  uVar21 = uVar15 * 0x400 | uVar15 >> 0x16;
  uVar20 = (uVar16 ^ uVar22 ^ uVar21) + iVar3 + uVar20;
  uVar27 = (uVar20 * 0x1000 | uVar20 >> 0x14) + uVar26;
  uVar17 = uVar16 * 0x400 | uVar16 >> 0x16;
  uVar26 = (uVar17 ^ uVar22 ^ uVar27) + w[4] + uVar26;
  uVar15 = (uVar26 * 0x20 | uVar26 >> 0x1b) + uVar21;
  uVar20 = uVar22 * 0x400 | uVar22 >> 0x16;
  iVar4 = w[5];
  uVar21 = (uVar20 ^ uVar27 ^ uVar15) + iVar4 + uVar21;
  iVar5 = w[6];
  uVar21 = (uVar21 * 0x100 | uVar21 >> 0x18) + uVar17;
  uVar16 = uVar27 * 0x400 | uVar27 >> 0x16;
  uVar17 = (uVar16 ^ uVar15 ^ uVar21) + iVar5 + uVar17;
  uVar17 = (uVar17 * 0x80 | uVar17 >> 0x19) + uVar20;
  iVar6 = w[7];
  uVar15 = uVar15 * 0x400 | uVar15 >> 0x16;
  uVar20 = (uVar17 ^ uVar15 ^ uVar21) + iVar6 + uVar20;
  uVar20 = (uVar20 * 0x200 | uVar20 >> 0x17) + uVar16;
  uVar21 = uVar21 * 0x400 | uVar21 >> 0x16;
  iVar7 = w[8];
  uVar16 = (uVar17 ^ uVar20 ^ uVar21) + iVar7 + uVar16;
  iVar8 = w[9];
  uVar16 = (uVar16 * 0x800 | uVar16 >> 0x15) + uVar15;
  uVar17 = uVar17 * 0x400 | uVar17 >> 0x16;
  uVar15 = (uVar17 ^ uVar20 ^ uVar16) + iVar8 + uVar15;
  uVar26 = (uVar15 * 0x2000 | uVar15 >> 0x13) + uVar21;
  uVar15 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar21 = (uVar15 ^ uVar16 ^ uVar26) + w[10] + uVar21;
  iVar9 = w[0xb];
  uVar20 = (uVar21 * 0x4000 | uVar21 >> 0x12) + uVar17;
  uVar21 = uVar16 * 0x400 | uVar16 >> 0x16;
  uVar17 = (uVar21 ^ uVar26 ^ uVar20) + iVar9 + uVar17;
  uVar16 = (uVar17 * 0x8000 | uVar17 >> 0x11) + uVar15;
  uVar26 = uVar26 * 0x400 | uVar26 >> 0x16;
  uVar15 = (uVar16 ^ uVar26 ^ uVar20) + w[0xc] + uVar15;
  uVar15 = (uVar15 * 0x40 | uVar15 >> 0x1a) + uVar21;
  uVar17 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar21 = (uVar16 ^ uVar15 ^ uVar17) + w[0xd] + uVar21;
  uVar20 = (uVar21 * 0x80 | uVar21 >> 0x19) + uVar26;
  uVar21 = uVar16 * 0x400 | uVar16 >> 0x16;
  iVar10 = w[0xe];
  uVar26 = (uVar21 ^ uVar15 ^ uVar20) + iVar10 + uVar26;
  uVar26 = (uVar26 * 0x200 | uVar26 >> 0x17) + uVar17;
  uVar16 = uVar15 * 0x400 | uVar15 >> 0x16;
  uVar17 = (uVar16 ^ uVar20 ^ uVar26) + w[0xf] + uVar17;
  uVar15 = (uVar17 * 0x100 | uVar17 >> 0x18) + uVar21;
  uVar17 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar21 = (~uVar15 & uVar17 | uVar26 & uVar15) + iVar6 + 0x5a827999 + uVar21;
  uVar20 = (uVar21 * 0x80 | uVar21 >> 0x19) + uVar16;
  uVar21 = uVar26 * 0x400 | uVar26 >> 0x16;
  iVar11 = w[4];
  uVar16 = (~uVar20 & uVar21 | uVar20 & uVar15) + iVar11 + 0x5a827999 + uVar16;
  uVar26 = (uVar16 * 0x40 | uVar16 >> 0x1a) + uVar17;
  uVar16 = uVar15 * 0x400 | uVar15 >> 0x16;
  uVar17 = (~uVar26 & uVar16 | uVar20 & uVar26) + w[0xd] + 0x5a827999 + uVar17;
  uVar15 = (uVar17 * 0x100 | uVar17 >> 0x18) + uVar21;
  uVar17 = uVar20 * 0x400 | uVar20 >> 0x16;
  iVar12 = w[1];
  uVar21 = (~uVar15 & uVar17 | uVar26 & uVar15) + iVar12 + 0x5a827999 + uVar21;
  uVar22 = (uVar21 * 0x2000 | uVar21 >> 0x13) + uVar16;
  uVar21 = uVar26 * 0x400 | uVar26 >> 0x16;
  uVar16 = (~uVar22 & uVar21 | uVar15 & uVar22) + w[10] + 0x5a827999 + uVar16;
  uVar20 = (uVar16 * 0x800 | uVar16 >> 0x15) + uVar17;
  uVar16 = uVar15 * 0x400 | uVar15 >> 0x16;
  uVar17 = (~uVar20 & uVar16 | uVar22 & uVar20) + iVar5 + 0x5a827999 + uVar17;
  uVar15 = (uVar17 * 0x200 | uVar17 >> 0x17) + uVar21;
  uVar17 = uVar22 * 0x400 | uVar22 >> 0x16;
  uVar21 = (~uVar15 & uVar17 | uVar15 & uVar20) + w[0xf] + 0x5a827999 + uVar21;
  uVar26 = (uVar21 * 0x80 | uVar21 >> 0x19) + uVar16;
  uVar21 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar16 = (~uVar26 & uVar21 | uVar15 & uVar26) + iVar3 + 0x5a827999 + uVar16;
  uVar20 = (uVar16 * 0x8000 | uVar16 >> 0x11) + uVar17;
  uVar16 = uVar15 * 0x400 | uVar15 >> 0x16;
  uVar17 = (~uVar20 & uVar16 | uVar26 & uVar20) + w[0xc] + 0x5a827999 + uVar17;
  uVar22 = (uVar17 * 0x80 | uVar17 >> 0x19) + uVar21;
  uVar17 = uVar26 * 0x400 | uVar26 >> 0x16;
  uVar21 = (~uVar22 & uVar17 | uVar20 & uVar22) + iVar1 + 0x5a827999 + uVar21;
  uVar15 = (uVar21 * 0x1000 | uVar21 >> 0x14) + uVar16;
  uVar21 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar16 = (~uVar15 & uVar21 | uVar22 & uVar15) + w[9] + 0x5a827999 + uVar16;
  uVar20 = (uVar16 * 0x8000 | uVar16 >> 0x11) + uVar17;
  uVar16 = uVar22 * 0x400 | uVar22 >> 0x16;
  uVar17 = (~uVar20 & uVar16 | uVar20 & uVar15) + iVar4 + 0x5a827999 + uVar17;
  uVar26 = (uVar17 * 0x200 | uVar17 >> 0x17) + uVar21;
  uVar17 = uVar15 * 0x400 | uVar15 >> 0x16;
  uVar21 = (~uVar26 & uVar17 | uVar20 & uVar26) + iVar2 + 0x5a827999 + uVar21;
  uVar15 = (uVar21 * 0x800 | uVar21 >> 0x15) + uVar16;
  uVar21 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar16 = (~uVar15 & uVar21 | uVar26 & uVar15) + w[0xe] + 0x5a827999 + uVar16;
  uVar22 = (uVar16 * 0x80 | uVar16 >> 0x19) + uVar17;
  uVar16 = uVar26 * 0x400 | uVar26 >> 0x16;
  uVar17 = (~uVar22 & uVar16 | uVar15 & uVar22) + iVar9 + 0x5a827999 + uVar17;
  uVar20 = (uVar17 * 0x2000 | uVar17 >> 0x13) + uVar21;
  uVar17 = uVar15 * 0x400 | uVar15 >> 0x16;
  uVar21 = (~uVar20 & uVar17 | uVar22 & uVar20) + iVar7 + 0x5a827999 + uVar21;
  uVar15 = (uVar21 * 0x1000 | uVar21 >> 0x14) + uVar16;
  uVar21 = uVar22 * 0x400 | uVar22 >> 0x16;
  uVar16 = ((~uVar20 | uVar15) ^ uVar21) + iVar3 + 0x6ed9eba1 + uVar16;
  uVar26 = (uVar16 * 0x800 | uVar16 >> 0x15) + uVar17;
  uVar16 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar17 = ((~uVar15 | uVar26) ^ uVar16) + w[10] + 0x6ed9eba1 + uVar17;
  uVar20 = (uVar17 * 0x2000 | uVar17 >> 0x13) + uVar21;
  uVar17 = uVar15 * 0x400 | uVar15 >> 0x16;
  uVar21 = ((~uVar26 | uVar20) ^ uVar17) + w[0xe] + 0x6ed9eba1 + uVar21;
  uVar22 = (uVar21 * 0x40 | uVar21 >> 0x1a) + uVar16;
  uVar21 = uVar26 * 0x400 | uVar26 >> 0x16;
  uVar16 = ((~uVar20 | uVar22) ^ uVar21) + iVar11 + 0x6ed9eba1 + uVar16;
  uVar15 = (uVar16 * 0x80 | uVar16 >> 0x19) + uVar17;
  uVar16 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar17 = ((~uVar22 | uVar15) ^ uVar16) + w[9] + 0x6ed9eba1 + uVar17;
  iVar13 = w[0xf];
  uVar20 = (uVar17 * 0x4000 | uVar17 >> 0x12) + uVar21;
  uVar17 = uVar22 * 0x400 | uVar22 >> 0x16;
  uVar21 = ((~uVar15 | uVar20) ^ uVar17) + iVar13 + 0x6ed9eba1 + uVar21;
  uVar26 = (uVar21 * 0x200 | uVar21 >> 0x17) + uVar16;
  uVar21 = uVar15 * 0x400 | uVar15 >> 0x16;
  uVar16 = ((~uVar20 | uVar26) ^ uVar21) + iVar7 + 0x6ed9eba1 + uVar16;
  uVar15 = (uVar16 * 0x2000 | uVar16 >> 0x13) + uVar17;
  uVar16 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar17 = ((~uVar26 | uVar15) ^ uVar16) + w[1] + 0x6ed9eba1 + uVar17;
  uVar20 = (uVar17 * 0x8000 | uVar17 >> 0x11) + uVar21;
  uVar17 = uVar26 * 0x400 | uVar26 >> 0x16;
  uVar21 = ((~uVar15 | uVar20) ^ uVar17) + iVar2 + 0x6ed9eba1 + uVar21;
  uVar26 = (uVar21 * 0x4000 | uVar21 >> 0x12) + uVar16;
  uVar21 = uVar15 * 0x400 | uVar15 >> 0x16;
  uVar16 = ((~uVar20 | uVar26) ^ uVar21) + iVar6 + 0x6ed9eba1 + uVar16;
  uVar15 = (uVar16 * 0x100 | uVar16 >> 0x18) + uVar17;
  uVar16 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar17 = ((~uVar26 | uVar15) ^ uVar16) + iVar1 + 0x6ed9eba1 + uVar17;
  uVar20 = (uVar17 * 0x2000 | uVar17 >> 0x13) + uVar21;
  uVar17 = uVar26 * 0x400 | uVar26 >> 0x16;
  uVar21 = ((~uVar15 | uVar20) ^ uVar17) + iVar5 + 0x6ed9eba1 + uVar21;
  iVar14 = w[0xd];
  uVar26 = (uVar21 * 0x40 | uVar21 >> 0x1a) + uVar16;
  uVar21 = uVar15 * 0x400 | uVar15 >> 0x16;
  uVar16 = ((~uVar20 | uVar26) ^ uVar21) + iVar14 + 0x6ed9eba1 + uVar16;
  uVar15 = (uVar16 * 0x20 | uVar16 >> 0x1b) + uVar17;
  uVar16 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar17 = ((~uVar26 | uVar15) ^ uVar16) + iVar9 + 0x6ed9eba1 + uVar17;
  uVar20 = (uVar17 * 0x1000 | uVar17 >> 0x14) + uVar21;
  uVar17 = uVar26 * 0x400 | uVar26 >> 0x16;
  uVar21 = ((~uVar15 | uVar20) ^ uVar17) + iVar4 + 0x6ed9eba1 + uVar21;
  uVar26 = (uVar21 * 0x80 | uVar21 >> 0x19) + uVar16;
  uVar21 = uVar15 * 0x400 | uVar15 >> 0x16;
  uVar16 = ((~uVar20 | uVar26) ^ uVar21) + w[0xc] + 0x6ed9eba1 + uVar16;
  uVar15 = (uVar16 * 0x20 | uVar16 >> 0x1b) + uVar17;
  uVar16 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar17 = (~uVar16 & uVar26 | uVar15 & uVar16) + w[1] + 0x8f1bbcdc + uVar17;
  uVar20 = (uVar17 * 0x800 | uVar17 >> 0x15) + uVar21;
  uVar17 = uVar26 * 0x400 | uVar26 >> 0x16;
  uVar21 = (~uVar17 & uVar15 | uVar17 & uVar20) + iVar8 + 0x8f1bbcdc + uVar21;
  uVar22 = (uVar21 * 0x1000 | uVar21 >> 0x14) + uVar16;
  uVar21 = uVar15 * 0x400 | uVar15 >> 0x16;
  uVar16 = (~uVar21 & uVar20 | uVar21 & uVar22) + iVar9 + 0x8f1bbcdc + uVar16;
  uVar26 = (uVar16 * 0x4000 | uVar16 >> 0x12) + uVar17;
  uVar16 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar17 = (~uVar16 & uVar22 | uVar16 & uVar26) + w[10] + 0x8f1bbcdc + uVar17;
  uVar27 = (uVar17 * 0x8000 | uVar17 >> 0x11) + uVar21;
  uVar17 = uVar22 * 0x400 | uVar22 >> 0x16;
  uVar21 = (~uVar17 & uVar26 | uVar27 & uVar17) + iVar1 + 0x8f1bbcdc + uVar21;
  uVar15 = (uVar21 * 0x4000 | uVar21 >> 0x12) + uVar16;
  uVar21 = uVar26 * 0x400 | uVar26 >> 0x16;
  uVar16 = (~uVar21 & uVar27 | uVar15 & uVar21) + iVar7 + 0x8f1bbcdc + uVar16;
  uVar20 = (uVar16 * 0x8000 | uVar16 >> 0x11) + uVar17;
  uVar16 = uVar27 * 0x400 | uVar27 >> 0x16;
  uVar17 = (~uVar16 & uVar15 | uVar16 & uVar20) + w[0xc] + 0x8f1bbcdc + uVar17;
  uVar22 = (uVar17 * 0x200 | uVar17 >> 0x17) + uVar21;
  uVar17 = uVar15 * 0x400 | uVar15 >> 0x16;
  uVar21 = (~uVar17 & uVar20 | uVar17 & uVar22) + iVar11 + 0x8f1bbcdc + uVar21;
  uVar26 = (uVar21 * 0x100 | uVar21 >> 0x18) + uVar16;
  uVar21 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar16 = (~uVar21 & uVar22 | uVar21 & uVar26) + iVar14 + 0x8f1bbcdc + uVar16;
  uVar27 = (uVar16 * 0x200 | uVar16 >> 0x17) + uVar17;
  uVar16 = uVar22 * 0x400 | uVar22 >> 0x16;
  uVar17 = (~uVar16 & uVar26 | uVar27 & uVar16) + iVar3 + 0x8f1bbcdc + uVar17;
  uVar15 = (uVar17 * 0x4000 | uVar17 >> 0x12) + uVar21;
  uVar17 = uVar26 * 0x400 | uVar26 >> 0x16;
  uVar21 = (~uVar17 & uVar27 | uVar15 & uVar17) + iVar6 + 0x8f1bbcdc + uVar21;
  uVar20 = (uVar21 * 0x20 | uVar21 >> 0x1b) + uVar16;
  uVar21 = uVar27 * 0x400 | uVar27 >> 0x16;
  uVar16 = (~uVar21 & uVar15 | uVar21 & uVar20) + iVar13 + 0x8f1bbcdc + uVar16;
  uVar22 = (uVar16 * 0x40 | uVar16 >> 0x1a) + uVar17;
  uVar16 = uVar15 * 0x400 | uVar15 >> 0x16;
  uVar17 = (~uVar16 & uVar20 | uVar16 & uVar22) + iVar10 + 0x8f1bbcdc + uVar17;
  uVar26 = (uVar17 * 0x100 | uVar17 >> 0x18) + uVar21;
  uVar17 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar21 = (~uVar17 & uVar22 | uVar17 & uVar26) + iVar4 + 0x8f1bbcdc + uVar21;
  uVar27 = (uVar21 * 0x40 | uVar21 >> 0x1a) + uVar16;
  uVar21 = uVar22 * 0x400 | uVar22 >> 0x16;
  uVar16 = (~uVar21 & uVar26 | uVar27 & uVar21) + iVar5 + 0x8f1bbcdc + uVar16;
  uVar15 = (uVar16 * 0x20 | uVar16 >> 0x1b) + uVar17;
  uVar16 = uVar26 * 0x400 | uVar26 >> 0x16;
  uVar17 = (~uVar16 & uVar27 | uVar15 & uVar16) + w[2] + 0x8f1bbcdc + uVar17;
  uVar20 = (uVar17 * 0x1000 | uVar17 >> 0x14) + uVar21;
  uVar17 = uVar27 * 0x400 | uVar27 >> 0x16;
  uVar21 = ((~uVar17 | uVar15) ^ uVar20) + iVar11 + 0xa953fd4e + uVar21;
  uVar22 = (uVar21 * 0x200 | uVar21 >> 0x17) + uVar16;
  uVar21 = uVar15 * 0x400 | uVar15 >> 0x16;
  uVar16 = ((~uVar21 | uVar20) ^ uVar22) + iVar1 + 0xa953fd4e + uVar16;
  uVar26 = (uVar16 * 0x8000 | uVar16 >> 0x11) + uVar17;
  uVar16 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar17 = ((~uVar16 | uVar22) ^ uVar26) + iVar4 + 0xa953fd4e + uVar17;
  uVar15 = (uVar17 * 0x20 | uVar17 >> 0x1b) + uVar21;
  uVar17 = uVar22 * 0x400 | uVar22 >> 0x16;
  uVar21 = ((~uVar17 | uVar26) ^ uVar15) + iVar8 + 0xa953fd4e + uVar21;
  uVar22 = (uVar21 * 0x800 | uVar21 >> 0x15) + uVar16;
  uVar21 = uVar26 * 0x400 | uVar26 >> 0x16;
  uVar16 = ((~uVar21 | uVar15) ^ uVar22) + iVar6 + 0xa953fd4e + uVar16;
  uVar20 = (uVar16 * 0x40 | uVar16 >> 0x1a) + uVar17;
  uVar16 = uVar15 * 0x400 | uVar15 >> 0x16;
  uVar17 = ((~uVar16 | uVar22) ^ uVar20) + w[0xc] + 0xa953fd4e + uVar17;
  uVar15 = (uVar17 * 0x100 | uVar17 >> 0x18) + uVar21;
  uVar17 = uVar22 * 0x400 | uVar22 >> 0x16;
  uVar21 = ((~uVar17 | uVar20) ^ uVar15) + w[2] + 0xa953fd4e + uVar21;
  uVar26 = (uVar21 * 0x2000 | uVar21 >> 0x13) + uVar16;
  uVar21 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar16 = ((~uVar21 | uVar15) ^ uVar26) + w[10] + 0xa953fd4e + uVar16;
  uVar20 = (uVar16 * 0x1000 | uVar16 >> 0x14) + uVar17;
  uVar16 = uVar15 * 0x400 | uVar15 >> 0x16;
  uVar17 = ((~uVar16 | uVar26) ^ uVar20) + iVar10 + 0xa953fd4e + uVar17;
  uVar15 = (uVar17 * 0x20 | uVar17 >> 0x1b) + uVar21;
  uVar17 = uVar26 * 0x400 | uVar26 >> 0x16;
  uVar21 = ((~uVar17 | uVar20) ^ uVar15) + iVar12 + 0xa953fd4e + uVar21;
  uVar26 = (uVar21 * 0x1000 | uVar21 >> 0x14) + uVar16;
  uVar21 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar16 = ((~uVar21 | uVar15) ^ uVar26) + iVar3 + 0xa953fd4e + uVar16;
  uVar20 = (uVar16 * 0x2000 | uVar16 >> 0x13) + uVar17;
  uVar16 = uVar15 * 0x400 | uVar15 >> 0x16;
  uVar17 = ((~uVar16 | uVar26) ^ uVar20) + iVar7 + 0xa953fd4e + uVar17;
  uVar15 = (uVar17 * 0x4000 | uVar17 >> 0x12) + uVar21;
  uVar17 = uVar26 * 0x400 | uVar26 >> 0x16;
  uVar21 = ((~uVar17 | uVar20) ^ uVar15) + w[0xb] + 0xa953fd4e + uVar21;
  uVar27 = (uVar21 * 0x800 | uVar21 >> 0x15) + uVar16;
  uVar21 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar16 = ((~uVar21 | uVar15) ^ uVar27) + iVar5 + 0xa953fd4e + uVar16;
  uVar18 = (uVar16 * 0x100 | uVar16 >> 0x18) + uVar17;
  uVar26 = uVar15 * 0x400 | uVar15 >> 0x16;
  uVar17 = ((~uVar26 | uVar27) ^ uVar18) + iVar13 + 0xa953fd4e + uVar17;
  uVar22 = (uVar17 * 0x20 | uVar17 >> 0x1b) + uVar21;
  uVar23 = uVar27 * 0x400 | uVar27 >> 0x16;
  uVar21 = ((~uVar23 | uVar18) ^ uVar22) + iVar14 + 0xa953fd4e + uVar21;
  uVar20 = S[1];
  uVar17 = S[3];
  uVar15 = S[2];
  uVar16 = ((~uVar17 | uVar15) ^ uVar20) + iVar4 + 0x50a28be6 + *S;
  uVar27 = (uVar16 * 0x100 | uVar16 >> 0x18) + S[4];
  uVar15 = uVar15 << 10 | uVar15 >> 0x16;
  uVar16 = ((~uVar15 | uVar20) ^ uVar27) + iVar10 + 0x50a28be6 + S[4];
  uVar19 = (uVar16 * 0x200 | uVar16 >> 0x17) + uVar17;
  uVar16 = uVar20 << 10 | uVar20 >> 0x16;
  uVar17 = ((~uVar16 | uVar27) ^ uVar19) + iVar6 + 0x50a28be6 + uVar17;
  uVar24 = (uVar17 * 0x200 | uVar17 >> 0x17) + uVar15;
  uVar17 = uVar27 * 0x400 | uVar27 >> 0x16;
  uVar15 = ((~uVar17 | uVar19) ^ uVar24) + iVar1 + 0x50a28be6 + uVar15;
  uVar20 = (uVar15 * 0x800 | uVar15 >> 0x15) + uVar16;
  uVar15 = uVar19 * 0x400 | uVar19 >> 0x16;
  uVar16 = ((~uVar15 | uVar24) ^ uVar20) + iVar8 + 0x50a28be6 + uVar16;
  uVar27 = (uVar16 * 0x2000 | uVar16 >> 0x13) + uVar17;
  uVar16 = uVar24 * 0x400 | uVar24 >> 0x16;
  uVar17 = ((~uVar16 | uVar20) ^ uVar27) + iVar2 + 0x50a28be6 + uVar17;
  uVar19 = (uVar17 * 0x8000 | uVar17 >> 0x11) + uVar15;
  uVar17 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar15 = ((~uVar17 | uVar27) ^ uVar19) + w[0xb] + 0x50a28be6 + uVar15;
  uVar20 = (uVar15 * 0x8000 | uVar15 >> 0x11) + uVar16;
  uVar15 = uVar27 * 0x400 | uVar27 >> 0x16;
  uVar16 = ((~uVar15 | uVar19) ^ uVar20) + iVar11 + 0x50a28be6 + uVar16;
  uVar27 = (uVar16 * 0x20 | uVar16 >> 0x1b) + uVar17;
  uVar16 = uVar19 * 0x400 | uVar19 >> 0x16;
  uVar17 = ((~uVar16 | uVar20) ^ uVar27) + iVar14 + 0x50a28be6 + uVar17;
  uVar19 = (uVar17 * 0x80 | uVar17 >> 0x19) + uVar15;
  uVar17 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar15 = ((~uVar17 | uVar27) ^ uVar19) + w[6] + 0x50a28be6 + uVar15;
  uVar20 = (uVar15 * 0x80 | uVar15 >> 0x19) + uVar16;
  uVar15 = uVar27 * 0x400 | uVar27 >> 0x16;
  uVar16 = ((~uVar15 | uVar19) ^ uVar20) + iVar13 + 0x50a28be6 + uVar16;
  uVar27 = (uVar16 * 0x100 | uVar16 >> 0x18) + uVar17;
  uVar16 = uVar19 * 0x400 | uVar19 >> 0x16;
  uVar17 = ((~uVar16 | uVar20) ^ uVar27) + iVar7 + 0x50a28be6 + uVar17;
  uVar19 = (uVar17 * 0x800 | uVar17 >> 0x15) + uVar15;
  uVar17 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar15 = ((~uVar17 | uVar27) ^ uVar19) + iVar12 + 0x50a28be6 + uVar15;
  uVar20 = (uVar15 * 0x4000 | uVar15 >> 0x12) + uVar16;
  uVar15 = uVar27 * 0x400 | uVar27 >> 0x16;
  uVar16 = ((~uVar15 | uVar19) ^ uVar20) + w[10] + 0x50a28be6 + uVar16;
  uVar27 = (uVar16 * 0x4000 | uVar16 >> 0x12) + uVar17;
  uVar16 = uVar19 * 0x400 | uVar19 >> 0x16;
  uVar17 = ((~uVar16 | uVar20) ^ uVar27) + w[3] + 0x50a28be6 + uVar17;
  uVar19 = (uVar17 * 0x1000 | uVar17 >> 0x14) + uVar15;
  uVar17 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar15 = ((~uVar17 | uVar27) ^ uVar19) + w[0xc] + 0x50a28be6 + uVar15;
  uVar20 = (uVar15 * 0x40 | uVar15 >> 0x1a) + uVar16;
  uVar15 = uVar27 * 0x400 | uVar27 >> 0x16;
  uVar16 = (~uVar15 & uVar19 | uVar15 & uVar20) + w[6] + 0x5c4dd124 + uVar16;
  uVar27 = (uVar16 * 0x200 | uVar16 >> 0x17) + uVar17;
  uVar16 = uVar19 * 0x400 | uVar19 >> 0x16;
  uVar17 = (~uVar16 & uVar20 | uVar27 & uVar16) + iVar9 + 0x5c4dd124 + uVar17;
  uVar19 = (uVar17 * 0x2000 | uVar17 >> 0x13) + uVar15;
  uVar17 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar15 = (~uVar17 & uVar27 | uVar19 & uVar17) + w[3] + 0x5c4dd124 + uVar15;
  uVar24 = (uVar15 * 0x8000 | uVar15 >> 0x11) + uVar16;
  uVar15 = uVar27 * 0x400 | uVar27 >> 0x16;
  uVar16 = (~uVar15 & uVar19 | uVar15 & uVar24) + iVar6 + 0x5c4dd124 + uVar16;
  uVar27 = (uVar16 * 0x80 | uVar16 >> 0x19) + uVar17;
  uVar16 = uVar19 * 0x400 | uVar19 >> 0x16;
  uVar17 = (~uVar16 & uVar24 | uVar16 & uVar27) + iVar1 + 0x5c4dd124 + uVar17;
  uVar20 = (uVar17 * 0x1000 | uVar17 >> 0x14) + uVar15;
  uVar17 = uVar24 * 0x400 | uVar24 >> 0x16;
  uVar15 = (~uVar17 & uVar27 | uVar17 & uVar20) + iVar14 + 0x5c4dd124 + uVar15;
  uVar19 = (uVar15 * 0x100 | uVar15 >> 0x18) + uVar16;
  uVar15 = uVar27 * 0x400 | uVar27 >> 0x16;
  uVar16 = (~uVar15 & uVar20 | uVar19 & uVar15) + iVar4 + 0x5c4dd124 + uVar16;
  uVar24 = (uVar16 * 0x200 | uVar16 >> 0x17) + uVar17;
  uVar16 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar17 = (~uVar16 & uVar19 | uVar24 & uVar16) + w[10] + 0x5c4dd124 + uVar17;
  uVar25 = (uVar17 * 0x800 | uVar17 >> 0x15) + uVar15;
  uVar17 = uVar19 * 0x400 | uVar19 >> 0x16;
  uVar15 = (~uVar17 & uVar24 | uVar17 & uVar25) + iVar10 + 0x5c4dd124 + uVar15;
  uVar27 = (uVar15 * 0x80 | uVar15 >> 0x19) + uVar16;
  uVar15 = uVar24 * 0x400 | uVar24 >> 0x16;
  uVar16 = (~uVar15 & uVar25 | uVar15 & uVar27) + iVar13 + 0x5c4dd124 + uVar16;
  uVar20 = (uVar16 * 0x80 | uVar16 >> 0x19) + uVar17;
  uVar16 = uVar25 * 0x400 | uVar25 >> 0x16;
  uVar17 = (~uVar16 & uVar27 | uVar16 & uVar20) + iVar7 + 0x5c4dd124 + uVar17;
  uVar19 = (uVar17 * 0x1000 | uVar17 >> 0x14) + uVar15;
  uVar17 = uVar27 * 0x400 | uVar27 >> 0x16;
  uVar15 = (~uVar17 & uVar20 | uVar19 & uVar17) + w[0xc] + 0x5c4dd124 + uVar15;
  uVar24 = (uVar15 * 0x80 | uVar15 >> 0x19) + uVar16;
  uVar15 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar16 = (~uVar15 & uVar19 | uVar24 & uVar15) + iVar11 + 0x5c4dd124 + uVar16;
  uVar25 = (uVar16 * 0x40 | uVar16 >> 0x1a) + uVar17;
  uVar16 = uVar19 * 0x400 | uVar19 >> 0x16;
  uVar17 = (~uVar16 & uVar24 | uVar16 & uVar25) + iVar8 + 0x5c4dd124 + uVar17;
  uVar27 = (uVar17 * 0x8000 | uVar17 >> 0x11) + uVar15;
  uVar17 = uVar24 * 0x400 | uVar24 >> 0x16;
  uVar15 = (~uVar17 & uVar25 | uVar17 & uVar27) + w[1] + 0x5c4dd124 + uVar15;
  uVar20 = (uVar15 * 0x2000 | uVar15 >> 0x13) + uVar16;
  uVar15 = uVar25 * 0x400 | uVar25 >> 0x16;
  uVar16 = (~uVar15 & uVar27 | uVar15 & uVar20) + iVar2 + 0x5c4dd124 + uVar16;
  uVar19 = (uVar16 * 0x800 | uVar16 >> 0x15) + uVar17;
  uVar16 = uVar27 * 0x400 | uVar27 >> 0x16;
  uVar17 = ((~uVar20 | uVar19) ^ uVar16) + iVar13 + 0x6d703ef3 + uVar17;
  uVar24 = (uVar17 * 0x200 | uVar17 >> 0x17) + uVar15;
  uVar17 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar15 = ((~uVar19 | uVar24) ^ uVar17) + iVar4 + 0x6d703ef3 + uVar15;
  uVar20 = (uVar15 * 0x80 | uVar15 >> 0x19) + uVar16;
  uVar15 = uVar19 * 0x400 | uVar19 >> 0x16;
  uVar16 = ((~uVar24 | uVar20) ^ uVar15) + w[1] + 0x6d703ef3 + uVar16;
  uVar27 = (uVar16 * 0x8000 | uVar16 >> 0x11) + uVar17;
  uVar16 = uVar24 * 0x400 | uVar24 >> 0x16;
  uVar17 = ((~uVar20 | uVar27) ^ uVar16) + iVar3 + 0x6d703ef3 + uVar17;
  uVar19 = (uVar17 * 0x800 | uVar17 >> 0x15) + uVar15;
  uVar17 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar15 = ((~uVar27 | uVar19) ^ uVar17) + iVar6 + 0x6d703ef3 + uVar15;
  uVar20 = (uVar15 * 0x100 | uVar15 >> 0x18) + uVar16;
  uVar15 = uVar27 * 0x400 | uVar27 >> 0x16;
  uVar16 = ((~uVar19 | uVar20) ^ uVar15) + iVar10 + 0x6d703ef3 + uVar16;
  uVar27 = (uVar16 * 0x40 | uVar16 >> 0x1a) + uVar17;
  uVar16 = uVar19 * 0x400 | uVar19 >> 0x16;
  uVar17 = ((~uVar20 | uVar27) ^ uVar16) + iVar5 + 0x6d703ef3 + uVar17;
  uVar19 = (uVar17 * 0x40 | uVar17 >> 0x1a) + uVar15;
  uVar17 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar15 = ((~uVar27 | uVar19) ^ uVar17) + iVar8 + 0x6d703ef3 + uVar15;
  uVar20 = (uVar15 * 0x4000 | uVar15 >> 0x12) + uVar16;
  uVar15 = uVar27 * 0x400 | uVar27 >> 0x16;
  uVar16 = ((~uVar19 | uVar20) ^ uVar15) + iVar9 + 0x6d703ef3 + uVar16;
  uVar27 = (uVar16 * 0x1000 | uVar16 >> 0x14) + uVar17;
  uVar16 = uVar19 * 0x400 | uVar19 >> 0x16;
  uVar17 = ((~uVar20 | uVar27) ^ uVar16) + iVar7 + 0x6d703ef3 + uVar17;
  uVar19 = (uVar17 * 0x2000 | uVar17 >> 0x13) + uVar15;
  uVar17 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar15 = ((~uVar27 | uVar19) ^ uVar17) + w[0xc] + 0x6d703ef3 + uVar15;
  uVar20 = (uVar15 * 0x20 | uVar15 >> 0x1b) + uVar16;
  uVar15 = uVar27 * 0x400 | uVar27 >> 0x16;
  uVar16 = ((~uVar19 | uVar20) ^ uVar15) + iVar2 + 0x6d703ef3 + uVar16;
  uVar27 = (uVar16 * 0x4000 | uVar16 >> 0x12) + uVar17;
  uVar16 = uVar19 * 0x400 | uVar19 >> 0x16;
  uVar17 = ((~uVar20 | uVar27) ^ uVar16) + w[10] + 0x6d703ef3 + uVar17;
  uVar19 = (uVar17 * 0x2000 | uVar17 >> 0x13) + uVar15;
  uVar17 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar15 = ((~uVar27 | uVar19) ^ uVar17) + iVar1 + 0x6d703ef3 + uVar15;
  uVar24 = (uVar15 * 0x2000 | uVar15 >> 0x13) + uVar16;
  uVar15 = uVar27 * 0x400 | uVar27 >> 0x16;
  uVar16 = ((~uVar19 | uVar24) ^ uVar15) + w[4] + 0x6d703ef3 + uVar16;
  uVar27 = (uVar16 * 0x80 | uVar16 >> 0x19) + uVar17;
  uVar16 = uVar19 * 0x400 | uVar19 >> 0x16;
  uVar17 = ((~uVar24 | uVar27) ^ uVar16) + iVar14 + 0x6d703ef3 + uVar17;
  uVar20 = (uVar17 * 0x20 | uVar17 >> 0x1b) + uVar15;
  uVar17 = uVar24 * 0x400 | uVar24 >> 0x16;
  uVar15 = (~uVar20 & uVar17 | uVar27 & uVar20) + iVar7 + 0x7a6d76e9 + uVar15;
  uVar19 = (uVar15 * 0x8000 | uVar15 >> 0x11) + uVar16;
  uVar15 = uVar27 * 0x400 | uVar27 >> 0x16;
  uVar16 = (~uVar19 & uVar15 | uVar20 & uVar19) + iVar5 + 0x7a6d76e9 + uVar16;
  uVar27 = (uVar16 * 0x20 | uVar16 >> 0x1b) + uVar17;
  uVar16 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar17 = (~uVar27 & uVar16 | uVar19 & uVar27) + w[4] + 0x7a6d76e9 + uVar17;
  uVar24 = (uVar17 * 0x100 | uVar17 >> 0x18) + uVar15;
  uVar17 = uVar19 * 0x400 | uVar19 >> 0x16;
  uVar15 = (~uVar24 & uVar17 | uVar27 & uVar24) + iVar12 + 0x7a6d76e9 + uVar15;
  uVar20 = (uVar15 * 0x800 | uVar15 >> 0x15) + uVar16;
  uVar15 = uVar27 * 0x400 | uVar27 >> 0x16;
  uVar16 = (~uVar20 & uVar15 | uVar20 & uVar24) + iVar3 + 0x7a6d76e9 + uVar16;
  uVar27 = (uVar16 * 0x4000 | uVar16 >> 0x12) + uVar17;
  uVar16 = uVar24 * 0x400 | uVar24 >> 0x16;
  uVar17 = (~uVar27 & uVar16 | uVar20 & uVar27) + iVar9 + 0x7a6d76e9 + uVar17;
  uVar19 = (uVar17 * 0x4000 | uVar17 >> 0x12) + uVar15;
  uVar17 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar15 = (~uVar19 & uVar17 | uVar27 & uVar19) + iVar13 + 0x7a6d76e9 + uVar15;
  uVar20 = (uVar15 * 0x40 | uVar15 >> 0x1a) + uVar16;
  uVar15 = uVar27 * 0x400 | uVar27 >> 0x16;
  uVar16 = (~uVar20 & uVar15 | uVar19 & uVar20) + iVar1 + 0x7a6d76e9 + uVar16;
  uVar24 = (uVar16 * 0x4000 | uVar16 >> 0x12) + uVar17;
  uVar16 = uVar19 * 0x400 | uVar19 >> 0x16;
  uVar17 = (~uVar24 & uVar16 | uVar20 & uVar24) + iVar4 + 0x7a6d76e9 + uVar17;
  uVar27 = (uVar17 * 0x40 | uVar17 >> 0x1a) + uVar15;
  uVar17 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar15 = (~uVar27 & uVar17 | uVar27 & uVar24) + w[0xc] + 0x7a6d76e9 + uVar15;
  uVar20 = (uVar15 * 0x200 | uVar15 >> 0x17) + uVar16;
  uVar15 = uVar24 * 0x400 | uVar24 >> 0x16;
  uVar16 = (~uVar20 & uVar15 | uVar27 & uVar20) + iVar2 + 0x7a6d76e9 + uVar16;
  uVar19 = (uVar16 * 0x1000 | uVar16 >> 0x14) + uVar17;
  uVar16 = uVar27 * 0x400 | uVar27 >> 0x16;
  uVar17 = (~uVar19 & uVar16 | uVar20 & uVar19) + iVar14 + 0x7a6d76e9 + uVar17;
  uVar27 = (uVar17 * 0x200 | uVar17 >> 0x17) + uVar15;
  uVar17 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar15 = (~uVar27 & uVar17 | uVar19 & uVar27) + iVar8 + 0x7a6d76e9 + uVar15;
  uVar24 = (uVar15 * 0x1000 | uVar15 >> 0x14) + uVar16;
  uVar15 = uVar19 * 0x400 | uVar19 >> 0x16;
  uVar16 = (~uVar24 & uVar15 | uVar27 & uVar24) + iVar6 + 0x7a6d76e9 + uVar16;
  uVar20 = (uVar16 * 0x20 | uVar16 >> 0x1b) + uVar17;
  uVar27 = uVar27 * 0x400 | uVar27 >> 0x16;
  uVar17 = (~uVar20 & uVar27 | uVar20 & uVar24) + w[10] + 0x7a6d76e9 + uVar17;
  uVar17 = (uVar17 * 0x8000 | uVar17 >> 0x11) + uVar15;
  uVar19 = uVar24 * 0x400 | uVar24 >> 0x16;
  uVar15 = (~uVar17 & uVar19 | uVar20 & uVar17) + iVar10 + 0x7a6d76e9 + uVar15;
  uVar16 = (uVar15 * 0x100 | uVar15 >> 0x18) + uVar27;
  uVar15 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar27 = (uVar15 ^ uVar17 ^ uVar16) + w[0xc] + uVar27;
  uVar20 = (uVar27 * 0x100 | uVar27 >> 0x18) + uVar19;
  uVar17 = uVar17 * 0x400 | uVar17 >> 0x16;
  uVar19 = (uVar17 ^ uVar16 ^ uVar20) + iVar13 + uVar19;
  uVar27 = (uVar19 * 0x20 | uVar19 >> 0x1b) + uVar15;
  uVar16 = uVar16 * 0x400 | uVar16 >> 0x16;
  uVar15 = (uVar16 ^ uVar20 ^ uVar27) + w[10] + uVar15;
  uVar19 = (uVar15 * 0x1000 | uVar15 >> 0x14) + uVar17;
  uVar15 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar17 = (uVar19 ^ uVar15 ^ uVar27) + iVar11 + uVar17;
  uVar17 = (uVar17 * 0x200 | uVar17 >> 0x17) + uVar16;
  uVar20 = uVar27 * 0x400 | uVar27 >> 0x16;
  uVar16 = (uVar19 ^ uVar17 ^ uVar20) + iVar12 + uVar16;
  uVar24 = (uVar16 * 0x1000 | uVar16 >> 0x14) + uVar15;
  uVar27 = uVar19 * 0x400 | uVar19 >> 0x16;
  uVar15 = (uVar27 ^ uVar17 ^ uVar24) + iVar4 + uVar15;
  uVar19 = (uVar15 * 0x20 | uVar15 >> 0x1b) + uVar20;
  uVar16 = uVar17 * 0x400 | uVar17 >> 0x16;
  uVar20 = (uVar16 ^ uVar24 ^ uVar19) + iVar7 + uVar20;
  uVar15 = (uVar20 * 0x4000 | uVar20 >> 0x12) + uVar27;
  uVar17 = uVar24 * 0x400 | uVar24 >> 0x16;
  uVar27 = (uVar17 ^ uVar19 ^ uVar15) + iVar6 + uVar27;
  uVar20 = (uVar27 * 0x40 | uVar27 >> 0x1a) + uVar16;
  uVar27 = uVar19 * 0x400 | uVar19 >> 0x16;
  uVar16 = (uVar20 ^ uVar27 ^ uVar15) + iVar5 + uVar16;
  uVar19 = (uVar16 * 0x100 | uVar16 >> 0x18) + uVar17;
  uVar15 = uVar15 * 0x400 | uVar15 >> 0x16;
  uVar17 = (uVar20 ^ uVar19 ^ uVar15) + iVar2 + uVar17;
  uVar16 = (uVar17 * 0x2000 | uVar17 >> 0x13) + uVar27;
  uVar20 = uVar20 * 0x400 | uVar20 >> 0x16;
  uVar27 = (uVar20 ^ uVar19 ^ uVar16) + iVar14 + uVar27;
  uVar24 = (uVar27 * 0x40 | uVar27 >> 0x1a) + uVar15;
  uVar17 = uVar19 * 0x400 | uVar19 >> 0x16;
  uVar15 = (uVar17 ^ uVar16 ^ uVar24) + iVar10 + uVar15;
  uVar27 = (uVar15 * 0x20 | uVar15 >> 0x1b) + uVar20;
  uVar16 = uVar16 * 0x400 | uVar16 >> 0x16;
  uVar20 = (uVar16 ^ uVar24 ^ uVar27) + iVar1 + uVar20;
  uVar15 = (uVar20 * 0x8000 | uVar20 >> 0x11) + uVar17;
  uVar19 = uVar24 * 0x400 | uVar24 >> 0x16;
  uVar17 = (uVar15 ^ uVar19 ^ uVar27) + iVar3 + uVar17;
  uVar17 = (uVar17 * 0x2000 | uVar17 >> 0x13) + uVar16;
  uVar27 = uVar27 * 0x400 | uVar27 >> 0x16;
  uVar16 = (uVar15 ^ uVar17 ^ uVar27) + iVar8 + uVar16;
  uVar20 = (uVar16 * 0x800 | uVar16 >> 0x15) + uVar19;
  uVar16 = uVar15 * 0x400 | uVar15 >> 0x16;
  uVar19 = (uVar16 ^ uVar17 ^ uVar20) + iVar9 + uVar19;
  iVar1 = S[1];
  S[1] = (uVar18 * 0x400 | uVar18 >> 0x16) + S[2] + uVar16;
  S[2] = S[3] + uVar27 + uVar23;
  S[3] = S[4] + (uVar19 * 0x800 | uVar19 >> 0x15) + uVar27 + uVar26;
  S[4] = *S + uVar20 + (uVar21 * 0x40 | uVar21 >> 0x1a) + uVar26;
  *S = (uVar17 * 0x400 | uVar17 >> 0x16) + iVar1 + uVar22;
  return;
}


} // namespace

uint32_t calculateDescriptorCrc32(const uint8_t* data, size_t start, size_t endInclusive)
{
    if (!data || start > endInclusive)
        return 0;

    uint32_t crc = 0xffffffffu;
    const auto& table = crcTable();
    for (size_t offset = start; offset <= endInclusive; ++offset)
        crc = (crc << 8) ^ table[((crc >> 24) ^ data[offset]) & 0xffu];
    return ~crc;
}

std::array<uint8_t, 20> calculateDescriptorSha1(const uint8_t* data,
                                                 size_t start,
                                                 size_t endInclusive)
{
    std::array<uint8_t, 20> digest{};
    if (!data || start > endInclusive)
        return digest;

    const size_t totalLength = endInclusive - start + 1;
    std::array<uint32_t, 5> state{
        0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u, 0xc3d2e1f0u};
    size_t offset = start;
    size_t remaining = totalLength;
    while (remaining >= 64) {
        std::array<uint32_t, 16> block{};
        for (size_t word = 0; word < block.size(); ++word)
            block[word] = readLe32(data, offset + word * 4);
        transformSha1Block(state, block);
        offset += 64;
        remaining -= 64;
    }

    // FinalizeDescriptorSha1 packs the tail at byte shifts 0, 8, 16, 24,
    // then runs the regular numeric SHA-1 transform.  Its padding therefore
    // differs from a byte-oriented SHA-1 implementation for non-4-byte tails.
    std::array<uint32_t, 16> finalBlock{};
    for (size_t index = 0; index < remaining; ++index)
        finalBlock[index / 4] ^= uint32_t(data[offset + index]) << ((index % 4) * 8);
    finalBlock[(totalLength / 4) & 0xf] ^= uint32_t(1) << ((totalLength % 4) * 8 + 7);
    if (remaining > 55) {
        transformSha1Block(state, finalBlock);
        finalBlock.fill(0);
    }
    const uint64_t bitLength = uint64_t(totalLength) * 8;
    finalBlock[14] = static_cast<uint32_t>(bitLength);
    finalBlock[15] = static_cast<uint32_t>(bitLength >> 32);
    transformSha1Block(state, finalBlock);

    for (size_t word = 0; word < state.size(); ++word) {
        digest[word * 4] = static_cast<uint8_t>(state[word]);
        digest[word * 4 + 1] = static_cast<uint8_t>(state[word] >> 8);
        digest[word * 4 + 2] = static_cast<uint8_t>(state[word] >> 16);
        digest[word * 4 + 3] = static_cast<uint8_t>(state[word] >> 24);
    }
    return digest;
}

std::vector<Descriptor> parseDescriptors(const QByteArray& rom)
{
    std::vector<Descriptor> descriptors;
    const auto* data = reinterpret_cast<const uint8_t*>(rom.constData());
    const size_t size = static_cast<size_t>(rom.size());
    if (!data || size < kHeaderPrefixSize + 8)
        return descriptors;

    // This is the recovered ParseChecksumDescriptors scan.  A descriptor's
    // FADECAFE marker is exactly 0x40 bytes after its header and is aligned at
    // an address ending in 0x40.  At each match the DLL's "scan start" equals the
    // marker offset, so it seeds the DEADBEEF candidate directly.
    for (size_t markerOffset = kHeaderPrefixSize;
         markerOffset + 8 <= size && descriptors.size() < kMaximumDescriptors;
         ++markerOffset) {
        if ((markerOffset & 0xffu) != 0x40u
            || readLe32(data, markerOffset) != kHeaderSignature0
            || readLe32(data, markerOffset + 4) != kHeaderSignature1) {
            continue;
        }

        const size_t headerOffset = markerOffset - kHeaderPrefixSize;
        uint16_t type = readLe16(data, headerOffset);
        uint16_t subtype = readLe16(data, headerOffset + 2);
        if (type >= 0xf1u || subtype >= 0xf1u)
            continue;

        // MED17 seeds the DEADBEEF search with read32(header+4) (the block
        // length) plus the current scan offset (which equals the marker offset),
        // then walks backwards byte-by-byte.  On MED17/EDC17 images header+4
        // holds the block length, so the candidate lands a few bytes past the
        // footer.
        const uint64_t candidate64 =
            uint64_t(readLe32(data, headerOffset + 4)) + markerOffset;
        size_t trailerOffset = candidate64 >= size ? size - 3 : static_cast<size_t>(candidate64);
        while (trailerOffset > markerOffset && trailerOffset + 4 <= size
               && readLe32(data, trailerOffset) != kTrailerSignature) {
            --trailerOffset;
        }

        // Recovered 0x40000/0x42000 special cases (the two branches previously
        // deferred): when the footer search ran off the end (trailer == size-3,
        // == 0x40, or == the scan start) AND the ROM is exactly 0x40000 or
        // 0x42000 bytes, fall back to trailer = 0x3fd00.  When size == 0x42000
        // and the scan start > 0x40000, additionally zero type/subtype and use
        // trailer = scanStart-1 (scan start == marker offset here).
        if ((trailerOffset == size - 3 || trailerOffset == kHeaderPrefixSize
             || trailerOffset == markerOffset)
            && (size == 0x42000 || size == 0x40000)) {
            trailerOffset = 0x3fd00;
            if (size == 0x42000 && markerOffset > 0x40000) {
                type = 0;
                subtype = 0;
                trailerOffset = markerOffset - 1;
            }
        }

        if (trailerOffset <= markerOffset || trailerOffset + 4 > size
            || readLe32(data, trailerOffset) != kTrailerSignature) {
            continue;
        }

        const uint32_t headerAddress = readLe32(data, headerOffset + 0x0c);
        if (headerAddress < trailerOffset)
            continue;
        const uint32_t addressBias = headerAddress - static_cast<uint32_t>(trailerOffset);
        const uint32_t rawCrcStart = readLe32(data, headerOffset + 0x38);
        const uint32_t rawCrcEnd = readLe32(data, headerOffset + 0x3c);
        if (rawCrcStart < addressBias || rawCrcEnd < addressBias + kSignatureTrailerSize)
            continue;

        const uint32_t crcStart = rawCrcStart - addressBias;
        const uint32_t crcEndExclusiveSignature = rawCrcEnd - addressBias;
        const uint32_t crcEndInclusive = crcEndExclusiveSignature - kSignatureTrailerSize;
        const uint32_t signatureOffset = crcEndExclusiveSignature - (kSignatureTrailerSize - 1);
        if (crcStart > crcEndInclusive || signatureOffset > size
            || size - signatureOffset < kSignatureSize) {
            continue;
        }

        Descriptor descriptor;
        descriptor.type = type;
        descriptor.subtype = subtype;
        descriptor.crcStartRaw = addressBias - static_cast<uint32_t>(kHeaderPrefixSize)
            + static_cast<uint32_t>(markerOffset);
        descriptor.headerOffset = static_cast<uint32_t>(headerOffset);
        descriptor.trailerOffset = static_cast<uint32_t>(trailerOffset);
        descriptor.addressBias = addressBias;
        descriptor.crcStart = crcStart;
        descriptor.crcEndInclusive = crcEndInclusive;
        descriptor.signatureOffset = signatureOffset;

        // MED17 records the descriptor's CRC-32 (over the same inclusive range
        // as the SHA-1) at record+0xa0 and flags whether the 128-byte signature
        // region holds a non-blank (non-0xAFAFAFAF) dword.
        descriptor.storedCrc32 = calculateDescriptorCrc32(data, crcStart, crcEndInclusive);
        descriptor.hasNonBlankWord = false;
        for (size_t i = 0; i < kSignatureSize; i += 4) {
            if (readLe32(data, signatureOffset + i) != 0xafafafafu) {
                descriptor.hasNonBlankWord = true;
                break;
            }
        }

        // Locate the block checksum structure table {start,end,value,~value}
        // records inside [headerOffset, trailerOffset] (min count 6, then 5/4/3).
        for (const uint8_t minCount : {uint8_t{6}, uint8_t{5}, uint8_t{4}, uint8_t{3}}) {
            size_t tableStart = headerOffset;
            size_t tableEnd = trailerOffset;
            uint32_t tableCount = 0;
            if (findChecksumStructureTable(data, size, &tableStart, &tableEnd, minCount, &tableCount)) {
                descriptor.hasSubTable = true;
                descriptor.subTableOffset = static_cast<uint32_t>(tableStart);
                descriptor.subTableCount = tableCount;
                break;
            }
        }

        // ParseChecksumDescriptors probes the complete SelectRsaPublicKey()
        // range until VerifyRsaSignatureBlock accepts this descriptor's
        // PKCS#1-style signature.  The extracted native table makes that
        // selection deterministic without the Windows DLL or GMP runtime.
        const QByteArray signature = QByteArray::fromRawData(
            reinterpret_cast<const char*>(data + signatureOffset), kSignatureSize);
        const std::array<uint8_t, 20> sha1 = calculateDescriptorSha1(data, crcStart, crcEndInclusive);
        for (uint32_t keyIndex = 0; keyIndex <= 0x8b; ++keyIndex) {
            const auto key = publicKeyForIndex(keyIndex);
            if (!key)
                continue;
            const RsaSignatureResult decoded = verifyRsaSignatureBlock(signature, *key);
            if (decoded.status != RsaSignatureStatus::Valid)
                continue;

            descriptor.signatureKeyIndex = keyIndex;
            // VerifySignatureDescriptorSet() hashes this exact inclusive
            // range and compares the 20 SHA-1 bytes with decodedBlock+0x0b.
            descriptor.rsaSignatureValid = std::equal(
                sha1.cbegin(), sha1.cend(), decoded.decodedBlock.cbegin() + 11);
            break;
        }
        descriptors.push_back(descriptor);
    }
    return descriptors;
}

} // namespace Checksum::MED17
