add_library(maxscaled SHARED maxscaled.cc)
target_link_libraries(maxscaled maxscale-common)
set_target_properties(maxscaled PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install_module(maxscaled core)
