add_library(checksum_trw_lucas MODULE
    TRWLucasPlugin.cpp
    TRWLucasPlugin.h
)

target_include_directories(checksum_trw_lucas PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(checksum_trw_lucas PRIVATE
    Qt6::Core
)

set_target_properties(checksum_trw_lucas PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/checksums"
    PREFIX "lib"
)
