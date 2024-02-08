configure_file(internal/defaults.hh.in internal/defaults.hh @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_library(maxscale-common SHARED
  admin.cc
  adminusers.cc
  backend.cc
  base_user_manager.cc
  buffer.cc
  cachingparser.cc
  config.cc
  config2.cc
  config_runtime.cc
  config_state.cc
  configmanager.cc
  dcb.cc
  event.cc
  filter.cc
  hint.cc
  history.cc
  httprequest.cc
  httpresponse.cc
  http_sql.cc
  indexedstorage.cc
  json.cc
  json_api.cc
  jwt.cc
  key_manager.cc
  listener.cc
  load_utils.cc
  log.cc
  mainworker.cc
  maxscale_pcre2.cc
  misc.cc
  modulecmd.cc
  monitor.cc
  monitormanager.cc
  monitorserver.cc
  parser.cc
  profiler.cc
  queryclassifier.cc
  resource.cc
  response_stat.cc
  router.cc
  routingworker.cc
  secrets.cc
  server.cc
  servermanager.cc
  service.cc
  session.cc
  session_stats.cc
  sql_conn_manager.cc
  sql_etl.cc
  sql_etl_generic.cc
  ssl.cc
  target.cc
  test.cc
  testparser.cc
  threadpool.cc
  users.cc
  utils.cc
  websocket.cc
  response_distribution.cc
  )

target_link_libraries(maxscale-common
  maxbase
  maxsql
  maxsimd
  mariadbprotocol
  mariadbauth
  ${MARIADB_CONNECTOR_LIBRARIES}
  ${LZMA_LINK_FLAGS}
  ${PCRE2_LIBRARIES}
  ${JANSSON_LIBRARIES}
  ssl
  pthread
  crypt
  crypto
  z
  rt
  m
  sqlite3
  stdc++
  ${MICROHTTPD_LIBRARIES}
  ${GNUTLS_LIBRARIES}
  ${LIBATOMIC_LIBRARIES}
  uuid
)

set_target_properties(maxscale-common PROPERTIES LINK_FLAGS -Wl,--exclude-libs=libmariadbclient.a)

if (BUILD_KMIP_KEY_MANAGER)
  include(${CMAKE_SOURCE_DIR}/cmake/BuildLibKMIP.cmake)
  add_dependencies(maxscale-common libkmip)
  add_compile_definitions(BUILD_KMIP_KEY_MANAGER)
  target_link_libraries(maxscale-common ${LIBKMIP_LIBRARIES})
  target_sources(maxscale-common PRIVATE key_manager_kmip.cc)
  target_include_directories(maxscale-common PUBLIC ${LIBKMIP_INCLUDE_DIR})
endif()

if (BUILD_VAULT_KEY_MANAGER)
  include(${CMAKE_SOURCE_DIR}/cmake/BuildLibVault.cmake)
  if (LIBVAULT_FOUND)
    add_dependencies(maxscale-common libvault)
    add_compile_definitions(BUILD_VAULT_KEY_MANAGER)
    target_link_libraries(maxscale-common ${LIBVAULT_LIBRARIES})
    target_sources(maxscale-common PRIVATE key_manager_vault.cc)
    target_include_directories(maxscale-common PUBLIC ${LIBVAULT_INCLUDE_DIR})

    # The libvault header uses pragmas unknown to GCC which cause warnings and
    # due to -Werror, these cause the build to fail.
    set_source_files_properties(key_manager_vault.cc PROPERTIES COMPILE_FLAGS -Wno-unknown-pragmas)
  else()
    message(STATUS "Will not build libvault, compiler is too old")
  endif()
endif()

find_library(HAVE_LIBDL NAMES dl)
if (HAVE_LIBDL)
  # libdl just exposes libc functionality on most systems.  This means that if
  # we can't find it, it's likely that libc already exposes the symbols.
  target_link_libraries(maxscale-common dl)
endif()

# Using initial-exec instead of the default global-dynamic tls-model
# reduces the cost of using thread-local variables in dynamic libraries.
target_compile_options(maxscale-common PRIVATE "-ftls-model=initial-exec")

add_dependencies(maxscale-common pcre2 connector-c libmicrohttpd jansson maxbase maxsimd jwt-cpp)
set_target_properties(maxscale-common PROPERTIES VERSION "1.0.0")
install_module(maxscale-common core)

add_executable(maxscale gateway.cc)
add_dependencies(maxscale pcre2)

if(WITH_JEMALLOC)
  target_link_libraries(maxscale ${JEMALLOC_LIBRARIES})
elseif(WITH_TCMALLOC)
  target_link_libraries(maxscale ${TCMALLOC_LIBRARIES})
endif()

target_link_libraries(maxscale maxscale-common)
install_executable(maxscale core)

add_executable(maxkeys maxkeys.cc)
target_link_libraries(maxkeys maxscale-common)
install_executable(maxkeys core)

add_executable(maxpasswd maxpasswd.cc)
target_link_libraries(maxpasswd maxscale-common)
install_executable(maxpasswd core)

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
