add_library(checksum_siemens_simos_pcr21 MODULE
    SiemensPCR21Plugin.cpp
    SiemensPCR21Plugin.h
)

target_include_directories(checksum_siemens_simos_pcr21 PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(checksum_siemens_simos_pcr21 PRIVATE
    Qt6::Core
)

set_target_properties(checksum_siemens_simos_pcr21 PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/checksums"
    PREFIX "lib"
)
