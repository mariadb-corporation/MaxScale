add_library(httpd SHARED httpd.cc)
target_link_libraries(httpd maxscale-common)
set_target_properties(httpd PROPERTIES VERSION "1.0.1" LINK_FLAGS -Wl,-z,defs)
install_module(httpd core)
