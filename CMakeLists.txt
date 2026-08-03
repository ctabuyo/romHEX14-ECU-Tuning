cmake_minimum_required(VERSION 3.16)

# ── Version — single source of truth: top-level VERSION file ─────────────────
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" RX14_VERSION_STRING)
string(STRIP "${RX14_VERSION_STRING}" RX14_VERSION_STRING)
# Extract numeric MAJOR.MINOR.PATCH for project() (which rejects pre-release suffixes)
if(NOT RX14_VERSION_STRING MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    message(FATAL_ERROR "VERSION file must start with MAJOR.MINOR.PATCH; got '${RX14_VERSION_STRING}'")
endif()
set(RX14_VERSION_MAJOR   "${CMAKE_MATCH_1}")
set(RX14_VERSION_MINOR   "${CMAKE_MATCH_2}")
set(RX14_VERSION_PATCH   "${CMAKE_MATCH_3}")
set(RX14_VERSION_NUMERIC "${RX14_VERSION_MAJOR}.${RX14_VERSION_MINOR}.${RX14_VERSION_PATCH}")

project(RX14 VERSION ${RX14_VERSION_NUMERIC} LANGUAGES C CXX)

include(CTest)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/version.h"
    @ONLY)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(RX14_PRO_BUILD "Include account system, update checker, and premium feature gates" OFF)

# ── Debug RPC + on-disk D:\rx14-debug logger ────────────────────────────────
# When enabled, builds in a TCP JSON-line RPC server (127.0.0.1:48714) that
# lets external dev tooling query UI state, trigger actions, and grab widget
# screenshots.  Also redirects the file logger to D:\rx14-debug\rx14.log so
# the dev workspace doesn't fill up the system drive.  Default ON for any
# non-Release build.
if(NOT DEFINED RX14_DEBUG_RPC_DEFAULT)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(RX14_DEBUG_RPC_DEFAULT OFF)
    else()
        set(RX14_DEBUG_RPC_DEFAULT ON)
    endif()
endif()
option(RX14_DEBUG_RPC "Enable in-app TCP debug RPC + D:\\rx14-debug logging" ${RX14_DEBUG_RPC_DEFAULT})

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent Network Sql LinguistTools)
find_package(ZLIB QUIET)

# ── BLAKE3 (portable C, no SIMD) ────────────────────────────────────────────
add_library(blake3 STATIC
    third_party/blake3/blake3.c
    third_party/blake3/blake3_dispatch.c
    third_party/blake3/blake3_portable.c
)
target_include_directories(blake3 PUBLIC third_party/blake3)
target_compile_definitions(blake3 PRIVATE
    BLAKE3_NO_SSE2 BLAKE3_NO_SSE41 BLAKE3_NO_AVX2 BLAKE3_NO_AVX512
    BLAKE3_USE_NEON=0
)

# ── Lua 5.4 (vendored — see third_party/lua-5.4/) ──────────────────────────
# Sprint L spec §4. Core sources only (no lua.c REPL, no luac.c compiler).
set(LUA_CORE_SOURCES
    third_party/lua-5.4/src/lapi.c
    third_party/lua-5.4/src/lauxlib.c
    third_party/lua-5.4/src/lbaselib.c
    third_party/lua-5.4/src/lcode.c
    third_party/lua-5.4/src/lcorolib.c
    third_party/lua-5.4/src/lctype.c
    third_party/lua-5.4/src/ldblib.c
    third_party/lua-5.4/src/ldebug.c
    third_party/lua-5.4/src/ldo.c
    third_party/lua-5.4/src/ldump.c
    third_party/lua-5.4/src/lfunc.c
    third_party/lua-5.4/src/lgc.c
    third_party/lua-5.4/src/linit.c
    third_party/lua-5.4/src/liolib.c
    third_party/lua-5.4/src/llex.c
    third_party/lua-5.4/src/lmathlib.c
    third_party/lua-5.4/src/lmem.c
    third_party/lua-5.4/src/loadlib.c
    third_party/lua-5.4/src/lobject.c
    third_party/lua-5.4/src/lopcodes.c
    third_party/lua-5.4/src/loslib.c
    third_party/lua-5.4/src/lparser.c
    third_party/lua-5.4/src/lstate.c
    third_party/lua-5.4/src/lstring.c
    third_party/lua-5.4/src/lstrlib.c
    third_party/lua-5.4/src/ltable.c
    third_party/lua-5.4/src/ltablib.c
    third_party/lua-5.4/src/ltm.c
    third_party/lua-5.4/src/lundump.c
    third_party/lua-5.4/src/lutf8lib.c
    third_party/lua-5.4/src/lvm.c
    third_party/lua-5.4/src/lzio.c
)
add_library(lua_static STATIC ${LUA_CORE_SOURCES})
set_target_properties(lua_static PROPERTIES
    AUTOMOC OFF AUTORCC OFF AUTOUIC OFF)
