add_library(comment SHARED commentfilter.cc commentfiltersession.cc)
target_link_libraries(comment maxscale-common)
set_target_properties(comment PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install_module(comment core)

