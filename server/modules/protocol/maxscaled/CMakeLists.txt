add_library(maxscaled SHARED maxscaled.c)
target_link_libraries(maxscaled maxscale-common)
set_target_properties(maxscaled PROPERTIES VERSION "1.0.0")
install_module(maxscaled core)
