add_library(cdc SHARED cdc.cc)
target_link_libraries(cdc maxscale-common)
set_target_properties(cdc PROPERTIES VERSION "1.0.1")
install_module(cdc core)
