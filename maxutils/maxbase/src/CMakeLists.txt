# Needed by inih
add_compile_definitions(INI_MAX_LINE=16777216 INI_USE_STACK=0 INI_ALLOW_REALLOC=1 INI_INITIAL_ALLOC=1024
        INI_ALLOW_MULTILINE=1 INI_ALLOW_INLINE_COMMENTS=0 INI_ALLOW_NO_VALUE=1 INI_HANDLER_LINENO=1
        INI_CALL_HANDLER_ON_NEW_SECTION=1)

add_library(maxbase STATIC
  alloc.cc
  atomic.cc
  average.cc
  eventcount.cc
  externcmd.cc
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
  proxy_protocol.cc
  queryresult.cc
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
  cpuinfo.cc
  csv_writer.cc
  compress.cc
  temp_file.cc
  inih/ini.c
  )

if(HAVE_SYSTEMD)
target_link_libraries(maxbase systemd)
endif()

add_dependencies(maxbase pcre2 jansson zstd)
set_target_properties(maxbase PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
target_link_libraries(maxbase
        ${CURL_LIBRARIES} ${PAM_LIBRARIES} ${PCRE2_LIBRARIES} ${JANSSON_LIBRARIES} ${ZSTD_LIBRARIES}
        pthread rt dl stdc++fs)
if (BUILD_TESTS)
  add_subdirectory(test)
endif()
