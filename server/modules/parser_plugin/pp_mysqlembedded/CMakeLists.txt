if (NOT DEFINED MARIADB_SERVER_SOURCE_DIR)
  message(FATAL_ERROR "MARIADB_SERVER_SOURCE_DIR must be defined if pp_mysqlembedded is to be built.")
endif()

if (NOT DEFINED MARIADB_SERVER_BUILD_DIR)
  set(MARIADB_SERVER_BUILD_DIR ${MARIADB_SERVER_SOURCE_DIR}/build CACHE INTERNAL "")
endif()

if (NOT DEFINED MARIADB_ERRMSG)
  set(MARIADB_ERRMSG ${MARIADB_SERVER_BUILD_DIR}/sql/share/english/errmsg.sys CACHE INTERNAL "")
endif()

##
## Figure out which MariaDB server version that is used.
##
find_file(MYSQL_VERSION_H mysql_version.h HINTS ${MARIADB_SERVER_BUILD_DIR}/include NO_DEFAULT_PATH)
if(MYSQL_VERSION_H MATCHES "MYSQL_VERSION_H-NOTFOUND")
  message(FATAL_ERROR "Cannot find the mysql_version.h header")
else()
  message(STATUS "Found mysql_version.h: ${MYSQL_VERSION_H}")
endif()

file(READ ${MYSQL_VERSION_H} MYSQL_VERSION_CONTENTS)
string(REGEX REPLACE ".*MYSQL_SERVER_VERSION[^0-9.]+([0-9.]+).*" "\\1" MYSQL_VERSION ${MYSQL_VERSION_CONTENTS})

message(STATUS "MariaDB version: ${MYSQL_VERSION}")

if(MYSQL_VERSION VERSION_LESS 10.4)
  message(FATAL_ERROR "Cannot build using MariaDB server version older than 10.4")
endif()

if(MYSQL_VERSION VERSION_GREATER_EQUAL 10.9)
  message(FATAL_ERROR "Cannot build using MariaDB server version newer than 10.8")
endif()

##
## Specify pp_mysqlembedded
##
add_library(pp_mysqlembedded SHARED pp_mysqlembedded.cc)

target_include_directories(pp_mysqlembedded BEFORE PUBLIC
  ${MARIADB_SERVER_SOURCE_DIR}/include
  ${MARIADB_SERVER_SOURCE_DIR}/libmariadb/include
  ${MARIADB_SERVER_BUILD_DIR}/include
  ${MARIADB_SERVER_SOURCE_DIR}/sql
  ${MARIADB_SERVER_SOURCE_DIR}/libmysqld
  ${MARIADB_EMBEDDED_INCLUDE_DIRS}
  )

# Libraries common for all server versions.
target_link_libraries(pp_mysqlembedded
  ${MARIADB_SERVER_BUILD_DIR}/libmysqld/libmysqld.a
  )

# Libraries for specific server versions.
if(NOT (MYSQL_VERSION VERSION_LESS 10.5))
  target_link_libraries(pp_mysqlembedded
    ${MARIADB_SERVER_BUILD_DIR}/tpool/libtpool.a
    )
endif()

# System and other libraries.
target_link_libraries(pp_mysqlembedded
  aio crypt crypto dl m pcre ssl stdc++ z
  )


set_target_properties(pp_mysqlembedded PROPERTIES VERSION "1.0.0")
set_target_properties(pp_mysqlembedded PROPERTIES LINK_FLAGS -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/pp_mysqlembedded.map)
#set_target_properties(pp_mysqlembedded PROPERTIES LINK_FLAGS -Wl,-z,defs)

install_module(pp_mysqlembedded libmysqld-parser)
install(PROGRAMS ${MARIADB_ERRMSG} DESTINATION ${MAXSCALE_VARDIR}/lib/maxscale)
