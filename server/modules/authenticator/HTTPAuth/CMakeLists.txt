add_library(HTTPAuth SHARED http_auth.c)
target_link_libraries(HTTPAuth maxscale-common)
set_target_properties(HTTPAuth PROPERTIES VERSION "1.0.0")
install_module(HTTPAuth core)
