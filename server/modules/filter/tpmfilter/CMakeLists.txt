add_library(tpmfilter SHARED tpmfilter.cc)
target_link_libraries(tpmfilter maxscale-common)
set_target_properties(tpmfilter PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install_module(tpmfilter experimental)
