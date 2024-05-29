add_library(psreuse SHARED psreuse.cc)
target_link_libraries(psreuse maxscale-common)
set_target_properties(psreuse PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs CXX_VISIBILITY_PRESET hidden)
install_module(psreuse core)
