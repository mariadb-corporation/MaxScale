add_library(CDC SHARED cdc.c)
target_link_libraries(CDC maxscale-common)
set_target_properties(CDC PROPERTIES VERSION "1.0.1")
install_module(CDC core)
