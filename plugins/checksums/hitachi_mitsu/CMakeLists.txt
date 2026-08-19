add_library(checksum_hitachi_mitsu MODULE
    HitachiMitsuPlugin.cpp
    HitachiMitsuPlugin.h
)

target_include_directories(checksum_hitachi_mitsu PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(checksum_hitachi_mitsu PRIVATE
    Qt6::Core
)

set_target_properties(checksum_hitachi_mitsu PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/checksums"
    PREFIX "lib"
)
