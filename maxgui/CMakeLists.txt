if (BUILD_GUI)
  set(MAXGUI_REPO "https://github.com/mariadb-corporation/MaxGui.git" CACHE STRING "The MaxGui repository")

  ExternalProject_Add(maxgui
    GIT_REPOSITORY ${MAXGUI_REPO}
    GIT_TAG "2.5.0"
    CONFIGURE_COMMAND ""
    BUILD_IN_SOURCE 1
    UPDATE_COMMAND ""
    BUILD_COMMAND npm install && buildPath=${CMAKE_CURRENT_BINARY_DIR}/ npm run build
    INSTALL_COMMAND "")

  install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/gui/ DESTINATION ${MAXSCALE_SHAREDIR}/gui COMPONENT "core")
endif()
