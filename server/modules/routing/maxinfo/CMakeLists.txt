add_library(maxinfo SHARED maxinfo.c maxinfo_parse.c maxinfo_error.c maxinfo_exec.c)
set_target_properties(maxinfo PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${MAXSCALE_MODULE_INSTALL}:MAXSCALE_LIBRARY_INSTALL)
target_link_libraries(maxinfo pthread log_manager)
install(TARGETS maxinfo DESTINATION ${MAXSCALE_MODULE_INSTALL})
