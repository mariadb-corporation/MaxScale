add_library(maxbase STATIC
  atomic.cc
  eventcount.cc
  log.cc
  logger.cc
  semaphore.cc
  stopwatch.cc
  string.cc
  stacktrace.cc
  )
set_target_properties(maxbase PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install(TARGETS maxbase DESTINATION lib)

add_subdirectory(test)
