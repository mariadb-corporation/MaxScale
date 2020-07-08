if (BUILD_GUI)
  file(GLOB_RECURSE MAXGUI_SOURCES ./src/* ./public/* *.json *.js)
  add_custom_target(maxgui ALL
    DEPENDS ${MAXGUI_SOURCES}
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/build.sh ${CMAKE_SOURCE_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

  install_directory(${CMAKE_CURRENT_BINARY_DIR}/gui/ ${MAXSCALE_SHAREDIR}/gui core)
endif()
