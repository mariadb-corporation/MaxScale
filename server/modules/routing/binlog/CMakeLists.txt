add_library(binlogrouter SHARED blr.c blr_master.c blr_cache.c blr_slave.c blr_file.c)
set_target_properties(binlogrouter PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${MAXSCALE_LIBDIR} VERSION "2.0.0")
target_link_libraries(binlogrouter ssl pthread log_manager)
install(TARGETS binlogrouter DESTINATION ${MAXSCALE_LIBDIR})

add_executable(maxbinlogcheck maxbinlogcheck.c blr_file.c blr_cache.c blr_master.c blr_slave.c blr.c
  ${CMAKE_SOURCE_DIR}/server/core/service.c ${CMAKE_SOURCE_DIR}/server/core/spinlock.c
  ${CMAKE_SOURCE_DIR}/server/core/buffer.c ${CMAKE_SOURCE_DIR}/server/core/atomic.c
  ${CMAKE_SOURCE_DIR}/server/core/hint.c ${CMAKE_SOURCE_DIR}/server/core/gwdirs.c
  ${CMAKE_SOURCE_DIR}/server/core/server.c ${CMAKE_SOURCE_DIR}/server/core/dcb.c
  ${CMAKE_SOURCE_DIR}/server/core/users.c ${CMAKE_SOURCE_DIR}/server/core/dbusers.c
  ${CMAKE_SOURCE_DIR}/server/core/utils.c ${CMAKE_SOURCE_DIR}/server/core/hashtable.c
  ${CMAKE_SOURCE_DIR}/server/core/poll.c ${CMAKE_SOURCE_DIR}/server/core/gwbitmask.c
  ${CMAKE_SOURCE_DIR}/server/core/config.c ${CMAKE_SOURCE_DIR}/server/core/session.c
  ${CMAKE_SOURCE_DIR}/server/core/housekeeper.c ${CMAKE_SOURCE_DIR}/server/core/filter.c
  ${CMAKE_SOURCE_DIR}/server/core/resultset.c ${CMAKE_SOURCE_DIR}/server/core/load_utils.c
  ${CMAKE_SOURCE_DIR}/server/core/monitor.c ${CMAKE_SOURCE_DIR}/server/core/gw_utils.c
  ${CMAKE_SOURCE_DIR}/server/core/thread.c ${CMAKE_SOURCE_DIR}/server/core/secrets.c
  ${CMAKE_SOURCE_DIR}/server/core/random_jkiss.c ${CMAKE_SOURCE_DIR}/server/core/maxscale_pcre2.c
  ${CMAKE_SOURCE_DIR}/log_manager/log_manager.cc ${CMAKE_SOURCE_DIR}/server/core/externcmd.c)


target_link_libraries(maxbinlogcheck utils ssl pthread ${LZMA_LINK_FLAGS} ${EMBEDDED_LIB} ${PCRE_LINK_FLAGS} aio rt crypt dl crypto inih z m stdc++ ${CURL_LIBRARIES} ${PCRE2_LIBRARIES})

install(TARGETS maxbinlogcheck DESTINATION bin)

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
