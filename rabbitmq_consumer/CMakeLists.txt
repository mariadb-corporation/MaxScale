find_library(MYSQL_CLIENT_LIB NAMES mysqlclient PATHS /usr/lib /usr/lib64 PATH_SUFFIXES mysql mariadb)

if( ( RABBITMQ_LIB AND RABBITMQ_HEADERS ) AND ( NOT ( ${MYSQL_CLIENT_LIB} STREQUAL "MYSQL_CLIENT_LIB-NOTFOUND" ) ) )

  add_executable (consumer consumer.c)
  target_link_libraries(consumer ${MYSQL_CLIENT_LIB} rabbitmq inih)
  install(TARGETS consumer DESTINATION bin)
  install(FILES consumer.cnf DESTINATION etc)


else()

  message(FATAL_ERROR "Error: Can not find requred libraries and headers: librabbitmq libmysqlclient")

endif()
