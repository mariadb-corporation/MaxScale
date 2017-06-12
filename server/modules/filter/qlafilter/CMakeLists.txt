add_library(qlafilter SHARED qlafilter.cc)
target_link_libraries(qlafilter maxscale-common)
set_target_properties(qlafilter PROPERTIES VERSION "1.1.1")
install_module(qlafilter core)
