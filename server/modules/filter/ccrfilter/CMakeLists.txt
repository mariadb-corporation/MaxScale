add_library(ccrfilter SHARED ccrfilter.c)
target_link_libraries(ccrfilter maxscale-common)
set_target_properties(ccrfilter PROPERTIES VERSION "1.0.0")
install_module(ccrfilter core)
