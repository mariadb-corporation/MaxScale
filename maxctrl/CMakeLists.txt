find_package(NPM)

if (NPM_FOUND)
  add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/maxctrl/maxctrl
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/build.sh ${CMAKE_SOURCE_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
  add_custom_target(maxctrl ALL DEPENDS ${CMAKE_BINARY_DIR}/maxctrl/maxctrl)
  install_script(${CMAKE_BINARY_DIR}/maxctrl/maxctrl core)

  add_custom_target(test_maxctrl
  COMMAND ${CMAKE_SOURCE_DIR}/test/run_npm_test.sh
  ${CMAKE_SOURCE_DIR}           # Path to MaxScale sources
  ${CMAKE_CURRENT_SOURCE_DIR}   # Path to test sources
  ${CMAKE_BINARY_DIR}/maxctrl-test/ # Location where tests are built and run
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

else()
  message(STATUS "Not building MaxCtrl: npm not found")
endif()
