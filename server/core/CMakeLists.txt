file(GLOB FULLCORE_SRC *.c)
add_library(fullcore STATIC ${FULLCORE_SRC})
target_link_libraries(fullcore utils log_manager aio pthread crypt dl mysqld crypto inih)

add_executable(maxscale atomic.c buffer.c spinlock.c gateway.c
	gw_utils.c utils.c dcb.c load_utils.c session.c service.c server.c 
	poll.c config.c users.c hashtable.c dbusers.c thread.c gwbitmask.c 
	monitor.c adminusers.c secrets.c filter.c modutil.c hint.c housekeeper.c)
set_target_properties(maxscale PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${CMAKE_INSTALL_PREFIX}/lib)
target_link_libraries(maxscale utils log_manager ssl aio pthread crypt dl ${EMBEDDED_LIB} crypto inih z)
install(TARGETS maxscale DESTINATION bin)

add_executable(maxkeys maxkeys.c secrets.c utils.c)
set_target_properties(maxkeys PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${CMAKE_INSTALL_PREFIX}/lib)
target_link_libraries(maxkeys utils log_manager aio pthread crypt dl mysqld crypto inih)
install(TARGETS maxkeys DESTINATION bin)

add_executable(maxpasswd maxpasswd.c secrets.c utils.c)
set_target_properties(maxpasswd PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${CMAKE_INSTALL_PREFIX}/lib)
target_link_libraries(maxpasswd utils log_manager aio pthread crypt dl mysqld crypto inih)
install(TARGETS maxpasswd DESTINATION bin)

add_subdirectory(test)