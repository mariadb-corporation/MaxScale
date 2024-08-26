if (BUILD_MAXCTRL)
  find_package(NPM)
  find_package(NodeJS)

  if (NPM_FOUND AND NODEJS_FOUND AND NODEJS_VERSION VERSION_GREATER "6.0.0")

    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/lib/version.js.in ${CMAKE_CURRENT_BINARY_DIR}/lib/version.js @ONLY)

    file(GLOB_RECURSE MAXCTRL_SOURCES lib/*)

    add_custom_target(maxctrl_copy_sources
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/copy_sources.sh ${CMAKE_SOURCE_DIR}
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

    # This target generates the node_modules directory which is needed by both
    # packaging methods.
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/node_modules
      DEPENDS maxctrl_copy_sources package.json package-lock.json
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/npm_install.sh ${CMAKE_SOURCE_DIR}
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    add_custom_target(maxctrl_node_modules DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/node_modules)

    if (WITH_SYSTEM_NODEJS)
      # Bundle the node_modules and sources into /usr/share/maxscale/ and then
      # install a wrapper script that just calls `node maxctrl.js` with the
      # arguments.
      configure_file(${CMAKE_CURRENT_SOURCE_DIR}/maxctrl.in ${CMAKE_CURRENT_BINARY_DIR}/maxctrl @ONLY)
      add_custom_target(maxctrl ALL DEPENDS maxctrl_node_modules)

      install_custom_file(maxctrl.js ${MAXSCALE_SHAREDIR}/maxctrl core)
      install_custom_file(${CMAKE_CURRENT_BINARY_DIR}/lib/version.js ${MAXSCALE_SHAREDIR}/maxctrl/lib core)
      install_directory(lib ${MAXSCALE_SHAREDIR}/maxctrl core)
      install_directory(${CMAKE_CURRENT_BINARY_DIR}/node_modules ${MAXSCALE_SHAREDIR}/maxctrl core)
      install_script(${CMAKE_CURRENT_BINARY_DIR}/maxctrl core)
    else()
      # Build MaxCtrl into a single executable using pkg. This makes it so that
      # the same Node.js version is used in all operating systems.
      add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/maxctrl
        DEPENDS ${MAXCTRL_SOURCES} maxctrl.js
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/build.sh ${CMAKE_SOURCE_DIR}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
      add_custom_target(maxctrl ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/maxctrl maxctrl_node_modules)

      install_script(${CMAKE_BINARY_DIR}/maxctrl/maxctrl core)
    endif()

    add_custom_target(test_maxctrl
      COMMAND ${CMAKE_SOURCE_DIR}/test/run_npm_test.sh
      ${CMAKE_SOURCE_DIR}           # Path to MaxScale sources
      ${CMAKE_CURRENT_SOURCE_DIR}   # Path to test sources
      ${CMAKE_BINARY_DIR}/maxctrl-test/ # Location where tests are built and run
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

  elseif(NOT BUILD_SYSTEM_TESTS)
    message(FATAL_ERROR "Not building MaxCtrl: npm or Node.js >= 6.0.0 not found. Add the following to skip MaxCtrl: -DBUILD_MAXCTRL=N")
  endif()
else()
  message(STATUS "Not building MaxCtrl: BUILD_MAXCTRL=N")
endif()
