add_library(nullauthdeny SHARED null_auth_deny.c)
target_link_libraries(nullauthdeny maxscale-common)
set_target_properties(nullauthdeny PROPERTIES VERSION "1.0.0")
install_module(nullauthdeny core)
