if(NOT ${CMAKE_VERSION} VERSION_LESS 3.0.0)
  cmake_policy(SET CMP0046 OLD)
endif()

include(ExternalProject)

ExternalProject_Add(maxscale_sqlite
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/sqlite-src-3110100
  BINARY_DIR ${CMAKE_BINARY_DIR}/sqlite-bld-3110100
  CONFIGURE_COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/sqlite-src-3110100/configure --with-pic --enable-maxscale
  BUILD_COMMAND make sqlite3.c
  INSTALL_COMMAND "")
add_dependencies(maxscale_sqlite ${CMAKE_CURRENT_SOURCE_DIR}/sqlite-src-3110100/src/parse.y)

include_directories(${CMAKE_BINARY_DIR}/sqlite-bld-3110100/tsrc)
include_directories(${CMAKE_BINARY_DIR}/sqlite-bld-3110100)
include_directories(${MARIADB_CONNECTOR_INCLUDE_DIR})

# GCC thinks there is an array-bounds error in sqlite code.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-error=array-bounds")

add_library(qc_sqlite SHARED qc_sqlite.c qc_sqlite3.c builtin_functions.c)
add_dependencies(qc_sqlite maxscale_sqlite)
add_definitions(-DMAXSCALE -DSQLITE_THREADSAFE=0 -DSQLITE_ENABLE_UPDATE_DELETE_LIMIT -DSQLITE_OMIT_ATTACH -DSQLITE_OMIT_REINDEX -DSQLITE_OMIT_AUTOVACUUM -DSQLITE_OMIT_PRAGMA)

set_target_properties(qc_sqlite PROPERTIES VERSION "1.0.0")
set_target_properties(qc_sqlite PROPERTIES LINK_FLAGS -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/qc_sqlite.map)
# NOTE: If libqc_sqlite is linked with the link-flags -Wl,-z,defs - that is, all
# NOTE: symbols are resolved at compile time - then for whatever reason, sqlite3_prepare
# NOTE: is *not* taken from sqlite3.c built into this library, but from the sqlite3
# NOTE: shared library on the system. Libmaxscale-common depends on the libsqlite, but
# NOTE: reason is not entirelly understood.
#
# target_link_libraries(qc_sqlite maxscale-common)
# set_target_properties(qc_sqlite PROPERTIES LINK_FLAGS -Wl,-z,defs)

install_module(qc_sqlite core)