target_include_directories(lua_static SYSTEM PUBLIC third_party/lua-5.4/src)
if(WIN32)
    target_compile_definitions(lua_static PUBLIC LUA_USE_WINDOWS)
endif()
target_compile_options(lua_static PRIVATE -w)  # silence Lua's own warnings

set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/commandpalette.cpp
    src/uiwidgets.cpp
    src/brandlogo.cpp
    src/hexwidget.cpp
    src/a2lparser.cpp
    src/a2limportdialog.cpp
    src/waveformwidget.cpp
    src/waveformeditor.cpp
    src/map3dwidget.cpp
    src/maplistwidget.cpp
    src/mapoverlay.cpp
    src/project.cpp
    src/projectview.cpp
    src/newprojectdialog.cpp
    src/aboutdialog.cpp
    src/aifunctionsdlg.cpp
    src/featuregate.h
    src/communityupdatechecker.cpp
    src/communityupdatechecker.h
    src/ecudetector.cpp
    src/romparser.cpp
    src/appconfig.cpp
    src/configdialog.cpp
    src/romlinker.cpp
    src/romlinkdialog.cpp
    src/rompatch.cpp
    src/mappack.cpp
    src/patcheditordlg.cpp
    src/mappackdlg.cpp
    src/logger.cpp
    src/mappropertiesdlg.cpp
    src/projectpropertiesdlg.cpp
    src/aiprovider.cpp
    src/aitoolexecutor.cpp
    src/aiassistant.cpp
    src/projectregistry.cpp
    src/projectmanagerdlg.cpp
    src/introwizard.cpp
    src/createmapdlg.cpp
    src/dtcdialog.cpp
    src/diffpanel.cpp
    src/hexcomparedlg.cpp
    src/alignmentmap.cpp
    src/olsparser.cpp
    src/kpimportdlg.cpp
    src/map3dsimwidget.cpp
    src/vehicledb.cpp
    src/io/ols/CArchiveReader.cpp
    src/io/ols/OlsHeader.cpp
    src/io/ols/OlsMagicScanner.cpp
    src/io/ols/OlsProjectMetadata.cpp
    src/io/ols/OlsVersionDirectory.cpp
    src/io/ols/OlsRomExtractor.cpp
    src/io/ols/OlsKennfeldParser.cpp
    src/io/ols/OlsImporter.cpp
    src/io/ols/KpImporter.cpp
    src/module/ModuleRegistry.cpp
    src/module/BcmProfile.cpp
    src/module/ModulePanel.cpp
    src/module/ToggleSwitch.cpp
    src/io/ols/OlsProjectBuilder.cpp
    src/io/ols/MapAutoDetect.cpp
    src/io/ols/EcuAutoDetect.cpp
    src/io/ols/ZipDecompressor.cpp
    src/io/rx14container.cpp
    src/util/Blake3.cpp
    # src/dtc/* — entire DTC port REMOVED.  Detection, DTC disable and
    # feature application now live on the cloudfx server (see CHANGELOG).
    # The community client only sends ROM bytes + family hint and consumes
    # the result.
    src/savepoints/SavepointManager.cpp
    src/savepoints/SavepointsPanel.cpp
    src/edit/SliderEditDlg.cpp
    src/annotations/AnnotationStore.cpp
    src/io/MapListExporter.cpp
    src/io/TuneReport.cpp
    src/io/XdfIo.cpp
    src/io/vag/FrfCodec.cpp
    src/io/vag/Aes128.cpp
    src/io/vag/FrfExtractor.cpp
    src/frfimportdlg.cpp
    src/valuesearchdlg.cpp
    src/edit/MapFingerprint.cpp
    src/edit/FindSimilarMapsDlg.cpp
    src/io/winols/WinOlsCatalogReader.cpp
    src/io/winols/WinOlsConfig.cpp
    src/io/winols/WinOlsOpener.cpp
    src/io/winols/OlsCfgParser.cpp
    src/io/winols/RomFingerprint.cpp
    src/io/winols/SimilarityIndex.cpp
    src/io/winols/SimilarFilesDlg.cpp
    src/io/winols/BuildIndexProgressDlg.cpp
    src/io/winols/WolsTwinFinder.cpp
    src/io/winols/WolsCatalogStore.cpp
    src/io/legion/Legion.cpp
    src/io/legion/LegionDlg.cpp
    src/io/legion/LegionPreviewWidget.cpp
    src/io/legion/LegionHarvestWorker.cpp
    src/cloud/CloudClient.cpp
    src/cloud/CloudToolsDlg.cpp
    src/datalog/LogTable.cpp
    src/datalog/LogReader.cpp
    src/datalog/CanonicalSignal.cpp
    src/datalog/EcuFamily.cpp
    src/datalog/ChannelAlias.cpp
    src/datalog/UnitNormalizer.cpp
    src/datalog/SidecarLoader.cpp
    src/datalog/PullDetector.cpp
    src/datalog/TimeSeriesPlotWidget.cpp
    src/datalog/ChannelTreeWidget.cpp
    src/datalog/LogViewerWindow.cpp
    src/datalog/CompareLogsDialog.cpp
    src/lua/LuaEngine.cpp
    src/lua/LuaConstants.cpp
    src/lua/LuaApi_Global.cpp
    src/lua/LuaApi_Http.cpp
    src/lua/LuaApi_Project.cpp
    src/lua/LuaApi_Stubs.cpp
    src/debug/DebugLog.cpp
)

