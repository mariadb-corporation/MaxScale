add_library(hintfilter SHARED hintfilter.cc hintparser.cc)
set_target_properties(hintfilter PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${MAXSCALE_LIBDIR} VERSION "1.0.0")
target_link_libraries(hintfilter maxscale-common)
install_module(hintfilter core)
