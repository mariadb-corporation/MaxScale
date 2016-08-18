add_library(testroute SHARED testroute.c)
target_link_libraries(testroute maxscale-common)
set_target_properties(testroute PROPERTIES VERSION "1.0.0")
install_module(testroute core)
