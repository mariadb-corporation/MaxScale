include(BuildLibmaxscalepg.cmake)

add_library(pp_postgres SHARED pp_postgres.cc)

add_dependencies(pp_postgres libmaxscalepg postgresprotocol)

target_include_directories(pp_postgres PRIVATE ${POSTGRES_INCLUDE_DIR})
target_link_libraries(pp_postgres
  maxscale-common
  postgresprotocol
  ${LIBMAXSCALEPG_STATIC_LIBRARIES}
  )

set_target_properties(pp_postgres PROPERTIES VERSION "1.0.0")
set_target_properties(pp_postgres PROPERTIES LINK_FLAGS -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/pp_postgres.map)

install_module(pp_postgres core)
