cmake_minimum_required (VERSION 2.6)

set(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} /usr/lib /usr/lib64 /usr/local/lib /usr/local/lib64 /usr/lib/mariadb /usr/lib64/mariadb)
set(CMAKE_INCLUDE_PATH ${CMAKE_INCLUDE_PATH} /usr/include /usr/local/include /usr/include/mysql /usr/local/include/mysql /usr/include/mariadb /usr/local/include/mariadb)

include(InstallRequiredSystemLibraries)

project (consumer)

find_path(MYSQL_INCLUDE_DIRS mysql.h)
find_library(MYSQL_LIBRARIES NAMES mysqlclient)
find_library(RABBITMQ_C_LIBRARIES NAMES rabbitmq)

include_directories(${MYSQL_INCLUDE_DIRS})
include_directories(${RABBITMQ_C_INCLUDE_DIRS})
include_directories(${CMAKE_SOURCE_DIR}/inih)

add_subdirectory (inih)
link_directories(${CMAKE_SOURCE_DIR}/inih)

if(RABBITMQ_C_LIBRARIES AND MYSQL_LIBRARIES AND MYSQL_INCLUDE_DIRS)

add_definitions(-DCONFIG_IN_ETC)
add_definitions(-DCONSUMER_CONFIG_PREFIX="${CMAKE_INSTALL_PREFIX}/share/consumer")

add_executable (consumer consumer.c ${MYSQL_LIBRARIES} ${RABBITMQ_C_LIBRARIES})
target_link_libraries(consumer mysqlclient)
target_link_libraries(consumer rabbitmq)
target_link_libraries(consumer inih)
install(TARGETS consumer DESTINATION bin)
install(FILES consumer.cnf DESTINATION share/consumer)


else(RABBITMQ_C_LIBRARIES AND MYSQL_LIBRARIES AND MYSQL_INCLUDE_DIRS)
message(FATAL_ERROR "Error: Can not find requred libraries: libmysqld, librabbitmq.")

endif(RABBITMQ_C_LIBRARIES AND MYSQL_LIBRARIES AND MYSQL_INCLUDE_DIRS)

set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "RabbitMQ Consumer Client")
set(CPACK_PACKAGE_NAME "RabbitMQ Consumer")
set(CPACK_GENERATOR "RPM")
set(CPACK_PACKAGE_VERSION_MAJOR "1")
set(CPACK_PACKAGE_VERSION_MINOR "0")
set(CPACK_RPM_PACKAGE_NAME "rabbitmq-consumer")
set(CPACK_RPM_PACKAGE_VENDOR "SkySQL Ab")
set(CPACK_RPM_PACKAGE_AUTOREQPROV " no")
include(CPack)