if (BUILD_GUI)
  file(GLOB_RECURSE MAXGUI_SOURCES ./src/* ./public/* *.json *.js)
  add_custom_target(maxgui_copy_sources
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/copy_sources.sh ${CMAKE_SOURCE_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/node_modules
    DEPENDS maxgui_copy_sources package.json package-lock.json
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/npm_install.sh ${CMAKE_SOURCE_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
  add_custom_target(maxgui_node_modules DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/node_modules)
  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gui
    DEPENDS ${MAXGUI_SOURCES} ${CMAKE_CURRENT_BINARY_DIR}/node_modules
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/build.sh ${CMAKE_SOURCE_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
  add_custom_target(maxgui ALL DEPENDS maxgui_node_modules ${CMAKE_CURRENT_BINARY_DIR}/gui)

  install_directory(${CMAKE_CURRENT_BINARY_DIR}/gui/ ${MAXSCALE_SHAREDIR}/gui core)
endif()
