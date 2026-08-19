add_library(checksum_bosch_me7 MODULE
    BoschME7Plugin.cpp
    BoschME7Plugin.h
)

target_include_directories(checksum_bosch_me7 PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(checksum_bosch_me7 PRIVATE
    Qt6::Core
)

set_target_properties(checksum_bosch_me7 PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/checksums"
    PREFIX "lib"
)
