add_library(testfilter SHARED testfilter.c)
target_link_libraries(testfilter maxscale-common)
set_target_properties(testfilter PROPERTIES VERSION "1.0.0")
install_module(testfilter core)
