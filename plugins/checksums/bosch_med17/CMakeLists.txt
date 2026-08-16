add_library(checksum_bosch_med17 SHARED
    BoschMED17Plugin.cpp
    BoschMED17Plugin.h
    ../../../src/checksums/BoschMED17.cpp
    ../../../src/checksums/BoschMED17.h
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
