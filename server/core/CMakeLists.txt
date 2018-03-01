add_library(maxscale-common SHARED adminusers.c alloc.c authenticator.c atomic.c buffer.c config.c config_runtime.c dcb.c filter.c filter.cc externcmd.c paths.c hashtable.c hint.c housekeeper.c load_utils.c log_manager.cc maxscale_pcre2.c misc.c mlist.c modutil.c monitor.c queuemanager.c query_classifier.cc poll.c random_jkiss.c resultset.c secrets.c server.c service.c session.c spinlock.c thread.c users.c utils.c skygw_utils.cc statistics.c listener.c ssl.c mysql_utils.c mysql_binlog.c modulecmd.c encryption.c)

if(WITH_JEMALLOC)
  target_link_libraries(maxscale-common ${JEMALLOC_LIBRARIES})
elseif(WITH_TCMALLOC)
  target_link_libraries(maxscale-common ${TCMALLOC_LIBRARIES})
endif()

target_link_libraries(maxscale-common ${MARIADB_CONNECTOR_LIBRARIES} ${LZMA_LINK_FLAGS} ${PCRE2_LIBRARIES} ${CURL_LIBRARIES} ssl pthread crypt dl crypto inih z rt m stdc++)

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
