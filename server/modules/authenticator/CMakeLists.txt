if (BUILD_GSSAPI)
  add_subdirectory(GSSAPI)
endif()

add_subdirectory(MariaDBAuth)
add_subdirectory(PAM)

