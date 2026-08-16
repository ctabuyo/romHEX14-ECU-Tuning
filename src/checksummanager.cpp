#include "checksummanager.h"
#include "checksums/BoschMED17.h"
#include "checksums/IChecksumPlugin.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPluginLoader>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QRegularExpression>
#include <cstdint>
#include <numeric>

ChecksumManager* ChecksumManager::s_instance = nullptr;

ChecksumManager* ChecksumManager::instance() {
    if (!s_instance)
        s_instance = new ChecksumManager(qApp);
    return s_instance;
}

ChecksumManager::ChecksumManager(QObject* parent) : QObject(parent) {
    buildRegistry();
    loadPlugins();
}

// ─────────────────────────────────────────────────────────────────────────────
// Registry
// ─────────────────────────────────────────────────────────────────────────────

void ChecksumManager::buildRegistry() {
    // devNum, description, hasNative
    // hasNative left false in the community build (native impls are pro-only)
    static const struct { int n; const char* desc; bool native; } kDlls[] = {
        {1,   "BOSCH EDC15 V3.1 PLCC 2x27C010 - ALL BRAND",                                true},
        {2,   "BOSCH EDC15 PSOP 29F400 CR1 - FAL/MERCEDES",                               true},
        {3,   "BOSCH EDC15 PSOP 29F400 CR2 - BMW/ROVER",                                  true},
        {4,   "",                                                                           false},
        {5,   "BOSCH EDC15 PSOP 29F400 CR2 - ALL BRAND",                                  true},
        {8,   "",                                                                           false},
        {9,   "",                                                                           false},
        {10,  "BOSCH EDC15 - MMC SMART CDI EURO2/EURO3",                                   true},
        {11,  "",                                                                           false},
        {13,  "BOSCH EDC15P,EDC15V,EDC15VM 4.1 - VAG",                                    true},
        {14,  "BOSCH M7.9.x - HYUNDAI/KIA",                                               true},
        {16,  "BOSCH ME2.7.X / ME2.8.X - MERCEDES",                                       true},
        {17,  "BOSCH M1.5.5 - FAL/OPEL",                                                  false},
        {18,  "",                                                                           false},
        {19,  "BOSCH ME7.1.1 - BMW",                                                       true},
        {20,  "SIEMENS MS41 - BMW",                                                        false},
        {21,  "BOSCH ME7.XXX - VAG 99-01",                                                 true},
        {22,  "BOSCH BMS46 1.9l - BMW",                                                    true},
        {23,  "SIEMENS MSS50 MSS52 MSS54 - BMW M3/M5",                                    false},
        {24,  "LUCAS D",                                                                    false},
        {25,  "SIEMENS SIRIUS32 29F200 - DAEWOO/RENAULT/VOLVO",                            false},
        {26,  "SIEMENS SID80X 29F400BB - PSA/SUZUKI HDI",                                  false},
        {27,  "BOSCH ME7.2 / ME7.8 - PORSCHE 993/996/BOXSTER",                            true},
        {28,  "SIEMENS SIRIUS 34 29F400 - RENAULT/VOLVO",                                  false},
        {29,  "SIEMENS EMS2 - BMW MINI",                                                    false},
        {31,  "DELPHI DCM1.2 / DDCR - HYUNDAI/KIA/DACIA/NISSAN/RENAULT/SUZUKI 1.5",       false},
        {32,  "BOSCH MP7.2 / M7.4.4 / ME7.4.4 - PSA/RENAULT",                            true},
        {33,  "SIEMENS/SIMOS 3-7.1-9.1 / BOSCH M3.8-5.9 - VAG",                          true},
        {34,  "VISTEON FORD",                                                               false},
        {35,  "SIEMENS SIM4L E/KE - MERCEDES",                                             false},
        {36,  "BOSCH EDC15 4.1 - NISSAN TDI",                                              true},
        {37,  "",                                                                           false},
        {38,  "BOSCH HYBRID ME7.3.1 - FAL",                                                true},
        {39,  "BOSCH EDC15 PSOP 29F400 - CHRYSLER JEEP CRD 2.2/2.5/2.7",                 true},
        {40,  "BOSCH EDC15P - VAG TDI V4.1 2002",                                         true},
        {41,  "",                                                                           false},
        {42,  "BOSCH ME7.XX - VAG EURO4 >01",                                              true},
        {44,  "BOSCH HYBRID ME7.3H4 - FIAT",                                               true},
        {45,  "BOSCH MEG1.0 ECU004/MEG1.1 ECU005 - MCC SMART EURO3/EURO4",               false},
        {46,  "BOSCH ME7.X 29F400/29F800 - VOLVO",                                        true},
        {47,  "SIEMENS SID 804/901 - FORD 1.4/3.0 TDCI",                                  false},
        {48,  "SIEMENS/CONTINENTAL PSOP 29F200/400/800 - BRILLIANCE/CHEVROLET/CHRYSLER",  false},
        {49,  "BOSCH MED7.1.1 - FAL JTS",                                                  true},
        {50,  "",                                                                           false},
        {51,  "BOSCH HYBRID ME7.X - FERRARI",                                              true},
        {52,  "BOSCH EDC16 MPC55X - ALL BRAND",                                            true},
        {53,  "BOSCH MEG1.1 ECU006 - MCC SMART EURO4",                                    false},
        {54,  "",                                                                           false},
        {55,  "LUCAS DIL28C64/PLCC28HC256 - VOLVO TRUCKS",                                 false},
        {56,  "SIEMENS SIM 19/2X/210 EMS 2102/2103 - FORD",                               false},
        {57,  "BOSCH ME1.5.5 / ME7.9.6 / ME7.6.1 - OPEL",                                true},
        {58,  "BOSCH MED9.1 MPC562 - VAG TFSI",                                            true},
        {59,  "",                                                                           false},
        {60,  "SAGEM 2000 - PSA",                                                           false},
        {61,  "MOTOROLA MEMS 3 - LAND ROVER TD5",                                          false},
        {62,  "DELPHI MPC555 - FORD/JAGUAR",                                               false},
        {63,  "BOSCH EDC7 - CASE/IVECO/NEW HOLLAND",                                       false},
        {64,  "MOTOROLA/TEMIC/SIEMENS - SCANIA TRUCK EMS S6/S7",                           false},
        {65,  "DELPHI MPC555 - SSANGYONG KYRON/REXTON/RODIUS",                             false},
        {66,  "BOSCH EDC7/C3 - MAN",                                                       false},
        {67,  "BOSCH EDC16+ MPC56X - ALL BRAND",                                           true},
        {68,  "SIEMENS SID 201/203/204/803/803A/902 - FORD/JAGUAR/LAND ROVER/PSA",         false},
        {69,  "SIEMENS SIMOS 5WP4 PPD1.1 / SIMOS 6.2 6.3 - VAG",                         true},
        {70,  "BOSCH ME9 - FORD/VOLVO",                                                    true},
        {71,  "BOSCH M7.4 - POLARIS SNOWMOBILE",                                           false},
        {72,  "BOSCH M5.2.2 - PORSCHE",                                                    false},
        {73,  "SAGEM 3000/SIM32/VALEO V40 SH7055/SH7058 - RENAULT",                       false},
        {74,  "MARELLI 48P.xx/MM6LPXX - PSA",                                             false},
        {75,  "BOSCH ME9/ME9+ - BMW",                                                      true},
        {76,  "DELPHI DMCI - DAF TRUCKS",                                                   false},
        {77,  "SIEMENS MS45 - BMW",                                                        false},
        {78,  "SIEMENS MSV70/MSS70 - BMW",                                                 false},
        {79,  "BOSCH ME9.7 - MEB",                                                         true},
        {80,  "",                                                                           false},
        {81,  "DELPHI DCM3.2 - KIA CARNIVAL 2/MERCEDES C-E/SSANGYONG ACTYON/REXTON",      false},
        {82,  "DELPHI - CHEVROLET/ISUZU",                                                   false},
        {83,  "SIEMENS SID301/305/306/307/310/807/EMS2204-2210 - PSA/RENAULT/FORD/VAG",    false},
        {84,  "BOSCH ME7.1.1 MICROPROCESSOR ST1027X - VAG",                                true},
        {85,  "BOSCH MED9.1 FSI MPC564 - VAG",                                             true},
        {86,  "SIEMENS VDO MSS60/MSS65 - BMW M POWER V8/V10",                             false},
        {87,  "BOSCH ME7.8 MICROPROCESSOR ST10XXX - PORSCHE 997",                         true},
        {88,  "BOSCH ME7.4.5 - PSA",                                                       true},
        {89,  "BOSCH ME7.3.2 - FERRARI/MASERATI",                                         true},
        {90,  "BOSCH MEG7.7.0 - SMART",                                                    false},
        {91,  "BOSCH ME7.9.10 / MED7.6.1 - FAL",                                         true},
        {92,  "FO.MO.CO VISTEON DCU 102/104/106 - FAL/FORD/LANDROVER",                    false},
        {93,  "BOSCH MED7.6.2 - FAL",                                                      true},
        {94,  "BOSCH MEDVC17 TC176x/TC179x/TC172x - ALL BRAND",                            true},
        {95,  "BOSCH BMSK ME9+ - BMW MOTORRAD",                                            true},
        {96,  "SIEMENS VDO SDI 3/4/6/7/8/9/10/21 - PORSCHE",                             false},
        {97,  "BOSCH ME7.9.51/M7.9.5 - TOYOTA",                                           true},
        {98,  "VDO SIEMENS MSD80/MSV80/MSD81/MSD85/MSD87/MSV90 - BMW",                    false},
        {100, "DELPHI DDCR/DCR3.x/DCM3.4/3.5/3.7/CRD2.xx/CRD3.xx - FORD/GM/GW",         false},
        {101, "MARELLI 4xx/5xx/6xx/7xx/8xx/9xx/10x/15x - DUCATI/FAL/FCA/OPEL/SUZUKI/VW", false},
        {102, "BOSCH MS6.x - ALL BRAND",                                                    false},
        {103, "VALEO JOHNSON J34P/V34P - PSA",                                             false},
        {104, "",                                                                           false},
        {105, "BOSCH ME 7.6.2/7.6.3/7.6.4/9.1/9.5/9.6 - OPEL",                          true},
        {106, "BOSCH KEFICO M798 - KIA",                                                    false},
        {107, "",                                                                           false},
        {108, "SIEMENS SIMTEC 70/71/75/76/81/90 - OPEL",                                   false},
        {109, "DENSO - LAND ROVER",                                                         false},
        {110, "SIEMENS VDO SIM90T - DODGE",                                                 false},
        {111, "SIEMENS VDO CONTINENTAL SIM2K - CHEVROLET/HYUNDAI",                         false},
        {112, "BOSCH M5.2/M5.2.1 - BMW",                                                   false},
        {113, "BOSCH MED9.1.1 - MASERATI",                                                  true},
        {114, "BOSCH ME 7.9.9 - CHEVROLET/FAL/OPEL",                                      true},
        {115, "SIEMENS VDO - DUCATI",                                                       false},
        {116, "DELPHI DELCO Z22SE ENGINE - OPEL",                                           false},
        {117, "VISTEON FORD ECC V/VI CANLINE - FORD",                                       false},
        {118, "BOSCH ME7.9.7 - DR/LADA/ROVER/KIA",                                        true},
        {119, "MOTOROLA ECM/PCM ECU - MERCURY MARINE",                                      false},
        {120, "",                                                                           false},
        {121, "DENSO",                                                                       false},
        {122, "BOSCH MSE3 - CF MOTOR",                                                      false},
        {123, "BOSCH MED91.5 M58BW016DB & MPC564 - VAG TFSI",                             true},
        {124, "SIEMENS SIM266 - MERCEDES",                                                  false},
        {125, "OPEL DENSO MULTEC",                                                           false},
        {126, "SIEMENS MSE 3.7 ROTAX",                                                      false},
        {127, "BOSCH ME7.8.8 - GREAT WALL MOTOR",                                          true},
        {128, "DELPHI MT80/MT60 - DAEWOO/GEELY",                                           false},
        {129, "",                                                                           false},
        {130, "BOSCH EDC7U1 - MACK TRUCK",                                                  false},
        {131, "SIEMENS SIMOS PCR 2.x - VAG",                                               true},
        {132, "CONTINENTAL TEMIC DDEC6 - FREIGHTLINER MERCEDES TRUCK",                      false},
        {133, "BOSCH EDC7U31 - CASE/NEW HOLLAND TIER4 WITH ADBLUE",                        false},
        {134, "SIEMENS SIMOS 8.x/10.x/11.x/12/18 - VAG",                                  true},
        {135, "",                                                                           false},
        {136, "DENSO - RENAULT 3.0 TDI",                                                    false},
        {137, "FoMoCo CONTINENTAL TC1797 SID208/SID209 - FORD/LAND ROVER",                 false},
        {138, "DELPHI ETC3 - DAF PACCAR TRUCKS",                                            false},
        {139, "MOTOROLA NGC4 - CHRYSLER",                                                    false},
        {140, "CONTINENTAL/SIEMENS VDO PW5xxx/PW8xxx - PROTON/SSANGYONG",                  false},
        {141, "BOSCH ME9.1.1 MPC564 - BUGATTI",                                             true},
        {142, "",                                                                           false},
        {143, "",                                                                           false},
        {144, "BOSCH ME7.7.1 29F400 - MITSUBISHI",                                        true},
        {145, "BOSCH EDC17C49/EDC17CV41 TC1797 - CASE/IVECO FPT/NEW HOLLAND",             true},
        {146, "",                                                                           false},
        {148, "",                                                                           false},
        {149, "",                                                                           false},
        {150, "",                                                                           false},
        {151, "DENSO NEC_NBD - TOYOTA",                                                     false},
        {152, "SIEMENS SID 904/913 - INTERNATIONAL",                                        false},
        {154, "DELPHI DCM6.1/DCM6.2 - FORD/PSA/SSANGYONG/VAG",                            false},
        {155, "MOTOROLA ECM0563 - MAXXFORCE",                                               false},
        {156, "BOSCH EDC17CV42 TC1797 - FENDT/MAN TRUCK",                                   true},
        {157, "SIEMENS SIM271DE/KE - MEB CAR",                                              false},
    };

    for (const auto& d : kDlls) {
        ChecksumDllInfo info;
        info.devNum      = d.n;
        info.description = QString::fromLatin1(d.desc);
        info.filename    = QString("DEV%1.dll").arg(d.n, 3, 10, QChar('0'));
        info.hasNative   = d.native;
        m_dlls.append(info);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ECU type → DLL matching
// ─────────────────────────────────────────────────────────────────────────────

ChecksumDllInfo ChecksumManager::findDllForEcu(const QString& ecuType) const {
    if (ecuType.isEmpty()) return {};
    const QString q = ecuType.toUpper().trimmed();

    int bestScore = 0;
    const ChecksumDllInfo* best = nullptr;

    for (const auto& dll : m_dlls) {
        if (dll.description.isEmpty()) continue;
        const QString desc = dll.description.toUpper();
        int score = 0;

        // Direct substring: ECU type contained in description
        if (desc.contains(q)) score += 100;

        // Split by delimiters and score each token
        const QStringList tokens = q.split(QRegularExpression("[\\s./,_-]+"), Qt::SkipEmptyParts);
        for (const QString& tok : tokens) {
            if (tok.length() < 2) continue;
            if (desc.contains(tok)) score += tok.length() * 3;
        }

        // ECU family keywords — bonus for exact family match
        static const struct { const char* key; int bonus; } kFamilies[] = {
            {"EDC17", 50}, {"EDC16", 50}, {"EDC15", 50},
            {"MED17", 80}, {"MEDVC17", 100}, {"MED9", 40},  {"MED7", 40},
            {"ME7",   40}, {"ME9",  40},  {"ME2",  40},
            {"SIMOS", 40}, {"PCR",  30},  {"PPD",  30},
            {"SID",   30}, {"MSS",  30},  {"MSV",  30}, {"MSD", 30},
            {"MS41",  35}, {"MS45", 35},  {"BMS",  30},
            {"TC1797",40}, {"TC1793",80}, {"TC179", 70}, {"TC176",70}, {"MPC55",35},
            {nullptr, 0}
        };
        for (int i = 0; kFamilies[i].key; i++) {
            const QString k = QString::fromLatin1(kFamilies[i].key);
            if (q.contains(k) && desc.contains(k)) score += kFamilies[i].bonus;
        }

        // DEV094 is the primary ALL BRAND algorithm for MEDVC17 / MED17 / EDC17 / TC179x / TC176x / TC172x
        if (dll.devNum == 94 && (q.contains("MED17") || q.contains("EDC17") || q.contains("TC179") || q.contains("TC176") || q.contains("TC172") || q.contains("MEDVC17"))) {
            score += 200;
        }

        // Penalise if description is for a specific heavy machinery brand when query is general
        static const QStringList kBrands = {"BOSCH","SIEMENS","DELPHI","DENSO",
                                             "MARELLI","MOTOROLA","CONTINENTAL","VISTEON",
                                             "LUCAS","SAGEM","VALEO"};
        for (const QString& brand : kBrands) {
            if (q.contains(brand) && !desc.contains(brand)) score -= 20;
        }

        if (score > bestScore) { bestScore = score; best = &dll; }
    }

    if (best && bestScore > 0) return *best;
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// ROM-based auto-detection
// ─────────────────────────────────────────────────────────────────────────────

static QString scanRomForEcuType(const QByteArray& rom) {
    // ECU families to look for in embedded ASCII strings, ordered by specificity
    static const struct { const char* pat; int priority; } kPatterns[] = {
        {"MED17",  100}, {"EDC17",  100}, {"MEDVC17", 110}, {"TC1793", 105}, {"TC1797", 100},
        {"MED9.",   90}, {"MED7.",   90},
        {"EDC16+",  95}, {"EDC16C",  95}, {"EDC16",   85},
        {"EDC15C",  90}, {"EDC15P",  90}, {"EDC15V",  90}, {"EDC15",   80},
        {"ME9.",    85}, {"ME7.",    85}, {"ME2.",     80},
        {"SIMOS",   80}, {"PCR2",    75}, {"PPD1",     75},
        {"MSS70",   80}, {"MSS65",   80}, {"MSS60",   80}, {"MSS54",   75},
        {"MSS52",   75}, {"MSS50",   75}, {"MSV80",   80}, {"MSV70",   80},
        {"MSD87",   80}, {"MSD85",   80}, {"MSD81",   80}, {"MSD80",   80},
        {"SID208",  75}, {"SID20",   70}, {"SID80",   70}, {"SID",     60},
        {"BMS46",   75}, {"BMS",     60},
        {"MS45",    75}, {"MS41",    75},
        {"TC1767",  85}, {"TC176",   80},
        {nullptr,    0}
    };

    const char* data = rom.constData();
    const int   size = rom.size();

    QString bestMatch;
    int     bestPriority = 0;

    for (int i = 0; i < size - 6; ) {
        const unsigned char c = static_cast<unsigned char>(data[i]);
        if (c < 0x20 || c > 0x7E) { ++i; continue; }

        // Find length of printable ASCII run
        int runLen = 0;
        while (i + runLen < size) {
            const unsigned char r = static_cast<unsigned char>(data[i + runLen]);
            if (r < 0x20 || r > 0x7E) break;
            ++runLen;
        }

        if (runLen >= 5) {
            const QString s  = QString::fromLatin1(data + i, runLen);
            const QString su = s.toUpper();

            for (int j = 0; kPatterns[j].pat; ++j) {
                const QString pat = QString::fromLatin1(kPatterns[j].pat);
                const int idx = su.indexOf(pat);
                if (idx == -1) continue;

                const int prio = kPatterns[j].priority;
                if (prio <= bestPriority) continue;

                // Extend past the pattern to grab version digits/dots
                int end = idx + pat.size();
                while (end < su.size() && (su[end].isDigit() || su[end] == '.' || su[end] == '_'))
                    ++end;

                bestMatch    = s.mid(idx, end - idx);
                bestPriority = prio;
            }
        }

        i += qMax(1, runLen);
    }

    return bestMatch;
}

// Returns true if the structural metadata the algorithm relies on
// (block descriptors, address ranges) fits within the supplied ROM.
static bool structureMatchesAlgorithm(const QByteArray& rom, int devNum)
{
    const int      sz = rom.size();
    const uint8_t* p  = reinterpret_cast<const uint8_t*>(rom.constData());

    auto u16 = [&](int off) -> uint16_t {
        return uint16_t(p[off]) | (uint16_t(p[off+1]) << 8);
    };
    auto u32 = [&](int off) -> uint32_t {
        return uint32_t(p[off]) | (uint32_t(p[off+1])<<8)
             | (uint32_t(p[off+2])<<16) | (uint32_t(p[off+3])<<24);
    };

    // EDC15 family — size must be a multiple of 0x8000, and the 16-bit
    // checksum word at 0x7FFE of the first block must be non-trivial.
    static const int kEdc15[] = {1,2,3,5,10,13,36,39,40,0};
    for (int i = 0; kEdc15[i]; ++i) {
        if (devNum != kEdc15[i]) continue;
        if (sz < 0x8000 || sz % 0x8000 != 0 || sz > 0x100000) return false;
        const uint16_t cs = u16(0x7FFE);
        return cs != 0x0000 && cs != 0xFFFF;
    }

    // ME7 family — block descriptor at 0x300: [start_addr, end_addr, …]
    // Both must be valid ROM offsets with start < end and span >= 16 KB.
    static const int kMe7[] = {
        19,21,22,27,32,38,42,44,46,49,51,57,
        84,87,88,89,91,93,97,105,114,118,127,144,0
    };
    for (int i = 0; kMe7[i]; ++i) {
        if (devNum != kMe7[i]) continue;
        if (sz < 0x40000 || sz < 0x310) return false;
        const uint32_t bstart = u32(0x300);
        const uint32_t bend   = u32(0x304);
        return bstart < uint32_t(sz) && bend <= uint32_t(sz)
            && bstart < bend && (bend - bstart) >= 0x4000;
    }

    // MED9/ME9 family — block descriptor typically at 0x400; size 512 KB–2 MB.
    static const int kMe9[] = {58,70,75,79,85,95,123,141,0};
    for (int i = 0; kMe9[i]; ++i) {
        if (devNum != kMe9[i]) continue;
        if (sz < 0x80000 || sz > 0x200000) return false;
        if (sz < 0x410) return true; // can't check descriptor
        const uint32_t bstart = u32(0x400);
        const uint32_t bend   = u32(0x404);
        if (bstart == 0 || bstart == 0xFFFFFFFFu) return sz >= 0x80000; // no descriptor
        return bstart < uint32_t(sz) && bend <= uint32_t(sz)
            && bstart < bend && (bend - bstart) >= 0x4000;
    }

    // EDC16 / EDC16+ — always 2 MB (0x200000).
    if (devNum == 52 || devNum == 67)
        return sz == 0x200000 || sz == 0x100000;

    // MED17 / EDC17 (TriCore) — 1–4 MB.
    if (devNum == 94 || devNum == 145 || devNum == 156)
        return sz >= 0x100000 && sz <= 0x400000;

    // SIMOS — various, just require size >= 512 KB.
    static const int kSimos[] = {33,69,131,134,0};
    for (int i = 0; kSimos[i]; ++i)
        if (devNum == kSimos[i]) return sz >= 0x80000;

    return true; // no specific check for this devNum — accept
}

ChecksumDllInfo ChecksumManager::autoDetect(const QByteArray& rom, const QString& ecuType) const
{
    // 1. Scan ROM for an embedded ASCII ECU identifier.
    if (!rom.isEmpty()) {
        const QString romId = scanRomForEcuType(rom);
        if (!romId.isEmpty()) {
            const ChecksumDllInfo candidate = findDllForEcu(romId);
            // Accept only if the address ranges used by this algorithm
            // actually fit within the ROM (structural sanity check).
            if (candidate.devNum != 0 && structureMatchesAlgorithm(rom, candidate.devNum))
                return candidate;
        }
    }

    // 2. Fall back to the user-supplied ECU type string — always trusted.
    return findDllForEcu(ecuType);
}

// ─────────────────────────────────────────────────────────────────────────────
// Path helpers
// ─────────────────────────────────────────────────────────────────────────────

QString ChecksumManager::dllFolder() const {
    // Look next to the exe, then in app dir / ChecksumDLL subdirectory
    const QString appDir = QCoreApplication::applicationDirPath();
    for (const QString& candidate : {
             appDir + "/ChecksumDLL",
             appDir + "/../ChecksumDLL",
             appDir,
         }) {
        if (QDir(candidate).exists("DEV001.dll"))
            return QDir::cleanPath(candidate);
    }
    return appDir + "/ChecksumDLL"; // fallback even if not present
}

QString ChecksumManager::helperPath() const {
    const QString appDir = QCoreApplication::applicationDirPath();
    for (const QString& candidate : {
             dllFolder() + "/checksumhelper.exe",   // prefer inside ChecksumDLL folder
             appDir + "/checksumhelper.exe",
             appDir + "/../checksumhelper.exe",
         }) {
        if (QFileInfo::exists(candidate))
            return QDir::cleanPath(candidate);
    }
    return dllFolder() + "/checksumhelper.exe";
}

bool ChecksumManager::isDllAvailable(const ChecksumDllInfo& dll) const {
    if (dll.devNum == 0) return false;
    return QFileInfo::exists(dllFolder() + "/" + dll.filename);
}

bool ChecksumManager::isHelperAvailable() const {
#ifdef Q_OS_WIN
    return QFileInfo::exists(helperPath());
#else
    return false;
#endif
}

QStringList ChecksumManager::pluginSearchPaths() const {
    QStringList paths;
    const QString appDir = QCoreApplication::applicationDirPath();
    paths << appDir + "/plugins/checksums";
    paths << appDir + "/../plugins/checksums";
    paths << appDir + "/plugins";
    paths << QDir::cleanPath(appDir + "/../../plugins/checksums");
    paths << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/checksums";
    return paths;
}

void ChecksumManager::loadPlugins() {
    const QStringList searchDirs = pluginSearchPaths();
    for (const QString& dirPath : searchDirs) {
        if (!QDir(dirPath).exists()) continue;

        QDirIterator it(dirPath, QStringList() << "*.so" << "*.dylib" << "*.dll",
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString pluginFile = it.next();
            QPluginLoader loader(pluginFile);
            QObject* instance = loader.instance();
            if (!instance) continue;

            auto* plugin = qobject_cast<Checksum::IChecksumPlugin*>(instance);
            if (!plugin) {
                // Fallback for non-qobject or static cast
                plugin = dynamic_cast<Checksum::IChecksumPlugin*>(instance);
            }
            if (plugin) {
                const uint32_t dNum = plugin->devNum();
                m_plugins[dNum] = plugin;

                // Update registry info
                for (auto& info : m_dlls) {
                    if (static_cast<uint32_t>(info.devNum) == dNum) {
                        info.hasNative = true;
                        break;
                    }
                }
            }
        }
    }
}


// ─────────────────────────────────────────────────────────────────────────────
// 32-bit DLL bridge (Windows only)
// ─────────────────────────────────────────────────────────────────────────────

ChecksumResult ChecksumManager::runHelper(const QByteArray& romIn, const ChecksumDllInfo& dll,
                                           bool correctMode, QByteArray& romOut, QString& errorMsg) {
#ifdef Q_OS_WIN
    // DLL loading is performed dynamically by checksumhelper.exe (32-bit subprocess).
    // The DLLs are NOT loaded into rx14's address space — this avoids 32/64-bit mismatch
    // and keeps the main process clean. checksumhelper.exe uses LoadLibrary at runtime.
    const QString helper = helperPath();
    if (!QFileInfo::exists(helper)) {
        errorMsg = tr("checksumhelper.exe not found.\n\n"
                      "The checksum DLLs are 32-bit and require a 32-bit bridge process.\n"
                      "Please build checksumhelper.exe (see tools/checksumhelper/) or\n"
                      "place it next to rx14.exe.\n\n"
                      "Expected path: %1").arg(helper);
        return ChecksumResult::Unsupported;
    }
    const QString dllPath = dllFolder() + "/" + dll.filename;
    if (!QFileInfo::exists(dllPath)) {
        errorMsg = tr("DLL not found: %1").arg(dllPath);
        return ChecksumResult::Unsupported;
    }

    // Write ROM to temp files — use QTemporaryFile only to reserve a unique filename
    // per call (autoRemove disabled so the helper process can open it on Windows).
    QString tmpInPath;
    QString tmpOutPath;
    {
        QTemporaryFile inTmp(QDir::tempPath() + "/rx14_chk_in_XXXXXX.bin");
        inTmp.setAutoRemove(false);
        if (!inTmp.open()) {
            errorMsg = tr("Failed to create temporary files");
            return ChecksumResult::Error;
        }
        tmpInPath = inTmp.fileName();
        inTmp.write(romIn);
        inTmp.close();

        QTemporaryFile outTmp(QDir::tempPath() + "/rx14_chk_out_XXXXXX.bin");
        outTmp.setAutoRemove(false);
        if (!outTmp.open()) {
            QFile::remove(tmpInPath);
            errorMsg = tr("Failed to create temporary files");
            return ChecksumResult::Error;
        }
        tmpOutPath = outTmp.fileName();
        outTmp.close();
    }

    const QString opcode = correctMode ? "103" : "102";
    QProcess proc;
    // Set working directory to the DLL folder so runtime deps (gmp.dll, msvcr80.dll,
    // mfc80.dll) are found alongside the checksum DLLs
    proc.setWorkingDirectory(dllFolder());
    proc.start(helper, {dllPath, tmpInPath, tmpOutPath, opcode});
    if (!proc.waitForFinished(15000)) {
        errorMsg = tr("Checksum helper process timed out");
        return ChecksumResult::Error;
    }

    const QByteArray out = proc.readAllStandardOutput().trimmed();
    const QByteArray err = proc.readAllStandardError().trimmed();
    const int exitCode = proc.exitCode();
    qDebug() << "checksumhelper:" << "exit=" << exitCode
             << "stdout=" << out << "stderr=" << err
             << "dll=" << dll.filename << "opcode=" << opcode
             << "inSize=" << romIn.size()
             << "outFile=" << tmpOutPath
             << "outExists=" << QFileInfo::exists(tmpOutPath);

    if (correctMode) {
        // Always try to read the output file — DLLs may return non-zero on success
        QFile f(tmpOutPath);
        if (f.open(QIODevice::ReadOnly)) {
            QByteArray corrected = f.readAll();
            if (corrected.size() == romIn.size()) {
                romOut = corrected;
                return ChecksumResult::OK;
            }
        }
        if (exitCode == 0) {
            errorMsg = tr("Failed to read corrected ROM from helper");
            return ChecksumResult::Error;
        }
    }

    if (!correctMode) {
        if (exitCode == 0) return ChecksumResult::OK;
        if (exitCode == 1) return ChecksumResult::Mismatch;
    }

    // Clean up temp files
    QFile::remove(tmpInPath);
    QFile::remove(tmpOutPath);

    // Parse JSON error
    QJsonDocument doc = QJsonDocument::fromJson(out);
    if (doc.isNull() || !doc.isObject()) {
        errorMsg = tr("Invalid JSON response from checksum helper");
        return ChecksumResult::Error;
    }
    const QJsonObject j = doc.object();
    errorMsg = j.value("error").toString(tr("Checksum helper returned exit code %1").arg(exitCode));
    if (j.contains("win32")) {
        const qint64 win32 = (qint64)j.value("win32").toDouble();
        errorMsg += tr(" (Windows error %1)").arg(win32);
        if (errorMsg.contains("LoadLibrary")) {
            // 126 = dependency not found, 14001 = SxS activation context failed —
            // both mean the VC++ 2005 SP1 x86 runtime the DLLs link against is missing
            if (win32 == 126 || win32 == 14001)
                errorMsg += "\n\n" + tr("The checksum DLL could not be loaded because a runtime "
                                        "dependency is missing. Please install the Microsoft "
                                        "Visual C++ 2005 SP1 Redistributable (x86) and try again.");
            else if (win32 == 193)
                errorMsg += "\n\n" + tr("The DLL is not a valid 32-bit library.");
        }
    }
    return ChecksumResult::Error;
#else
    Q_UNUSED(romIn); Q_UNUSED(dll); Q_UNUSED(correctMode); Q_UNUSED(romOut);
    errorMsg = tr("DLL bridge not available on this platform (Windows only)");
    return ChecksumResult::Unsupported;
#endif
}

ChecksumResult ChecksumManager::bridgeVerify(const QByteArray& rom, const ChecksumDllInfo& dll, QString& errorMsg) {
    QByteArray dummy;
    return runHelper(rom, dll, false, dummy, errorMsg);
}

ChecksumResult ChecksumManager::bridgeCorrect(QByteArray& rom, const ChecksumDllInfo& dll, QString& errorMsg) {
    QByteArray out;
    ChecksumResult r = runHelper(rom, dll, true, out, errorMsg);
    if (r == ChecksumResult::OK && !out.isEmpty())
        rom = out;
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API — DLL bridge only (native implementations are pro-only)
// ─────────────────────────────────────────────────────────────────────────────

ChecksumResult ChecksumManager::verify(const QByteArray& rom, const ChecksumDllInfo& dll, QString& errorMsg) {
    if (dll.devNum == 0) {
        errorMsg = tr("Unknown ECU \u2014 no checksum DLL matched");
        return ChecksumResult::Unsupported;
    }
    // 1. Dynamic Checksum Plugin
    if (m_plugins.contains(dll.devNum) && m_plugins[dll.devNum]) {
        int r = m_plugins[dll.devNum]->verify(rom, errorMsg);
        if (r == 0) return ChecksumResult::OK;
        if (r == 1) return ChecksumResult::Mismatch;
        return ChecksumResult::Error;
    }
    // 2. Built-in C++ Native Engine Fallback
    if (dll.devNum == 94) {
        Checksum::BoschMED17::Status st = Checksum::BoschMED17::verify(rom, errorMsg);
        if (st == Checksum::BoschMED17::Status::OK) return ChecksumResult::OK;
        if (st == Checksum::BoschMED17::Status::Mismatch) return ChecksumResult::Mismatch;
        return ChecksumResult::Error;
    }
#ifdef Q_OS_WIN
    // Windows: use the vendor DLL bridge (checksumhelper.exe + vendor DLLs).
    if (isHelperAvailable() && isDllAvailable(dll))
        return bridgeVerify(rom, dll, errorMsg);
#endif
#ifdef Q_OS_WIN
    errorMsg = tr("checksumhelper.exe or DLL not found for %1").arg(dll.description);
#else
    errorMsg = tr("Checksum support for %1 requires the Windows "
                  "DLL bridge (checksumhelper.exe + the vendor checksum DLLs).")
                   .arg(dll.description);
#endif
    return ChecksumResult::Unsupported;
}

ChecksumResult ChecksumManager::correct(QByteArray& rom, const ChecksumDllInfo& dll, QString& errorMsg) {
    if (dll.devNum == 0) {
        errorMsg = tr("Unknown ECU \u2014 no checksum DLL matched");
        return ChecksumResult::Unsupported;
    }
    // 1. Dynamic Checksum Plugin
    if (m_plugins.contains(dll.devNum) && m_plugins[dll.devNum]) {
        int r = m_plugins[dll.devNum]->correct(rom, errorMsg);
        if (r == 0) return ChecksumResult::OK;
        return ChecksumResult::Error;
    }
    // 2. Built-in C++ Native Engine Fallback
    if (dll.devNum == 94) {
        Checksum::BoschMED17::Status st = Checksum::BoschMED17::correct(rom, errorMsg);
        if (st == Checksum::BoschMED17::Status::OK) return ChecksumResult::OK;
        if (st == Checksum::BoschMED17::Status::Mismatch) return ChecksumResult::Mismatch;
        return ChecksumResult::Error;
    }
#ifdef Q_OS_WIN
    if (isHelperAvailable() && isDllAvailable(dll))
        return bridgeCorrect(rom, dll, errorMsg);
#endif
#ifdef Q_OS_WIN
    errorMsg = tr("checksumhelper.exe or DLL not found for %1").arg(dll.description);
#else
    errorMsg = tr("Checksum support for %1 requires the Windows "
                  "DLL bridge (checksumhelper.exe + the vendor checksum DLLs).")
                   .arg(dll.description);
#endif
    return ChecksumResult::Unsupported;
}

ChecksumResult ChecksumManager::verifyForEcu(const QByteArray& rom, const QString& ecuType, QString& errorMsg) {
    return verify(rom, findDllForEcu(ecuType), errorMsg);
}

ChecksumResult ChecksumManager::correctForEcu(QByteArray& rom, const QString& ecuType, QString& errorMsg) {
    return correct(rom, findDllForEcu(ecuType), errorMsg);
}

// ─────────────────────────────────────────────────────────────────────────────
// Platform note
// ─────────────────────────────────────────────────────────────────────────────

QString ChecksumManager::platformNote() {
#ifdef Q_OS_WIN
    return QString(); // No warning on Windows
#else
    return tr("Checksum verify/correct requires the Windows DLL bridge "
              "(checksumhelper.exe + the vendor checksum DLLs), so it is "
              "unavailable on this platform.");
#endif
}
