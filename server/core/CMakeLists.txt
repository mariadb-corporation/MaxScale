file(GLOB FULLCORE_SRC *.c)
add_library(fullcore STATIC ${FULLCORE_SRC})
target_link_libraries(fullcore utils log_manager aio pthread crypt dl mysqld crypto inih)

add_executable(maxscale atomic.c buffer.c spinlock.c gateway.c
	gw_utils.c utils.c dcb.c load_utils.c session.c service.c server.c 
	poll.c config.c users.c hashtable.c dbusers.c thread.c gwbitmask.c 
	monitor.c adminusers.c secrets.c filter.c modutil.c hint.c housekeeper.c)
target_link_libraries(maxscale ${EMBEDDED_LIB} utils log_manager ssl aio pthread crypt dl crypto inih z rt m stdc++)
install(TARGETS maxscale DESTINATION bin)

add_executable(maxkeys maxkeys.c secrets.c utils.c)
target_link_libraries(maxkeys utils log_manager pthread  crypt crypto)
install(TARGETS maxkeys DESTINATION bin)

add_executable(maxpasswd maxpasswd.c secrets.c utils.c)
target_link_libraries(maxpasswd utils log_manager pthread crypt crypto)
install(TARGETS maxpasswd DESTINATION bin)

if(BUILD_TESTS)
add_subdirectory(test)
endif()