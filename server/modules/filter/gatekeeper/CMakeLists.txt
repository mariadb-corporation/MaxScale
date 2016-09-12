add_library(gatekeeper SHARED gatekeeper.c)
target_link_libraries(gatekeeper maxscale-common)
set_target_properties(gatekeeper PROPERTIES VERSION "1.0.0")
install_module(gatekeeper core)
