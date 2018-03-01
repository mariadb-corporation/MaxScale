add_library(readconnroute SHARED readconnroute.c)
target_link_libraries(readconnroute maxscale-common)
set_target_properties(readconnroute PROPERTIES VERSION "1.1.0")
install_module(readconnroute core)
