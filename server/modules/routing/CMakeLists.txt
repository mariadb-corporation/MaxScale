if(BUILD_TESTS)
  add_subdirectory(test)
endif()

add_library(testroute SHARED testroute.c)
target_link_libraries(testroute log_manager utils)
install(TARGETS testroute DESTINATION modules)

add_library(dbshard SHARED dbshard/dbshard.c)
target_link_libraries(dbshard log_manager utils query_classifier)
install(TARGETS dbshard DESTINATION modules)

add_library(shardrouter SHARED dbshard/shardrouter.c)
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
add_subdirectory(dbshard/test)
if(BUILD_BINLOG)
  add_subdirectory(binlog)
endif()

