add_library(grmon SHARED grmon.cc)
target_link_libraries(grmon maxscale-common)
set_target_properties(grmon PROPERTIES VERSION "1.0.0")
install_module(grmon experimental)
