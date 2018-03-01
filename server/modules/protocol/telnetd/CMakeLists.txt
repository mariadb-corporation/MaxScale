add_library(telnetd SHARED telnetd.c)
target_link_libraries(telnetd maxscale-common)
set_target_properties(telnetd PROPERTIES VERSION "1.0.1")
install_module(telnetd core)
