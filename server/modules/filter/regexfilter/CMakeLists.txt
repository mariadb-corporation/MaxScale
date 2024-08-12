add_library(regexfilter SHARED regexfilter.cc)
target_link_libraries(regexfilter maxscale-common)
set_target_properties(regexfilter PROPERTIES VERSION "1.1.0" LINK_FLAGS -Wl,-z,defs)
install_module(regexfilter core)
