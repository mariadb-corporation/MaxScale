add_library(testroute SHARED testroute.c)
set_target_properties(testroute PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${CMAKE_INSTALL_PREFIX}/lib)
target_link_libraries(testroute utils log_manager)
install(TARGETS testroute DESTINATION modules)

add_library(readconnroute SHARED readconnroute.c)
set_target_properties(readconnroute PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${CMAKE_INSTALL_PREFIX}/lib)
target_link_libraries(readconnroute utils log_manager)
install(TARGETS readconnroute DESTINATION modules)

add_library(debugcli SHARED debugcli.c debugcmd.c)
set_target_properties(debugcli PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${CMAKE_INSTALL_PREFIX}/lib)
target_link_libraries(debugcli utils log_manager)
install(TARGETS debugcli DESTINATION modules)

add_library(cli SHARED cli.c debugcmd.c)
set_target_properties(cli PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${CMAKE_INSTALL_PREFIX}/lib)
target_link_libraries(cli utils log_manager)
install(TARGETS cli DESTINATION modules)

add_subdirectory(readwritesplit)
add_subdirectory(binlog)