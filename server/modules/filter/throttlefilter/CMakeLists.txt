add_library(throttlefilter SHARED throttlefilter.cc throttlesession.cc)
target_link_libraries(throttlefilter maxscale-common)
set_target_properties(throttlefilter PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install_module(throttlefilter core)
