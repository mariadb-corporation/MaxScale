add_library(maxscale-common SHARED
  admin.cc
  adminusers.cc
  alloc.cc
  atomic.cc
  authenticator.cc
  backend.cc
  buffer.cc
  config.cc
  config_runtime.cc
  dcb.cc
  encryption.cc
  externcmd.cc
  filter.cc
  hashtable.cc
  hint.cc
  housekeeper.cc
  httprequest.cc
  httpresponse.cc
  json_api.cc
  listener.cc
  load_utils.cc
  log_manager.cc
  maxscale_pcre2.cc
  misc.cc
  mlist.cc
  messagequeue.cc
  modulecmd.cc
  modutil.cc
  monitor.cc
  mysql_binlog.cc
  mysql_utils.cc
  paths.cc
  poll.cc
  query_classifier.cc
  queuemanager.cc
  random_jkiss.cc
  resultset.cc
  resource.cc
  router.cc
  secrets.cc
  semaphore.cc
  server.cc
  service.cc
  session.cc
  session_command.cc
  skygw_utils.cc
  spinlock.cc
  ssl.cc
  statistics.cc
  thread.cc
  users.cc
  utils.cc
  worker.cc
  workertask.cc
  )

if(WITH_JEMALLOC)
  target_link_libraries(maxscale-common ${JEMALLOC_LIBRARIES})
elseif(WITH_TCMALLOC)
  target_link_libraries(maxscale-common ${TCMALLOC_LIBRARIES})
endif()

target_link_libraries(maxscale-common
  ${MARIADB_CONNECTOR_LIBRARIES}
  ${LZMA_LINK_FLAGS}
  ${PCRE2_LIBRARIES}
  ${CURL_LIBRARIES}
  ${JANSSON_LIBRARIES}
  ssl
  pthread
  crypt
  dl
  crypto
  inih
  z
  rt
  m
  stdc++
  gnutls
  gcrypt
  ${MICROHTTPD_LIBRARIES}
)

add_dependencies(maxscale-common pcre2 connector-c)
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

add_executable(maxkeys maxkeys.c)
target_link_libraries(maxkeys maxscale-common)
install_executable(maxkeys core)

add_executable(maxpasswd maxpasswd.c)
target_link_libraries(maxpasswd maxscale-common)
install_executable(maxpasswd core)

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
