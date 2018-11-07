add_library(topfilter SHARED topfilter.cc)
target_link_libraries(topfilter maxscale-common)
set_target_properties(topfilter PROPERTIES VERSION "1.0.1" LINK_FLAGS -Wl,-z,defs)
install_module(topfilter core)
