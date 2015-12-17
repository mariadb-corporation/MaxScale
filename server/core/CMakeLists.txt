if(BUILD_TESTS OR BUILD_TOOLS)
  add_library(fullcore STATIC random_jkiss.c adminusers.c atomic.c config.c buffer.c dbusers.c dcb.c filter.c gwbitmask.c gw_utils.c hashtable.c hint.c housekeeper.c load_utils.c memlog.c modutil.c monitor.c poll.c resultset.c secrets.c server.c service.c session.c spinlock.c thread.c users.c utils.c gwdirs.c  externcmd.c maxscale_pcre2.c)
  if(WITH_JEMALLOC)
    target_link_libraries(fullcore ${JEMALLOC_LIBRARIES})
  elseif(WITH_TCMALLOC)
    target_link_libraries(fullcore ${TCMALLOC_LIBRARIES})
  endif()
  target_link_libraries(fullcore ${CURL_LIBRARIES} utils ${PCRE2_LIBRARIES} log_manager pthread ${LZMA_LINK_FLAGS} ${EMBEDDED_LIB} ${PCRE_LINK_FLAGS} ssl aio rt crypt dl crypto inih z m stdc++)
  add_dependencies(fullcore pcre2)
endif()

add_executable(maxscale atomic.c buffer.c spinlock.c gateway.c
	gw_utils.c utils.c dcb.c load_utils.c session.c service.c server.c 
	poll.c config.c users.c hashtable.c dbusers.c thread.c gwbitmask.c 
	monitor.c adminusers.c secrets.c filter.c modutil.c hint.c
	housekeeper.c memlog.c resultset.c  gwdirs.c externcmd.c random_jkiss.c maxscale_pcre2.c)
add_dependencies(maxscale pcre2)

if(WITH_JEMALLOC)
  target_link_libraries(maxscale ${JEMALLOC_LIBRARIES})
elseif(WITH_TCMALLOC)
  target_link_libraries(maxscale ${TCMALLOC_LIBRARIES})
endif()

target_link_libraries(maxscale ${LZMA_LINK_FLAGS} ${PCRE2_LIBRARIES} ${EMBEDDED_LIB} ${PCRE_LINK_FLAGS} ${CURL_LIBRARIES} log_manager utils ssl aio pthread crypt dl crypto inih z rt m stdc++)
install(TARGETS maxscale DESTINATION ${MAXSCALE_BINDIR})

add_executable(maxkeys maxkeys.c spinlock.c secrets.c utils.c gwdirs.c random_jkiss.c ${CMAKE_SOURCE_DIR}/log_manager/log_manager.cc)
target_link_libraries(maxkeys utils pthread crypt crypto ${PCRE2_LIBRARIES})
install(TARGETS maxkeys DESTINATION ${MAXSCALE_BINDIR})

add_executable(maxpasswd maxpasswd.c spinlock.c secrets.c utils.c gwdirs.c random_jkiss.c ${CMAKE_SOURCE_DIR}/log_manager/log_manager.cc)
target_link_libraries(maxpasswd utils pthread crypt crypto ${PCRE2_LIBRARIES})
install(TARGETS maxpasswd DESTINATION ${MAXSCALE_BINDIR})

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