# ── Debug RPC sources (gated by RX14_DEBUG_RPC) ─────────────────────────────
# DebugLog (Q_LOGGING_CATEGORY definitions) is always compiled — release-mode
# code paths use qCInfo(catFind) etc., they're free and don't drag in the RPC
# server.  Only DebugRpc itself is gated.
if(RX14_DEBUG_RPC)
    list(APPEND SOURCES
        src/debug/DebugRpc.cpp
    )
endif()

set(HEADERS
    src/mainwindow.h
    src/commandpalette.h
    src/uiwidgets.h
    src/brandlogo.h
    src/hexwidget.h
    src/a2lparser.h
    src/a2limportdialog.h
    src/waveformwidget.h
    src/waveformeditor.h
    src/map3dwidget.h
    src/maplistwidget.h
    src/mapoverlay.h
    src/romdata.h
    src/project.h
    src/projectview.h
    src/newprojectdialog.h
    src/aboutdialog.h
    src/aifunctionsdlg.h
    src/ecudetector.h
    src/romparser.h
    src/appconfig.h
    src/configdialog.h
    src/romlinker.h
    src/romlinkdialog.h
    src/rompatch.h
    src/mappack.h
    src/patcheditordlg.h
    src/mappackdlg.h
    src/logger.h
    src/mappropertiesdlg.h
    src/projectpropertiesdlg.h
    src/aiprovider.h
    src/aitoolexecutor.h
    src/aiassistant.h
    src/projectregistry.h
    src/projectmanagerdlg.h
    src/introwizard.h
    src/createmapdlg.h
    src/dtcdialog.h
    src/diffpanel.h
    src/alignmentmap.h
    src/olsparser.h
    src/kpimportdlg.h
    src/map3dsimwidget.h
    src/vehicledb.h
    src/io/ols/CArchiveReader.h
    src/io/ols/OlsHeader.h
    src/io/ols/OlsMagicScanner.h
    src/io/ols/OlsProjectMetadata.h
    src/io/ols/OlsVersionDirectory.h
    src/io/ols/OlsRomExtractor.h
    src/io/ols/OlsKennfeldParser.h
    src/io/ols/OlsImporter.h
    src/io/ols/KpImporter.h
    src/module/ModuleData.h
    src/module/ModuleProfile.h
    src/module/ModuleRegistry.h
    src/module/BcmProfile.h
    src/module/ModulePanel.h
    src/module/ToggleSwitch.h
    src/io/ols/OlsProjectBuilder.h
    src/io/ols/MapAutoDetect.h
    src/io/ols/EcuAutoDetect.h
    src/io/ols/ZipDecompressor.h
    src/io/rx14container.h
    src/util/Blake3.h
    src/savepoints/SavepointManager.h
    src/savepoints/SavepointsPanel.h
    src/edit/SliderEditDlg.h
    src/annotations/AnnotationStore.h
    src/io/MapListExporter.h
    src/edit/MapFingerprint.h
    src/edit/FindSimilarMapsDlg.h
    src/io/winols/WinOlsCatalogReader.h
    src/io/winols/WinOlsConfig.h
    src/io/winols/WinOlsOpener.h
    src/io/winols/OlsCfgParser.h
    src/io/winols/RomFingerprint.h
    src/io/winols/SimilarityIndex.h
    src/io/winols/SimilarFilesDlg.h
    src/io/winols/BuildIndexProgressDlg.h
    src/io/winols/WolsTwinFinder.h
    src/io/winols/WolsCatalogStore.h
    src/cloud/CloudClient.h
    src/cloud/CloudToolsDlg.h
    src/datalog/LogTable.h
    src/datalog/LogReader.h
    src/datalog/CanonicalSignal.h
    src/datalog/EcuFamily.h
    src/datalog/ChannelAlias.h
    src/datalog/UnitNormalizer.h
    src/datalog/SidecarLoader.h
    src/datalog/PullDetector.h
    src/datalog/TimeSeriesPlotWidget.h
    src/datalog/ChannelTreeWidget.h
    src/datalog/LogViewerWindow.h
    src/datalog/CompareLogsDialog.h
    src/lua/LuaEngine.h
    src/lua/LuaPropertyIds.h
    src/debug/DebugLog.h
)

