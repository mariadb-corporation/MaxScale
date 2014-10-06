if (NOT ( DEFINED MYSQL_CLIENT_LIB ) )
  find_library(MYSQL_CLIENT_LIB NAMES mysqlclient PATHS /usr/lib /usr/lib64 PATH_SUFFIXES mysql mariadb)
endif()

if (NOT ( DEFINED MYSQL_CLIENT_HEADERS ) )
  find_path(MYSQL_CLIENT_HEADERS NAMES mysql.h PATH_SUFFIXES mysql mariadb)
endif()

if( ( RABBITMQ_LIB AND RABBITMQ_HEADERS ) AND ( NOT ( ${MYSQL_CLIENT_LIB} STREQUAL "MYSQL_CLIENT_LIB-NOTFOUND" ) ) AND ( NOT ( ${MYSQL_CLIENT_HEADERS} STREQUAL "MYSQL_CLIENT_HEADERS-NOTFOUND" ) ) )
  include_directories(${MYSQL_CLIENT_HEADERS})
  add_executable (consumer consumer.c)
  target_link_libraries(consumer ${MYSQL_CLIENT_LIB} rabbitmq inih)
  install(TARGETS consumer DESTINATION bin)
  install(FILES consumer.cnf DESTINATION etc)


else()

  message(FATAL_ERROR "Error: Can not find requred libraries and headers: librabbitmq libmysqlclient")

endif()
