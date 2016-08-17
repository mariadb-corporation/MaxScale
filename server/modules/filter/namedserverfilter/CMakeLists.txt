add_library(namedserverfilter SHARED namedserverfilter.c)
target_link_libraries(namedserverfilter maxscale-common)
set_target_properties(namedserverfilter PROPERTIES VERSION "1.1.0")
install_module(namedserverfilter core)
