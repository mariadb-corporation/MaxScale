add_library(HTTPD SHARED httpd.c)
target_link_libraries(HTTPD maxscale-common)
set_target_properties(HTTPD PROPERTIES VERSION "1.0.1")
install_module(HTTPD core)
