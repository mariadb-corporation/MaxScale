if(BUILD_CDC)
  add_subdirectory(CDCPlainAuth)
endif()

add_subdirectory(GSSAPI)
add_subdirectory(HTTPAuth)
add_subdirectory(MaxAdminAuth)
add_subdirectory(MySQLAuth)
add_subdirectory(MySQLBackendAuth)
add_subdirectory(NullAuthAllow)
add_subdirectory(NullAuthDeny)
add_subdirectory(PAM)

