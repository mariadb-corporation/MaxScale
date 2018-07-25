add_library(nullauthallow SHARED null_auth_allow.cc)
target_link_libraries(nullauthallow maxscale-common mysqlcommon)
set_target_properties(nullauthallow PROPERTIES VERSION "1.0.0"  LINK_FLAGS -Wl,-z,defs)
install_module(nullauthallow core)
