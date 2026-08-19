add_library(checksum_bosch_med17 SHARED
    BoschMED17Plugin.cpp
    BoschMED17Plugin.h
    ../../../src/checksums/BoschMED17.cpp
    ../../../src/checksums/BoschMED17.h
    ../../../src/checksums/Med17Rsa.cpp
    ../../../src/checksums/Med17Rsa.h
    ../../../src/checksums/Med17Descriptor.cpp
    ../../../src/checksums/Med17Descriptor.h
    ../../../src/checksums/Med17Keys.cpp
    ../../../src/checksums/Med17Keys.h
    ../../../src/checksums/Med17Correction.cpp
    ../../../src/checksums/Med17Correction.h
    ../../../src/checksums/Med17Variant.cpp
    ../../../src/checksums/Med17Variant.h
    ../../../src/checksums/Med17BlockChecksum.cpp
    ../../../src/checksums/Med17BlockChecksum.h
    ../../../src/checksums/Med17CustomerBlock.cpp
    ../../../src/checksums/Med17CustomerBlock.h
    ../../../src/checksums/common/RsaMath1024.cpp
    ../../../src/checksums/common/RsaMath1024.h
    ../../../src/checksums/common/BleichenbacherForge.cpp
    ../../../src/checksums/common/BleichenbacherForge.h
    ../../../src/checksums/IChecksumPlugin.h
)

target_include_directories(checksum_bosch_med17 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
)

target_link_libraries(checksum_bosch_med17 PRIVATE
    Qt6::Core
)

# Output directly into the plugins/checksums directory in the build folder
set_target_properties(checksum_bosch_med17 PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/checksums"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/checksums"
)
