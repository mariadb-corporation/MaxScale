if(BUILD_TESTS)
  add_subdirectory(test)
  add_library(testroute SHARED testroute.c)
  target_link_libraries(testroute log_manager utils)
  install(TARGETS testroute DESTINATION ${MAXSCALE_LIBDIR})
endif()

add_library(readconnroute SHARED readconnroute.c)
target_link_libraries(readconnroute log_manager utils)
install(TARGETS readconnroute DESTINATION ${MAXSCALE_LIBDIR})

add_library(debugcli SHARED debugcli.c debugcmd.c)
target_link_libraries(debugcli log_manager utils)
install(TARGETS debugcli DESTINATION ${MAXSCALE_LIBDIR})

add_library(cli SHARED cli.c debugcmd.c)
target_link_libraries(cli log_manager utils)
install(TARGETS cli DESTINATION ${MAXSCALE_LIBDIR})

add_subdirectory(readwritesplit)
add_subdirectory(schemarouter)
if(BUILD_BINLOG)
  add_subdirectory(binlog)
endif()
add_subdirectory(maxinfo)

