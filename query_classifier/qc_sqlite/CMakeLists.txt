include(ExternalProject)

ExternalProject_Add(sqlite
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/sqlite-src-3110100/
  BINARY_DIR ${CMAKE_BINARY_DIR}/sqlite
  CONFIGURE_COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/sqlite-src-3110100/configure --with-pic
  BUILD_COMMAND make libsqlite3.la
  INSTALL_COMMAND "")

include_directories(${CMAKE_BINARY_DIR}/sqlite/tsrc)

add_library(qc_sqlite SHARED qc_sqlite.c)

target_link_libraries(qc_sqlite maxscale-common ${CMAKE_BINARY_DIR}/sqlite/.libs/libsqlite3.a)
set_target_properties(qc_sqlite PROPERTIES VERSION "1.0.0")
set_target_properties(qc_sqlite PROPERTIES LINK_FLAGS -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/qc_sqlite.map)
set_target_properties(qc_sqlite PROPERTIES LINK_FLAGS -Wl,-z,defs)
install(TARGETS qc_sqlite COMPONENT lib DESTINATION ${MAXSCALE_LIBDIR})
