cmake_minimum_required(VERSION 2.6)
enable_testing()
project(MaxScale)

set(MAXSCALE_VERSION_MAJOR "1")
set(MAXSCALE_VERSION_MINOR "0.0-beta")
set(MAXSCALE_VERSION "${MAXSCALE_VERSION_MAJOR}.${MAXSCALE_VERSION_MINOR}")

configure_file(${CMAKE_SOURCE_DIR}/server/include/version.h.in ${CMAKE_SOURCE_DIR}/server/include/version.h)

find_path(MYSQL_DIR mysql.h PATH_SUFFIXES mysql mariadb)
if(MYSQL_DIR_NOT_FOUND)
message(FATAL_ERROR "Fatal Error: MySQL headers were not found.")
endif(MYSQL_DIR_NOT_FOUND)


find_file(ERRMSG errmsg.sys PATHS /usr/share/mysql /usr/local/share/mysql PATH_SUFFIXES english)
if(ERRMSG_NOT_FOUND)
message(FATAL_ERROR "Fatal Error: The errmsg.sys file was not found.")
endif(ERRMSG_NOT_FOUND)

set(CMAKE_C_FLAGS ${CMAKE_C_FLAGS} -fPIC)
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -fPIC)

if(NOT EMBEDDED_LIB)
  find_library(EMBEDDED_LIB libmysqld.a PATHS /usr/lib /usr/lib64 PATH_SUFFIXES mysql)
  if(EMBEDDED_LIB_NOT_FOUND)
    message(ERROR "Library not found: libmysqld. If your install of MySQL is in a non-default location, please provide the location with -DEMBEDDED_LIB=<path to library>")
  endif(EMBEDDED_LIB_NOT_FOUND)
endif(NOT EMBEDDED_LIB)

include_directories(${MYSQL_DIR})
include_directories(${MYSQL_DIR}/private)
include_directories(${MYSQL_DIR}/extra)
include_directories(utils)
include_directories(log_manager)
include_directories(query_classifier)
include_directories(server/include)
include_directories(server/inih)
include_directories(server/modules/include)

add_subdirectory(utils)
add_subdirectory(log_manager)
add_subdirectory(query_classifier)
add_subdirectory(server)
add_subdirectory(client)

if(BUILD_RABBITMQ)
add_subdirectory(rabbitmq_consumer)
endif(BUILD_RABBITMQ)

file(GLOB DOCS Documentation/*.pdf)

install(FILES server/test/MaxScale_test.cnf RENAME MaxScale.cnf DESTINATION etc)
install(FILES ${ERRMSG} DESTINATION mysql)
install(FILES ${EMBEDDED_LIB} DESTINATION lib)
install(FILES ${DOCS} DESTINATION Documentation)