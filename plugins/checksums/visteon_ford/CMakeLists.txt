add_library(checksum_visteon_ford MODULE
    VisteonFordPlugin.cpp
    VisteonFordPlugin.h
)

target_include_directories(checksum_visteon_ford PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(checksum_visteon_ford PRIVATE
    Qt6::Core
)

set_target_properties(checksum_visteon_ford PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/checksums"
    PREFIX "lib"
)
