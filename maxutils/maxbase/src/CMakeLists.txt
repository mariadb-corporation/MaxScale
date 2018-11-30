add_library(maxbase STATIC
  alloc.cc
  atomic.cc
  eventcount.cc
  format.cc
  log.cc
  logger.cc
  maxbase.cc
  messagequeue.cc
  semaphore.cc
  stopwatch.cc
  string.cc
  stacktrace.cc
  worker.cc
  workertask.cc
  average.cc
  )

if(HAVE_SYSTEMD)
target_link_libraries(maxbase systemd)
endif()

set_target_properties(maxbase PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
add_subdirectory(test)
