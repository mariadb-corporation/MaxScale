add_library(grmon SHARED grmon.cc)
target_link_libraries(grmon maxscale-common)
set_target_properties(grmon PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install_module(grmon experimental)
