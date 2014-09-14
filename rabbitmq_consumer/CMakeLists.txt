cmake_minimum_required (VERSION 2.6)

project (consumer)

find_path(RABBITMQ_C_DIRS amqp.h)
find_library(RABBITMQ_C_LIB NAMES rabbitmq)
find_library(MYSQL_CLIENT_LIB NAMES mysqlclient PATHS /usr/lib /usr/lib64 PATH_SUFFIXES mysql mariadb)

include_directories(${RABBITMQ_C_DIRS})

if( ( RABBITMQ_C_LIB AND RABBITMQ_C_DIRS ) AND MYSQL_CLIENT_LIB )

  add_executable (consumer consumer.c ${MYSQL_CLIENT_LIB} ${RABBITMQ_C_LIBRARIES})
  target_link_libraries(consumer mysqlclient)
  target_link_libraries(consumer rabbitmq)
  target_link_libraries(consumer inih)
  install(TARGETS consumer DESTINATION bin)
  install(FILES consumer.cnf DESTINATION etc)


else()
  message(FATAL_ERROR "Error: Can not find requred libraries.")
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