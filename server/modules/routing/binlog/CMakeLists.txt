add_library(binlogrouter SHARED blr.c blr_master.c blr_cache.c blr_slave.c blr_file.c)
set_target_properties(binlogrouter PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${MAXSCALE_LIBDIR})
target_link_libraries(binlogrouter ssl pthread log_manager)
install(TARGETS binlogrouter DESTINATION ${MAXSCALE_LIBDIR})