if(RX14_DEBUG_RPC)
    list(APPEND HEADERS
        src/debug/DebugRpc.h
    )
endif()

# ── Pro build: account system, update checker, premium feature gates ──────────
if(RX14_PRO_BUILD)
    list(APPEND SOURCES
        src/featuregate_pro.cpp
        src/apiclient.cpp
        src/logindialog.cpp
        src/accountwidget.cpp
        src/updatechecker.cpp
    )
    list(APPEND HEADERS
        src/apiclient.h
        src/logindialog.h
        src/accountwidget.h
        src/updatechecker.h
    )
else()
    list(APPEND SOURCES src/featuregate_community.cpp)
    list(APPEND HEADERS
        src/stubs/apiclient.h
        src/stubs/updatechecker.h
        src/stubs/accountwidget.h
        src/stubs/logindialog.h
        src/stubs/checksummanager.h
    )
endif()

if(APPLE)
    qt_add_executable(rx14 MACOSX_BUNDLE
        ${SOURCES}
        ${HEADERS}
        resources/resources.qrc
        data/vehicledb.qrc
    )
    set_target_properties(rx14 PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER com.ct14.rx14
        MACOSX_BUNDLE_BUNDLE_NAME "RX14"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    )
else()
    qt_add_executable(rx14
        ${SOURCES}
        ${HEADERS}
        resources/resources.qrc
        data/vehicledb.qrc
    )
endif()

# ── Translations ─────────────────────────────────────────────────────────────
set(TS_FILES
    translations/rx14_en.ts
    translations/rx14_zh_CN.ts
    translations/rx14_es.ts
    translations/rx14_th.ts
)
if(Qt6_VERSION VERSION_GREATER_EQUAL "6.7.0")
    qt_add_translations(rx14
        TS_FILES ${TS_FILES}
        RESOURCE_PREFIX "/i18n"
        LUPDATE_OPTIONS -no-obsolete
    )
else()
    qt_add_translations(rx14
        TS_FILES ${TS_FILES}
        RESOURCE_PREFIX "/i18n"
    )
endif()

