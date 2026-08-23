#include "obdunlock.h"

#include <cstdint>
#include <cstring>

namespace ObdUnlock {

namespace {

struct GenDefs {
    uint32_t          unlockOffset;
    const std::uint8_t *unlockApplied;
    const std::uint8_t *unlockCheck;
    const std::uint8_t *obdFind;   int obdFindLen;
    const std::uint8_t *obdPatch;  int obdPatchLen;
};
static const std::uint8_t kZero4[4] = { 0x00, 0x00, 0x00, 0x00 };

static const std::uint8_t kGen1Unlock[4] = { 0x39, 0x7E, 0xB6, 0x88 };
static const std::uint8_t kGen1Find[5]    = { 0x80, 0x2A, 0x03, 0xE2, 0x07 };
static const std::uint8_t kGen1Patch[5]   = { 0x80, 0x48, 0x03, 0x44, 0x00 };

static const std::uint8_t kPre2020Unlock[4] = { 0x38, 0xD1, 0xBF, 0xDC };
static const std::uint8_t kPre2020Find[6]   = { 0x91, 0x10, 0x00, 0x26, 0xF6, 0x27 };
static const std::uint8_t kPre2020Patch[6]  = { 0x91, 0x0A, 0x00, 0x26, 0x82, 0x02 };
static const GenDefs kGen1    = { 0x40260, kGen1Unlock, kZero4, kGen1Find, 5, kGen1Patch, 5 };
static const GenDefs kGen2Pre = { 0x5F7DC, kPre2020Unlock, kZero4, kPre2020Find, 6, kPre2020Patch, 6 };

static constexpr uint32_t kGen2ObdOffset = 0x2CA5C;
static const std::uint8_t kGen2Locked[4]   = { 0xF6, 0x27, 0xDA, 0x0E };
static const std::uint8_t kGen2Unlocked[4] = { 0x82, 0x02, 0xDA, 0x0E };

struct Mevd17PatchDef {
    uint32_t offset;
    const std::uint8_t *find;
    const std::uint8_t *replace;
    int len;
};
static const std::uint8_t kMevd17P0f[12] = { 0x82,0x03,0x8F,0x23,0x20,0xF0,0xD9,0x4F,0x04,0x00,0x01,0xF3 };
static const std::uint8_t kMevd17P0r[12] = { 0x82,0x03,0x1D,0x00,0x22,0x00,0xD1,0x0E,0xCD,0xA2,0x01,0xF3 };
static const std::uint8_t kMevd17P1f[4]  = { 0xC2,0x1F,0xA8,0x0F };
static const std::uint8_t kMevd17P1r[4]  = { 0xC2,0x0F,0xA8,0x0F };
static const std::uint8_t kMevd17P2f[4]  = { 0x8C,0xC0,0xC2,0x1F };
static const std::uint8_t kMevd17P2r[4]  = { 0x8C,0xC0,0xC2,0x0F };
static const std::uint8_t kMevd17P3f[4]  = { 0x39,0x3F,0x4C,0xED };
static const std::uint8_t kMevd17P3r[4]  = { 0x6E,0x64,0xDB,0xEE };
static const std::uint8_t kMevd17P4f[4]  = { 0x50,0x02,0x01,0x00 };
static const std::uint8_t kMevd17P4r[4]  = { 0x55,0x02,0x01,0x00 };
static const std::uint8_t kMevd17P5f[4]  = { 0x7D,0x30,0x7F,0x5F };
static const std::uint8_t kMevd17P5r[4]  = { 0x7D,0x60,0x7F,0x5F };
static const std::uint8_t kMevd17P6f[4]  = { 0x09,0x00,0x06,0x00 };
static const std::uint8_t kMevd17P6r[4]  = { 0x09,0x00,0x00,0x00 };
static const Mevd17PatchDef kMevd17Patches[] = {
    { 0x001BCC, kMevd17P0f, kMevd17P0r, 12 },
    { 0x005A60, kMevd17P1f, kMevd17P1r,  4 },
    { 0x005AA0, kMevd17P2f, kMevd17P2r,  4 },
    { 0x00FBFC, kMevd17P3f, kMevd17P3r,  4 },
    { 0x00FD8C, kMevd17P4f, kMevd17P4r,  4 },
    { 0x017FBFC, kMevd17P5f, kMevd17P5r, 4 },
    { 0x1BF020, kMevd17P6f, kMevd17P6r, 4 },
};
static constexpr int kMevd17PatchCount = 7;

static const std::uint8_t kMevd17Sig[6] = { 0x4D, 0x45, 0x56, 0x44, 0x31, 0x37 };

int findSeq(const QByteArray &hay, const std::uint8_t *needle, int n)
{
    if (hay.isEmpty() || n <= 0 || n > hay.size())
        return -1;
    const std::uint8_t *h = reinterpret_cast<const std::uint8_t *>(hay.constData());
    for (int i = 0; i <= hay.size() - n; ++i) {
        if (std::memcmp(h + i, needle, static_cast<size_t>(n)) == 0)
            return i;
    }
    return -1;
}

bool seqEq(const QByteArray &data, uint32_t off, const std::uint8_t *needle, int n)
{
    if (data.size() < 0 || off > static_cast<uint32_t>(data.size())
        || static_cast<uint64_t>(off) + static_cast<uint32_t>(n) > static_cast<uint32_t>(data.size()))
        return false;
    const std::uint8_t *h = reinterpret_cast<const std::uint8_t *>(data.constData()) + off;
    return std::memcmp(h, needle, static_cast<size_t>(n)) == 0;
}

bool seqEq(const void *a, const void *b, int n)
{
    return std::memcmp(a, b, static_cast<size_t>(n)) == 0;
}

QString hexOf(uint32_t v)
{
    return QStringLiteral("%1").arg(v, 8, 16, QLatin1Char('0')).toUpper();
}

QString bytesToHex(const void *p, int n)
{
    return QByteArray(reinterpret_cast<const char *>(p), n).toHex(' ').toUpper();
}

bool isMevd17Signature(const QByteArray &rom)
{
    const int searchLimit = qMin(rom.size(), 40);
    if (searchLimit < static_cast<int>(sizeof(kMevd17Sig)))
        return false;
    const std::uint8_t *h = reinterpret_cast<const std::uint8_t *>(rom.constData());
    for (int i = 0; i <= searchLimit - static_cast<int>(sizeof(kMevd17Sig)); ++i) {
        if (std::memcmp(h + i, kMevd17Sig, sizeof(kMevd17Sig)) == 0)
            return true;
    }
    return false;
}

bool isAlreadyPatched(const QByteArray &rom, Kind k)
{
    switch (k) {
    case Kind::Gen1:
        return seqEq(rom, kGen1.unlockOffset, kGen1.unlockApplied, 4)
               && findSeq(rom, kGen1.obdPatch, kGen1.obdPatchLen) != -1;
    case Kind::Gen2Pre2020:
        return seqEq(rom, kGen2Pre.unlockOffset, kGen2Pre.unlockApplied, 4)
               && findSeq(rom, kGen2Pre.obdPatch, kGen2Pre.obdPatchLen) != -1;
    case Kind::Gen2:
        return seqEq(rom, kGen2ObdOffset, kGen2Unlocked, 4);
    case Kind::Mevd172G:
        for (int i = 0; i < kMevd17PatchCount; ++i) {
            const Mevd17PatchDef &p = kMevd17Patches[i];
            if (static_cast<uint64_t>(rom.size()) < p.offset + static_cast<uint32_t>(p.len)
                || !seqEq(rom, p.offset, p.replace, p.len))
                return false;
        }
        return true;
    default:
        return false;
    }
}

ApplyReport patchStandard(QByteArray &rom, const GenDefs &def)
{
    ApplyReport rep;
    bool success = true;

    const QByteArray slice = rom.mid(def.unlockOffset, 4);
    const int have = slice.size();
    const bool appliedEq = have >= 4 && seqEq(slice.constData(), def.unlockApplied, 4);
    const bool checkEq   = have >= 4 && seqEq(slice.constData(), def.unlockCheck, 4);
    if (appliedEq) {
        rep.messages << QStringLiteral("Unlock patch already present.");
    } else if (!checkEq) {
        rep.messages << QStringLiteral(
            "Warning: Unexpected bytes at unlock offset 0x%1. Patch not applied.")
                         .arg(hexOf(def.unlockOffset));
        success = false;
    } else {
        std::memcpy(rom.data() + def.unlockOffset, def.unlockApplied, 4);
    }

    const int obd = findSeq(rom, def.obdFind, def.obdFindLen);
    if (obd == -1) {
        if (findSeq(rom, def.obdPatch, def.obdPatchLen) != -1) {
            rep.messages << QStringLiteral("OBD patch already present.");
        } else {
            rep.messages << QStringLiteral("Error: OBD patch sequence not found.");
            success = false;
        }
    } else {
        std::memcpy(rom.data() + obd, def.obdPatch, def.obdPatchLen);
    }

    rep.success = success;
    return rep;
}

ApplyReport patchGen2ObdOnly(QByteArray &rom)
{
    ApplyReport rep;
    bool success = true;

    if (static_cast<uint64_t>(rom.size()) <
        static_cast<uint64_t>(kGen2ObdOffset) + sizeof(kGen2Locked)) {
        rep.messages << QStringLiteral("Error: File too short for Gen2 OBD patch at 0x%1.")
                            .arg(hexOf(kGen2ObdOffset));
        return rep;
    }

    const QByteArray slice = rom.mid(kGen2ObdOffset, 4);
    if (seqEq(slice.constData(), kGen2Unlocked, 4)) {
        rep.messages << QStringLiteral("Gen2 OBD patch already present.");
    } else if (seqEq(slice.constData(), kGen2Locked, 4)) {
        std::memcpy(rom.data() + kGen2ObdOffset, kGen2Unlocked, 4);
    } else {
        rep.messages << QStringLiteral(
            "Warning: Unexpected Gen2 OBD bytes at 0x%1. "
            "Expected locked bytes F6 27 DA 0E, found %2. Patch not applied.")
                         .arg(hexOf(kGen2ObdOffset))
                         .arg(bytesToHex(slice.constData(), slice.size()));
        success = false;
    }

    rep.success = success;
    return rep;
}

ApplyReport patchMevd17(QByteArray &rom)
{
    ApplyReport rep;
    bool success = true;

    for (int i = 0; i < kMevd17PatchCount; ++i) {
        const Mevd17PatchDef &p = kMevd17Patches[i];

        if (static_cast<uint64_t>(rom.size()) < p.offset + static_cast<uint32_t>(p.len)) {
            rep.messages << QStringLiteral("Error: File too short to apply patch at 0x%1. Skipping.")
                                .arg(hexOf(p.offset));
            success = false;
            continue;
        }

        const QByteArray slice = rom.mid(p.offset, p.len);
        if (seqEq(slice.constData(), p.replace, p.len)) continue;
        if (seqEq(slice.constData(), p.find, p.len)) {
            std::memcpy(rom.data() + p.offset, p.replace, p.len);
            continue;
        }
        rep.messages << QStringLiteral("Warning: Unexpected bytes at 0x%1. Expected: %2, Found: %3. "
                                       "Patch not applied.")
                            .arg(hexOf(p.offset))
                            .arg(bytesToHex(p.find, p.len), bytesToHex(slice.constData(), slice.size()));
        success = false;
    }

    rep.success = success;
    return rep;
}

}

QString Detection::describe() const
{
    switch (kind) {
    case Kind::Gen1:          return QStringLiteral("Gen1 ECU");
    case Kind::Gen2:          return QStringLiteral("Gen2 ECU (post-2020)");
    case Kind::Gen2Pre2020:   return QStringLiteral("Gen2 ECU (pre-2020)");
    case Kind::Mevd172G:      return QStringLiteral("MEVD17.2.G");
    default:                  return QStringLiteral("None");
    }
}

Detection detect(const QByteArray &rom)
{
    Detection d;
    if (rom.isEmpty())
        return d;

    if (isMevd17Signature(rom)) {
        d.kind = Kind::Mevd172G;
        d.canPatch = true;
        int notFoundCount = 0;
        for (int i = 0; i < kMevd17PatchCount; ++i) {
            const Mevd17PatchDef &p = kMevd17Patches[i];
            if (static_cast<uint64_t>(rom.size()) < p.offset + static_cast<uint32_t>(p.len)) {
                ++notFoundCount;
                continue;
            }
            const QByteArray cur = rom.mid(p.offset, p.len);
            if (!seqEq(cur.constData(), p.find, p.len)
                && !seqEq(cur.constData(), p.replace, p.len))
                ++notFoundCount;
        }
        d.canPatch = notFoundCount <= 3;
        if (d.canPatch)
            d.alreadyPatched = isAlreadyPatched(rom, Kind::Mevd172G);
        return d;
    }

    if (findSeq(rom, kGen1.obdFind, kGen1.obdFindLen) != -1
        || findSeq(rom, kGen1.obdPatch, kGen1.obdPatchLen) != -1) {
        d.kind = Kind::Gen1;
        d.alreadyPatched = isAlreadyPatched(rom, Kind::Gen1);
        return d;
    }

    const bool gen2Post =
        static_cast<uint64_t>(rom.size()) >= static_cast<uint64_t>(kGen2ObdOffset) + sizeof(kGen2Locked)
        && (seqEq(rom, kGen2ObdOffset, kGen2Locked, 4)
            || seqEq(rom, kGen2ObdOffset, kGen2Unlocked, 4));

    const bool gen2Pre =
        findSeq(rom, kGen2Pre.obdFind, kGen2Pre.obdFindLen) != -1
        || findSeq(rom, kGen2Pre.obdPatch, kGen2Pre.obdPatchLen) != -1
        || (static_cast<uint64_t>(rom.size()) >=
                static_cast<uint64_t>(kGen2Pre.unlockOffset) + sizeof(kPre2020Unlock)
            && seqEq(rom, kGen2Pre.unlockOffset, kGen2Pre.unlockApplied,
                     static_cast<int>(sizeof(kPre2020Unlock))));

    if (gen2Post) {
        d.kind = Kind::Gen2;
        d.alreadyPatched = isAlreadyPatched(rom, Kind::Gen2);
        return d;
    }
    if (gen2Pre) {
        d.kind = Kind::Gen2Pre2020;
        d.alreadyPatched = isAlreadyPatched(rom, Kind::Gen2Pre2020);
        return d;
    }
    return d;
}

ApplyReport applyUnlock(QByteArray &rom, const Detection &d)
{
    switch (d.kind) {
    case Kind::Gen1:          return patchStandard(rom, kGen1);
    case Kind::Gen2Pre2020:   return patchStandard(rom, kGen2Pre);
    case Kind::Gen2:          return patchGen2ObdOnly(rom);
    case Kind::Mevd172G:      return patchMevd17(rom);
    default: {
        ApplyReport rep;
        rep.messages << QStringLiteral("No supported ECU detected — nothing to do.");
        return rep;
    }
    }
}

}
