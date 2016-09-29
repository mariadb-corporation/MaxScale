if(BUILD_AVRO)
  add_subdirectory(CDC)
  add_subdirectory(examples)
endif()

add_subdirectory(HTTPD)
add_subdirectory(maxscaled)
add_subdirectory(MySQLBackend)
add_subdirectory(MySQLClient)
add_subdirectory(telnetd)

if(BUILD_TESTS)
  add_subdirectory(testprotocol)
endif()
