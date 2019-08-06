if(BUILD_CDC)
  add_subdirectory(CDCPlainAuth)
endif()

add_subdirectory(GSSAPI)
add_subdirectory(HTTPAuth)
add_subdirectory(MaxAdminAuth)
add_subdirectory(MariaDBAuth)
add_subdirectory(PAM)

