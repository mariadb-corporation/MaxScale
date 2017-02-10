add_library(roundrobinrouter SHARED roundrobinrouter.cpp)
target_link_libraries(roundrobinrouter maxscale-common)
set_target_properties(roundrobinrouter PROPERTIES VERSION "1.0.0")
install_module(roundrobinrouter core)

install_custom_file(roundrobinrouter.cpp ${CMAKE_INSTALL_INCLUDEDIR}/maxscale/Documentation devel)
