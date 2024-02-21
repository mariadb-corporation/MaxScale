find_package(LibXml2 REQUIRED)

include_directories(${LIBXML2_INCLUDE_DIR})

add_library(maxbase STATIC
  alloc.cc
  atomic.cc
  average.cc
  eventcount.cc
  filesystem.cc
  format.cc
  hexdump.cc
  host.cc
  http.cc
  ini.cc
  json.cc
  log.cc
  logger.cc
  maxbase.cc
  messagequeue.cc
  pam_utils.cc
  pretty_print.cc
  random.cc
  regex.cc
  secrets.cc
  semaphore.cc
  shareddata.cc
  ssl.cc
  stacktrace.cc
  stopwatch.cc
  string.cc
  system.cc
  threadpool.cc
  watchedworker.cc
  watchdognotifier.cc
  worker.cc
  workertask.cc
  xml.cc
  cpuinfo.cc
  )

if(HAVE_SYSTEMD)
target_link_libraries(maxbase systemd)
endif()

add_dependencies(maxbase pcre2 jansson)
set_target_properties(maxbase PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
target_link_libraries(maxbase
        ${CURL_LIBRARIES} ${PAM_LIBRARIES} ${PCRE2_LIBRARIES} ${JANSSON_LIBRARIES} pthread rt inih)
if (BUILD_TESTS)
  add_subdirectory(test)
endif()
