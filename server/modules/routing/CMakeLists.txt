add_library(testroute SHARED testroute.c)
target_link_libraries(testroute utils log_manager)
install(TARGETS testroute DESTINATION modules)

add_library(readconnroute SHARED readconnroute.c)
target_link_libraries(readconnroute utils log_manager)
install(TARGETS readconnroute DESTINATION modules)

add_library(debugcli SHARED debugcli.c debugcmd.c)
target_link_libraries(debugcli utils log_manager)
install(TARGETS debugcli DESTINATION modules)

add_library(cli SHARED cli.c debugcmd.c)
target_link_libraries(cli utils log_manager)
install(TARGETS cli DESTINATION modules)

add_subdirectory(readwritesplit)
add_subdirectory(binlog)