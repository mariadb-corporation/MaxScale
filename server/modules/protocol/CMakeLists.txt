if(BUILD_CDC)
  add_subdirectory(CDC)
  add_subdirectory(examples)
endif()

add_subdirectory(HTTPD)
add_subdirectory(maxscaled)
add_subdirectory(MySQL)
add_subdirectory(telnetd)

