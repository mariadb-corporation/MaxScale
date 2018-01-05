add_library(nullauthallow SHARED null_auth_allow.c)
target_link_libraries(nullauthallow maxscale-common mysqlcommon)
set_target_properties(nullauthallow PROPERTIES VERSION "1.0.0")
install_module(nullauthallow core)
