add_library(nullauthdeny SHARED null_auth_deny.cc)
target_link_libraries(nullauthdeny maxscale-common)
set_target_properties(nullauthdeny PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install_module(nullauthdeny core)
