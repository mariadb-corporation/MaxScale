find_package(NPM)

if (NPM_FOUND)
  add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/maxctrl/maxctrl
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/build.sh ${CMAKE_SOURCE_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
  add_custom_target(maxctrl ALL DEPENDS ${CMAKE_BINARY_DIR}/maxctrl/maxctrl)
  install_script(${CMAKE_BINARY_DIR}/maxctrl/maxctrl core)
else()
  message(STATUS "Not building MaxCtrl: npm not found")
endif()
