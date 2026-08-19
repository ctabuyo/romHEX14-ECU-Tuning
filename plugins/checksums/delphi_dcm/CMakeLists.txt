add_library(checksum_delphi_dcm MODULE
    DelphiDCMPlugin.cpp
    DelphiDCMPlugin.h
)

target_include_directories(checksum_delphi_dcm PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(checksum_delphi_dcm PRIVATE
    Qt6::Core
)

set_target_properties(checksum_delphi_dcm PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/checksums"
    PREFIX "lib"
)
