cmake_minimum_required (VERSION 2.8)

project (consumer)

find_library(MYSQL_CLIENT_LIB NAMES mysqlclient PATHS /usr/lib /usr/lib64 PATH_SUFFIXES mysql mariadb)

if( ( RABBITMQ_LIB AND RABBITMQ_HEADERS ) AND MYSQL_CLIENT_LIB )

  add_executable (consumer consumer.c)
  target_link_libraries(consumer ${MYSQL_CLIENT_LIB} rabbitmq inih)
  install(TARGETS consumer DESTINATION bin)
  install(FILES consumer.cnf DESTINATION etc)


else()

  message(FATAL_ERROR "Error: Can not find requred libraries and headers: librabbitmq libmysqlclient")

endif()

# Packaging of RabbitMQ disabled for now

#set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "RabbitMQ Consumer Client")
#set(CPACK_PACKAGE_NAME "RabbitMQ Consumer")
#set(CPACK_GENERATOR "RPM")
#set(CPACK_PACKAGE_VERSION_MAJOR "1")
#set(CPACK_PACKAGE_VERSION_MINOR "0")
#set(CPACK_RPM_PACKAGE_NAME "rabbitmq-consumer")
#set(CPACK_RPM_PACKAGE_VENDOR "SkySQL Ab")
#set(CPACK_RPM_PACKAGE_AUTOREQPROV " no")
#include(CPack)