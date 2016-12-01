add_library(NullAuthDeny SHARED null_auth_deny.c)
target_link_libraries(NullAuthDeny maxscale-common)
set_target_properties(NullAuthDeny PROPERTIES VERSION "1.0.0")
install_module(NullAuthDeny core)
