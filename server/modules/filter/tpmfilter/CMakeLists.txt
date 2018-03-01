add_library(tpmfilter SHARED tpmfilter.c)
target_link_libraries(tpmfilter maxscale-common)
set_target_properties(tpmfilter PROPERTIES VERSION "1.0.0")
install_module(tpmfilter experimental)
