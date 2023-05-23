if (NOT DEFINED MARIADB_SERVER_SOURCE_DIR)
  message(FATAL_ERROR "MARIADB_SERVER_SOURCE_DIR must be defined if pp_mysqlembedded is to be built.")
endif()

if (NOT DEFINED MARIADB_SERVER_BUILD_DIR)
  set(MARIADB_SERVER_BUILD_DIR ${MARIADB_SERVER_SOURCE_DIR}/build CACHE INTERNAL "")
endif()

if (NOT DEFINED MARIADB_ERRMSG)
  set(MARIADB_ERRMSG ${MARIADB_SERVER_BUILD_DIR}/sql/share/english/errmsg.sys CACHE INTERNAL "")
endif()

set(MARIADB_EMBEDDED_INCLUDE_DIRS
  ${MARIADB_SERVER_SOURCE_DIR}/include
  ${MARIADB_SERVER_SOURCE_DIR}/libmariadb/include
  ${MARIADB_SERVER_BUILD_DIR}/include
  ${MARIADB_SERVER_SOURCE_DIR}/sql
  ${MARIADB_SERVER_SOURCE_DIR}/libmysqld
  CACHE INTERNAL "")

add_library(pp_mysqlembedded SHARED pp_mysqlembedded.cc)

target_include_directories(pp_mysqlembedded BEFORE PUBLIC ${MARIADB_EMBEDDED_INCLUDE_DIRS})

target_link_libraries(pp_mysqlembedded
  ${MARIADB_SERVER_BUILD_DIR}/libmysqld/libsql_embedded.a
  ${MARIADB_SERVER_BUILD_DIR}/libmysqld/libmariadbd.a
  aio crypt crypto dl m
  pcre
  ssl stdc++ z)
set_target_properties(pp_mysqlembedded PROPERTIES VERSION "1.0.0")
set_target_properties(pp_mysqlembedded PROPERTIES LINK_FLAGS -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/pp_mysqlembedded.map)
#set_target_properties(pp_mysqlembedded PROPERTIES LINK_FLAGS -Wl,-z,defs)

install_module(pp_mysqlembedded libmysqld-parser)
install(PROGRAMS ${MARIADB_ERRMSG} DESTINATION ${MAXSCALE_VARDIR}/lib/maxscale)
