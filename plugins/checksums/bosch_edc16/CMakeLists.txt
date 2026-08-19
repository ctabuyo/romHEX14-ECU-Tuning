add_library(checksum_bosch_edc16 MODULE
    BoschEDC16Plugin.cpp
    BoschEDC16Plugin.h
)

target_include_directories(checksum_bosch_edc16 PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(checksum_bosch_edc16 PRIVATE
    Qt6::Core
)

set_target_properties(checksum_bosch_edc16 PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/checksums"
    PREFIX "lib"
)
