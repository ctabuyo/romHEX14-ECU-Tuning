add_library(checksum_siemens_ms MODULE
    SiemensMSPlugin.cpp
    SiemensMSPlugin.h
)

target_include_directories(checksum_siemens_ms PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(checksum_siemens_ms PRIVATE
    Qt6::Core
)

set_target_properties(checksum_siemens_ms PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/checksums"
    PREFIX "lib"
)
