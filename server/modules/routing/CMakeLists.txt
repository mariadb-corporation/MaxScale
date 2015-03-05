if(BUILD_TESTS)
  add_subdirectory(test)
  add_library(testroute SHARED testroute.c)
  target_link_libraries(testroute log_manager utils)
  install(TARGETS testroute DESTINATION modules)
endif()

add_library(schemarouter SHARED schemarouter/schemarouter.c)
target_link_libraries(schemarouter log_manager utils query_classifier)
install(TARGETS schemarouter DESTINATION modules)

add_library(shardrouter SHARED schemarouter/shardrouter.c)
target_link_libraries(shardrouter log_manager utils query_classifier)
install(TARGETS shardrouter DESTINATION modules)

add_library(readconnroute SHARED readconnroute.c)
target_link_libraries(readconnroute log_manager utils)
install(TARGETS readconnroute DESTINATION modules)

add_library(debugcli SHARED debugcli.c debugcmd.c)
target_link_libraries(debugcli log_manager utils)
install(TARGETS debugcli DESTINATION modules)

add_library(cli SHARED cli.c debugcmd.c)
target_link_libraries(cli log_manager utils)
install(TARGETS cli DESTINATION modules)

add_subdirectory(readwritesplit)
add_subdirectory(schemarouter/test)
if(BUILD_BINLOG)
  add_subdirectory(binlog)
endif()
add_subdirectory(maxinfo)

