add_library(checksum_marelli_multijet MODULE
    MarelliMultijetPlugin.cpp
    MarelliMultijetPlugin.h
)

target_include_directories(checksum_marelli_multijet PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(checksum_marelli_multijet PRIVATE
    Qt6::Core
)

set_target_properties(checksum_marelli_multijet PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/checksums"
    PREFIX "lib"
)
