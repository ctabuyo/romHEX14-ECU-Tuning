#include <QByteArray>

#include <cstring>
#include <iostream>

#include "obdunlock/obdunlock.h"

namespace {

int g_testsRun = 0;
int g_testsPassed = 0;

#define TEST_ASSERT(condition, description) do { \
    ++g_testsRun; \
    if (condition) { \
        ++g_testsPassed; \
        std::cout << "  [PASS] " << description << '\n'; \
    } else { \
        std::cerr << "  [FAIL] " << description << " (" << __FILE__ \
                  << ':' << __LINE__ << ")\n"; \
    } \
} while (false)

QByteArray bytes(const char *hex)
{
    return QByteArray::fromHex(hex);
}

void put(QByteArray &rom, qsizetype offset, const char *hex)
{
    const QByteArray value = bytes(hex);
    std::memcpy(rom.data() + offset, value.constData(),
                static_cast<size_t>(value.size()));
}

bool has(const QByteArray &rom, qsizetype offset, const char *hex)
{
    const QByteArray value = bytes(hex);
    return rom.mid(offset, value.size()) == value;
}

void testNoMatch()
{
    const QByteArray rom(4096, static_cast<char>(0xAA));
    const auto detection = ObdUnlock::detect(rom);
    TEST_ASSERT(detection.kind == ObdUnlock::Kind::None,
                "Unrelated data is rejected");
}

void testGen1()
{
    QByteArray rom(0x40264, '\0');
    put(rom, 0x100, "802A03E207");

    auto detection = ObdUnlock::detect(rom);
    TEST_ASSERT(detection.kind == ObdUnlock::Kind::Gen1,
                "Gen1 signature is detected");
    TEST_ASSERT(!detection.alreadyPatched, "Gen1 starts locked");

    const auto report = ObdUnlock::applyUnlock(rom, detection);
    TEST_ASSERT(report.success, "Gen1 patch succeeds");
    TEST_ASSERT(has(rom, 0x40260, "397EB688"),
                "Gen1 unlock bytes are written");
    TEST_ASSERT(has(rom, 0x100, "8048034400"),
                "Gen1 OBD bytes are written");
    TEST_ASSERT(ObdUnlock::detect(rom).alreadyPatched,
                "Patched Gen1 is recognized");
}

void testGen2Post2020()
{
    QByteArray rom(0x2CA60, '\0');
    put(rom, 0x2CA5C, "F627DA0E");

    auto detection = ObdUnlock::detect(rom);
    TEST_ASSERT(detection.kind == ObdUnlock::Kind::Gen2,
                "Gen2 post-2020 signature is detected");
    TEST_ASSERT(ObdUnlock::applyUnlock(rom, detection).success,
                "Gen2 post-2020 patch succeeds");
    TEST_ASSERT(has(rom, 0x2CA5C, "8202DA0E"),
                "Gen2 post-2020 OBD bytes are written");
    TEST_ASSERT(ObdUnlock::detect(rom).alreadyPatched,
                "Patched Gen2 post-2020 is recognized");
}

void testGen2Pre2020()
{
    QByteArray rom(0x5F7E0, '\0');
    put(rom, 0x200, "91100026F627");

    auto detection = ObdUnlock::detect(rom);
    TEST_ASSERT(detection.kind == ObdUnlock::Kind::Gen2Pre2020,
                "Gen2 pre-2020 signature is detected");
    TEST_ASSERT(ObdUnlock::applyUnlock(rom, detection).success,
                "Gen2 pre-2020 patch succeeds");
    TEST_ASSERT(has(rom, 0x5F7DC, "38D1BFDC"),
                "Gen2 pre-2020 unlock bytes are written");
    TEST_ASSERT(has(rom, 0x200, "910A00268202"),
                "Gen2 pre-2020 OBD bytes are written");
    TEST_ASSERT(ObdUnlock::detect(rom).alreadyPatched,
                "Patched Gen2 pre-2020 is recognized");
}

void testMevd172G()
{
    struct Patch {
        qsizetype offset;
        const char *find;
        const char *replace;
    };
    static const Patch patches[] = {
        { 0x001BCC, "82038F2320F0D94F040001F3", "82031D002200D10ECDA201F3" },
        { 0x005A60, "C21FA80F",                 "C20FA80F" },
        { 0x005AA0, "8CC0C21F",                 "8CC0C20F" },
        { 0x00FBFC, "393F4CED",                 "6E64DBEE" },
        { 0x00FD8C, "50020100",                 "55020100" },
        { 0x17FBFC, "7D307F5F",                 "7D607F5F" },
        { 0x1BF020, "09000600",                 "09000000" },
    };

    QByteArray rom(0x1BF024, '\0');
    put(rom, 0, "4D4556443137");
    for (const Patch &patch : patches)
        put(rom, patch.offset, patch.find);

    auto detection = ObdUnlock::detect(rom);
    TEST_ASSERT(detection.kind == ObdUnlock::Kind::Mevd172G,
                "MEVD17.2.G signature is detected");
    TEST_ASSERT(detection.canPatch, "Supported MEVD17.2.G is patchable");
    TEST_ASSERT(ObdUnlock::applyUnlock(rom, detection).success,
                "MEVD17.2.G patch succeeds");
    for (const Patch &patch : patches)
        TEST_ASSERT(has(rom, patch.offset, patch.replace),
                    "MEVD17.2.G patch site is written");
    TEST_ASSERT(ObdUnlock::detect(rom).alreadyPatched,
                "Patched MEVD17.2.G is recognized");

    QByteArray unsupported(0x1BF024, '\0');
    put(unsupported, 0, "4D4556443137");
    detection = ObdUnlock::detect(unsupported);
    TEST_ASSERT(detection.kind == ObdUnlock::Kind::Mevd172G,
                "Unsupported MEVD17.2.G still identifies its family");
    TEST_ASSERT(!detection.canPatch,
                "Unexpected MEVD17.2.G patch sites fail closed");
}

void testInvalidApplyFailsClosed()
{
    QByteArray rom(0x2CA60, '\0');
    const QByteArray original = rom;
    ObdUnlock::Detection forced;
    forced.kind = ObdUnlock::Kind::Gen2;
    const auto report = ObdUnlock::applyUnlock(rom, forced);
    TEST_ASSERT(!report.success, "Unexpected Gen2 bytes are rejected");
    TEST_ASSERT(rom == original, "Rejected Gen2 ROM remains unchanged");
}

}

int main()
{
    std::cout << "romHEX14 OBD Unlock Regression Tests\n";
    testNoMatch();
    testGen1();
    testGen2Post2020();
    testGen2Pre2020();
    testMevd172G();
    testInvalidApplyFailsClosed();
    std::cout << "Test Summary: " << g_testsPassed << '/' << g_testsRun
              << " passed.\n";
    return g_testsPassed == g_testsRun ? 0 : 1;
}
