add_library(gssapibackendauth SHARED gssapi_backend_auth.c ../gssapi_auth_common.c)
target_link_libraries(gssapibackendauth maxscale-common  ${GSSAPI_LIBS} mysqlcommon)
set_target_properties(gssapibackendauth PROPERTIES VERSION "1.0.0")
install_module(gssapibackendauth core)
