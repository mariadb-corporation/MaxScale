add_library(httpauth SHARED http_auth.cc)
target_link_libraries(httpauth maxscale-common)
set_target_properties(httpauth PROPERTIES VERSION "1.0.0")
install_module(httpauth core)
