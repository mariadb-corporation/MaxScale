add_library(maxrows SHARED maxrows.cc)
target_link_libraries(maxrows maxscale-common)
set_target_properties(maxrows PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install_module(maxrows core)
