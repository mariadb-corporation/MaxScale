add_library(cdc SHARED cdc.cc)
target_link_libraries(cdc maxscale-common)
set_target_properties(cdc PROPERTIES VERSION "1.0.1" LINK_FLAGS -Wl,-z,defs)
install_module(cdc core)
