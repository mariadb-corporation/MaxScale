include(BuildLibpg_query.cmake)

add_library(pp_pg_query SHARED pp_pg_query.cc)

add_dependencies(pp_pg_query libpg_query postgresprotocol)

target_include_directories(pp_pg_query PRIVATE ${LIBPG_QUERY_INCLUDE_DIR})

target_link_libraries(pp_pg_query
  maxscale-common
  postgresprotocol
  ${LIBPG_QUERY_STATIC_LIBRARIES}
  )

set_target_properties(pp_pg_query PROPERTIES VERSION "1.0.0")
set_target_properties(pp_pg_query PROPERTIES LINK_FLAGS -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/pp_pg_query.map)

install_module(pp_pg_query core)
