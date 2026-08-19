add_library(checksum_bosch_bms MODULE
    BoschBMSPlugin.cpp
    BoschBMSPlugin.h
)

target_include_directories(checksum_bosch_bms PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(checksum_bosch_bms PRIVATE
    Qt6::Core
)

set_target_properties(checksum_bosch_bms PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/checksums"
    PREFIX "lib"
)
