add_library(maxinfo SHARED maxinfo.c maxinfo_parse.c maxinfo_error.c maxinfo_exec.c)
set_target_properties(maxinfo PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${MAXSCALE_LIBDIR} VERSION "1.0.0")
target_link_libraries(maxinfo maxscale-common)
install_module(maxinfo core)
