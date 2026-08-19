add_library(checksum_siemens_sid MODULE
    SiemensSIDPlugin.cpp
    SiemensSIDPlugin.h
)

target_include_directories(checksum_siemens_sid PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(checksum_siemens_sid PRIVATE
    Qt6::Core
)

set_target_properties(checksum_siemens_sid PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/checksums"
    PREFIX "lib"
)
