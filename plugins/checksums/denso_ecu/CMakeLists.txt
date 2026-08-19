add_library(checksum_denso_ecu MODULE
    DensoECUPlugin.cpp
    DensoECUPlugin.h
)

target_include_directories(checksum_denso_ecu PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(checksum_denso_ecu PRIVATE
    Qt6::Core
)

set_target_properties(checksum_denso_ecu PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/checksums"
    PREFIX "lib"
)
