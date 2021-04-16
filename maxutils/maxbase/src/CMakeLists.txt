find_package(LibXml2 REQUIRED)

include_directories(${LIBXML2_INCLUDE_DIR})

add_library(maxbase STATIC
  alloc.cc
  atomic.cc
  average.cc
  eventcount.cc
  format.cc
  hexdump.cc
  host.cc
  http.cc
  json.cc
  log.cc
  logger.cc
  maxbase.cc
  messagequeue.cc
  pam_utils.cc
  pretty_print.cc
  random.cc
  regex.cc
  semaphore.cc
  shareddata.cc
  ssl.cc
  stacktrace.cc
  stopwatch.cc
  string.cc
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
target_link_libraries(maxbase ${CURL_LIBRARIES} ${PAM_LIBRARIES} ${PCRE2_LIBRARIES} pthread rt)
if (BUILD_TESTS)
  add_subdirectory(test)
endif()

if(BUILD_TOOLS)
  # Tool for testing local PAM config.
  add_executable(test_pam_login test_pam_login.cc)
  target_link_libraries(test_pam_login maxbase)
  install_executable(test_pam_login core)
endif()
