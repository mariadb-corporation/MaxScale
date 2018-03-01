add_library(NullAuthAllow SHARED null_auth_allow.c)
target_link_libraries(NullAuthAllow maxscale-common MySQLCommon)
set_target_properties(NullAuthAllow PROPERTIES VERSION "1.0.0")
install_module(NullAuthAllow core)
