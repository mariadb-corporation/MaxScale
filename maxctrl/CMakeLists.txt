if (BUILD_MAXCTRL)
  find_package(NPM)
  find_package(NodeJS)

  if (NPM_FOUND AND NODEJS_FOUND AND NODEJS_VERSION VERSION_GREATER "6.0.0")

    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/lib/version.js.in ${CMAKE_CURRENT_BINARY_DIR}/lib/version.js @ONLY)

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
    message(FATAL_ERROR "Not building MaxCtrl: npm or Node.js >= 6.0.0 not found. Add the following to skip MaxCtrl: -DBUILD_MAXCTRL=N")
  endif()
else()
  message(STATUS "Not building MaxCtrl: BUILD_MAXCTRL=N")
endif()
