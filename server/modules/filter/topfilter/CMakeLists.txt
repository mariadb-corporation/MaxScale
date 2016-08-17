add_library(topfilter SHARED topfilter.c)
target_link_libraries(topfilter maxscale-common)
set_target_properties(topfilter PROPERTIES VERSION "1.0.1")
install_module(topfilter core)
