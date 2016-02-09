add_library(maxscale-common SHARED adminusers.c atomic.c buffer.c config.c dbusers.c dcb.c filter.c externcmd.c gwbitmask.c gwdirs.c gw_utils.c hashtable.c hint.c housekeeper.c load_utils.c log_manager.cc maxscale_pcre2.c memlog.c mlist.c modutil.c monitor.c query_classifier.c poll.c random_jkiss.c resultset.c secrets.c server.c service.c session.c slist.c spinlock.c thread.c users.c utils.c ${CMAKE_SOURCE_DIR}/utils/skygw_utils.cc statistics.c)

target_link_libraries(maxscale-common ${MARIADB_CONNECTOR_LIBRARIES} ${LZMA_LINK_FLAGS} ${PCRE2_LIBRARIES} ${CURL_LIBRARIES} ssl aio pthread crypt dl crypto inih z rt m stdc++)

if(WITH_JEMALLOC)
  target_link_libraries(maxscale-common ${JEMALLOC_LIBRARIES})
elseif(WITH_TCMALLOC)
  target_link_libraries(maxscale-common ${TCMALLOC_LIBRARIES})
endif()

add_dependencies(maxscale-common pcre2)
install(TARGETS maxscale-common DESTINATION ${MAXSCALE_LIBDIR})
set_target_properties(maxscale-common PROPERTIES VERSION "1.0.0")

add_executable(maxscale gateway.c)
add_dependencies(maxscale pcre2)

if(WITH_JEMALLOC)
  target_link_libraries(maxscale ${JEMALLOC_LIBRARIES})
elseif(WITH_TCMALLOC)
  target_link_libraries(maxscale ${TCMALLOC_LIBRARIES})
endif()

target_link_libraries(maxscale maxscale-common)
install(TARGETS maxscale DESTINATION ${MAXSCALE_BINDIR})

add_executable(maxkeys maxkeys.c)
target_link_libraries(maxkeys maxscale-common)
install(TARGETS maxkeys DESTINATION ${MAXSCALE_BINDIR})

add_executable(maxpasswd maxpasswd.c)
target_link_libraries(maxpasswd maxscale-common)
install(TARGETS maxpasswd DESTINATION ${MAXSCALE_BINDIR})

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
