add_library(ccrfilter SHARED ccrfilter.cc)
target_link_libraries(ccrfilter maxscale-common)
set_target_properties(ccrfilter PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install_module(ccrfilter core)
