add_library(maxbase STATIC
  alloc.cc
  atomic.cc
  average.cc
  eventcount.cc
  format.cc
  host.cc
  http.cc
  log.cc
  logger.cc
  maxbase.cc
  messagequeue.cc
  pam_utils.cc
  pretty_print.cc
  random.cc
  semaphore.cc
  shareddata.cc
  stacktrace.cc
  stopwatch.cc
  string.cc
  watchedworker.cc
  watchdognotifier.cc
  worker.cc
  workertask.cc
  )

if(HAVE_SYSTEMD)
target_link_libraries(maxbase systemd)
endif()

set_target_properties(maxbase PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
target_link_libraries(maxbase ${CURL_LIBRARIES} ${PAM_LIBRARIES})
add_subdirectory(test)
