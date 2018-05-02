add_library(maxadminauth SHARED max_admin_auth.cc)
target_link_libraries(maxadminauth maxscale-common)
set_target_properties(maxadminauth PROPERTIES VERSION "1.0.0")
install_module(maxadminauth core)
