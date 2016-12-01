add_library(MaxAdminAuth SHARED max_admin_auth.c)
target_link_libraries(MaxAdminAuth maxscale-common)
set_target_properties(MaxAdminAuth PROPERTIES VERSION "1.0.0")
install_module(MaxAdminAuth core)
