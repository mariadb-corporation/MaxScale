# Install static C headers
file(GLOB HEADERS "*.h")
foreach(var ${HEADERS})
  get_filename_component(header ${var} NAME)
  install_header(${header} devel)
endforeach()

# Install static C++ headers
file(GLOB HEADERS "*.hh")
foreach(var ${HEADERS})
  get_filename_component(header ${var} NAME)
  install_header(${header} devel)
endforeach()

# Install generated C headers
file(GLOB GENERATD_HEADERS "${CMAKE_BINARY_DIR}/include/maxscale/*.h")
foreach(var ${GENERATD_HEADERS})
  install_header(${var} devel)
endforeach()

# Install generated C++ headers
file(GLOB GENERATD_HEADERS "${CMAKE_BINARY_DIR}/include/maxscale/*.hh")
foreach(var ${GENERATD_HEADERS})
  install_header(${var} devel)
endforeach()

add_subdirectory(protocol)
