add_library(maxinfo SHARED maxinfo.cc maxinfo_parse.cc maxinfo_error.cc maxinfo_exec.cc)
set_target_properties(maxinfo PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${MAXSCALE_LIBDIR} VERSION "1.0.0")
# Can't add `LINK_FLAGS -Wl,-z,defs` as the module uses maxscale_shutdown() which is defined in gateway.cc
target_link_libraries(maxinfo maxscale-common)
install_module(maxinfo core)
