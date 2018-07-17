include(../VERSION22.cmake)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/lib/version.js.in ${CMAKE_CURRENT_BINARY_DIR}/lib/version.js @ONLY)