target_include_directories(rx14 PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/stubs" "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# Sprint L §4 — sol2 (header-only) include path, marked SYSTEM to
# silence sol2's own template-instantiation warnings.
target_include_directories(rx14 SYSTEM PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sol2"
)

target_link_libraries(rx14 PRIVATE
    blake3
    lua_static       # Sprint L §4 — embedded Lua 5.4
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::Network
    Qt6::Sql
)

if(RX14_PRO_BUILD)
    target_compile_definitions(rx14 PRIVATE RX14_PRO_BUILD)
endif()
if(RX14_DEBUG_RPC)
    target_compile_definitions(rx14 PRIVATE RX14_DEBUG_RPC)
    message(STATUS "RX14: Debug RPC enabled (TCP 127.0.0.1:48714, log → D:/rx14-debug/rx14.log)")
endif()
if(ZLIB_FOUND)
    target_link_libraries(rx14 PRIVATE ZLIB::ZLIB)
else()
    # Fallback: use MinGW bundled zlib / Qt bundled zlib
    target_link_libraries(rx14 PRIVATE z)
    target_include_directories(rx14 PRIVATE
        "${Qt6Core_DIR}/../../../include/QtZlib")
endif()

# Platform-specific configuration
if(WIN32)
    # Windows executable icon
    set(WIN_RC "")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.ico")
        set(WIN_RC "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.rc")
    endif()
    set_target_properties(rx14 PROPERTIES WIN32_EXECUTABLE TRUE)
    if(WIN_RC)
        target_sources(rx14 PRIVATE ${WIN_RC})
    endif()
    # dbghelp: stack-walk + symbol resolution in windowsExceptionFilter
    target_link_libraries(rx14 PRIVATE dbghelp)
elseif(APPLE)
    # macOS app bundle
    set_target_properties(rx14 PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.ct14garage.romhex14"
        MACOSX_BUNDLE_BUNDLE_NAME "romHEX 14"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "icon.icns"
    )
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.icns")
        target_sources(rx14 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.icns")
        set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.icns"
            PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    endif()
endif()

# ── Deploy checksumhelper.exe and ChecksumDLL next to the executable ──────────
# The 32-bit checksumhelper.exe bridges 64-bit rx14 to 32-bit OLS checksum DLLs.
# Both must be deployed alongside rx14.exe for checksum features to work (Windows only).
if(WIN32)
    # Copy checksumhelper.exe to build dir
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/checksumhelper.exe")
        add_custom_command(TARGET rx14 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/checksumhelper.exe"
                "$<TARGET_FILE_DIR:rx14>/checksumhelper.exe"
            COMMENT "Deploying checksumhelper.exe")
    endif()
    # Copy ChecksumDLL folder to build dir
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ChecksumDLL")
        add_custom_command(TARGET rx14 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/ChecksumDLL"
                "$<TARGET_FILE_DIR:rx14>/ChecksumDLL"
            COMMENT "Deploying ChecksumDLL folder (148 ECU DLLs)")
    endif()
endif()

# Strip absolute paths from __FILE__ / debug info so release binaries do
# not embed the local checkout location.  Available on gcc/clang 8+; MSVC
# does not expand __FILE__ to absolute paths by default for relative
# source paths, so this is a no-op there.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rx14 PRIVATE
        "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.")
endif()

# Security hardening for release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    if(MSVC)
        target_compile_options(rx14 PRIVATE /GS /guard:cf /Qspectre)
        target_link_options(rx14 PRIVATE /DYNAMICBASE /NXCOMPAT /GUARD:CF)
    elseif(MINGW)
        target_compile_options(rx14 PRIVATE -O2 -fstack-protector-strong -D_FORTIFY_SOURCE=2)
        target_link_options(rx14 PRIVATE -Wl,--dynamicbase -Wl,--nxcompat -Wl,--high-entropy-va -s)
    elseif(CMAKE_COMPILER_IS_GNUCXX)
        target_compile_options(rx14 PRIVATE -O2 -fstack-protector-strong -D_FORTIFY_SOURCE=2)
    endif()
endif()

# ── Map auto-detect corpus harness (issue #25) ─────────────────────────────
# Prints precision/recall over labelled ROMs in testdata/mapdetect so prior
# tuning in MapAutoDetect.cpp can be tracked run-over-run.
add_executable(test_mapdetect_corpus tests/test_mapdetect_corpus.cpp
    src/io/ols/MapAutoDetect.cpp
    src/io/ols/KpImporter.cpp
    src/io/ols/ZipDecompressor.cpp
)
target_include_directories(test_mapdetect_corpus PRIVATE src)
target_compile_definitions(test_mapdetect_corpus PRIVATE
    TESTDATA_DIR="${CMAKE_SOURCE_DIR}/testdata")
target_link_libraries(test_mapdetect_corpus PRIVATE Qt6::Core Qt6::Gui)
if(ZLIB_FOUND)
    target_link_libraries(test_mapdetect_corpus PRIVATE ZLIB::ZLIB)
else()
    target_link_libraries(test_mapdetect_corpus PRIVATE z)
endif()

if(BUILD_TESTING)
    add_executable(test_kp_schema479_503 tests/test_kp_schema479_503.cpp
        src/io/ols/KpImporter.cpp
        src/io/ols/ZipDecompressor.cpp
    )
    target_include_directories(test_kp_schema479_503 PRIVATE src)
    target_compile_definitions(test_kp_schema479_503 PRIVATE
        KP_CORPUS_DIR="${CMAKE_SOURCE_DIR}/mappacks"
    )
    target_link_libraries(test_kp_schema479_503 PRIVATE Qt6::Core Qt6::Gui)
    if(ZLIB_FOUND)
        target_link_libraries(test_kp_schema479_503 PRIVATE ZLIB::ZLIB)
    else()
        target_link_libraries(test_kp_schema479_503 PRIVATE z)
    endif()
    add_test(NAME kp_schema479_503_codec COMMAND test_kp_schema479_503)

    add_executable(test_kp_schema597 tests/test_kp_schema597.cpp
        src/io/ols/KpImporter.cpp
        src/io/ols/ZipDecompressor.cpp
    )
    target_include_directories(test_kp_schema597 PRIVATE src)
    target_compile_definitions(test_kp_schema597 PRIVATE
        KP_SCHEMA597_FIXTURE="${CMAKE_SOURCE_DIR}/mappacks/Honda_Civic_1.0_Turbo_VTec_125Hp_MED17.9.3_0261B18059_37805-5AZ-E140_10SW01865793B2V115_018657_4D57.kp"
    )
    target_link_libraries(test_kp_schema597 PRIVATE Qt6::Core Qt6::Gui)
    if(ZLIB_FOUND)
        target_link_libraries(test_kp_schema597 PRIVATE ZLIB::ZLIB)
    else()
        target_link_libraries(test_kp_schema597 PRIVATE z)
    endif()
    add_test(NAME kp_schema597_codec COMMAND test_kp_schema597)

    add_executable(test_kp_schema292 tests/test_kp_schema292.cpp
        src/io/ols/KpImporter.cpp
        src/io/ols/ZipDecompressor.cpp
    )
    target_include_directories(test_kp_schema292 PRIVATE src)
    target_compile_definitions(test_kp_schema292 PRIVATE
        KP_SCHEMA292_FIXTURE="${CMAKE_SOURCE_DIR}/mappacks/31 exclusive firmware packages for chip-tuning_RM-Chiptuning__Tuning DPF EGR off files_Audi_A3 1.9 TDI Tune Stage 1 Sw 371093 Hw 0281011832 EDC16U1_Tuning MapPack.kp"
        KP_CORPUS_DIR="${CMAKE_SOURCE_DIR}/mappacks"
    )
    target_link_libraries(test_kp_schema292 PRIVATE Qt6::Core Qt6::Gui)
    if(ZLIB_FOUND)
        target_link_libraries(test_kp_schema292 PRIVATE ZLIB::ZLIB)
    else()
        target_link_libraries(test_kp_schema292 PRIVATE z)
    endif()
    add_test(NAME kp_schema292_codec COMMAND test_kp_schema292)

    add_executable(test_kp_schema750 tests/test_kp_schema750.cpp
        src/io/ols/KpImporter.cpp
        src/io/ols/ZipDecompressor.cpp
    )
    target_include_directories(test_kp_schema750 PRIVATE src)
    target_compile_definitions(test_kp_schema750 PRIVATE
        KP_SCHEMA750_PRIMARY_FIXTURE="${CMAKE_SOURCE_DIR}/TC1793_10SW10605393D2V180-20250125-154817.kp"
        KP_SCHEMA750_SECONDARY_FIXTURE="${CMAKE_SOURCE_DIR}/mappacks/TC1793_10SW10605393D2V180-20250125-154817 CIVIC FK6 1.0 3 CILINDER AMT MAP PACK V1.kp"
    )
    target_link_libraries(test_kp_schema750 PRIVATE Qt6::Core Qt6::Gui)
    if(ZLIB_FOUND)
        target_link_libraries(test_kp_schema750 PRIVATE ZLIB::ZLIB)
    else()
        target_link_libraries(test_kp_schema750 PRIVATE z)
    endif()
    add_test(NAME kp_schema750_codec COMMAND test_kp_schema750)
endif()
