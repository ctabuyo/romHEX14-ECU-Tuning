add_library(checksum_continental_generic MODULE
    ContinentalGenericPlugin.cpp
    ContinentalGenericPlugin.h
)

target_include_directories(checksum_continental_generic PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(checksum_continental_generic PRIVATE
    Qt6::Core
)

set_target_properties(checksum_continental_generic PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/checksums"
    PREFIX "lib"
)
